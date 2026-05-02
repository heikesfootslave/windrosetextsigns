param(
    [string]$ModRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns",
    [string]$DeploymentsDir = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\Deployments",
    [string]$ClientModsRoot = "C:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods",
    [string]$ServerModsRoot = "C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods",
    [switch]$SkipClient,
    [switch]$SkipServer
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

function Resolve-LatestZipPath {
    param([Parameter(Mandatory = $true)][string]$Root)

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
    Write-Step "Verified deployment target: `"$target`""
}

Write-Step "Starting clean deployment from latest package"

if (!(Test-Path -LiteralPath $DeploymentsDir)) {
    throw "Deployments directory not found: `"$DeploymentsDir`""
}

$zipPath = Resolve-LatestZipPath -Root $DeploymentsDir
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
    } else {
        Write-Step "SkipClient set; client deployment skipped"
    }

    if (-not $SkipServer) {
        Deploy-ToTarget -ExtractedModDir $extractedModDir -ModsRoot $ServerModsRoot
    } else {
        Write-Step "SkipServer set; server deployment skipped"
    }
}
finally {
    if (Test-Path -LiteralPath $tmpRoot) {
        Remove-Item -LiteralPath $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Step "Clean deployment completed"
