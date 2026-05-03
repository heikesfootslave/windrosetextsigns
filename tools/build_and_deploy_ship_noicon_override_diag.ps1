param(
    [string]$SourceLegacyRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy",
    [string]$StageRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_ShipNoIcon_Diag",
    [string]$PackageBaseName = "pakchunk999-LabelDiag_P",
    [string]$ModsDir = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods",
    [switch]$SkipDeploy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ("{0} {1}" -f ([DateTime]::UtcNow.ToString("o")), $Message)
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDeployScript = Join-Path $scriptDir "build_and_deploy_label_text_sbm.ps1"
if (-not (Test-Path -LiteralPath $buildDeployScript)) {
    throw "Missing required script: $buildDeployScript"
}

$srcDir = Join-Path $SourceLegacyRoot "R5\Content\Gameplay\Building\BuildingUtilities"
$srcUasset = Join-Path $srcDir "DA_BI_Utilities_Lables_Wooden_Ship_NoIcon.uasset"
$srcUexp = Join-Path $srcDir "DA_BI_Utilities_Lables_Wooden_Ship_NoIcon.uexp"
if (-not (Test-Path -LiteralPath $srcUasset)) { throw "Missing source file: $srcUasset" }
if (-not (Test-Path -LiteralPath $srcUexp)) { throw "Missing source file: $srcUexp" }

$dstDir = Join-Path $StageRoot "R5\Content\Gameplay\Building\BuildingUtilities"
$dstUasset = Join-Path $dstDir "DA_BI_Utilities_Lables_Wooden_Ship.uasset"
$dstUexp = Join-Path $dstDir "DA_BI_Utilities_Lables_Wooden_Ship.uexp"

if (Test-Path -LiteralPath $StageRoot) {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $dstDir -Force | Out-Null

# Diagnostic override:
# ship filename/path + noicon payload
Copy-Item -LiteralPath $srcUasset -Destination $dstUasset -Force
Copy-Item -LiteralPath $srcUexp -Destination $dstUexp -Force

Write-Step "Prepared diagnostic override stage:"
Write-Host "  $dstUasset"
Write-Host "  $dstUexp"

$params = @{
    InputRoot = $StageRoot
    PackageBaseName = $PackageBaseName
    ModsDir = $ModsDir
    ExpectedAssetToken = "DA_BI_Utilities_Lables_Wooden_Ship"
    SourceAssetRelativePath = "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Ship.uasset"
}
if ($SkipDeploy) { $params.SkipDeploy = $true }

Write-Step "Building/deploying diagnostic package via build_and_deploy_label_text_sbm.ps1"
& $buildDeployScript @params
if ($LASTEXITCODE -ne 0) {
    throw "Diagnostic build/deploy failed with exit code $LASTEXITCODE"
}

Write-Step "Diagnostic package completed."
Write-Host "In-game expected effect if loaded:"
Write-Host "  Label: Ship icon should become blank/no-icon."
