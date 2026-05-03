param(
    [string]$SourceLegacyRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy",
    [string]$OutputRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Text_Mod",
    [string]$SourceAssetName = "DA_BI_Utilities_Lables_Wooden_Ship_NoIcon",
    [string]$TargetAssetName = "DA_BI_Utilities_Lables_Wooden_Text",
    [string]$LabelKeyFrom = "Building_Lable_8",
    [string]$LabelKeyTo = "Building_Lable_8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-SameLength {
    param([string]$A, [string]$B, [string]$Context)
    if ($A.Length -ne $B.Length) {
        throw "$Context requires same-length tokens. '$A' ($($A.Length)) != '$B' ($($B.Length))."
    }
}

function Replace-AsciiTokenInBytes {
    param(
        [byte[]]$Bytes,
        [string]$OldToken,
        [string]$NewToken
    )

    Ensure-SameLength -A $OldToken -B $NewToken -Context "Binary patch"
    $old = [Text.Encoding]::ASCII.GetBytes($OldToken)
    $new = [Text.Encoding]::ASCII.GetBytes($NewToken)

    $count = 0
    for ($i = 0; $i -le $Bytes.Length - $old.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $old.Length; $j++) {
            if ($Bytes[$i + $j] -ne $old[$j]) {
                $match = $false
                break
            }
        }
        if ($match) {
            for ($j = 0; $j -lt $new.Length; $j++) {
                $Bytes[$i + $j] = $new[$j]
            }
            $count++
            $i += ($old.Length - 1)
        }
    }
    return $count
}

function Replace-LengthPrefixedAnsiTokenInBytes {
    param(
        [byte[]]$Bytes,
        [string]$OldToken,
        [string]$NewToken
    )

    $old = [Text.Encoding]::ASCII.GetBytes($OldToken)
    $new = [Text.Encoding]::ASCII.GetBytes($NewToken)
    $oldLenField = $old.Length + 1
    $newLenField = $new.Length + 1

    $count = 0
    for ($i = 4; $i -le $Bytes.Length - $old.Length - 1; $i++) {
        $match = $true
        for ($j = 0; $j -lt $old.Length; $j++) {
            if ($Bytes[$i + $j] -ne $old[$j]) {
                $match = $false
                break
            }
        }
        if (-not $match) { continue }

        if ($Bytes[$i + $old.Length] -ne 0) { continue }

        $lenBytes = [BitConverter]::GetBytes([int]$oldLenField)
        $isLenMatch = $true
        for ($j = 0; $j -lt 4; $j++) {
            if ($Bytes[($i - 4) + $j] -ne $lenBytes[$j]) {
                $isLenMatch = $false
                break
            }
        }
        if (-not $isLenMatch) { continue }

        if ($new.Length -gt $old.Length) {
            $extra = $new.Length - $old.Length
            $hasPadding = $true
            for ($k = 1; $k -le $extra; $k++) {
                $padIndex = $i + $old.Length + $k
                if ($padIndex -ge $Bytes.Length -or $Bytes[$padIndex] -ne 0) {
                    $hasPadding = $false
                    break
                }
            }
            if (-not $hasPadding) { continue }
        }

        $newLenBytes = [BitConverter]::GetBytes([int]$newLenField)
        for ($j = 0; $j -lt 4; $j++) {
            $Bytes[($i - 4) + $j] = $newLenBytes[$j]
        }
        for ($j = 0; $j -lt $new.Length; $j++) {
            $Bytes[$i + $j] = $new[$j]
        }
        $Bytes[$i + $new.Length] = 0

        if ($new.Length -lt $old.Length) {
            for ($j = $i + $new.Length + 1; $j -le $i + $old.Length; $j++) {
                $Bytes[$j] = 0
            }
        }

        $count++
        $i += $new.Length
    }
    return $count
}

$sourceDir = Join-Path $SourceLegacyRoot "R5\Content\Gameplay\Building\BuildingUtilities"
$targetDir = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities"
New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

$sourceUasset = Join-Path $sourceDir ($SourceAssetName + ".uasset")
$sourceUexp = Join-Path $sourceDir ($SourceAssetName + ".uexp")
if (-not (Test-Path -LiteralPath $sourceUasset)) {
    throw "Source .uasset not found: $sourceUasset"
}
if (-not (Test-Path -LiteralPath $sourceUexp)) {
    throw "Source .uexp not found: $sourceUexp"
}

