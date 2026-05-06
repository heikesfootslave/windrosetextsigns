param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [switch]$SkipGit,
    [switch]$SkipNode
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:Failures = [System.Collections.Generic.List[string]]::new()
$script:Warnings = [System.Collections.Generic.List[string]]::new()

function Write-Result {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("PASS", "WARN", "FAIL")]
        [string]$Status,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$Detail = ""
    )

    $line = if ([string]::IsNullOrWhiteSpace($Detail)) {
        "[{0}] {1}" -f $Status, $Name
    } else {
        "[{0}] {1}: {2}" -f $Status, $Name, $Detail
    }

    switch ($Status) {
        "PASS" { Write-Host $line -ForegroundColor Green }
        "WARN" { Write-Host $line -ForegroundColor Yellow }
        "FAIL" { Write-Host $line -ForegroundColor Red }
    }
}

function Add-Failure {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:Failures.Add($Message)
}

function Add-Warning {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:Warnings.Add($Message)
}

function Test-CommandExists {
    param([Parameter(Mandatory = $true)][string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-RelativeRepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $base = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\') + '\'
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($base.Length)
    }
    return $full
}

function Invoke-Check {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Body
    )

    try {
        $detail = & $Body
        Write-Result -Status "PASS" -Name $Name -Detail ([string]$detail)
    } catch {
        $message = $_.Exception.Message
        Add-Failure "$Name failed: $message"
        Write-Result -Status "FAIL" -Name $Name -Detail $message
    }
}

function Invoke-WarnCheck {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Body
    )

    try {
        $detail = & $Body
        Write-Result -Status "PASS" -Name $Name -Detail ([string]$detail)
    } catch {
        $message = $_.Exception.Message
        Add-Warning "$Name warning: $message"
        Write-Result -Status "WARN" -Name $Name -Detail $message
    }
}

function Get-RepoFiles {
    param([string[]]$Extensions)

    $excludedDirs = @(
        ".git",
        "build_logs",
        "Deployments",
        "_scratch\target",
        "_scratch\pak_extract\target"
    )

    Get-ChildItem -LiteralPath $RepoRoot -Recurse -File |
        Where-Object {
            $relative = Get-RelativeRepoPath -Path $_.FullName
            foreach ($dir in $excludedDirs) {
                if ($relative.StartsWith($dir, [StringComparison]::OrdinalIgnoreCase)) {
                    return $false
                }
            }
            return $Extensions -contains $_.Extension.ToLowerInvariant()
        }
}

function Assert-PowerShellParses {
    $errors = [System.Collections.Generic.List[object]]::new()
    $files = Get-RepoFiles -Extensions @(".ps1", ".psm1")
    foreach ($file in $files) {
        $tokens = $null
        $parseErrors = $null
        [System.Management.Automation.Language.Parser]::ParseFile(
            $file.FullName,
            [ref]$tokens,
            [ref]$parseErrors) | Out-Null
        foreach ($parseError in $parseErrors) {
            $errors.Add([pscustomobject]@{
                File = Get-RelativeRepoPath -Path $file.FullName
                Error = $parseError.Message
                Text = $parseError.Extent.Text
            })
        }
    }

    if ($errors.Count -gt 0) {
        $text = ($errors | Format-Table -AutoSize | Out-String).Trim()
        throw "PowerShell parse errors:`n$text"
    }
    return "$($files.Count) PowerShell files parsed"
}

