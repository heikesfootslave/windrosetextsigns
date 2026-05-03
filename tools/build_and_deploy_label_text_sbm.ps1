param(
    [string]$InputRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\WoodenLabel_Text_SBMTemplate_stage",
    [bool]$AutoPrepare = $true,
    [string]$SourceLegacyRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy",
    [string]$SbmZipPath = "C:\Users\User\Downloads\SBM_ContainersChest_P.zip",
    [string]$SbmPakPath = "",
    [ValidateSet("NoIconTexture", "ShipIcon")]
    [string]$LabelIconMode = "NoIconTexture",
    [ValidateSet("WoodenLabels", "StorageProbe", "StorageOverrideProbe")]
    [string]$PrepareMode = "WoodenLabels",
    [string]$PackageBaseName = "WoodenLabel_Text_SBMTemplate_P",
    [string]$ModsDir = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods",
    [string]$ServerModsDir = "C:\Games\WindowsServer\R5\Content\Paks\~mods",
    [string]$DeploySubfolderName = "",
    [string]$ExpectedAssetToken = "DA_BI_Utilities_Lables_Wooden_Text",
    [string]$SourceAssetRelativePath = "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uasset",
    [string]$EngineVersion = "UE5_6",
    [switch]$SkipServerDeploy,
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

function Resolve-DeploySubfolderName {
    param(
        [string]$RawName,
        [Parameter(Mandatory = $true)][string]$DefaultName
    )
    if ([string]::IsNullOrWhiteSpace($RawName)) {
        return $DefaultName
    }
    $trimmed = $RawName.Trim()
    if ($trimmed.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "DeploySubfolderName contains invalid path chars: '$trimmed'"
    }
    return $trimmed
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

function Resolve-SbmPakPath {
    param(
        [string]$ExplicitPakPath,
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$WorkingRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPakPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPakPath)) {
            throw "SBM pak not found: $ExplicitPakPath"
        }
        return $ExplicitPakPath
    }

    if (-not (Test-Path -LiteralPath $ZipPath)) {
        throw "SBM reference zip not found: $ZipPath"
    }

    $zipExtractRoot = Join-Path $WorkingRoot "_reference_sbm_zip"
    if (Test-Path -LiteralPath $zipExtractRoot) {
        Remove-Item -LiteralPath $zipExtractRoot -Recurse -Force
    }
    Ensure-Directory -Path $zipExtractRoot
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $zipExtractRoot -Force

    $pakPath = Join-Path $zipExtractRoot "SBM_ContainersChest_P\SBM_ContainersChest_P.pak"
    if (-not (Test-Path -LiteralPath $pakPath)) {
        throw "SBM pak not found after extracting zip: $pakPath"
    }
    return $pakPath
}

function Add-BuildMenuCategoryJson {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePakPath,
        [Parameter(Mandatory = $true)][string]$OutputContentRoot,
        [Parameter(Mandatory = $true)][string]$WorkingRoot,
        [Parameter(Mandatory = $true)][string]$Mode
    )

    $categoryExtractRoot = Join-Path $WorkingRoot "_category_json"
    if (Test-Path -LiteralPath $categoryExtractRoot) {
        Remove-Item -LiteralPath $categoryExtractRoot -Recurse -Force
    }
    Ensure-Directory -Path $categoryExtractRoot

    Invoke-Repak -Args @(
        "unpack", "-q", "-f",
        "-o", $categoryExtractRoot,
        "-i", "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json",
        $SourcePakPath
    )

    $sourceCategoryJsonPath = Join-Path $categoryExtractRoot "R5\Content\UI\HUD\Building\DA_BuildingUICategories.json"
    if (-not (Test-Path -LiteralPath $sourceCategoryJsonPath)) {
        throw "Could not extract DA_BuildingUICategories.json from $SourcePakPath"
    }

    $categoryJson = Get-Content -LiteralPath $sourceCategoryJsonPath -Raw | ConvertFrom-Json
    $utilitiesCategory = ($categoryJson.CategoriesMap.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Utilities"}' } | Select-Object -First 1).Value
    if ($null -eq $utilitiesCategory) {
        throw "Could not find UI.Building.Utilities category in DA_BuildingUICategories.json"
    }

    $storageGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "ComfortType.Utilities.Storage"}' } | Select-Object -First 1).Value
    if ($null -ne $storageGroup) {
        $storageGroup.Items = @($storageGroup.Items | Where-Object {
            $_ -notmatch "DA_BI_Utilities_Storage_WoodenChest_(Alchemy|Clothing|Food|FoodIngridients|Ore|Ship|Trade|Treasure|Weapons|Wood)"
        })
    }

    if ($Mode -eq "WoodenLabels") {
        $woodLabelsGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Lables.Wood"}' } | Select-Object -First 1).Value
        if ($null -eq $woodLabelsGroup) {
            throw "Could not find UI.Building.Lables.Wood group in DA_BuildingUICategories.json"
        }

        $labelEntry = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text.DA_BI_Utilities_Lables_Wooden_Text"
        $woodLabelsGroup.Items = @($woodLabelsGroup.Items | Where-Object { $_ -ne $labelEntry })
        $woodLabelsGroup.Items += $labelEntry
    } elseif ($Mode -eq "StorageProbe") {
        if ($null -eq $storageGroup) {
            throw "Could not find ComfortType.Utilities.Storage group in DA_BuildingUICategories.json"
        }
        $storageEntry = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_11.DA_BI_Utilities_Storage_WoodenChest_11"
        $storageGroup.Items = @($storageGroup.Items | Where-Object { $_ -ne $storageEntry })
        $storageGroup.Items += $storageEntry
    }

    $targetCategoryJsonPath = Join-Path $OutputContentRoot "UI\HUD\Building\DA_BuildingUICategories.json"
    Ensure-Directory -Path (Split-Path -Parent $targetCategoryJsonPath)
    $categoryJson | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $targetCategoryJsonPath -Encoding ASCII
    return $targetCategoryJsonPath
}

