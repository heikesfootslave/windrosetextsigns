param(
    [string]$SbmRawRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\SBM_ContainersChest_P_raw",
    [string]$SbmZipPath = "C:\Users\User\Downloads\SBM_ContainersChest_P.zip",
    [string]$SbmPakPath = "",
    [string]$StageRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\SBM_OneChest_Ship_stage",
    [string]$PackageBaseName = "SBM_OneChest_Ship_P",
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

function Remove-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Path)
    Ensure-Directory -Path $Path
    Get-ChildItem -LiteralPath $Path -Force | Remove-Item -Recurse -Force
}

function Copy-ProbeChunk {
    param(
        [Parameter(Mandatory = $true)][string]$ChunkId,
        [Parameter(Mandatory = $true)][string]$SourceChunksRoot,
        [Parameter(Mandatory = $true)][string]$DestinationChunksRoot
    )
    $src = Join-Path $SourceChunksRoot $ChunkId
    if (-not (Test-Path -LiteralPath $src)) {
        throw "Required SBM raw chunk missing: $src"
    }

    Ensure-Directory -Path $DestinationChunksRoot
    $dst = Join-Path $DestinationChunksRoot $ChunkId
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

function Invoke-Retoc {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & retoc @Args
    if ($LASTEXITCODE -ne 0) {
        throw "retoc failed (exit $LASTEXITCODE): retoc $($Args -join ' ')"
    }
}

function Invoke-Repak {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & repak @Args
    if ($LASTEXITCODE -ne 0) {
        throw "repak failed (exit $LASTEXITCODE): repak $($Args -join ' ')"
    }
}

$probeAssetPaths = @(
    "../../../Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_Ship.uasset",
    "../../../Gameplay/Inventory/BP_Storage_WoodenChest_Ship.uasset",
    "../../../Environment/Gameplay/Building/BuildingDecoration/SM_SBM_WoodenChest_Ship_03.uasset",
    "../../../Environment/Gameplay/Building/BuildingDecoration/SM_SBM_WoodenChest_Ship_03.ubulk",
    "../../../Environment/Gameplay/Workbenches/Materials/MI_Building_WoodenChest_Ship_01.uasset",
    "../../../UI/HUD/Building/Icons/BuildingBits/SBM/T_PlaqueT02_Ship.uasset",
    "../../../UI/HUD/Building/Icons/BuildingBits/SBM/T_PlaqueT02_Ship.ubulk",
    "../../../UI/HUD/Building/Icons/BuildingBits/SBM/IconsUI/T_BI_Craft_WoodenChest_SBM_Ship.uasset",
    "../../../UI/HUD/Building/Icons/BuildingBits/SBM/IconsUI/T_BI_Craft_WoodenChest_SBM_Ship.ubulk"
)

$probePakPaths = @(
    "R5/Content/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_Ship.uasset",
    "R5/Content/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_Ship.uexp",
    "R5/Content/Gameplay/Inventory/BP_Storage_WoodenChest_Ship.uasset",
    "R5/Content/Gameplay/Inventory/BP_Storage_WoodenChest_Ship.uexp",
    "R5/Content/Environment/Gameplay/Building/BuildingDecoration/SM_SBM_WoodenChest_Ship_03.uasset",
    "R5/Content/Environment/Gameplay/Building/BuildingDecoration/SM_SBM_WoodenChest_Ship_03.uexp",
    "R5/Content/Environment/Gameplay/Building/BuildingDecoration/SM_SBM_WoodenChest_Ship_03.ubulk",
    "R5/Content/Environment/Gameplay/Workbenches/Materials/MI_Building_WoodenChest_Ship_01.uasset",
    "R5/Content/Environment/Gameplay/Workbenches/Materials/MI_Building_WoodenChest_Ship_01.uexp",
    "R5/Content/UI/HUD/Building/Icons/BuildingBits/SBM/T_PlaqueT02_Ship.uasset",
    "R5/Content/UI/HUD/Building/Icons/BuildingBits/SBM/T_PlaqueT02_Ship.uexp",
    "R5/Content/UI/HUD/Building/Icons/BuildingBits/SBM/T_PlaqueT02_Ship.ubulk",
    "R5/Content/UI/HUD/Building/Icons/BuildingBits/SBM/IconsUI/T_BI_Craft_WoodenChest_SBM_Ship.uasset",
    "R5/Content/UI/HUD/Building/Icons/BuildingBits/SBM/IconsUI/T_BI_Craft_WoodenChest_SBM_Ship.uexp",
    "R5/Content/UI/HUD/Building/Icons/BuildingBits/SBM/IconsUI/T_BI_Craft_WoodenChest_SBM_Ship.ubulk",
    "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json"
)

Write-Step "Starting SBM one-chest reduction probe"
$retocPath = Ensure-Command -Name "retoc"
Write-Step "Using retoc: $retocPath"
$repakPath = Ensure-Command -Name "repak"
Write-Step "Using repak: $repakPath"

if (-not (Test-Path -LiteralPath $SbmRawRoot)) {
    throw "SBM raw root not found: $SbmRawRoot. Run retoc unpack-raw on the working SBM package first."
}
$sourceManifestPath = Join-Path $SbmRawRoot "manifest.json"
$sourceChunksRoot = Join-Path $SbmRawRoot "chunks"
if (-not (Test-Path -LiteralPath $sourceManifestPath)) {
    throw "SBM raw manifest not found: $sourceManifestPath"
}
if (-not (Test-Path -LiteralPath $sourceChunksRoot)) {
    throw "SBM raw chunks folder not found: $sourceChunksRoot"
}

if ([string]::IsNullOrWhiteSpace($SbmPakPath)) {
    $zipExtractRoot = Join-Path (Split-Path -Parent $StageRoot) "SBM_ContainersChest_P_zip"
    $SbmPakPath = Join-Path $zipExtractRoot "SBM_ContainersChest_P\SBM_ContainersChest_P.pak"
    if (-not (Test-Path -LiteralPath $SbmPakPath)) {
        if (-not (Test-Path -LiteralPath $SbmZipPath)) {
            throw "SBM pak not found and zip missing: $SbmZipPath"
        }
        Write-Step "Extracting SBM reference zip for pak-side JSON/assets"
        Ensure-Directory -Path $zipExtractRoot
        Expand-Archive -LiteralPath $SbmZipPath -DestinationPath $zipExtractRoot -Force
    }
}
if (-not (Test-Path -LiteralPath $SbmPakPath)) {
    throw "SBM pak not found: $SbmPakPath"
}

Write-Step "Preparing stage root: $StageRoot"
Remove-DirectoryContents -Path $StageRoot

$sourceManifest = Get-Content -LiteralPath $sourceManifestPath -Raw | ConvertFrom-Json
$chunkPaths = $sourceManifest.chunk_paths.PSObject.Properties
$selected = [ordered]@{}
foreach ($assetPath in $probeAssetPaths) {
    $match = $chunkPaths | Where-Object { $_.Value -eq $assetPath } | Select-Object -First 1
    if ($null -eq $match) {
        throw "Could not find raw chunk for SBM asset path: $assetPath"
    }
    $selected[$match.Name] = $match.Value
}

$containerHeaders = @(Get-ChildItem -LiteralPath $sourceChunksRoot -File | Where-Object { $_.Name.EndsWith("00000006", [StringComparison]::OrdinalIgnoreCase) })
if ($containerHeaders.Count -ne 1) {
    throw "Expected exactly one raw ContainerHeader chunk ending in 00000006, found $($containerHeaders.Count)."
}

$stageChunksRoot = Join-Path $StageRoot "chunks"
foreach ($chunkId in $selected.Keys) {
    Copy-ProbeChunk -ChunkId $chunkId -SourceChunksRoot $sourceChunksRoot -DestinationChunksRoot $stageChunksRoot
}
Copy-ProbeChunk -ChunkId $containerHeaders[0].Name -SourceChunksRoot $sourceChunksRoot -DestinationChunksRoot $stageChunksRoot

$manifest = [ordered]@{
    chunk_paths = $selected
    version = $sourceManifest.version
    mount_point = $sourceManifest.mount_point
}
$manifestPath = Join-Path $StageRoot "manifest.json"
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding ASCII

$outUtoc = Join-Path $StageRoot ($PackageBaseName + ".utoc")
$outUcas = Join-Path $StageRoot ($PackageBaseName + ".ucas")
$outPak = Join-Path $StageRoot ($PackageBaseName + ".pak")

Write-Step "Building zen container from one-chest raw chunks"
Invoke-Retoc -Args @("pack-raw", $StageRoot, $outUtoc)

Write-Step "Extracting one-chest pak-side files and build-menu registry"
$pakExtractRoot = Join-Path $StageRoot "pak_extract"
Remove-DirectoryContents -Path $pakExtractRoot
$unpackArgs = @("unpack", "-q", "-f", "-o", $pakExtractRoot)
foreach ($path in $probePakPaths) {
    $unpackArgs += @("-i", $path)
}
$unpackArgs += $SbmPakPath
Invoke-Repak -Args $unpackArgs

$categoryJsonPath = Join-Path $pakExtractRoot "R5\Content\UI\HUD\Building\DA_BuildingUICategories.json"
if (-not (Test-Path -LiteralPath $categoryJsonPath)) {
    throw "Expected build-menu category JSON was not extracted: $categoryJsonPath"
}

Write-Step "Reducing SBM category JSON to Ship chest only"
$categoryJson = Get-Content -LiteralPath $categoryJsonPath -Raw | ConvertFrom-Json
$utilitiesCategory = ($categoryJson.CategoriesMap.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Utilities"}' } | Select-Object -First 1).Value
if ($null -eq $utilitiesCategory) {
    throw "Could not find UI.Building.Utilities category in DA_BuildingUICategories.json"
}
$storageGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "ComfortType.Utilities.Storage"}' } | Select-Object -First 1).Value
if ($null -eq $storageGroup) {
    throw "Could not find ComfortType.Utilities.Storage group in DA_BuildingUICategories.json"
}
$storageGroup.Items = @($storageGroup.Items | Where-Object {
    $_ -notmatch "DA_BI_Utilities_Storage_WoodenChest_(Alchemy|Clothing|Food|FoodIngridients|Ore|Trade|Treasure|Weapons|Wood)"
})
$shipEntry = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_Ship.DA_BI_Utilities_Storage_WoodenChest_Ship"
if ($storageGroup.Items -notcontains $shipEntry) {
    throw "Reduced category JSON lost the Ship chest entry."
}
$categoryJson | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $categoryJsonPath -Encoding ASCII

Write-Step "Building SBM-style pak with category JSON"
$pakInputRoot = Join-Path $pakExtractRoot "R5\Content"
Invoke-Repak -Args @("pack", "-q", "--version", "V3", "--compression", "Zlib", "-m", "../../../R5/Content/", $pakInputRoot, $outPak)

foreach ($path in @($outUtoc, $outUcas, $outPak)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Build output missing: $path"
    }
    if ((Get-Item -LiteralPath $path).Length -le 0) {
        throw "Build output is empty: $path"
    }
}

