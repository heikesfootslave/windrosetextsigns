param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:Failures = [System.Collections.Generic.List[string]]::new()

function Write-TestResult {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("PASS", "FAIL")]
        [string]$Status,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$Detail = ""
    )

    $line = if ([string]::IsNullOrWhiteSpace($Detail)) {
        "[{0}] {1}" -f $Status, $Name
    } else {
        "[{0}] {1}: {2}" -f $Status, $Name, $Detail
    }

    if ($Status -eq "PASS") {
        Write-Host $line -ForegroundColor Green
    } else {
        Write-Host $line -ForegroundColor Red
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if ($Expected -ne $Actual) {
        throw "$Message expected='$Expected' actual='$Actual'"
    }
}

function New-BridgeClientState {
    return [pscustomobject]@{
        Labels = [ordered]@{}
        Pending = @{}
        SnapshotActive = $false
        SnapshotEndSeen = $false
        SnapshotExpectedCount = -1
        SnapshotId = ""
        SnapshotSeenKeys = @{}
        RecoveryCount = 0
        ReconcileCount = 0
        PrunedKeys = [System.Collections.Generic.List[string]]::new()
        NowSeconds = 0
    }
}

function Add-ClientLabel {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)][string]$Key,
        [string]$Text = ""
    )
    $State.Labels[$Key] = [pscustomobject]@{ Key = $Key; Text = $Text }
}

function Add-PendingProtection {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)][string]$Key,
        [int]$AgeSeconds = 0
    )
    $State.Pending[$Key] = $State.NowSeconds - $AgeSeconds
}

function Invoke-SnapshotReconcile {
    param(
        [Parameter(Mandatory = $true)]$State,
        [string]$Reason = "fixture"
    )

    if (-not $State.SnapshotActive) {
        return
    }

    $removed = [System.Collections.Generic.List[string]]::new()
    $keys = @($State.Labels.Keys)
    foreach ($key in $keys) {
        if (-not $State.SnapshotSeenKeys.ContainsKey($key)) {
            if ($State.Pending.ContainsKey($key)) {
                $age = $State.NowSeconds - [int]$State.Pending[$key]
                if ($age -lt 120) {
                    continue
                }
                $State.Pending.Remove($key)
            }
            $State.Labels.Remove($key)
            $removed.Add($key)
            $State.PrunedKeys.Add($key)
        }
    }

    if ($removed.Count -gt 0) {
        $State.RecoveryCount += $removed.Count
    }
    $State.ReconcileCount += 1
    $State.SnapshotActive = $false
    $State.SnapshotEndSeen = $false
    $State.SnapshotExpectedCount = -1
    $State.SnapshotId = ""
    $State.SnapshotSeenKeys = @{}
}

function Receive-Upsert {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)][string]$Key,
        [string]$Text = "",
        [string]$SnapshotId = "",
        [int]$SnapshotCount = -1
    )

    $State.Pending.Remove($Key)

    if (-not [string]::IsNullOrWhiteSpace($SnapshotId)) {
        if (-not $State.SnapshotActive -or $State.SnapshotId -ne $SnapshotId) {
            $State.SnapshotActive = $true
            $State.SnapshotEndSeen = $false
            $State.SnapshotExpectedCount = $SnapshotCount
            $State.SnapshotId = $SnapshotId
            $State.SnapshotSeenKeys = @{}
        } elseif ($SnapshotCount -ge 0) {
            $State.SnapshotExpectedCount = $SnapshotCount
        }
        $State.SnapshotSeenKeys[$Key] = $true
    } else {
        Add-PendingProtection -State $State -Key $Key
    }

    Add-ClientLabel -State $State -Key $Key -Text $Text

    if (-not [string]::IsNullOrWhiteSpace($SnapshotId) -and
        $State.SnapshotActive -and
        $State.SnapshotEndSeen -and
        $State.SnapshotExpectedCount -ge 0 -and
        $State.SnapshotSeenKeys.Count -ge $State.SnapshotExpectedCount) {
        Invoke-SnapshotReconcile -State $State -Reason "snapshot_complete_after_upsert"
    }
}

function Receive-Clear {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)][string]$Key
    )
    $State.Pending.Remove($Key)
    $State.Labels.Remove($Key)
}

function Receive-SnapshotEnd {
    param(
        [Parameter(Mandatory = $true)]$State,
        [string]$SnapshotId = "",
        [int]$SnapshotCount = -1
    )

    if (-not [string]::IsNullOrWhiteSpace($SnapshotId)) {
        if (-not $State.SnapshotActive -or $State.SnapshotId -ne $SnapshotId) {
            $State.SnapshotActive = $true
            $State.SnapshotId = $SnapshotId
            $State.SnapshotSeenKeys = @{}
        }
        if ($SnapshotCount -ge 0) {
            $State.SnapshotExpectedCount = $SnapshotCount
        }
        $State.SnapshotEndSeen = $true
        if ($State.SnapshotExpectedCount -ge 0 -and $State.SnapshotSeenKeys.Count -ge $State.SnapshotExpectedCount) {
            Invoke-SnapshotReconcile -State $State -Reason "snapshot_complete"
        }
        return
    }

    Invoke-SnapshotReconcile -State $State -Reason "snapshot_end_legacy"
}

