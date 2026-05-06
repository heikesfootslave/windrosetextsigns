param(
    [string]$ModRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns",
    [string]$DeploymentsDir = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Deployments",
    [string]$DeploymentZipPath = "",
    [string]$ClientModsRoot = "C:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods",
    [string]$ServerModsRoot = "C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods",
    [string]$ContentBuildScript = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\tools\build_and_deploy_label_text_sbm.ps1",
    [string]$ContentPackageStageRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\WoodenLabel_Text_SBMTemplate_stage",
    [string]$ContentPackageBaseName = "WindroseTextSigns_Content_P",
    [string]$ClientPakModsRoot = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods",
    [string]$ServerPakModsRoot = "C:\Games\WindowsServer\R5\Content\Paks\~mods",
    [string]$ContentDeploySubfolderName = "",
    [string[]]$CleanPakPackageNames = @(
        "WoodenLabel_Text_SBMTemplate",
        "WoodenLabel_Text_SBMTemplate_P",
        "WoodenLabel_Text_MinimalCloneProbe_P",
        "WoodenLabel_Text_NoIconProbe_P",
        "WoodenLabel_CategoryNativeProbe_P",
        "pakchunk999-LabelDiag_P",
        "SBM_OneChest_Ship_P",
        "WindroseTextSigns_Content_P"
    ),
    [string[]]$StaleUe4ssModNames = @(
        "WindroseTextSignsAssetLoader"
    ),
    [switch]$SkipClient,
    [switch]$SkipServer,
    [switch]$SkipContentBuild,
    [switch]$SkipContentDeploy,
    [switch]$EnableContentPackage,
    [switch]$DisableContentPackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "{0} {1}" -f ([DateTime]::UtcNow.ToString("o")), $Message
    Write-Host $line
}

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Assert-ExpectedPakModsPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $expectedSuffix = "\R5\Content\Paks\~mods"
    if (-not $normalized.EndsWith($expectedSuffix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing content deploy: pak Mods path must end with '$expectedSuffix'. Got '$normalized'"
    }
}

function Assert-SafePackageName {
    param([Parameter(Mandatory = $true)][string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name)) {
        throw "Package name cannot be blank."
    }
    if ($Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "Package name contains invalid path chars: '$Name'"
    }
}

function Assert-ExpectedUe4ssModsPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $expectedSuffix = "\R5\Binaries\Win64\ue4ss\Mods"
    if (-not $normalized.EndsWith($expectedSuffix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing runtime deploy: UE4SS Mods path must end with '$expectedSuffix'. Got '$normalized'"
    }
}

function Assert-SafeModFolderName {
    param([Parameter(Mandatory = $true)][string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name)) {
        throw "Mod folder name cannot be blank."
    }
    if ($Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "Mod folder name contains invalid path chars: '$Name'"
    }
}

function Resolve-ContentDeploySubfolderName {
    param(
        [string]$RawName,
        [Parameter(Mandatory = $true)][string]$DefaultName
    )

    if ([string]::IsNullOrWhiteSpace($RawName)) {
        return $DefaultName
    }

    $trimmed = $RawName.Trim()
    Assert-SafePackageName -Name $trimmed
    return $trimmed
}

