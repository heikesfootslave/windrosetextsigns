param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$UnrealEditorPath = "",
    [string]$ProjectPath = "",
    [string]$FontFile = "",
    [string]$DestinationPath = "/Game/WindroseTextSigns/Fonts",
    [string]$AssetName = "PencilantScript",
    [switch]$ProbeOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FirstExistingPath {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    return ""
}

$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($FontFile)) {
    $FontFile = Resolve-FirstExistingPath @(
        (Join-Path $RepoRoot "assets\fonts\Pencilant Script.ttf"),
        (Join-Path $RepoRoot "assets\fonts\Pencilant Script.otf")
    )
}

if ([string]::IsNullOrWhiteSpace($FontFile) -or -not (Test-Path -LiteralPath $FontFile)) {
    throw "Pencilant font file not found under assets\fonts"
}

if ([string]::IsNullOrWhiteSpace($UnrealEditorPath)) {
    $cmd = Get-Command UnrealEditor.exe, UE4Editor.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        $UnrealEditorPath = $cmd.Source
    }
}

Write-Host "Pencilant font source: $FontFile"
Write-Host "Desired Unreal asset: $DestinationPath/$AssetName"

if ([string]::IsNullOrWhiteSpace($UnrealEditorPath) -or -not (Test-Path -LiteralPath $UnrealEditorPath)) {
    Write-Warning "UnrealEditor.exe/UE4Editor.exe was not found. repak/retoc/FModel can package existing cooked assets, but they cannot import a raw TTF/OTF into a UFont asset."
    Write-Warning "Install or point this script at a compatible Unreal Editor/dev-kit project, import the font as a UFont/Font asset at $DestinationPath/$AssetName, then cook/copy the resulting .uasset/.uexp into the content pak stage."
    exit 2
}

if ($ProbeOnly) {
    Write-Host "Unreal editor detected: $UnrealEditorPath"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ProjectPath) -or -not (Test-Path -LiteralPath $ProjectPath)) {
    throw "ProjectPath is required to import/cook the font with Unreal Editor. Provide a compatible .uproject path."
}

Write-Warning "Automatic UFont import is intentionally not run yet. Unreal's font import class differs by engine/project setup, and this script should be extended against a confirmed Windrose-compatible dev-kit project."
Write-Warning "Next concrete step: create/import $AssetName in the project at $DestinationPath, cook it, and stage the cooked asset in R5\Content\WindroseTextSigns\Fonts."
exit 3
