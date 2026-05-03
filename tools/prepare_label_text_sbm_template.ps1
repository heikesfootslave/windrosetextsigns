param(
    [string]$SourceLegacyRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy",
    [string]$StorageProbeSourceRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\StorageProbe_Source",
    [string]$NoIconTextureSource = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\assets\textures\T_PlaqueT02_Text_WoodOnly_Source.png",
    [string]$TexconvPath = "C:\Games\Wabbajack\4.0.5.1\Tools\texconv.exe",
    [string]$OutputRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Text_SBMTemplate",
    [ValidateSet("NoIconTexture", "ShipIcon")]
    [string]$LabelIconMode = "NoIconTexture",
    [ValidateSet("WoodenLabels", "StorageProbe", "StorageOverrideProbe")]
    [string]$PrepareMode = "WoodenLabels"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Replace-AsciiTokenInBytes {
    param(
        [byte[]]$Bytes,
        [string]$OldToken,
        [string]$NewToken
    )

    if ($OldToken.Length -ne $NewToken.Length) {
        throw "Replace-AsciiTokenInBytes requires equal-length tokens. '$OldToken' ($($OldToken.Length)) vs '$NewToken' ($($NewToken.Length))."
    }

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

function Clone-SameLengthAssetPair {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$RelativeDir,
        [Parameter(Mandatory = $true)][string]$SourceName,
        [Parameter(Mandatory = $true)][string]$TargetName
    )

    if ($SourceName.Length -ne $TargetName.Length) {
        throw "Clone-SameLengthAssetPair requires same-length names. '$SourceName' ($($SourceName.Length)) vs '$TargetName' ($($TargetName.Length))."
    }

    $sourceDir = Join-Path (Join-Path $SourceRoot "R5\Content") $RelativeDir
    $targetDir = Join-Path (Join-Path $OutputRoot "R5\Content") $RelativeDir
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

    $copied = @()
    foreach ($ext in @(".uasset", ".uexp", ".ubulk")) {
        $sourceFile = Join-Path $sourceDir ($SourceName + $ext)
        if (-not (Test-Path -LiteralPath $sourceFile)) {
            continue
        }

        $targetFile = Join-Path $targetDir ($TargetName + $ext)
        Copy-Item -LiteralPath $sourceFile -Destination $targetFile -Force

        $bytes = [IO.File]::ReadAllBytes($targetFile)
        $r1 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken $SourceName -NewToken $TargetName
        $sourcePath = "/Game/" + ($RelativeDir -replace "\\", "/") + "/" + $SourceName
        $targetPath = "/Game/" + ($RelativeDir -replace "\\", "/") + "/" + $TargetName
        if ($sourcePath.Length -ne $targetPath.Length) {
            throw "Clone-SameLengthAssetPair requires same-length object paths. '$sourcePath' vs '$targetPath'."
        }
        $r2 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken $sourcePath -NewToken $targetPath
        [IO.File]::WriteAllBytes($targetFile, $bytes)
        $copied += ("{0}: name={1} path={2}" -f $targetFile, $r1, $r2)
    }

    if ($copied.Count -eq 0) {
        throw "Clone-SameLengthAssetPair did not copy any files from $sourceDir for $SourceName"
    }
    return $copied
}

function Get-Bc7MipByteCount {
    param(
        [Parameter(Mandatory = $true)][int]$Width,
        [Parameter(Mandatory = $true)][int]$Height
    )

    $blocksX = [Math]::Max(1, [int][Math]::Ceiling($Width / 4.0))
    $blocksY = [Math]::Max(1, [int][Math]::Ceiling($Height / 4.0))
    return $blocksX * $blocksY * 16
}

function Get-DdsDataOffset {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    if ($Bytes.Length -lt 128) {
        throw "DDS file is too small."
    }
    $magic = [Text.Encoding]::ASCII.GetString($Bytes, 0, 4)
    if ($magic -ne "DDS ") {
        throw "Not a DDS file."
    }

    $fourCc = [Text.Encoding]::ASCII.GetString($Bytes, 84, 4)
    if ($fourCc -eq "DX10") {
        return 148
    }
    return 128
}

function Convert-PngToBc7Dds {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePng,
        [Parameter(Mandatory = $true)][string]$OutputDir,
        [Parameter(Mandatory = $true)][string]$ConverterPath
    )

    if (-not (Test-Path -LiteralPath $SourcePng)) {
        throw "NoIcon texture source not found: $SourcePng"
    }
    if (-not (Test-Path -LiteralPath $ConverterPath)) {
        throw "texconv not found: $ConverterPath"
    }

    if (Test-Path -LiteralPath $OutputDir) {
        Remove-Item -LiteralPath $OutputDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

    & $ConverterPath -y -w 256 -h 256 -m 0 -f BC7_UNORM -ft dds -o $OutputDir $SourcePng | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "texconv failed with exit code $LASTEXITCODE"
    }

    $ddsPath = Join-Path $OutputDir (([IO.Path]::GetFileNameWithoutExtension($SourcePng)) + ".dds")
    if (-not (Test-Path -LiteralPath $ddsPath)) {
        $ddsPath = Get-ChildItem -LiteralPath $OutputDir -Filter "*.dds" -File | Select-Object -First 1 -ExpandProperty FullName
    }
    if ([string]::IsNullOrWhiteSpace($ddsPath) -or -not (Test-Path -LiteralPath $ddsPath)) {
        throw "texconv did not produce a DDS file in $OutputDir"
    }
    return $ddsPath
}

