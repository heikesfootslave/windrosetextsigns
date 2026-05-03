param(
    [string]$SourceLegacyRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy",
    [string]$OutputRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Text_SBMTemplate"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cloneScript = Join-Path $scriptDir "clone_wooden_label_text.ps1"
if (-not (Test-Path -LiteralPath $cloneScript)) {
    throw "Missing clone script: $cloneScript"
}

# SBM-template behavior:
# - keep shared recipe/display-key behavior from source utility item
# - create a new sibling DA asset identity
# - keep no-icon variant for this custom entry
& $cloneScript `
    -SourceLegacyRoot $SourceLegacyRoot `
    -OutputRoot $OutputRoot `
    -SourceAssetName "DA_BI_Utilities_Lables_Wooden_Ship_NoIcon" `
    -TargetAssetName "DA_BI_Utilities_Lables_Wooden_Text" `
    -LabelKeyFrom "Building_Lable_8" `
    -LabelKeyTo "Building_Lable_11"

$targetUasset = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uasset"
$targetUexp = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uexp"

foreach ($f in @($targetUasset, $targetUexp)) {
    if (-not (Test-Path -LiteralPath $f)) {
        throw "Missing expected generated file: $f"
    }
}

$uassetText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUasset))
$uexpText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUexp))

$checks = [ordered]@{
    has_new_asset_name = $uassetText.Contains("DA_BI_Utilities_Lables_Wooden_Text")
    has_new_asset_path = $uassetText.Contains("/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text")
    keeps_shared_recipe = $uassetText.Contains("DA_RD_BuildObject_Utilities_Labels_Wooden_T01")
    keeps_noicon_token = $uassetText.Contains("T_PlaqueT02_None")
    has_unique_label_key_11 = $uexpText.Contains("Building_Lable_11")
    no_ship_asset_name_remaining = -not $uassetText.Contains("DA_BI_Utilities_Lables_Wooden_Ship")
}

$allPass = $true
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("prepare_label_text_sbm_template.ps1")
$lines.Add("OutputRoot=$OutputRoot")
$lines.Add("")
$lines.Add("Checks:")
foreach ($k in $checks.Keys) {
    $v = [bool]$checks[$k]
    if (-not $v) { $allPass = $false }
    $lines.Add(("  {0}={1}" -f $k, $v))
}
$lines.Add("")
$lines.Add(("overall_pass={0}" -f $allPass))

$reportPath = Join-Path $OutputRoot "sbm_template_report.txt"
[IO.File]::WriteAllLines($reportPath, $lines)

Write-Host "Generated SBM-template label asset:"
Write-Host "  $targetUasset"
Write-Host "  $targetUexp"
Write-Host "Report:"
Write-Host "  $reportPath"
Write-Host "overall_pass=$allPass"

if (-not $allPass) {
    exit 2
}