function Assert-IniSane {
    $iniPath = Join-Path $RepoRoot "Config\WindroseTextSigns.ini"
    if (-not (Test-Path -LiteralPath $iniPath)) {
        throw "Missing $iniPath"
    }

    $entries = [ordered]@{}
    $duplicates = [System.Collections.Generic.List[string]]::new()
    $orderedKeys = [System.Collections.Generic.List[string]]::new()
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $iniPath) {
        ++$lineNumber
        if ($line -notmatch "^\s*[^#;].*=") {
            continue
        }
        $parts = $line -split "=", 2
        $key = $parts[0].Trim()
        $value = if ($parts.Count -gt 1) { $parts[1].Trim() } else { "" }
        if ($entries.Contains($key)) {
            $duplicates.Add("$key at line $lineNumber")
        } else {
            $entries[$key] = $value
            $orderedKeys.Add($key)
        }
    }

    if ($duplicates.Count -gt 0) {
        throw "Duplicate INI keys: $($duplicates -join ', ')"
    }

    $requiredDefaults = @{
        "WTS_BRIDGE_SERVER_HOST" = "auto"
        "WTS_BRIDGE_UDP_PORT" = "45801"
        "WTS_BRIDGE_UPNP_ENABLED" = "true"
        "WTS_MAX_TARGET_DISTANCE" = "1000"
        "WTS_VERBOSE_LOG" = "false"
        "WTS_WORLD_TEXT_FONT_ENABLED" = "false"
        "WTS_WORLD_TEXT_FONT_ASSET" = "none"
        "WTS_WORLD_TEXT_FONT_NAME_HINT" = ""
        "WTS_WORLD_TEXT_FONT_NATIVE_FALLBACK" = "false"
        "WTS_WORLD_TEXT_FONT_INVENTORY_PROBE" = "false"
        "WTS_RELAY_ENABLED" = "false"
        "WTS_NATIVE_TRANSPORT_INVENTORY_PROBE" = "false"
        "WTS_PLAYER_MARKER_REPLICATION_PROBE" = "false"
    }
    foreach ($key in $requiredDefaults.Keys) {
        if (-not $entries.Contains($key)) {
            throw "Missing INI key $key"
        }
        if ([string]$entries[$key] -ne $requiredDefaults[$key]) {
            throw "Unexpected default $key=$($entries[$key]); expected $($requiredDefaults[$key])"
        }
    }

    $expectedTopKeys = @(
        "WTS_ENABLED",
        "WTS_HOTKEY",
        "WTS_MAX_TARGET_DISTANCE",
        "WTS_MIN_VIEW_DOT",
        "WTS_BRIDGE_SERVER_HOST",
        "WTS_BRIDGE_UDP_PORT",
        "WTS_BRIDGE_UPNP_ENABLED"
    )
    for ($i = 0; $i -lt $expectedTopKeys.Count; ++$i) {
        if ($orderedKeys.Count -le $i -or $orderedKeys[$i] -ne $expectedTopKeys[$i]) {
            throw "Production INI keys are not at the top. Expected first keys: $($expectedTopKeys -join ', ')"
        }
    }

    return "$($entries.Count) keys, required defaults OK"
}

function Assert-EnabledTxtPresent {
    $enabledPath = Join-Path $RepoRoot "enabled.txt"
    if (-not (Test-Path -LiteralPath $enabledPath)) {
        throw "Missing enabled.txt"
    }
    return "enabled.txt present"
}

function Assert-JsonFilesParse {
    $jsonFiles = @(
        (Join-Path $RepoRoot "relay\cloudflare-worker\package.json")
    )

    $parsed = 0
    foreach ($path in $jsonFiles) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Missing JSON file: $path"
        }
        Get-Content -LiteralPath $path -Raw | ConvertFrom-Json | Out-Null
        ++$parsed
    }
    return "$parsed JSON files parsed"
}

function Assert-FontAssetsReady {
    $fontDir = Join-Path $RepoRoot "assets\fonts"
    $ttf = Join-Path $fontDir "Pencilant Script.ttf"
    $otf = Join-Path $fontDir "Pencilant Script.otf"
    if (-not (Test-Path -LiteralPath $ttf) -and -not (Test-Path -LiteralPath $otf)) {
        throw "Missing Pencilant Script raw font under assets\fonts"
    }

    $packagedCandidates = @(
        (Join-Path $fontDir "PencilantScript.uasset"),
        (Join-Path $fontDir "PencilantScript_Font.uasset"),
        (Join-Path $RepoRoot "build_logs\WoodenLabel_Text_SBMTemplate_stage\R5\Content\WindroseTextSigns\Fonts\PencilantScript.uasset"),
        (Join-Path $RepoRoot "build_logs\WoodenLabel_Text_SBMTemplate_stage\R5\Content\WindroseTextSigns\Fonts\PencilantScript_Font.uasset")
    )
    foreach ($candidate in $packagedCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            return "raw font present; packaged UFont candidate present: $(Get-RelativeRepoPath -Path $candidate)"
        }
    }

    throw "Raw Pencilant font exists, but no cooked UFont .uasset candidate is staged yet"
}