function Remove-StaleUe4ssModFolders {
    param(
        [Parameter(Mandatory = $true)][string]$ModsRoot,
        [Parameter(Mandatory = $true)][string[]]$ModNames
    )

    Assert-ExpectedUe4ssModsPath -Path $ModsRoot
    if (-not (Test-Path -LiteralPath $ModsRoot)) {
        return
    }

    $modsRootFull = [System.IO.Path]::GetFullPath($ModsRoot).TrimEnd('\')
    foreach ($modName in ($ModNames | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        Assert-SafeModFolderName -Name $modName
        $target = Join-Path $ModsRoot $modName
        $targetFull = [System.IO.Path]::GetFullPath($target).TrimEnd('\')
        if ($targetFull.StartsWith($modsRootFull, [System.StringComparison]::OrdinalIgnoreCase) -and
            [System.IO.Path]::GetFileName($targetFull) -eq $modName -and
            (Test-Path -LiteralPath $target)) {
            Write-Step "Removing stale UE4SS mod folder: `"$target`""
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction Stop
        }
    }
}

function Resolve-LatestZipPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$ExplicitZipPath = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitZipPath)) {
        $resolved = [System.IO.Path]::GetFullPath($ExplicitZipPath)
        if (-not (Test-Path -LiteralPath $resolved)) {
            throw "Explicit deployment zip not found: `"$resolved`""
        }
        if ([System.IO.Path]::GetExtension($resolved) -ne ".zip") {
            throw "Explicit deployment path must be a .zip file: `"$resolved`""
        }
        return $resolved
    }

    $latestTxt = Join-Path $Root "LATEST.txt"
    if (Test-Path -LiteralPath $latestTxt) {
        $name = (Get-Content -LiteralPath $latestTxt -ErrorAction Stop | Out-String).Trim()
        if ([string]::IsNullOrWhiteSpace($name)) {
            throw "LATEST.txt exists but is empty: `"$latestTxt`""
        }
        $pathFromLatest = Join-Path $Root $name
        if (Test-Path -LiteralPath $pathFromLatest) {
            return $pathFromLatest
        }
        throw "LATEST.txt points to missing zip: `"$pathFromLatest`""
    }

    $zip = Get-ChildItem -LiteralPath $Root -File -Filter "WindroseTextSigns_*.zip" |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $zip) {
        throw "No deployment zip found in `"$Root`"."
    }
    return $zip.FullName
}

function Invoke-ContentPackageBuild {
    param(
        [Parameter(Mandatory = $true)][string]$BuildScript,
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$PackageBaseName
    )

    if (-not (Test-Path -LiteralPath $BuildScript)) {
        throw "Content build script not found: `"$BuildScript`""
    }

    Write-Step "Packaging Label Text content pak"
    & $BuildScript -InputRoot $StageRoot -PackageBaseName $PackageBaseName -SkipDeploy
    if (-not $?) {
        throw "Content build script reported failure: `"$BuildScript`""
    }
}

function Remove-PakPackageIfPresent {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPakModsRoot,
        [Parameter(Mandatory = $true)][string]$PackageName
    )

    Assert-ExpectedPakModsPath -Path $TargetPakModsRoot
    Assert-SafePackageName -Name $PackageName

    if (-not (Test-Path -LiteralPath $TargetPakModsRoot)) {
        return
    }

    $modsRootFull = [System.IO.Path]::GetFullPath($TargetPakModsRoot).TrimEnd('\')
    $packageDir = Join-Path $TargetPakModsRoot $PackageName
    $packageDirFull = [System.IO.Path]::GetFullPath($packageDir).TrimEnd('\')

    if ($packageDirFull.StartsWith($modsRootFull, [System.StringComparison]::OrdinalIgnoreCase) -and
        [System.IO.Path]::GetFileName($packageDirFull) -eq $PackageName -and
        (Test-Path -LiteralPath $packageDir)) {
        Write-Step "Removing stale pak package folder: `"$packageDir`""
        Remove-Item -LiteralPath $packageDir -Recurse -Force -ErrorAction Stop
    }

    foreach ($ext in @("pak", "utoc", "ucas")) {
        $rootFile = Join-Path $TargetPakModsRoot ($PackageName + "." + $ext)
        if (Test-Path -LiteralPath $rootFile) {
            Write-Step "Removing stale root-level pak file: `"$rootFile`""
            Remove-Item -LiteralPath $rootFile -Force -ErrorAction Stop
        }
    }
}

function Deploy-ContentPackageToTarget {
    param(
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$PackageBaseName,
        [Parameter(Mandatory = $true)][string]$TargetPakModsRoot,
        [Parameter(Mandatory = $true)][string]$SubfolderName,
        [Parameter(Mandatory = $true)][string[]]$CleanPackageNames
    )

    Assert-ExpectedPakModsPath -Path $TargetPakModsRoot
    Assert-SafePackageName -Name $PackageBaseName
    Assert-SafePackageName -Name $SubfolderName
    Ensure-Directory -Path $TargetPakModsRoot

    $cleanupNames = @($CleanPackageNames + @($PackageBaseName, $SubfolderName) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique)
    foreach ($name in $cleanupNames) {
        Remove-PakPackageIfPresent -TargetPakModsRoot $TargetPakModsRoot -PackageName $name
    }

    $targetDir = Join-Path $TargetPakModsRoot $SubfolderName
    Ensure-Directory -Path $targetDir

    foreach ($ext in @("utoc", "ucas", "pak")) {
        $source = Join-Path $StageRoot ($PackageBaseName + "." + $ext)
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Content deploy source missing: `"$source`""
        }

        $target = Join-Path $targetDir ($PackageBaseName + "." + $ext)
        Copy-Item -LiteralPath $source -Destination $target -Force
        if (-not (Test-Path -LiteralPath $target)) {
            throw "Content deploy verification failed. Missing `"$target`""
        }
    }

    Write-Step "Verified content pak deployment: `"$targetDir`""
}

function Clean-ContentPackagesFromTarget {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPakModsRoot,
        [Parameter(Mandatory = $true)][string]$PackageBaseName,
        [Parameter(Mandatory = $true)][string]$SubfolderName,
        [Parameter(Mandatory = $true)][string[]]$CleanPackageNames
    )

    Assert-ExpectedPakModsPath -Path $TargetPakModsRoot
    if (-not (Test-Path -LiteralPath $TargetPakModsRoot)) {
        return
    }

    $cleanupNames = @($CleanPackageNames + @($PackageBaseName, $SubfolderName) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique)
    foreach ($name in $cleanupNames) {
        Remove-PakPackageIfPresent -TargetPakModsRoot $TargetPakModsRoot -PackageName $name
    }
}

function Assert-SafeTargetPath {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$ModsRoot
    )

    $modsRootFull = [System.IO.Path]::GetFullPath($ModsRoot).TrimEnd('\')
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath).TrimEnd('\')

    if (-not $targetFull.StartsWith($modsRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe target path outside mods root. target=`"$targetFull`" root=`"$modsRootFull`""
    }

    if ([System.IO.Path]::GetFileName($targetFull) -ne "WindroseTextSigns") {
        throw "Refusing to delete/deploy non-mod directory: `"$targetFull`""
    }
}

function Remove-TargetModDir {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$ModsRoot
    )

    Assert-SafeTargetPath -TargetPath $TargetPath -ModsRoot $ModsRoot
    if (Test-Path -LiteralPath $TargetPath) {
        Write-Step "Removing existing target: `"$TargetPath`""
        Remove-Item -LiteralPath $TargetPath -Recurse -Force -ErrorAction Stop
    } else {
        Write-Step "Target does not exist (nothing to remove): `"$TargetPath`""
    }
}

function Deploy-ToTarget {
    param(
        [Parameter(Mandatory = $true)][string]$ExtractedModDir,
        [Parameter(Mandatory = $true)][string]$ModsRoot
    )

    Ensure-Directory -Path $ModsRoot
    if ($StaleUe4ssModNames.Count -gt 0) {
        Remove-StaleUe4ssModFolders -ModsRoot $ModsRoot -ModNames $StaleUe4ssModNames
    }

    $target = Join-Path $ModsRoot "WindroseTextSigns"
    Remove-TargetModDir -TargetPath $target -ModsRoot $ModsRoot

    Write-Step "Copying mod to target: `"$target`""
    Copy-Item -LiteralPath $ExtractedModDir -Destination $ModsRoot -Recurse -Force

    $required = @(
        (Join-Path $target "enabled.txt"),
        (Join-Path $target "Config"),
        (Join-Path $target "dlls\main.dll")
    )
    foreach ($req in $required) {
        if (!(Test-Path -LiteralPath $req)) {
            throw "Deployment verification failed. Missing `"$req`""
        }
    }
    Set-Content -LiteralPath (Join-Path $target "enabled.txt") -Value "1" -Encoding ASCII
    Write-Step "Verified deployment target: `"$target`""
}

Write-Step "Starting clean WindroseTextSigns deployment"

if (!(Test-Path -LiteralPath $DeploymentsDir)) {
    throw "Deployments directory not found: `"$DeploymentsDir`""
}

$contentSubfolderName = Resolve-ContentDeploySubfolderName -RawName $ContentDeploySubfolderName -DefaultName $ContentPackageBaseName
$contentPackageEnabled = $EnableContentPackage -and (-not $DisableContentPackage)

if ($DisableContentPackage) {
    Write-Step "DisableContentPackage set; content pak will be cleaned from targets and not rebuilt/deployed"
} elseif (-not $contentPackageEnabled) {
    Write-Step "Content package disabled by default after F8-convert pivot; stale build-menu/content paks will be cleaned from targets"
} elseif (-not $SkipContentBuild) {
    Invoke-ContentPackageBuild -BuildScript $ContentBuildScript -StageRoot $ContentPackageStageRoot -PackageBaseName $ContentPackageBaseName
} else {
    Write-Step "SkipContentBuild set; using existing content package outputs"
}

if ($DisableContentPackage) {
    Write-Step "DisableContentPackage set; content package output verification skipped"
} elseif (-not $contentPackageEnabled) {
    Write-Step "Content package output verification skipped because content package is not enabled"
} elseif (-not $SkipContentDeploy) {
    foreach ($ext in @("utoc", "ucas", "pak")) {
        $source = Join-Path $ContentPackageStageRoot ($ContentPackageBaseName + "." + $ext)
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Content package output missing before deployment: `"$source`""
        }
    }
} else {
    Write-Step "SkipContentDeploy set; content pak deployment skipped"
}