function Deploy-PackageToMods {
    param(
        [Parameter(Mandatory = $true)][string]$TargetModsDir,
        [Parameter(Mandatory = $true)][string]$SubfolderName,
        [Parameter(Mandatory = $true)][string]$PackageName,
        [Parameter(Mandatory = $true)][string]$SourceRoot
    )

    Assert-ExpectedModsPath -Path $TargetModsDir
    Ensure-Directory -Path $TargetModsDir
    $targetDir = Join-Path $TargetModsDir $SubfolderName
    Ensure-Directory -Path $targetDir

    foreach ($ext in @("utoc", "ucas", "pak")) {
        $rootLevelStale = Join-Path $TargetModsDir ($PackageName + "." + $ext)
        if (Test-Path -LiteralPath $rootLevelStale) {
            Write-Step "Removing legacy root-level file: $rootLevelStale"
            Remove-Item -LiteralPath $rootLevelStale -Force
        }

        $target = Join-Path $targetDir ($PackageName + "." + $ext)
        if (Test-Path -LiteralPath $target) {
            Write-Step "Removing existing file: $target"
            Remove-Item -LiteralPath $target -Force
        }

        $source = Join-Path $SourceRoot ($PackageName + "." + $ext)
        Copy-Item -LiteralPath $source -Destination $target -Force
        if (-not (Test-Path -LiteralPath $target)) {
            throw "Deploy verification failed (missing target): $target"
        }
    }

    Write-Host "Deployed files:"
    foreach ($ext in @("utoc", "ucas", "pak")) {
        Write-Host ("  " + (Join-Path $targetDir ($PackageName + "." + $ext)))
    }
}

Write-Step "Starting build/verify/deploy for SBM-template Wooden Label"
$retocPath = Ensure-Command -Name "retoc"
Write-Step "Using retoc: $retocPath"
$repakPath = Ensure-Command -Name "repak"
Write-Step "Using repak: $repakPath"

$effectiveExpectedToken = $ExpectedAssetToken
$effectiveSourceAssetRelativePath = $SourceAssetRelativePath
if ($PrepareMode -eq "StorageProbe") {
    if ($effectiveExpectedToken -eq "DA_BI_Utilities_Lables_Wooden_Text") {
        $effectiveExpectedToken = "DA_BI_Utilities_Storage_WoodenChest_11"
    }
    if ($effectiveSourceAssetRelativePath -eq "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uasset") {
        $effectiveSourceAssetRelativePath = "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Storage_WoodenChest_11.uasset"
    }
}
if ($PrepareMode -eq "StorageOverrideProbe") {
    if ($effectiveExpectedToken -eq "DA_BI_Utilities_Lables_Wooden_Text") {
        $effectiveExpectedToken = "DA_BI_Utilities_Storage_WoodenChest_01"
    }
    if ($effectiveSourceAssetRelativePath -eq "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uasset") {
        $effectiveSourceAssetRelativePath = "Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Storage_WoodenChest_01.uasset"
    }
}

if ($AutoPrepare) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $prepareScript = Join-Path $scriptDir "prepare_label_text_sbm_template.ps1"
    if (-not (Test-Path -LiteralPath $prepareScript)) {
        throw "AutoPrepare enabled but script is missing: $prepareScript"
    }
    Write-Step "AutoPrepare enabled: regenerating template asset payload"
    & $prepareScript -SourceLegacyRoot $SourceLegacyRoot -OutputRoot $InputRoot -PrepareMode $PrepareMode -LabelIconMode $LabelIconMode
    if (-not $?) {
        throw "prepare_label_text_sbm_template.ps1 reported failure."
    }
}

if (-not (Test-Path -LiteralPath $InputRoot)) {
    throw "InputRoot does not exist after preparation: $InputRoot"
}

