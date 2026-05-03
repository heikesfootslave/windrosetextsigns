param(
    [string]$SbmZipPath = "C:\Users\User\Downloads\SBM_ContainersChest_P.zip",
    [string]$StageRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\WoodenLabel_CategoryNativeProbe_stage",
    [string]$PackageBaseName = "WoodenLabel_CategoryNativeProbe_P",
    [string]$ModsDir = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods",
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

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Remove-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Path)
    Ensure-Directory -Path $Path
    Get-ChildItem -LiteralPath $Path -Force | Remove-Item -Recurse -Force
}

function Assert-ExpectedModsPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $normalized.EndsWith("\R5\Content\Paks\~mods", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing deploy to unexpected mods path: $normalized"
    }
}

function Invoke-Repak {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & repak @Args
    if ($LASTEXITCODE -ne 0) {
        throw "repak failed (exit $LASTEXITCODE): repak $($Args -join ' ')"
    }
}

Write-Step "Starting native wooden-label category probe"
$repakPath = Ensure-Command -Name "repak"
Write-Step "Using repak: $repakPath"

if (-not (Test-Path -LiteralPath $SbmZipPath)) {
    throw "SBM reference zip not found: $SbmZipPath"
}

Write-Step "Preparing stage root: $StageRoot"
Remove-DirectoryContents -Path $StageRoot

$zipExtractRoot = Join-Path $StageRoot "zip_extract"
Ensure-Directory -Path $zipExtractRoot
Expand-Archive -LiteralPath $SbmZipPath -DestinationPath $zipExtractRoot -Force
$sourcePak = Join-Path $zipExtractRoot "SBM_ContainersChest_P\SBM_ContainersChest_P.pak"
if (-not (Test-Path -LiteralPath $sourcePak)) {
    throw "SBM pak not found after zip extract: $sourcePak"
}

$pakExtractRoot = Join-Path $StageRoot "pak_extract"
Write-Step "Extracting category JSON from SBM reference pak"
Invoke-Repak -Args @(
    "unpack", "-q", "-f",
    "-o", $pakExtractRoot,
    "-i", "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json",
    $sourcePak
)

$categoryJsonPath = Join-Path $pakExtractRoot "R5\Content\UI\HUD\Building\DA_BuildingUICategories.json"
if (-not (Test-Path -LiteralPath $categoryJsonPath)) {
    throw "Category JSON missing after extraction: $categoryJsonPath"
}

Write-Step "Appending one native Label: Ship entry under Wooden Labels"
$categoryJson = Get-Content -LiteralPath $categoryJsonPath -Raw | ConvertFrom-Json
$utilitiesCategory = ($categoryJson.CategoriesMap.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Utilities"}' } | Select-Object -First 1).Value
if ($null -eq $utilitiesCategory) {
    throw "Could not find UI.Building.Utilities category."
}
$storageGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "ComfortType.Utilities.Storage"}' } | Select-Object -First 1).Value
if ($null -ne $storageGroup) {
    $storageGroup.Items = @($storageGroup.Items | Where-Object {
        $_ -notmatch "DA_BI_Utilities_Storage_WoodenChest_(Alchemy|Clothing|Food|FoodIngridients|Ore|Ship|Trade|Treasure|Weapons|Wood)"
    })
}
$woodLabelsGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Lables.Wood"}' } | Select-Object -First 1).Value
if ($null -eq $woodLabelsGroup) {
    throw "Could not find UI.Building.Lables.Wood group."
}
$nativeShipEntry = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Ship.DA_BI_Utilities_Lables_Wooden_Ship"
$woodLabelsGroup.Items += $nativeShipEntry
$categoryJson | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $categoryJsonPath -Encoding ASCII

$outPak = Join-Path $StageRoot ($PackageBaseName + ".pak")
Write-Step "Building pak with category JSON only"
$pakInputRoot = Join-Path $pakExtractRoot "R5\Content"
Invoke-Repak -Args @("pack", "-q", "--version", "V3", "--compression", "Zlib", "-m", "../../../R5/Content/", $pakInputRoot, $outPak)

$pakText = (& repak get $outPak "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json" | Out-String)
if (-not $pakText.Contains("DA_BI_Utilities_Lables_Wooden_Ship")) {
    throw "Verification failed: native ship label entry missing."
}
if ($pakText.Contains("DA_BI_Utilities_Lables_Wooden_Text")) {
    throw "Verification failed: Text label entry unexpectedly present."
}
Write-Step "Verification passed"

if ($SkipDeploy) {
    Write-Step "SkipDeploy enabled; stopping before deployment."
    Write-Host "Output pak:"
    Write-Host "  $outPak"
    exit 0
}

Write-Step "Deploying category-only probe to client ~mods"
Assert-ExpectedModsPath -Path $ModsDir
Ensure-Directory -Path $ModsDir
$targetDir = Join-Path $ModsDir $PackageBaseName
if (Test-Path -LiteralPath $targetDir) {
    Remove-Item -LiteralPath $targetDir -Recurse -Force
}
Ensure-Directory -Path $targetDir
Copy-Item -LiteralPath $outPak -Destination (Join-Path $targetDir ($PackageBaseName + ".pak")) -Force

Write-Step "Deploy complete"
Write-Host "Deployed file:"
Write-Host ("  " + (Join-Path $targetDir ($PackageBaseName + ".pak")))