function Patch-InlineBc7TextureMips {
    param(
        [Parameter(Mandatory = $true)][string]$TargetUexp,
        [Parameter(Mandatory = $true)][string]$DdsPath
    )

    $uexp = [IO.File]::ReadAllBytes($TargetUexp)
    $dds = [IO.File]::ReadAllBytes($DdsPath)
    $ddsDataOffset = Get-DdsDataOffset -Bytes $dds
    $ddsCursor = $ddsDataOffset

    $pfBytes = [Text.Encoding]::ASCII.GetBytes("PF_BC7")
    $pfOffset = -1
    for ($i = 0; $i -le $uexp.Length - $pfBytes.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $pfBytes.Length; $j++) {
            if ($uexp[$i + $j] -ne $pfBytes[$j]) {
                $match = $false
                break
            }
        }
        if ($match) {
            $pfOffset = $i
            break
        }
    }
    if ($pfOffset -lt 0) {
        throw "Could not find PF_BC7 marker in $TargetUexp"
    }

    $sizeXOffset = $pfOffset - 16
    $sizeYOffset = $pfOffset - 12
    $mipCountOffset = -1
    for ($candidate = $pfOffset + 6; $candidate -le $pfOffset + 12; $candidate++) {
        if ($candidate + 4 -gt $uexp.Length) {
            break
        }
        $candidateValue = [BitConverter]::ToInt32($uexp, $candidate)
        if ($candidateValue -gt 0 -and $candidateValue -le 16) {
            $mipCountOffset = $candidate
            break
        }
    }
    if ($mipCountOffset -lt 0) {
        throw "Could not locate mip count after PF_BC7 marker in $TargetUexp"
    }
    $dataOffset = $mipCountOffset + 8

    $width = [BitConverter]::ToInt32($uexp, $sizeXOffset)
    $height = [BitConverter]::ToInt32($uexp, $sizeYOffset)
    $mipCount = [BitConverter]::ToInt32($uexp, $mipCountOffset)
    if ($width -ne 256 -or $height -ne 256 -or $mipCount -ne 9) {
        throw "Unexpected Texture2D layout for $TargetUexp. width=$width height=$height mipCount=$mipCount"
    }

    $uexpCursor = $dataOffset
    for ($mip = 0; $mip -lt $mipCount; $mip++) {
        $mipWidth = [Math]::Max(1, $width -shr $mip)
        $mipHeight = [Math]::Max(1, $height -shr $mip)
        $mipBytes = Get-Bc7MipByteCount -Width $mipWidth -Height $mipHeight

        if ($ddsCursor + $mipBytes -gt $dds.Length) {
            throw "DDS ended before mip $mip could be read."
        }
        if ($uexpCursor + $mipBytes + 12 -gt $uexp.Length) {
            throw "UEXP ended before mip $mip could be patched."
        }

        [Array]::Copy($dds, $ddsCursor, $uexp, $uexpCursor, $mipBytes)
        $ddsCursor += $mipBytes
        $uexpCursor += $mipBytes

        $storedWidth = [BitConverter]::ToInt32($uexp, $uexpCursor)
        $storedHeight = [BitConverter]::ToInt32($uexp, $uexpCursor + 4)
        $storedDepth = [BitConverter]::ToInt32($uexp, $uexpCursor + 8)
        if ($storedWidth -ne $mipWidth -or $storedHeight -ne $mipHeight -or $storedDepth -ne 1) {
            throw "Unexpected mip dimensions at mip $mip in $TargetUexp. expected=${mipWidth}x${mipHeight}x1 got=${storedWidth}x${storedHeight}x${storedDepth}"
        }
        $uexpCursor += 12
        if ($mip -lt ($mipCount - 1)) {
            $uexpCursor += 4
        }
    }

    $expectedDdsDataBytes = $dds.Length - $ddsDataOffset
    if ($ddsCursor - $ddsDataOffset -ne $expectedDdsDataBytes) {
        throw "DDS data was not fully consumed. consumed=$($ddsCursor - $ddsDataOffset) expected=$expectedDdsDataBytes"
    }

    [IO.File]::WriteAllBytes($TargetUexp, $uexp)
    return "Patched $mipCount BC7 mips from $DdsPath into $TargetUexp"
}

