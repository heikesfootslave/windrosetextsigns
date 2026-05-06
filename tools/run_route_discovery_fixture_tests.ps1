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

function Parse-IPv4 {
    param([Parameter(Mandatory = $true)][string]$Ip)
    $parts = $Ip -split '\.'
    if ($parts.Count -ne 4) {
        return $null
    }
    $octets = @()
    foreach ($part in $parts) {
        if ($part -notmatch '^\d{1,3}$') {
            return $null
        }
        $value = [int]$part
        if ($value -lt 0 -or $value -gt 255) {
            return $null
        }
        $octets += $value
    }
    return $octets
}

function Test-PrivateIPv4 {
    param([Parameter(Mandatory = $true)][string]$Ip)
    $o = Parse-IPv4 -Ip $Ip
    if ($null -eq $o) {
        return $false
    }
    return $o[0] -eq 10 -or
        ($o[0] -eq 172 -and $o[1] -ge 16 -and $o[1] -le 31) -or
        ($o[0] -eq 192 -and $o[1] -eq 168) -or
        ($o[0] -eq 169 -and $o[1] -eq 254)
}

function Test-PublicIPv4 {
    param([Parameter(Mandatory = $true)][string]$Ip)
    $o = Parse-IPv4 -Ip $Ip
    if ($null -eq $o) {
        return $false
    }
    if ($o[0] -eq 0 -or $o[0] -eq 127 -or $o[0] -ge 224) {
        return $false
    }
    return -not (Test-PrivateIPv4 -Ip $Ip)
}

function Test-SameLanHint {
    param(
        [Parameter(Mandatory = $true)][string]$A,
        [Parameter(Mandatory = $true)][string]$B
    )
    $left = Parse-IPv4 -Ip $A
    $right = Parse-IPv4 -Ip $B
    if ($null -eq $left -or $null -eq $right) {
        return $false
    }
    if (-not (Test-PrivateIPv4 -Ip $A) -or -not (Test-PrivateIPv4 -Ip $B)) {
        return $false
    }
    return $left[0] -eq $right[0] -and $left[1] -eq $right[1] -and $left[2] -eq $right[2]
}