Write-Step "Verifying container contents"
$listOutput = & retoc list --path $outUtoc 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "retoc list failed for '$outUtoc'"
}
$listText = $listOutput | Out-String
foreach ($token in @(
    "DA_BI_Utilities_Storage_WoodenChest_Ship",
    "BP_Storage_WoodenChest_Ship",
    "SM_SBM_WoodenChest_Ship_03",
    "MI_Building_WoodenChest_Ship_01",
    "T_BI_Craft_WoodenChest_SBM_Ship",
    "T_PlaqueT02_Ship"
)) {
    if (-not ($listText -match [Regex]::Escape($token))) {
        throw "Verification failed: token '$token' not found in retoc list output."
    }
}
Write-Step "Verification passed"

Write-Step "Verifying pak-side category registry"
$pakListOutput = & repak list $outPak 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "repak list failed for '$outPak'"
}
$pakListText = $pakListOutput | Out-String
foreach ($token in @(
    "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json",
    "DA_BI_Utilities_Storage_WoodenChest_Ship.uasset"
)) {
    if (-not ($pakListText -match [Regex]::Escape($token))) {
        throw "Pak verification failed: token '$token' not found in repak list output."
    }
}
Write-Step "Pak verification passed"

if ($SkipDeploy) {
    Write-Step "SkipDeploy enabled; stopping before deployment."
    Write-Host "Output container:"
    Write-Host "  $outUtoc"
    Write-Host "  $outUcas"
    if (Test-Path -LiteralPath $outPak) {
        Write-Host "  $outPak"
    }
    Write-Host "Manifest:"
    Write-Host "  $manifestPath"
    exit 0
}

