param(
    [string]$ModsDir = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-ExpectedModsPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $expectedSuffix = "\R5\Content\Paks\~mods"
    if (-not $normalized.EndsWith($expectedSuffix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing cleanup: ModsDir must end with '$expectedSuffix'. Got '$normalized'"
    }
}

Assert-ExpectedModsPath -Path $ModsDir
if (-not (Test-Path -LiteralPath $ModsDir)) {
    throw "Mods dir does not exist: $ModsDir"
}

$prefixes = @(
    "WoodenLabel_Text_SBMTemplate",
    "WoodenLabel_Text_SBMTemplate_P",
    "pakchunk999-LabelDiag_P"
)

$exts = @(".pak", ".utoc", ".ucas")
$removed = New-Object System.Collections.Generic.List[string]
$locked = New-Object System.Collections.Generic.List[string]

foreach ($prefix in $prefixes) {
    foreach ($ext in $exts) {
        $path = Join-Path $ModsDir ($prefix + $ext)
        if (Test-Path -LiteralPath $path) {
            try {
                Remove-Item -LiteralPath $path -Force
                $removed.Add($path)
            } catch {
                $locked.Add($path)
            }
        }
    }
}

Write-Host "Removed files:"
if ($removed.Count -eq 0) {
    Write-Host "  (none)"
} else {
    foreach ($p in $removed) { Write-Host ("  " + $p) }
}

if ($locked.Count -gt 0) {
    Write-Host ""
    Write-Host "Locked files (close game/client and run again):"
    foreach ($p in $locked) { Write-Host ("  " + $p) }
    exit 2
}

Write-Host ""
Write-Host "Cleanup complete."
