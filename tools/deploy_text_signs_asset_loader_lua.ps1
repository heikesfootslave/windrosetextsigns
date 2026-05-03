param(
    [string]$RepoRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns",
    [string]$ClientUe4ssModsRoot = "C:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods",
    [string]$ServerUe4ssModsRoot = "C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods",
    [switch]$SkipClient,
    [switch]$SkipServer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ("{0} {1}" -f ([DateTime]::UtcNow.ToString("o")), $Message)
}

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Assert-SafeModsRoot {
    param([Parameter(Mandatory = $true)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $fullPath.EndsWith("\R5\Binaries\Win64\ue4ss\Mods", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to deploy to unexpected UE4SS Mods path: $fullPath"
    }
}

function Enable-ModInModsTxt {
    param(
        [Parameter(Mandatory = $true)][string]$ModsRoot,
        [Parameter(Mandatory = $true)][string]$ModName
    )

    $modsTxt = Join-Path $ModsRoot "mods.txt"
    if (-not (Test-Path -LiteralPath $modsTxt)) {
        throw "mods.txt not found: $modsTxt"
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.AddRange([string[]](Get-Content -LiteralPath $modsTxt))

    $entry = "$ModName : 1"
    $found = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\s*$([regex]::Escape($ModName))\s*:") {
            $lines[$i] = $entry
            $found = $true
        }
    }

    if (-not $found) {
        $insertIndex = $lines.Count
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match "Built-in keybinds") {
                $insertIndex = [Math]::Max(0, $i - 1)
                break
            }
        }
        $lines.Insert($insertIndex, $entry)
    }

    Set-Content -LiteralPath $modsTxt -Value $lines -Encoding ASCII
    Write-Step "Enabled $ModName in $modsTxt"
}

function Deploy-Loader {
    param(
        [Parameter(Mandatory = $true)][string]$ModsRoot,
        [Parameter(Mandatory = $true)][string]$SourceModDir,
        [Parameter(Mandatory = $true)][string]$ModName
    )

    Assert-SafeModsRoot -Path $ModsRoot
    Ensure-Directory -Path $ModsRoot

    $target = Join-Path $ModsRoot $ModName
    if (Test-Path -LiteralPath $target) {
        $targetFull = [IO.Path]::GetFullPath($target).TrimEnd('\')
        $modsRootFull = [IO.Path]::GetFullPath($ModsRoot).TrimEnd('\')
        if (-not $targetFull.StartsWith($modsRootFull, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe target path outside Mods root: $targetFull"
        }
        if ([IO.Path]::GetFileName($targetFull) -ne $ModName) {
            throw "Refusing to remove unexpected target: $targetFull"
        }
        Remove-Item -LiteralPath $target -Recurse -Force
    }

    Copy-Item -LiteralPath $SourceModDir -Destination $ModsRoot -Recurse -Force
    $mainLua = Join-Path $target "Scripts\main.lua"
    if (-not (Test-Path -LiteralPath $mainLua)) {
        throw "Deployment verification failed. Missing $mainLua"
    }

    Enable-ModInModsTxt -ModsRoot $ModsRoot -ModName $ModName
    Write-Step "Deployed $ModName to $target"
}

$modName = "WindroseTextSignsAssetLoader"
$sourceModDir = Join-Path $RepoRoot "LuaMods\$modName"
if (-not (Test-Path -LiteralPath (Join-Path $sourceModDir "Scripts\main.lua"))) {
    throw "Source Lua mod missing Scripts\main.lua: $sourceModDir"
}

if (-not $SkipClient) {
    Deploy-Loader -ModsRoot $ClientUe4ssModsRoot -SourceModDir $sourceModDir -ModName $modName
}

if (-not $SkipServer) {
    Deploy-Loader -ModsRoot $ServerUe4ssModsRoot -SourceModDir $sourceModDir -ModName $modName
}

Write-Step "Asset loader Lua deployment complete"
