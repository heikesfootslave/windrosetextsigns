param(
    [string]$SbmZipPath = "C:\Users\User\Downloads\SBM_ContainersChest_P.zip",
    [string]$StageRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\SBM_OriginalContainer_OneChest_stage",
    [string]$PackageBaseName = "SBM_ContainersChest_P",
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
    $expectedSuffix = "\R5\Content\Paks\~mods"
    if (-not $normalized.EndsWith($expectedSuffix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing deploy: ModsDir must end with '$expectedSuffix'. Got '$normalized'"
    }
}

function Invoke-Repak {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & repak @Args
    if ($LASTEXITCODE -ne 0) {
        throw "repak failed (exit $LASTEXITCODE): repak $($Args -join ' ')"
    }
}

Write-Step "Starting original-container SBM one-chest probe"
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

$sourceRoot = Join-Path $zipExtractRoot $PackageBaseName
$sourcePak = Join-Path $sourceRoot ($PackageBaseName + ".pak")
$sourceUcas = Join-Path $sourceRoot ($PackageBaseName + ".ucas")
$sourceUtoc = Join-Path $sourceRoot ($PackageBaseName + ".utoc")
foreach ($path in @($sourcePak, $sourceUcas, $sourceUtoc)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Expected SBM package file missing: $path"
    }
}

$pakExtractRoot = Join-Path $StageRoot "pak_extract"
Write-Step "Extracting original SBM pak"
Invoke-Repak -Args @("unpack", "-q", "-f", "-o", $pakExtractRoot, $sourcePak)

$categoryJsonPath = Join-Path $pakExtractRoot "R5\Content\UI\HUD\Building\DA_BuildingUICategories.json"
if (-not (Test-Path -LiteralPath $categoryJsonPath)) {
    throw "Expected build-menu category JSON was not extracted: $categoryJsonPath"
}

Write-Step "Patching category JSON to keep only the SBM Ship chest addition"
$categoryJson = Get-Content -LiteralPath $categoryJsonPath -Raw | ConvertFrom-Json
$utilitiesCategory = ($categoryJson.CategoriesMap.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Utilities"}' } | Select-Object -First 1).Value
if ($null -eq $utilitiesCategory) {
    throw "Could not find UI.Building.Utilities category."
}
$storageGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "ComfortType.Utilities.Storage"}' } | Select-Object -First 1).Value
if ($null -eq $storageGroup) {
    throw "Could not find ComfortType.Utilities.Storage group."
}
$storageGroup.Items = @($storageGroup.Items | Where-Object {
    $_ -notmatch "DA_BI_Utilities_Storage_WoodenChest_(Alchemy|Clothing|Food|FoodIngridients|Ore|Trade|Treasure|Weapons|Wood)"
})
$shipEntry = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_Ship.DA_BI_Utilities_Storage_WoodenChest_Ship"
if ($storageGroup.Items -notcontains $shipEntry) {
    throw "Patched category JSON does not contain the Ship chest entry."
}
$categoryJson | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $categoryJsonPath -Encoding ASCII

$outPak = Join-Path $StageRoot ($PackageBaseName + ".pak")
$outUcas = Join-Path $StageRoot ($PackageBaseName + ".ucas")
$outUtoc = Join-Path $StageRoot ($PackageBaseName + ".utoc")

Write-Step "Repacking patched SBM pak"
$pakInputRoot = Join-Path $pakExtractRoot "R5\Content"
Invoke-Repak -Args @("pack", "-q", "--version", "V3", "--compression", "Zlib", "-m", "../../../R5/Content/", $pakInputRoot, $outPak)

Copy-Item -LiteralPath $sourceUcas -Destination $outUcas -Force
Copy-Item -LiteralPath $sourceUtoc -Destination $outUtoc -Force

foreach ($path in @($outPak, $outUcas, $outUtoc)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Build output missing: $path"
    }
    if ((Get-Item -LiteralPath $path).Length -le 0) {
        throw "Build output is empty: $path"
    }
}

Write-Step "Verifying patched pak"
$pakText = (& repak get $outPak "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json" | Out-String)
foreach ($token in @("WoodenChest_Ship", "WoodenChest_01")) {
    if (-not $pakText.Contains($token)) {
        throw "Verification failed: '$token' missing from patched category JSON."
    }
}
foreach ($token in @("WoodenChest_Alchemy", "WoodenChest_Wood", "WoodenChest_Treasure")) {
    if ($pakText.Contains($token)) {
        throw "Verification failed: '$token' still present in patched category JSON."
    }
}
Write-Step "Verification passed"

if ($SkipDeploy) {
    Write-Step "SkipDeploy enabled; stopping before deployment."
    Write-Host "Output files:"
    Write-Host "  $outPak"
    Write-Host "  $outUcas"
    Write-Host "  $outUtoc"
    exit 0
}

Write-Step "Deploying original-container probe to SBM-style ~mods subfolder"
Assert-ExpectedModsPath -Path $ModsDir
Ensure-Directory -Path $ModsDir
$targetDir = Join-Path $ModsDir $PackageBaseName
Ensure-Directory -Path $targetDir
foreach ($ext in @("pak", "ucas", "utoc")) {
    $from = Join-Path $StageRoot ($PackageBaseName + "." + $ext)
    $rootLevelStale = Join-Path $ModsDir ($PackageBaseName + "." + $ext)
    if (Test-Path -LiteralPath $rootLevelStale) {
        Remove-Item -LiteralPath $rootLevelStale -Force
    }
    $to = Join-Path $targetDir ($PackageBaseName + "." + $ext)
    Copy-Item -LiteralPath $from -Destination $to -Force
}

Write-Step "Deploy complete"
Write-Host "Deployed files:"
foreach ($ext in @("pak", "ucas", "utoc")) {
    Write-Host ("  " + (Join-Path $targetDir ($PackageBaseName + "." + $ext)))
}
