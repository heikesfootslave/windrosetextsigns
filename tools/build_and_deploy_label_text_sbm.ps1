param(
    [string]$InputRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Text_SBMTemplate",
    [string]$PackageBaseName = "WoodenLabel_Text_SBMTemplate",
    [string]$ModsDir = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods",
    [string]$ExpectedAssetToken = "DA_BI_Utilities_Lables_Wooden_Text",
    [string]$SourceAssetRelativePath = "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uasset",
    [string]$EngineVersion = "UE5_6",
    [switch]$SkipDeploy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ("{0} {1}" -f ([DateTime]::UtcNow.ToString("o")), $Message)
}

function Ensure-Command {
    param([Parameter(Mandatory = $true)][string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        throw "Required command not found in PATH: $Name"
    }
    return $cmd.Source
}

function Assert-ExpectedModsPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $expectedSuffix = "\R5\Content\Paks\~mods"
    if (-not $normalized.EndsWith($expectedSuffix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing deploy: ModsDir must end with '$expectedSuffix'. Got '$normalized'"
    }
}

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Invoke-Retoc {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & retoc @Args
    if ($LASTEXITCODE -ne 0) {
        throw "retoc failed (exit $LASTEXITCODE): retoc $($Args -join ' ')"
    }
}

Write-Step "Starting build/verify/deploy for SBM-template Wooden Label"
$retocPath = Ensure-Command -Name "retoc"
Write-Step "Using retoc: $retocPath"

if (-not (Test-Path -LiteralPath $InputRoot)) {
    throw "InputRoot does not exist: $InputRoot"
}

$contentRootCandidate = Join-Path $InputRoot "R5\Content"
$packSourceRoot = $InputRoot
if (Test-Path -LiteralPath $contentRootCandidate) {
    # Match SBM container path shape (../../../Gameplay/...) by packing from Content root.
    $packSourceRoot = $contentRootCandidate
}

$sourceAssetPath = Join-Path $packSourceRoot $SourceAssetRelativePath
if (-not (Test-Path -LiteralPath $sourceAssetPath)) {
    throw "Expected source asset missing: $sourceAssetPath"
}

$outUtoc = Join-Path $InputRoot ($PackageBaseName + ".utoc")
$outUcas = Join-Path $InputRoot ($PackageBaseName + ".ucas")
$outPak = Join-Path $InputRoot ($PackageBaseName + ".pak")
$modUtoc = Join-Path $ModsDir ($PackageBaseName + ".utoc")
$modUcas = Join-Path $ModsDir ($PackageBaseName + ".ucas")
$modPak = Join-Path $ModsDir ($PackageBaseName + ".pak")

Write-Step "Step 1/3: Building zen container"
Write-Step "Packing source root: $packSourceRoot"
Invoke-Retoc -Args @("to-zen", "--version", $EngineVersion, $packSourceRoot, $outUtoc)

foreach ($f in @($outUtoc, $outUcas, $outPak)) {
    if (-not (Test-Path -LiteralPath $f)) {
        throw "Build output missing: $f"
    }
    $len = (Get-Item -LiteralPath $f).Length
    if ($len -le 0) {
        throw "Build output is empty: $f"
    }
}
Write-Step "Build outputs verified (.utoc/.ucas/.pak)"

Write-Step "Step 2/3: Verifying container contains expected asset token"
$listOutput = & retoc list --path $outUtoc 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "retoc list failed (exit $LASTEXITCODE) for '$outUtoc'"
}
$joined = ($listOutput | Out-String)
if (-not ($joined -match [Regex]::Escape($ExpectedAssetToken))) {
    throw "Verification failed: token '$ExpectedAssetToken' not found in container listing."
}
Write-Step "Verification passed: found token '$ExpectedAssetToken'"

if ($SkipDeploy) {
    Write-Step "SkipDeploy enabled; stopping before deployment."
    Write-Host "Output container:"
    Write-Host "  $outUtoc"
    Write-Host "  $outUcas"
    Write-Host "  $outPak"
    exit 0
}

Write-Step "Step 3/3: Clean deploy to ~mods"
Assert-ExpectedModsPath -Path $ModsDir
Ensure-Directory -Path $ModsDir

$removeTargets = @(
    $modUtoc,
    $modUcas,
    $modPak
)

foreach ($target in $removeTargets) {
    if (Test-Path -LiteralPath $target) {
        Write-Step "Removing existing file: $target"
        Remove-Item -LiteralPath $target -Force
    }
}

Copy-Item -LiteralPath $outUtoc -Destination $modUtoc -Force
Copy-Item -LiteralPath $outUcas -Destination $modUcas -Force
Copy-Item -LiteralPath $outPak -Destination $modPak -Force

foreach ($target in $removeTargets) {
    if (-not (Test-Path -LiteralPath $target)) {
        throw "Deploy verification failed (missing target): $target"
    }
}

Write-Step "Deploy complete."
Write-Host "Deployed files:"
Write-Host ("  " + $modUtoc)
Write-Host ("  " + $modUcas)
Write-Host ("  " + $modPak)