Write-Step "Deploying probe to ~mods"
Assert-ExpectedModsPath -Path $ModsDir
Ensure-Directory -Path $ModsDir
$targetDir = Join-Path $ModsDir $PackageBaseName
Ensure-Directory -Path $targetDir

foreach ($target in @(
    (Join-Path $targetDir ($PackageBaseName + ".utoc")),
    (Join-Path $targetDir ($PackageBaseName + ".ucas")),
    (Join-Path $targetDir ($PackageBaseName + ".pak"))
)) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Force
    }
}

Copy-Item -LiteralPath $outUtoc -Destination (Join-Path $targetDir ($PackageBaseName + ".utoc")) -Force
Copy-Item -LiteralPath $outUcas -Destination (Join-Path $targetDir ($PackageBaseName + ".ucas")) -Force
if (Test-Path -LiteralPath $outPak) {
    Copy-Item -LiteralPath $outPak -Destination (Join-Path $targetDir ($PackageBaseName + ".pak")) -Force
}

Write-Step "Deploy complete"
Write-Host "Deployed files:"
Write-Host ("  " + (Join-Path $targetDir ($PackageBaseName + ".utoc")))
Write-Host ("  " + (Join-Path $targetDir ($PackageBaseName + ".ucas")))
if (Test-Path -LiteralPath $outPak) {
    Write-Host ("  " + (Join-Path $targetDir ($PackageBaseName + ".pak")))
}