function Invoke-RouteDiscoveryFixture {
    param([Parameter(Mandatory = $true)][string]$LogText)

    $candidateRx = [regex]::new('\b(?:UDP|TCP)\s+([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)\s+[A-Fa-f0-9]+\s+\d+\s+(host|srflx)\b', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    $localHosts = [System.Collections.Generic.List[string]]::new()
    $remoteHost = ""
    $remotePublic = ""

    foreach ($line in ($LogText -split "`r?`n")) {
        $localLine = $line.Contains("Added Local Ice Candidates")
        $remoteLine = $line.Contains("SetRemoteIceData") -or $line.Contains("Added remote candidates")
        if (-not $localLine -and -not $remoteLine) {
            continue
        }

        foreach ($match in $candidateRx.Matches($line)) {
            $ip = $match.Groups[1].Value
            $type = $match.Groups[3].Value.ToLowerInvariant()
            if ($localLine -and $type -eq "host") {
                if (-not $localHosts.Contains($ip)) {
                    $localHosts.Add($ip)
                }
            } elseif ($remoteLine -and $type -eq "host") {
                $remoteHost = $ip
            } elseif ($remoteLine -and $type -eq "srflx" -and (Test-PublicIPv4 -Ip $ip)) {
                $remotePublic = $ip
            }
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($remoteHost) -and $localHosts.Contains($remoteHost)) {
        return [pscustomobject]@{
            Found = $true
            Host = "127.0.0.1"
            Reason = "same_machine_host_candidate"
            RemoteHostCandidate = $remoteHost
            RemotePublicCandidate = $remotePublic
            LocalHosts = ($localHosts -join ",")
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($remoteHost)) {
        foreach ($localHost in $localHosts) {
            if (Test-SameLanHint -A $localHost -B $remoteHost) {
                return [pscustomobject]@{
                    Found = $true
                    Host = $remoteHost
                    Reason = "same_lan_host_candidate"
                    RemoteHostCandidate = $remoteHost
                    RemotePublicCandidate = $remotePublic
                    LocalHosts = ($localHosts -join ",")
                }
            }
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($remotePublic)) {
        return [pscustomobject]@{
            Found = $true
            Host = $remotePublic
            Reason = "public_srflx_candidate"
            RemoteHostCandidate = $remoteHost
            RemotePublicCandidate = $remotePublic
            LocalHosts = ($localHosts -join ",")
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($remoteHost)) {
        return [pscustomobject]@{
            Found = $true
            Host = $remoteHost
            Reason = "remote_host_fallback"
            RemoteHostCandidate = $remoteHost
            RemotePublicCandidate = $remotePublic
            LocalHosts = ($localHosts -join ",")
        }
    }

    return [pscustomobject]@{
        Found = $false
        Host = ""
        Reason = ""
        RemoteHostCandidate = $remoteHost
        RemotePublicCandidate = $remotePublic
        LocalHosts = ($localHosts -join ",")
    }
}

function Invoke-Fixture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$LogText,
        [Parameter(Mandatory = $true)][bool]$ExpectedFound,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$ExpectedHost,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$ExpectedReason
    )

    try {
        $result = Invoke-RouteDiscoveryFixture -LogText $LogText
        Assert-Equal $ExpectedFound $result.Found "found mismatch"
        Assert-Equal $ExpectedHost $result.Host "host mismatch"
        Assert-Equal $ExpectedReason $result.Reason "reason mismatch"
        Write-TestResult -Status "PASS" -Name $Name -Detail "host=$($result.Host) reason=$($result.Reason)"
    } catch {
        $message = $_.Exception.Message
        $script:Failures.Add("$Name failed: $message")
        Write-TestResult -Status "FAIL" -Name $Name -Detail $message
    }
}

Write-Host "Bridge route discovery fixtures" -ForegroundColor Cyan

Invoke-Fixture `
    -Name "same machine host candidate becomes loopback" `
    -ExpectedFound $true `
    -ExpectedHost "127.0.0.1" `
    -ExpectedReason "same_machine_host_candidate" `
    -LogText @"
[2026.05.06] Added Local Ice Candidates: UDP 192.168.0.10:45801 ABCD 1 host UDP 50.53.209.78:45801 ABCD 1 srflx
[2026.05.06] SetRemoteIceData: UDP 192.168.0.10:45801 BEEF 1 host UDP 50.53.209.78:45801 BEEF 1 srflx
"@

Invoke-Fixture `
    -Name "same LAN private candidate wins over public candidate" `
    -ExpectedFound $true `
    -ExpectedHost "192.168.0.10" `
    -ExpectedReason "same_lan_host_candidate" `
    -LogText @"
[2026.05.06] Added Local Ice Candidates: UDP 192.168.0.55:45801 ABCD 1 host
[2026.05.06] Added remote candidates: UDP 192.168.0.10:45801 BEEF 1 host UDP 50.53.209.78:45801 BEEF 1 srflx
"@

Invoke-Fixture `
    -Name "internet client chooses public srflx candidate" `
    -ExpectedFound $true `
    -ExpectedHost "50.53.209.78" `
    -ExpectedReason "public_srflx_candidate" `
    -LogText @"
[2026.05.06] Added Local Ice Candidates: UDP 172.20.10.6:45801 ABCD 1 host
[2026.05.06] SetRemoteIceData: UDP 192.168.0.10:45801 BEEF 1 host UDP 50.53.209.78:45801 BEEF 1 srflx
"@

Invoke-Fixture `
    -Name "remote host fallback when public candidate absent" `
    -ExpectedFound $true `
    -ExpectedHost "203.0.113.20" `
    -ExpectedReason "remote_host_fallback" `
    -LogText @"
[2026.05.06] Added Local Ice Candidates: UDP 10.7.0.4:45801 ABCD 1 host
[2026.05.06] SetRemoteIceData: UDP 203.0.113.20:45801 BEEF 1 host
"@

Invoke-Fixture `
    -Name "invalid private srflx is ignored" `
    -ExpectedFound $true `
    -ExpectedHost "198.51.100.9" `
    -ExpectedReason "remote_host_fallback" `
    -LogText @"
[2026.05.06] Added Local Ice Candidates: UDP 10.7.0.4:45801 ABCD 1 host
[2026.05.06] SetRemoteIceData: UDP 198.51.100.9:45801 BEEF 1 host UDP 192.168.0.44:45801 BEEF 1 srflx
"@

Invoke-Fixture `
    -Name "no candidates keeps waiting" `
    -ExpectedFound $false `
    -ExpectedHost "" `
    -ExpectedReason "" `
    -LogText @"
[2026.05.06] R5LogNet: unrelated line
[2026.05.06] Added Local Ice Candidates: no parseable candidates here
"@

if ($script:Failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Route fixture failures:" -ForegroundColor Red
    foreach ($failure in $script:Failures) {
        Write-Host "FAIL: $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "Route fixture summary: pass" -ForegroundColor Cyan
exit 0