function Assert-ReleaseDocsPresent {
    $readmePath = Join-Path $RepoRoot "README.md"
    if (-not (Test-Path -LiteralPath $readmePath)) {
        throw "Missing README.md"
    }
    $text = Get-Content -LiteralPath $readmePath -Raw
    $requiredTokens = @(
        "Downloads",
        "Installation",
        "Configuration",
        "WTS_BRIDGE_SERVER_HOST=auto",
        "WTS_BRIDGE_UDP_PORT=45801",
        "UPnP",
        "static server address",
        "Font Notes",
        "Known Limitations"
    )
    foreach ($token in $requiredTokens) {
        if (-not $text.Contains($token)) {
            throw "README missing release-hardening token: $token"
        }
    }
    return "release docs include bridge/font production notes"
}

function Assert-DeployDefaults {
    $deployPath = Join-Path $RepoRoot "deploy_TextSigns_clean.ps1"
    if (-not (Test-Path -LiteralPath $deployPath)) {
        throw "Missing deploy_TextSigns_clean.ps1"
    }
    $text = Get-Content -LiteralPath $deployPath -Raw
    $requiredTokens = @(
        "[switch]`$EnableContentPackage",
        "Content package disabled by default after F8-convert pivot",
        "Use -EnableContentPackage to build/deploy pak files",
        "Clean-ContentPackagesFromTarget"
    )
    foreach ($token in $requiredTokens) {
        if (-not $text.Contains($token)) {
            throw "deploy script missing default-clean content-package token: $token"
        }
    }
    if ($text.Contains("[switch]`$DisableContentPackage") -or $text.Contains("DisableContentPackage set")) {
        throw "deploy script still exposes DisableContentPackage opt-out behavior"
    }
    return "content pak disabled/cleaned by default; opt-in uses -EnableContentPackage"
}

function Assert-NodeSyntax {
    if (-not (Test-CommandExists -Name "node")) {
        throw "node not found; skipping JS syntax check"
    }
    $workerPath = Join-Path $RepoRoot "relay\cloudflare-worker\src\index.js"
    if (-not (Test-Path -LiteralPath $workerPath)) {
        throw "Missing $workerPath"
    }
    $output = & node --check $workerPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ($output | Out-String).Trim()
    }
    return "relay worker syntax OK"
}

function Assert-StaleReferencesAbsent {
    $patterns = @(
        "mods\.txt",
        "enable_phase5_placement_probe\.flag",
        "\bF10\b",
        "WTS_.*PROBE\s*=\s*true",
        "WTS_RELAY_ENABLED\s*=\s*true"
    )
    $includeExts = @(".cpp", ".hpp", ".ps1", ".ini", ".md", ".js", ".json", ".lua")
    $files = Get-RepoFiles -Extensions $includeExts
    $hits = [System.Collections.Generic.List[string]]::new()

    foreach ($file in $files) {
        $relative = Get-RelativeRepoPath -Path $file.FullName
        if ($relative -eq "tools\run_offline_qa.ps1") {
            continue
        }
        foreach ($pattern in $patterns) {
            $matches = Select-String -LiteralPath $file.FullName -Pattern $pattern -AllMatches
            foreach ($match in $matches) {
                $hits.Add("{0}:{1}: {2}" -f $relative, $match.LineNumber, $match.Line.Trim())
            }
        }
    }

    if ($hits.Count -gt 0) {
        throw "Stale references found:`n$($hits -join "`n")"
    }
    return "no stale refs/default-enabled probes found"
}

function Assert-GitDiffCheck {
    if (-not (Test-CommandExists -Name "git")) {
        throw "git not found"
    }
    $oldLocation = Get-Location
    $oldErrorActionPreference = $ErrorActionPreference
    try {
        Set-Location -LiteralPath $RepoRoot
        $ErrorActionPreference = "Continue"
        $output = & git diff --check 2>&1
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldErrorActionPreference
        $outputText = ($output | Out-String).Trim()
        $lines = @($outputText -split "\r?\n")
        $actionable = @($lines | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            $_ -match ":\d+:" -and
            $_ -notmatch "will be replaced by" -and
            $_ -notmatch "NativeCommandError" -and
            $_ -notmatch "CategoryInfo" -and
            $_ -notmatch "FullyQualifiedErrorId" -and
            $_ -notmatch "run_offline_qa\.ps1" -and
            $_ -notmatch "^\s*\+ " -and
            $_ -notmatch "^\s*~"
        })
        if ($exitCode -ne 0 -and $actionable.Count -gt 0) {
            throw ($actionable -join "`n")
        }
        $text = ($actionable -join "`n").Trim()
        if ([string]::IsNullOrWhiteSpace($text)) {
            return "no whitespace errors"
        }
        return $text
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
        Set-Location -LiteralPath $oldLocation
    }
}