function Patch-LabelIconReference {
    param(
        [Parameter(Mandatory = $true)][string[]]$Files,
        [Parameter(Mandatory = $true)][string]$FromName,
        [Parameter(Mandatory = $true)][string]$ToName
    )

    if ($FromName.Length -ne $ToName.Length) {
        throw "Patch-LabelIconReference requires same-length names. '$FromName' vs '$ToName'."
    }

    $fromPath = "/Game/UI/HUD/Building/Icons/BuildingBits/" + $FromName
    $toPath = "/Game/UI/HUD/Building/Icons/BuildingBits/" + $ToName
    if ($fromPath.Length -ne $toPath.Length) {
        throw "Patch-LabelIconReference requires same-length paths. '$fromPath' vs '$toPath'."
    }

    $rows = @()
    foreach ($file in $Files) {
        $bytes = [IO.File]::ReadAllBytes($file)
        $r1 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken $FromName -NewToken $ToName
        $r2 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken $fromPath -NewToken $toPath
        [IO.File]::WriteAllBytes($file, $bytes)
        $rows += ("{0}: name={1} path={2}" -f $file, $r1, $r2)
    }
    return $rows
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cloneScript = Join-Path $scriptDir "clone_wooden_label_text.ps1"
if (-not (Test-Path -LiteralPath $cloneScript)) {
    throw "Missing clone script: $cloneScript"
}

$outputContentRoot = Join-Path $OutputRoot "R5"
if (Test-Path -LiteralPath $outputContentRoot) {
    Remove-Item -LiteralPath $outputContentRoot -Recurse -Force
}

$targetUasset = ""
$targetUexp = ""
$checks = [ordered]@{}

if ($PrepareMode -eq "WoodenLabels") {
    # SBM-template behavior:
    # - keep shared recipe/display-key behavior from source utility item
    # - create a new sibling DA asset identity
    # - keep no-icon variant for this custom entry
    & $cloneScript `
        -SourceLegacyRoot $SourceLegacyRoot `
        -OutputRoot $OutputRoot `
        -SourceAssetName "DA_BI_Utilities_Lables_Wooden_Ship_NoIcon" `
        -TargetAssetName "DA_BI_Utilities_Lables_Wooden_Text" `
        -LabelKeyFrom "Building_Lable_8" `
        -LabelKeyTo "Building_Lable_11"

    $targetUasset = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uasset"
    $targetUexp = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities\DA_BI_Utilities_Lables_Wooden_Text.uexp"

    foreach ($f in @($targetUasset, $targetUexp)) {
        if (-not (Test-Path -LiteralPath $f)) {
            throw "Missing expected generated file: $f"
        }
    }

    if ($LabelIconMode -eq "NoIconTexture") {
        $iconCloneRows = Clone-SameLengthAssetPair `
            -SourceRoot $SourceLegacyRoot `
            -OutputRoot $OutputRoot `
            -RelativeDir "UI\HUD\Building\Icons\BuildingBits" `
            -SourceName "T_PlaqueT02_Ship" `
            -TargetName "T_PlaqueT02_None"
        foreach ($row in $iconCloneRows) {
            Write-Host ("NoIcon texture clone: " + $row)
        }
        $targetIconUexp = Join-Path $OutputRoot "R5\Content\UI\HUD\Building\Icons\BuildingBits\T_PlaqueT02_None.uexp"
        $ddsOutDir = Join-Path $OutputRoot "_texture_build"
        $ddsPath = Convert-PngToBc7Dds -SourcePng $NoIconTextureSource -OutputDir $ddsOutDir -ConverterPath $TexconvPath
        $texturePatchReport = Patch-InlineBc7TextureMips -TargetUexp $targetIconUexp -DdsPath $ddsPath
        Write-Host ("NoIcon texture payload: " + $texturePatchReport)
    } else {
        $shipIconRows = Patch-LabelIconReference `
            -Files @($targetUasset, $targetUexp) `
            -FromName "T_PlaqueT02_None" `
            -ToName "T_PlaqueT02_Ship"
        foreach ($row in $shipIconRows) {
            Write-Host ("ShipIcon probe patch: " + $row)
        }
    }

    $uassetText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUasset))
    $uexpText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUexp))
    $targetIcon = Join-Path $OutputRoot "R5\Content\UI\HUD\Building\Icons\BuildingBits\T_PlaqueT02_None.uasset"

    $checks = [ordered]@{
        has_new_asset_name = $uassetText.Contains("DA_BI_Utilities_Lables_Wooden_Text")
        has_new_asset_path = $uassetText.Contains("/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text")
        keeps_shared_recipe = $uassetText.Contains("DA_RD_BuildObject_Utilities_Labels_Wooden_T01")
        has_expected_icon_reference = if ($LabelIconMode -eq "NoIconTexture") { $uassetText.Contains("T_PlaqueT02_None") } else { $uassetText.Contains("T_PlaqueT02_Ship") }
        has_noicon_texture_asset = if ($LabelIconMode -eq "NoIconTexture") { Test-Path -LiteralPath $targetIcon } else { $true }
        has_noicon_texture_source = if ($LabelIconMode -eq "NoIconTexture") { Test-Path -LiteralPath $NoIconTextureSource } else { $true }
        has_unique_label_key_11 = $uexpText.Contains("Building_Lable_11")
        no_ship_asset_name_remaining = -not $uassetText.Contains("DA_BI_Utilities_Lables_Wooden_Ship")
    }
} elseif ($PrepareMode -eq "StorageProbe") {
    # Storage probe:
    # - clone native legacy pair for WoodenChest_01
    # - rename internal asset identity to WoodenChest_11 (same-length token swap)
    # - optionally swap icon token to plaque icon for visibility in menu
    $sourceDir = Join-Path $StorageProbeSourceRoot "R5\Content\Gameplay\Building\BuildingUtilities"
    $sourceUasset = Join-Path $sourceDir "DA_BI_Utilities_Storage_WoodenChest_01.uasset"
    $sourceUexp = Join-Path $sourceDir "DA_BI_Utilities_Storage_WoodenChest_01.uexp"
    foreach ($f in @($sourceUasset, $sourceUexp)) {
        if (-not (Test-Path -LiteralPath $f)) {
            throw "StorageProbe source missing: $f"
        }
    }

    $targetDir = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities"
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    $targetUasset = Join-Path $targetDir "DA_BI_Utilities_Storage_WoodenChest_11.uasset"
    $targetUexp = Join-Path $targetDir "DA_BI_Utilities_Storage_WoodenChest_11.uexp"
    Copy-Item -LiteralPath $sourceUasset -Destination $targetUasset -Force
    Copy-Item -LiteralPath $sourceUexp -Destination $targetUexp -Force

    $patchRows = @()
    foreach ($f in @($targetUasset, $targetUexp)) {
        $bytes = [IO.File]::ReadAllBytes($f)
        $r1 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "DA_BI_Utilities_Storage_WoodenChest_01" -NewToken "DA_BI_Utilities_Storage_WoodenChest_11"
        $r2 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_01" -NewToken "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_11"
        $r3 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "T_WoodenChest_01" -NewToken "T_PlaqueT02_Ship"
        [IO.File]::WriteAllBytes($f, $bytes)
        $patchRows += ("{0}: asset={1} path={2} icon={3}" -f $f, $r1, $r2, $r3)
    }
    foreach ($row in $patchRows) {
        Write-Host ("StorageProbe patch: " + $row)
    }

    $uassetText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUasset))
    $uexpText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUexp))
    $checks = [ordered]@{
        has_storage_probe_asset_name = $uassetText.Contains("DA_BI_Utilities_Storage_WoodenChest_11")
        has_storage_probe_asset_path = $uassetText.Contains("/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Storage_WoodenChest_11")
        has_probe_icon_swap = $uassetText.Contains("T_PlaqueT02_Ship")
        keeps_storage_recipe = ($uassetText -match "DA_RD_BuildObject_Container_WoodChest_T0[0-9]")
        no_source_01_name_remaining = (-not $uassetText.Contains("DA_BI_Utilities_Storage_WoodenChest_01")) -and (-not $uexpText.Contains("DA_BI_Utilities_Storage_WoodenChest_01"))
    }
} else {
    # Storage override probe:
    # - keep native asset identity (WoodenChest_01) to force override
    # - patch icon token only; this is a clear visual "is mod loaded" test
    $sourceDir = Join-Path $StorageProbeSourceRoot "R5\Content\Gameplay\Building\BuildingUtilities"
    $sourceUasset = Join-Path $sourceDir "DA_BI_Utilities_Storage_WoodenChest_01.uasset"
    $sourceUexp = Join-Path $sourceDir "DA_BI_Utilities_Storage_WoodenChest_01.uexp"
    foreach ($f in @($sourceUasset, $sourceUexp)) {
        if (-not (Test-Path -LiteralPath $f)) {
            throw "StorageOverrideProbe source missing: $f"
        }
    }

    $targetDir = Join-Path $OutputRoot "R5\Content\Gameplay\Building\BuildingUtilities"
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    $targetUasset = Join-Path $targetDir "DA_BI_Utilities_Storage_WoodenChest_01.uasset"
    $targetUexp = Join-Path $targetDir "DA_BI_Utilities_Storage_WoodenChest_01.uexp"
    Copy-Item -LiteralPath $sourceUasset -Destination $targetUasset -Force
    Copy-Item -LiteralPath $sourceUexp -Destination $targetUexp -Force

    $patchRows = @()
    foreach ($f in @($targetUasset, $targetUexp)) {
        $bytes = [IO.File]::ReadAllBytes($f)
        $r1 = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "T_WoodenChest_01" -NewToken "T_PlaqueT02_Ship"
        [IO.File]::WriteAllBytes($f, $bytes)
        $patchRows += ("{0}: icon={1}" -f $f, $r1)
    }
    foreach ($row in $patchRows) {
        Write-Host ("StorageOverrideProbe patch: " + $row)
    }

    $uassetText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUasset))
    $checks = [ordered]@{
        keeps_native_asset_identity = $uassetText.Contains("DA_BI_Utilities_Storage_WoodenChest_01")
        has_override_icon_swap = $uassetText.Contains("T_PlaqueT02_Ship")
        has_no_native_icon_token = -not $uassetText.Contains("T_WoodenChest_01")
    }
}

$allPass = $true
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("prepare_label_text_sbm_template.ps1")
$lines.Add("OutputRoot=$OutputRoot")
$lines.Add("PrepareMode=$PrepareMode")
$lines.Add("LabelIconMode=$LabelIconMode")
$lines.Add("")
$lines.Add("Checks:")
foreach ($k in $checks.Keys) {
    $v = [bool]$checks[$k]
    if (-not $v) { $allPass = $false }
    $lines.Add(("  {0}={1}" -f $k, $v))
}
$lines.Add("")
$lines.Add(("overall_pass={0}" -f $allPass))

$reportPath = Join-Path $OutputRoot "sbm_template_report.txt"
[IO.File]::WriteAllLines($reportPath, $lines)

Write-Host "Generated SBM-template label asset:"
Write-Host "  $targetUasset"
if ($targetUexp -ne "") {
    Write-Host "  $targetUexp"
}
Write-Host "Report:"
Write-Host "  $reportPath"
Write-Host "overall_pass=$allPass"

if (-not $allPass) {
    exit 2
}