$zipPath = Resolve-LatestZipPath -Root $DeploymentsDir -ExplicitZipPath $DeploymentZipPath
Write-Step "Using deployment zip: `"$zipPath`""

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$tmpRoot = Join-Path $DeploymentsDir ("_deploy_extract_" + $stamp)
Ensure-Directory -Path $tmpRoot

try {
    Write-Step "Extracting zip to temp: `"$tmpRoot`""
    Expand-Archive -LiteralPath $zipPath -DestinationPath $tmpRoot -Force

    $extractedModDir = Join-Path $tmpRoot "WindroseTextSigns"
    if (!(Test-Path -LiteralPath $extractedModDir)) {
        throw "Extracted zip missing expected top-level folder `"$extractedModDir`""
    }

    $requiredExtracted = @(
        (Join-Path $extractedModDir "enabled.txt"),
        (Join-Path $extractedModDir "Config"),
        (Join-Path $extractedModDir "dlls\main.dll")
    )
    foreach ($req in $requiredExtracted) {
        if (!(Test-Path -LiteralPath $req)) {
            throw "Extracted package is missing required path `"$req`""
        }
    }

    if (-not $SkipClient) {
        Deploy-ToTarget -ExtractedModDir $extractedModDir -ModsRoot $ClientModsRoot
        if (-not $contentPackageEnabled) {
            Clean-ContentPackagesFromTarget `
                -TargetPakModsRoot $ClientPakModsRoot `
                -PackageBaseName $ContentPackageBaseName `
                -SubfolderName $contentSubfolderName `
                -CleanPackageNames $CleanPakPackageNames
        } elseif (-not $SkipContentDeploy) {
            Deploy-ContentPackageToTarget `
                -StageRoot $ContentPackageStageRoot `
                -PackageBaseName $ContentPackageBaseName `
                -TargetPakModsRoot $ClientPakModsRoot `
                -SubfolderName $contentSubfolderName `
                -CleanPackageNames $CleanPakPackageNames
        }
    } else {
        Write-Step "SkipClient set; client runtime/content deployment skipped"
    }

    if (-not $SkipServer) {
        Deploy-ToTarget -ExtractedModDir $extractedModDir -ModsRoot $ServerModsRoot
        if (-not $contentPackageEnabled) {
            Clean-ContentPackagesFromTarget `
                -TargetPakModsRoot $ServerPakModsRoot `
                -PackageBaseName $ContentPackageBaseName `
                -SubfolderName $contentSubfolderName `
                -CleanPackageNames $CleanPakPackageNames
        } elseif (-not $SkipContentDeploy) {
            Deploy-ContentPackageToTarget `
                -StageRoot $ContentPackageStageRoot `
                -PackageBaseName $ContentPackageBaseName `
                -TargetPakModsRoot $ServerPakModsRoot `
                -SubfolderName $contentSubfolderName `
                -CleanPackageNames $CleanPakPackageNames
        }
    } else {
        Write-Step "SkipServer set; server runtime/content deployment skipped"
    }
}
finally {
    if (Test-Path -LiteralPath $tmpRoot) {
        Remove-Item -LiteralPath $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Step "Clean deployment completed"