function Assert-NoForbiddenGeneratedTargets {
    $badPaths = @(
        "tools\deploy_text_signs_asset_loader_lua.ps1"
    )
    foreach ($relative in $badPaths) {
        $path = Join-Path $RepoRoot $relative
        if (Test-Path -LiteralPath $path) {
            throw "Stale file still exists: $relative"
        }
    }
    return "known stale files absent"
}

function Invoke-FixtureScript {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $scriptPath = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        throw "Missing fixture script: $RelativePath"
    }
    $output = & $scriptPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw (($output | Out-String).Trim())
    }
    return "$Name passed"
}

$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (-not (Test-Path -LiteralPath $RepoRoot)) {
    throw "RepoRoot not found: $RepoRoot"
}

Write-Host "WindroseTextSigns offline QA" -ForegroundColor Cyan
Write-Host "RepoRoot: $RepoRoot"

Invoke-Check -Name "PowerShell parse" -Body { Assert-PowerShellParses }
Invoke-Check -Name "INI sanity" -Body { Assert-IniSane }
Invoke-Check -Name "enabled.txt" -Body { Assert-EnabledTxtPresent }
Invoke-Check -Name "JSON parse" -Body { Assert-JsonFilesParse }
Invoke-WarnCheck -Name "font asset readiness" -Body { Assert-FontAssetsReady }
Invoke-Check -Name "release docs" -Body { Assert-ReleaseDocsPresent }
Invoke-Check -Name "deploy defaults" -Body { Assert-DeployDefaults }
Invoke-Check -Name "stale file cleanup" -Body { Assert-NoForbiddenGeneratedTargets }
Invoke-Check -Name "stale reference scan" -Body { Assert-StaleReferencesAbsent }
Invoke-Check -Name "sidecar persistence fixtures" -Body { Invoke-FixtureScript -RelativePath "tools\run_sidecar_fixture_tests.ps1" -Name "sidecar persistence fixtures" }
Invoke-Check -Name "bridge payload fixtures" -Body { Invoke-FixtureScript -RelativePath "tools\run_bridge_fixture_tests.ps1" -Name "bridge payload fixtures" }
Invoke-Check -Name "route discovery fixtures" -Body { Invoke-FixtureScript -RelativePath "tools\run_route_discovery_fixture_tests.ps1" -Name "route discovery fixtures" }

if ($SkipNode) {
    Add-Warning "Node syntax check skipped by -SkipNode"
    Write-Result -Status "WARN" -Name "Node syntax" -Detail "skipped"
} else {
    Invoke-WarnCheck -Name "Node syntax" -Body { Assert-NodeSyntax }
}

if ($SkipGit) {
    Add-Warning "git diff --check skipped by -SkipGit"
    Write-Result -Status "WARN" -Name "git diff --check" -Detail "skipped"
} else {
    Invoke-Check -Name "git diff --check" -Body { Assert-GitDiffCheck }
}

Write-Host ""
Write-Host ("Summary: {0} failure(s), {1} warning(s)" -f $script:Failures.Count, $script:Warnings.Count) -ForegroundColor Cyan
if ($script:Warnings.Count -gt 0) {
    foreach ($warning in $script:Warnings) {
        Write-Host "WARN: $warning" -ForegroundColor Yellow
    }
}
if ($script:Failures.Count -gt 0) {
    foreach ($failure in $script:Failures) {
        Write-Host "FAIL: $failure" -ForegroundColor Red
    }
    exit 1
}

exit 0