$targetUasset = Join-Path $targetDir ($TargetAssetName + ".uasset")
$targetUexp = Join-Path $targetDir ($TargetAssetName + ".uexp")

Copy-Item -LiteralPath $sourceUasset -Destination $targetUasset -Force
Copy-Item -LiteralPath $sourceUexp -Destination $targetUexp -Force

$fullPathShip = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Ship"
$fullPathText = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text"
Ensure-SameLength -A $fullPathShip -B $fullPathText -Context "Asset path clone"

Ensure-SameLength -A "T_PlaqueT02_Ship" -B "T_PlaqueT02_None" -Context "Icon token patch"
$fullIconShip = "/Game/UI/HUD/Building/Icons/BuildingBits/T_PlaqueT02_Ship"
$fullIconNone = "/Game/UI/HUD/Building/Icons/BuildingBits/T_PlaqueT02_None"
Ensure-SameLength -A $fullIconShip -B $fullIconNone -Context "Icon path patch"

$tokenReplacements = @(
    @{ Old = "DA_BI_Utilities_Lables_Wooden_Ship"; New = "DA_BI_Utilities_Lables_Wooden_Text" },
    @{ Old = $fullPathShip; New = $fullPathText },
    @{ Old = "T_PlaqueT02_Ship"; New = "T_PlaqueT02_None" },
    @{ Old = $fullIconShip; New = $fullIconNone }
)

if ($LabelKeyFrom -ne $LabelKeyTo) {
    if ($LabelKeyFrom.Length -eq $LabelKeyTo.Length) {
        $tokenReplacements += @{ Old = $LabelKeyFrom; New = $LabelKeyTo }
    }
}

$report = [System.Collections.Generic.List[string]]::new()
$report.Add("clone_wooden_label_text.ps1")
$report.Add("Source: $sourceUasset")
$report.Add("Source: $sourceUexp")
$report.Add("Target: $targetUasset")
$report.Add("Target: $targetUexp")
$report.Add("")

foreach ($targetFile in @($targetUasset, $targetUexp)) {
    $bytes = [IO.File]::ReadAllBytes($targetFile)
    $report.Add("File: $targetFile")
    foreach ($pair in $tokenReplacements) {
        $changes = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken $pair.Old -NewToken $pair.New
        $report.Add(("  {0} -> {1} : {2}" -f $pair.Old, $pair.New, $changes))
    }
    [IO.File]::WriteAllBytes($targetFile, $bytes)
}

if ($LabelKeyFrom -ne $LabelKeyTo -and $LabelKeyFrom.Length -ne $LabelKeyTo.Length) {
    $uexpBytes = [IO.File]::ReadAllBytes($targetUexp)
    $patched = Replace-LengthPrefixedAnsiTokenInBytes -Bytes $uexpBytes -OldToken $LabelKeyFrom -NewToken $LabelKeyTo
    [IO.File]::WriteAllBytes($targetUexp, $uexpBytes)
    $report.Add("Length-prefixed label-key patch in .uexp: $LabelKeyFrom -> $LabelKeyTo : $patched")
    if ($patched -eq 0) {
        $report.Add("WARN: Length-prefixed label-key patch did not find a writable occurrence.")
    }
}

# Verification pass: ensure ship-name token is gone from target pair.
$verifyFailures = 0
foreach ($targetFile in @($targetUasset, $targetUexp)) {
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetFile))
    if ($text.Contains("DA_BI_Utilities_Lables_Wooden_Ship")) {
        $verifyFailures++
        $report.Add("VERIFY FAIL: still found DA_BI_Utilities_Lables_Wooden_Ship in $targetFile")
    }
    if ($text.Contains($fullPathShip)) {
        $verifyFailures++
        $report.Add("VERIFY FAIL: still found $fullPathShip in $targetFile")
    }
}

if ($verifyFailures -eq 0) {
    $report.Add("VERIFY PASS: Target pair no longer references ship internal object/path.")
}

$reportPath = Join-Path $OutputRoot "clone_report.txt"
[IO.File]::WriteAllLines($reportPath, $report)

Write-Host "Generated:"
Write-Host "  $targetUasset"
Write-Host "  $targetUexp"
Write-Host "Report:"
Write-Host "  $reportPath"
if ($verifyFailures -ne 0) {
    exit 2
}
