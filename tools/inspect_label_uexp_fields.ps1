param(
    [string]$InputDir = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy_All\R5\Content\Gameplay\Building\BuildingUtilities"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

foreach ($f in Get-ChildItem -LiteralPath $InputDir -Filter "*.uexp" -File | Sort-Object Name) {
    $b = [IO.File]::ReadAllBytes($f.FullName)
    $hex = ($b[0x0F8..0x118] | ForEach-Object { $_.ToString("X2") }) -join " "
    $f32 = @()
    foreach ($o in @(0x0F8, 0x0FC, 0x100, 0x104, 0x108, 0x10C, 0x110, 0x114)) {
        if ($o + 3 -lt $b.Length) {
            $f32 += ("{0:X3}:{1}" -f $o, [BitConverter]::ToSingle($b, $o))
        }
    }
    Write-Host $f.BaseName
    Write-Host $hex
    Write-Host ($f32 -join " | ")
    Write-Host ""
}
