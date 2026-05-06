param(
    [string]$ScratchRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) "_scratch\sidecar_fixture_tests")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Reset-Scratch {
    if (Test-Path -LiteralPath $ScratchRoot) {
        Remove-Item -LiteralPath $ScratchRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $ScratchRoot -Force | Out-Null
}

function New-SidecarJson {
    param(
        [int]$Revision,
        [hashtable]$Labels
    )

    [ordered]@{
        version = 2
        worldIslandId = "TEST_WORLD"
        authority = "authoritative"
        revision = $Revision
        lastWriteUtc = "2026-05-06T00:00:00Z"
        labels = $Labels
    } | ConvertTo-Json -Depth 16
}

function Write-AtomicSidecar {
    param(
        [string]$Path,
        [string]$Json
    )

    $tmp = "$Path.tmp"
    $bak = "$Path.bak"
    Set-Content -LiteralPath $tmp -Value $Json -Encoding UTF8
    if (Test-Path -LiteralPath $Path) {
        Copy-Item -LiteralPath $Path -Destination $bak -Force
    }
    Move-Item -LiteralPath $tmp -Destination $Path -Force
}

function Read-JsonOrNull {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Select-RecoverySidecar {
    param([string]$Path)

    $candidates = [System.Collections.Generic.List[string]]::new()
    $candidates.Add($Path)
    $candidates.Add("$Path.bak")
    $candidates.Add("$Path.tmp")

    $backupDir = Join-Path (Split-Path -Parent $Path) "Backups"
    if (Test-Path -LiteralPath $backupDir) {
        Get-ChildItem -LiteralPath $backupDir -File -Filter "SignTexts.backup_*.json" |
            Sort-Object LastWriteTimeUtc -Descending |
            ForEach-Object { $candidates.Add($_.FullName) }
    }

    foreach ($candidate in $candidates) {
        $json = Read-JsonOrNull -Path $candidate
        if ($null -eq $json -or $null -eq $json.labels) {
            continue
        }
        return [pscustomobject]@{
            Path = $candidate
            Revision = [int]$json.revision
            Count = @($json.labels.PSObject.Properties).Count
        }
    }

    return $null
}

function Assert-Equal {
    param(
        [object]$Actual,
        [object]$Expected,
        [string]$Name
    )
    if ($Actual -ne $Expected) {
        throw "$Name expected '$Expected' but got '$Actual'"
    }
}

Reset-Scratch
$sidecar = Join-Path $ScratchRoot "SignTexts.json"
$labels = @{
    "TEST_WORLD/BuildingBlock|ABC|1" = @{
        kind = "LabelText"
        text = "Alpha"
        stableId = "BuildingBlock|ABC|1"
    }
}

Write-AtomicSidecar -Path $sidecar -Json (New-SidecarJson -Revision 1 -Labels $labels)
$loaded = Select-RecoverySidecar -Path $sidecar
Assert-Equal $loaded.Count 1 "primary label count"
Assert-Equal $loaded.Revision 1 "primary revision"

$labels["TEST_WORLD/BuildingBlock|ABC|2"] = @{
    kind = "LabelText"
    text = "Beta"
    stableId = "BuildingBlock|ABC|2"
}
Write-AtomicSidecar -Path $sidecar -Json (New-SidecarJson -Revision 2 -Labels $labels)
$loaded = Select-RecoverySidecar -Path $sidecar
Assert-Equal $loaded.Count 2 "updated primary label count"
Assert-Equal $loaded.Revision 2 "updated primary revision"

Set-Content -LiteralPath $sidecar -Value "{broken" -Encoding UTF8
$loaded = Select-RecoverySidecar -Path $sidecar
Assert-Equal $loaded.Count 1 "backup fallback label count"
Assert-Equal $loaded.Revision 1 "backup fallback revision"

Remove-Item -LiteralPath "$sidecar.bak" -Force
$backupDir = Join-Path $ScratchRoot "Backups"
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
Set-Content -LiteralPath (Join-Path $backupDir "SignTexts.backup_20260506_000001.json") -Value (New-SidecarJson -Revision 3 -Labels $labels) -Encoding UTF8
$loaded = Select-RecoverySidecar -Path $sidecar
Assert-Equal $loaded.Count 2 "snapshot fallback label count"
Assert-Equal $loaded.Revision 3 "snapshot fallback revision"

Write-Host "[PASS] sidecar fixture tests passed"
exit 0