$contentRootCandidate = Join-Path $InputRoot "R5\Content"
$packSourceRoot = $InputRoot
if (Test-Path -LiteralPath $contentRootCandidate) {
    # Match SBM container path shape (../../../Gameplay/...) by packing from Content root.
    $packSourceRoot = $contentRootCandidate
}

$sbmPakForCategoryJson = Resolve-SbmPakPath -ExplicitPakPath $SbmPakPath -ZipPath $SbmZipPath -WorkingRoot $InputRoot
Write-Step "Adding build-menu category JSON"
$categoryJsonPath = Add-BuildMenuCategoryJson -SourcePakPath $sbmPakForCategoryJson -OutputContentRoot $packSourceRoot -WorkingRoot $InputRoot -Mode $PrepareMode
Write-Step "Category JSON staged: $categoryJsonPath"

$sourceAssetPath = Join-Path $packSourceRoot $effectiveSourceAssetRelativePath
if (-not (Test-Path -LiteralPath $sourceAssetPath)) {
    throw "Expected source asset missing: $sourceAssetPath"
}

$outUtoc = Join-Path $InputRoot ($PackageBaseName + ".utoc")
$outUcas = Join-Path $InputRoot ($PackageBaseName + ".ucas")
$outPak = Join-Path $InputRoot ($PackageBaseName + ".pak")
$deploySubfolderName = Resolve-DeploySubfolderName -RawName $DeploySubfolderName -DefaultName $PackageBaseName
$modTargetDir = Join-Path $ModsDir $deploySubfolderName
$modUtoc = Join-Path $modTargetDir ($PackageBaseName + ".utoc")
$modUcas = Join-Path $modTargetDir ($PackageBaseName + ".ucas")
$modPak = Join-Path $modTargetDir ($PackageBaseName + ".pak")

Write-Step "Step 1/3: Building zen container"
Write-Step "Packing source root: $packSourceRoot"
foreach ($stale in @($outUtoc, $outUcas, $outPak)) {
    if (Test-Path -LiteralPath $stale) {
        Write-Step "Removing stale build output: $stale"
        Remove-Item -LiteralPath $stale -Force
    }
}
Invoke-Retoc -Args @("to-zen", "--version", $EngineVersion, $packSourceRoot, $outUtoc)

Write-Step "Replacing retoc pak with SBM-style pak that includes category JSON"
if (Test-Path -LiteralPath $outPak) {
    Remove-Item -LiteralPath $outPak -Force
}
Invoke-Repak -Args @("pack", "-q", "--version", "V3", "--compression", "Zlib", "-m", "../../../R5/Content/", $packSourceRoot, $outPak)

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
if (-not ($joined -match [Regex]::Escape($effectiveExpectedToken))) {
    throw "Verification failed: token '$effectiveExpectedToken' not found in container listing."
}
Write-Step "Verification passed: found token '$effectiveExpectedToken'"

Write-Step "Verifying pak-side category registry"
$pakListOutput = & repak list $outPak 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "repak list failed (exit $LASTEXITCODE) for '$outPak'"
}
$pakListText = $pakListOutput | Out-String
if (-not ($pakListText -match [Regex]::Escape("R5/Content/UI/HUD/Building/DA_BuildingUICategories.json"))) {
    throw "Pak verification failed: category JSON missing from pak."
}
if (-not ($pakListText -match [Regex]::Escape($effectiveExpectedToken))) {
    throw "Pak verification failed: asset token '$effectiveExpectedToken' missing from pak."
}

$categoryText = (& repak get $outPak "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json" | Out-String)
if ($PrepareMode -eq "WoodenLabels" -and -not $categoryText.Contains("DA_BI_Utilities_Lables_Wooden_Text")) {
    throw "Category JSON verification failed: Label Text entry missing."
}
if ($categoryText.Contains("DA_BI_Utilities_Storage_WoodenChest_Ship")) {
    throw "Category JSON verification failed: SBM Ship chest probe entry still present."
}
Write-Step "Pak category verification passed"

if ($SkipDeploy) {
    Write-Step "SkipDeploy enabled; stopping before deployment."
    Write-Host "Output container:"
    Write-Host "  $outUtoc"
    Write-Host "  $outUcas"
    Write-Host "  $outPak"
    exit 0
}

Write-Step "Step 3/3: Clean deploy to client ~mods"
Deploy-PackageToMods -TargetModsDir $ModsDir -SubfolderName $deploySubfolderName -PackageName $PackageBaseName -SourceRoot $InputRoot

if (-not $SkipServerDeploy) {
    Write-Step "Deploying same package to server ~mods"
    Deploy-PackageToMods -TargetModsDir $ServerModsDir -SubfolderName $deploySubfolderName -PackageName $PackageBaseName -SourceRoot $InputRoot
}

Write-Step "Deploy complete."