function Invoke-Fixture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Body
    )
    try {
        & $Body
        Write-TestResult -Status "PASS" -Name $Name
    } catch {
        $message = $_.Exception.Message
        $script:Failures.Add("$Name failed: $message")
        Write-TestResult -Status "FAIL" -Name $Name -Detail $message
    }
}

Write-Host "Bridge payload ordering fixtures" -ForegroundColor Cyan

Invoke-Fixture -Name "snapshot end before records defers reconcile" -Body {
    $state = New-BridgeClientState
    Add-ClientLabel -State $state -Key "world/A" -Text "old A"
    Add-ClientLabel -State $state -Key "world/B" -Text "old B"

    Receive-SnapshotEnd -State $state -SnapshotId "snap-1" -SnapshotCount 2
    Assert-Equal 0 $state.ReconcileCount "reconcile should wait for upserts"
    Assert-True $state.SnapshotActive "snapshot should remain active"

    Receive-Upsert -State $state -Key "world/A" -Text "new A" -SnapshotId "snap-1" -SnapshotCount 2
    Assert-Equal 0 $state.ReconcileCount "one of two records should still defer"

    Receive-Upsert -State $state -Key "world/B" -Text "new B" -SnapshotId "snap-1" -SnapshotCount 2
    Assert-Equal 1 $state.ReconcileCount "second record should complete snapshot"
    Assert-Equal 2 $state.Labels.Count "no records should be removed"
    Assert-Equal "new A" $state.Labels["world/A"].Text "A should update"
}

Invoke-Fixture -Name "incomplete snapshot does not prune" -Body {
    $state = New-BridgeClientState
    Add-ClientLabel -State $state -Key "world/A" -Text "A"
    Add-ClientLabel -State $state -Key "world/B" -Text "B"

    Receive-Upsert -State $state -Key "world/A" -Text "A2" -SnapshotId "snap-2" -SnapshotCount 2
    Receive-SnapshotEnd -State $state -SnapshotId "snap-2" -SnapshotCount 2
    Assert-Equal 0 $state.ReconcileCount "incomplete snapshot must not reconcile"
    Assert-Equal 2 $state.Labels.Count "incomplete snapshot must not prune"
}

Invoke-Fixture -Name "complete snapshot prunes missing stale record" -Body {
    $state = New-BridgeClientState
    Add-ClientLabel -State $state -Key "world/A" -Text "A"
    Add-ClientLabel -State $state -Key "world/B" -Text "B"

    Receive-Upsert -State $state -Key "world/A" -Text "A2" -SnapshotId "snap-3" -SnapshotCount 1
    Receive-SnapshotEnd -State $state -SnapshotId "snap-3" -SnapshotCount 1
    Assert-Equal 1 $state.ReconcileCount "complete snapshot should reconcile"
    Assert-Equal 1 $state.Labels.Count "missing record should be pruned"
    Assert-True (-not $state.Labels.Contains("world/B")) "B should be removed"
    Assert-Equal 1 $state.RecoveryCount "removed record should be captured as recovery candidate"
}

Invoke-Fixture -Name "live write protected from stale snapshot" -Body {
    $state = New-BridgeClientState
    Add-ClientLabel -State $state -Key "world/A" -Text "A"
    Add-ClientLabel -State $state -Key "world/B" -Text "B"

    Receive-Upsert -State $state -Key "world/C" -Text "live C"
    Receive-Upsert -State $state -Key "world/A" -Text "A2" -SnapshotId "snap-4" -SnapshotCount 2
    Receive-Upsert -State $state -Key "world/B" -Text "B2" -SnapshotId "snap-4" -SnapshotCount 2
    Receive-SnapshotEnd -State $state -SnapshotId "snap-4" -SnapshotCount 2

    Assert-Equal 3 $state.Labels.Count "live C should be protected from stale snapshot"
    Assert-True $state.Labels.Contains("world/C") "C should remain"
}

Invoke-Fixture -Name "expired pending protection allows prune" -Body {
    $state = New-BridgeClientState
    Add-ClientLabel -State $state -Key "world/A" -Text "A"
    Add-ClientLabel -State $state -Key "world/C" -Text "old pending C"
    Add-PendingProtection -State $state -Key "world/C" -AgeSeconds 121

    Receive-Upsert -State $state -Key "world/A" -Text "A2" -SnapshotId "snap-5" -SnapshotCount 1
    Receive-SnapshotEnd -State $state -SnapshotId "snap-5" -SnapshotCount 1

    Assert-Equal 1 $state.Labels.Count "expired pending C should be pruned"
    Assert-True (-not $state.Labels.Contains("world/C")) "C should be removed after protection expires"
}

Invoke-Fixture -Name "clear removes pending and label" -Body {
    $state = New-BridgeClientState
    Add-ClientLabel -State $state -Key "world/A" -Text "A"
    Add-PendingProtection -State $state -Key "world/A"

    Receive-Clear -State $state -Key "world/A"
    Assert-Equal 0 $state.Labels.Count "clear should remove label"
    Assert-Equal 0 $state.Pending.Count "clear should remove pending protection"
}

if ($script:Failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Bridge fixture failures:" -ForegroundColor Red
    foreach ($failure in $script:Failures) {
        Write-Host "FAIL: $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "Bridge fixture summary: pass" -ForegroundColor Cyan
exit 0
