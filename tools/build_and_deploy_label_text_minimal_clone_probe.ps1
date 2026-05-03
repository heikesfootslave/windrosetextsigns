param(
    [string]$SourceLegacyRoot = "C:\Users\User\Documents\Windrose Addons\Output\WoodenLabel_Legacy",
    [string]$SbmZipPath = "C:\Users\User\Downloads\SBM_ContainersChest_P.zip",
    [string]$NoIconTextureSource = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\assets\textures\T_PlaqueT02_Text_WoodOnly_Source.png",
    [string]$TexconvPath = "C:\Games\Wabbajack\4.0.5.1\Tools\texconv.exe",
    [string]$StageRoot = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\build_logs\WoodenLabel_Text_MinimalCloneProbe_stage",
    [string]$PackageBaseName = "WoodenLabel_Text_MinimalCloneProbe_P",
    [string]$ModsDir = "C:\SteamLibrary\steamapps\common\Windrose\R5\Content\Paks\~mods",
    [string]$ServerModsDir = "C:\Games\WindowsServer\R5\Content\Paks\~mods",
    [ValidateSet("ShipIcon", "NoIconTexture")]
    [string]$LabelIconMode = "ShipIcon",
    [ValidateSet("NativeShip", "TextKey")]
    [string]$DisplayNameMode = "TextKey",
    [float]$WorldSignIndex = 0.0,
    [switch]$PatchWorldSignIndex,
    [ValidateSet("UE5_6")]
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

function Assert-ExpectedModsPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $normalized.EndsWith("\R5\Content\Paks\~mods", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing deploy to unexpected mods path: $normalized"
    }
}

function Replace-AsciiTokenInBytes {
    param(
        [byte[]]$Bytes,
        [string]$OldToken,
        [string]$NewToken
    )

    if ($OldToken.Length -ne $NewToken.Length) {
        throw "Binary patch requires same-length tokens. '$OldToken' ($($OldToken.Length)) != '$NewToken' ($($NewToken.Length))."
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
        if (-not $match) {
            continue
        }

        for ($j = 0; $j -lt $new.Length; $j++) {
            $Bytes[$i + $j] = $new[$j]
        }
        $count++
        $i += ($old.Length - 1)
    }

    return $count
}

function Set-Float32LE {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][float]$Value
    )

    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw "Float patch offset 0x$($Offset.ToString("X")) is outside byte array length $($Bytes.Length)."
    }
    $valueBytes = [BitConverter]::GetBytes([float]$Value)
    [Array]::Copy($valueBytes, 0, $Bytes, $Offset, 4)
}

function Invoke-Repak {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & repak @Args
    if ($LASTEXITCODE -ne 0) {
        throw "repak failed (exit $LASTEXITCODE): repak $($Args -join ' ')"
    }
}

function Invoke-Retoc {
    param([Parameter(Mandatory = $true)][string[]]$Args)
    & retoc @Args
    if ($LASTEXITCODE -ne 0) {
        throw "retoc failed (exit $LASTEXITCODE): retoc $($Args -join ' ')"
    }
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
        [Parameter(Mandatory = $true)][string]$ConverterPath,
        [bool]$SrgbOutput = $true
    )

    if (-not (Test-Path -LiteralPath $SourcePng)) {
        throw "No-icon texture source not found: $SourcePng"
    }
    if (-not (Test-Path -LiteralPath $ConverterPath)) {
        throw "texconv not found: $ConverterPath"
    }

    if (Test-Path -LiteralPath $OutputDir) {
        Remove-Item -LiteralPath $OutputDir -Recurse -Force
    }
    Ensure-Directory -Path $OutputDir

    $texconvArgs = @("-y", "-w", "256", "-h", "256", "-m", "0")
    if ($SrgbOutput) {
        $texconvArgs += "-srgbo"
    }
    $texconvArgs += @("-f", "BC7_UNORM", "-ft", "dds", "-o", $OutputDir, $SourcePng)
    & $ConverterPath @texconvArgs | Out-Host
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

function Write-UnrealStringBinary {
    param(
        [Parameter(Mandatory = $true)][System.IO.BinaryWriter]$Writer,
        [AllowNull()][string]$Value
    )

    if ([string]::IsNullOrEmpty($Value)) {
        $Writer.Write([int]0)
        return
    }

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
    $Writer.Write([int]($bytes.Length + 1))
    $Writer.Write($bytes)
    $Writer.Write([byte]0)
}

function New-LabelTextLocRes {
    param([Parameter(Mandatory = $true)][string]$ContentRoot)

    $locresPath = Join-Path $ContentRoot "Localization\Game\en\WindroseTextSigns.locres"
    Ensure-Directory -Path (Split-Path -Parent $locresPath)

    # UE locres v1 compact format. This adds only the new text key used by the cloned data asset.
    [byte[]]$magic = @(0x0E, 0x14, 0x74, 0x75, 0x67, 0x4A, 0x03, 0xFC, 0x4A, 0x15, 0x90, 0x9D, 0xC3, 0x37, 0x7F, 0x1B)
    $stream = [System.IO.File]::Create($locresPath)
    try {
        $writer = [System.IO.BinaryWriter]::new($stream)
        try {
            $writer.Write($magic)
            $writer.Write([byte]1)
            $stringTableOffsetPatchPosition = $stream.Position
            $writer.Write([long]0)

            $writer.Write([int]1)
            Write-UnrealStringBinary -Writer $writer -Value "BuildingItems"
            $writer.Write([int]1)
            Write-UnrealStringBinary -Writer $writer -Value "Building_Lable_T"
            $writer.Write([uint32]0)
            $writer.Write([int]0)

            $stringTableOffset = $stream.Position
            $writer.Write([int]1)
            Write-UnrealStringBinary -Writer $writer -Value "Label: Text"

            $stream.Position = $stringTableOffsetPatchPosition
            $writer.Write([long]$stringTableOffset)
            $stream.Position = $stream.Length
        } finally {
            $writer.Dispose()
        }
    } finally {
        $stream.Dispose()
    }

    return $locresPath
}

function New-TransparentNoIconSource {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePng,
        [Parameter(Mandatory = $true)][string]$OutputPng
    )

    if (-not (Test-Path -LiteralPath $SourcePng)) {
        throw "No-icon texture source not found: $SourcePng"
    }

    Add-Type -AssemblyName System.Drawing

    $source = [System.Drawing.Bitmap]::new($SourcePng)
    try {
        $minX = $source.Width
        $minY = $source.Height
        $maxX = -1
        $maxY = -1

        $mask = New-Object 'bool[,]' $source.Width, $source.Height
        for ($y = 0; $y -lt $source.Height; $y++) {
            for ($x = 0; $x -lt $source.Width; $x++) {
                $c = $source.GetPixel($x, $y)
                # The source art uses an almost-white baked backdrop. Treat only high, low-saturation pixels as background.
                $max = [Math]::Max($c.R, [Math]::Max($c.G, $c.B))
                $min = [Math]::Min($c.R, [Math]::Min($c.G, $c.B))
                $isBackground = ($c.A -lt 8) -or ($min -ge 224 -and ($max - $min) -le 18)
                $isForeground = -not $isBackground
                $mask[$x, $y] = $isForeground
                if ($isForeground) {
                    if ($x -lt $minX) { $minX = $x }
                    if ($y -lt $minY) { $minY = $y }
                    if ($x -gt $maxX) { $maxX = $x }
                    if ($y -gt $maxY) { $maxY = $y }
                }
            }
        }

        if ($maxX -lt 0 -or $maxY -lt 0) {
            throw "Could not find foreground pixels in no-icon texture source: $SourcePng"
        }

        $cropW = $maxX - $minX + 1
        $cropH = $maxY - $minY + 1
        $targetW = 246
        $targetH = 154
        $scale = [Math]::Min($targetW / [double]$cropW, $targetH / [double]$cropH)
        $drawW = [Math]::Max(1, [int][Math]::Round($cropW * $scale))
        $drawH = [Math]::Max(1, [int][Math]::Round($cropH * $scale))
        $drawX = [int][Math]::Round((256 - $drawW) / 2.0)
        $drawY = [int][Math]::Round((256 - $drawH) / 2.0)

        $trimmed = [System.Drawing.Bitmap]::new($cropW, $cropH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $rTotal = 0L
            $gTotal = 0L
            $bTotal = 0L
            $foregroundCount = 0L
            for ($y = 0; $y -lt $cropH; $y++) {
                for ($x = 0; $x -lt $cropW; $x++) {
                    $srcX = $minX + $x
                    $srcY = $minY + $y
                    if ($mask[$srcX, $srcY]) {
                        $c = $source.GetPixel($srcX, $srcY)
                        $rTotal += $c.R
                        $gTotal += $c.G
                        $bTotal += $c.B
                        $foregroundCount++
                    }
                }
            }
            if ($foregroundCount -le 0) {
                throw "Could not compute foreground average for no-icon texture source: $SourcePng"
            }
            $avgWoodColor = [System.Drawing.Color]::FromArgb(
                0,
                [int]($rTotal / $foregroundCount),
                [int]($gTotal / $foregroundCount),
                [int]($bTotal / $foregroundCount)
            )

            for ($y = 0; $y -lt $cropH; $y++) {
                for ($x = 0; $x -lt $cropW; $x++) {
                    $srcX = $minX + $x
                    $srcY = $minY + $y
                    if ($mask[$srcX, $srcY]) {
                        $trimmed.SetPixel($x, $y, $source.GetPixel($srcX, $srcY))
                    } else {
                        $trimmed.SetPixel($x, $y, $avgWoodColor)
                    }
                }
            }

            $canvas = [System.Drawing.Bitmap]::new(256, 256, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $graphics = [System.Drawing.Graphics]::FromImage($canvas)
                try {
                    $graphics.Clear($avgWoodColor)
                    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    $graphics.DrawImage($trimmed, $drawX, $drawY, $drawW, $drawH)
                } finally {
                    $graphics.Dispose()
                }

                # Match the native wooden-label UI texture grade. The world object already uses
                # native material data; this keeps the build-menu icon from looking like separate art.
                [double]$targetMeanR = 106.8160982003
                [double]$targetMeanG = 91.7776747754
                [double]$targetMeanB = 76.1346098520
                [double]$targetStdR = 39.8369045814
                [double]$targetStdG = 34.6461329576
                [double]$targetStdB = 29.3824123692
                [double]$sourceTotalR = 0.0
                [double]$sourceTotalG = 0.0
                [double]$sourceTotalB = 0.0
                $sourceCount = 0
                for ($y = 0; $y -lt $canvas.Height; $y++) {
                    for ($x = 0; $x -lt $canvas.Width; $x++) {
                        $c = $canvas.GetPixel($x, $y)
                        if ($c.A -lt 32) {
                            continue
                        }
                        $sourceTotalR += $c.R
                        $sourceTotalG += $c.G
                        $sourceTotalB += $c.B
                        $sourceCount++
                    }
                }
                if ($sourceCount -gt 0) {
                    [double]$sourceMeanR = $sourceTotalR / $sourceCount
                    [double]$sourceMeanG = $sourceTotalG / $sourceCount
                    [double]$sourceMeanB = $sourceTotalB / $sourceCount
                    [double]$sourceVarianceR = 0.0
                    [double]$sourceVarianceG = 0.0
                    [double]$sourceVarianceB = 0.0
                    for ($y = 0; $y -lt $canvas.Height; $y++) {
                        for ($x = 0; $x -lt $canvas.Width; $x++) {
                            $c = $canvas.GetPixel($x, $y)
                            if ($c.A -lt 32) {
                                continue
                            }
                            $sourceVarianceR += ($c.R - $sourceMeanR) * ($c.R - $sourceMeanR)
                            $sourceVarianceG += ($c.G - $sourceMeanG) * ($c.G - $sourceMeanG)
                            $sourceVarianceB += ($c.B - $sourceMeanB) * ($c.B - $sourceMeanB)
                        }
                    }
                    [double]$sourceStdR = [Math]::Max(0.001, [Math]::Sqrt($sourceVarianceR / $sourceCount))
                    [double]$sourceStdG = [Math]::Max(0.001, [Math]::Sqrt($sourceVarianceG / $sourceCount))
                    [double]$sourceStdB = [Math]::Max(0.001, [Math]::Sqrt($sourceVarianceB / $sourceCount))
                    for ($y = 0; $y -lt $canvas.Height; $y++) {
                        for ($x = 0; $x -lt $canvas.Width; $x++) {
                            $c = $canvas.GetPixel($x, $y)
                            if ($c.A -lt 32) {
                                continue
                            }
                            $r = [int][Math]::Max(0, [Math]::Min(255, [Math]::Round((($c.R - $sourceMeanR) / $sourceStdR) * $targetStdR + $targetMeanR)))
                            $g = [int][Math]::Max(0, [Math]::Min(255, [Math]::Round((($c.G - $sourceMeanG) / $sourceStdG) * $targetStdG + $targetMeanG)))
                            $b = [int][Math]::Max(0, [Math]::Min(255, [Math]::Round((($c.B - $sourceMeanB) / $sourceStdB) * $targetStdB + $targetMeanB)))
                            $canvas.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($c.A, $r, $g, $b))
                        }
                    }
                }

                Ensure-Directory -Path (Split-Path -Parent $OutputPng)
                $canvas.Save($OutputPng, [System.Drawing.Imaging.ImageFormat]::Png)
            } finally {
                $canvas.Dispose()
            }
        } finally {
            $trimmed.Dispose()
        }
    } finally {
        $source.Dispose()
    }

    return $OutputPng
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

function Deploy-ProbePackage {
    param(
        [Parameter(Mandatory = $true)][string]$TargetModsDir,
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$PackageBaseName
    )

    Assert-ExpectedModsPath -Path $TargetModsDir
    Ensure-Directory -Path $TargetModsDir

    foreach ($knownProbe in @("WoodenLabel_CategoryNativeProbe_P", "WoodenLabel_Text_SBMTemplate_P", "WoodenLabel_Text_MinimalCloneProbe_P", "WoodenLabel_Text_NoIconProbe_P", $PackageBaseName)) {
        $knownProbeDir = Join-Path $TargetModsDir $knownProbe
        if (Test-Path -LiteralPath $knownProbeDir) {
            Write-Step "Removing prior probe folder: $knownProbeDir"
            Remove-Item -LiteralPath $knownProbeDir -Recurse -Force
        }
    }

    $targetDir = Join-Path $TargetModsDir $PackageBaseName
    Ensure-Directory -Path $targetDir
    foreach ($ext in @("utoc", "ucas", "pak")) {
        Copy-Item -LiteralPath (Join-Path $StageRoot ($PackageBaseName + "." + $ext)) -Destination (Join-Path $targetDir ($PackageBaseName + "." + $ext)) -Force
    }

    Write-Host "Deployed files:"
    foreach ($ext in @("utoc", "ucas", "pak")) {
        Write-Host ("  " + (Join-Path $targetDir ($PackageBaseName + "." + $ext)))
    }
}

Write-Step "Starting minimal Label: Text clone probe"
$retocPath = Ensure-Command -Name "retoc"
Write-Step "Using retoc: $retocPath"
$repakPath = Ensure-Command -Name "repak"
Write-Step "Using repak: $repakPath"

if (-not (Test-Path -LiteralPath $SbmZipPath)) {
    throw "SBM reference zip not found: $SbmZipPath"
}

Write-Step "Preparing stage root: $StageRoot"
Remove-DirectoryContents -Path $StageRoot

$contentRoot = Join-Path $StageRoot "R5\Content"
$buildingUtilityDir = Join-Path $contentRoot "Gameplay\Building\BuildingUtilities"
Ensure-Directory -Path $buildingUtilityDir

$sourceDir = Join-Path $SourceLegacyRoot "R5\Content\Gameplay\Building\BuildingUtilities"
$sourceUasset = Join-Path $sourceDir "DA_BI_Utilities_Lables_Wooden_Ship.uasset"
$sourceUexp = Join-Path $sourceDir "DA_BI_Utilities_Lables_Wooden_Ship.uexp"
foreach ($source in @($sourceUasset, $sourceUexp)) {
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Native Ship label source missing: $source"
    }
}

$targetUasset = Join-Path $buildingUtilityDir "DA_BI_Utilities_Lables_Wooden_Text.uasset"
$targetUexp = Join-Path $buildingUtilityDir "DA_BI_Utilities_Lables_Wooden_Text.uexp"
Copy-Item -LiteralPath $sourceUasset -Destination $targetUasset -Force
Copy-Item -LiteralPath $sourceUexp -Destination $targetUexp -Force

Write-Step "Renaming native Ship package/object to Text; keeping native label key"
$patchReport = [System.Collections.Generic.List[string]]::new()
foreach ($file in @($targetUasset, $targetUexp)) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $nameChanges = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "DA_BI_Utilities_Lables_Wooden_Ship" -NewToken "DA_BI_Utilities_Lables_Wooden_Text"
    $pathChanges = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Ship" -NewToken "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text"
    if ([IO.Path]::GetExtension($file) -eq ".uexp") {
        if ($DisplayNameMode -eq "TextKey") {
            $labelKeyChanges = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "Building_Lable_8" -NewToken "Building_Lable_T"
            $patchReport.Add(("{0}: displayKey={1}" -f $file, $labelKeyChanges))
        }
        if ($PatchWorldSignIndex) {
            Set-Float32LE -Bytes $bytes -Offset 0x108 -Value $WorldSignIndex
            $patchReport.Add(("{0}: worldSignIndexOffset0x108={1}" -f $file, $WorldSignIndex))
        }
    }
    [IO.File]::WriteAllBytes($file, $bytes)
    $patchReport.Add(("{0}: name={1} path={2}" -f $file, $nameChanges, $pathChanges))
}

if ($LabelIconMode -eq "NoIconTexture") {
    Write-Step "Staging no-symbol wooden label texture and patching Text DA icon reference"
    $iconSourceDir = Join-Path $SourceLegacyRoot "R5\Content\UI\HUD\Building\Icons\BuildingBits"
    $iconTargetDir = Join-Path $contentRoot "UI\HUD\Building\Icons\BuildingBits"
    Ensure-Directory -Path $iconTargetDir

    $sourceIconUasset = Join-Path $iconSourceDir "T_PlaqueT02_Ship.uasset"
    $sourceIconUexp = Join-Path $iconSourceDir "T_PlaqueT02_Ship.uexp"
    foreach ($sourceIcon in @($sourceIconUasset, $sourceIconUexp)) {
        if (-not (Test-Path -LiteralPath $sourceIcon)) {
            throw "Native Ship icon source missing: $sourceIcon"
        }
    }

    $targetIconUasset = Join-Path $iconTargetDir "T_PlaqueT02_None.uasset"
    $targetIconUexp = Join-Path $iconTargetDir "T_PlaqueT02_None.uexp"
    Copy-Item -LiteralPath $sourceIconUasset -Destination $targetIconUasset -Force
    Copy-Item -LiteralPath $sourceIconUexp -Destination $targetIconUexp -Force

    $iconPathShip = "/Game/UI/HUD/Building/Icons/BuildingBits/T_PlaqueT02_Ship"
    $iconPathNone = "/Game/UI/HUD/Building/Icons/BuildingBits/T_PlaqueT02_None"
    foreach ($file in @($targetUasset, $targetUexp, $targetIconUasset, $targetIconUexp)) {
        $bytes = [IO.File]::ReadAllBytes($file)
        $iconNameChanges = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken "T_PlaqueT02_Ship" -NewToken "T_PlaqueT02_None"
        $iconPathChanges = Replace-AsciiTokenInBytes -Bytes $bytes -OldToken $iconPathShip -NewToken $iconPathNone
        [IO.File]::WriteAllBytes($file, $bytes)
        $patchReport.Add(("{0}: iconName={1} iconPath={2}" -f $file, $iconNameChanges, $iconPathChanges))
    }

    $ddsOutDir = Join-Path $StageRoot "_texture_build"
    $processedNoIconSource = Join-Path (Join-Path $StageRoot "_texture_processed") "T_PlaqueT02_None_Processed.png"
    $processedNoIconSource = New-TransparentNoIconSource -SourcePng $NoIconTextureSource -OutputPng $processedNoIconSource
    $patchReport.Add("Processed transparent no-icon source: $processedNoIconSource")
    $ddsPath = Convert-PngToBc7Dds -SourcePng $processedNoIconSource -OutputDir $ddsOutDir -ConverterPath $TexconvPath -SrgbOutput $true
    $texturePatchReport = Patch-InlineBc7TextureMips -TargetUexp $targetIconUexp -DdsPath $ddsPath
    $patchReport.Add($texturePatchReport)
}

$uassetText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUasset))
$uexpText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($targetUexp))
$checks = [ordered]@{
    has_text_asset_name = $uassetText.Contains("DA_BI_Utilities_Lables_Wooden_Text")
    has_text_asset_path = $uassetText.Contains("/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text")
    has_expected_icon = if ($LabelIconMode -eq "NoIconTexture") { $uassetText.Contains("T_PlaqueT02_None") } else { $uassetText.Contains("T_PlaqueT02_Ship") }
    has_expected_display_key = if ($DisplayNameMode -eq "TextKey") { $uexpText.Contains("Building_Lable_T") } else { $uexpText.Contains("Building_Lable_8") }
    no_ship_asset_name_remaining = (-not $uassetText.Contains("DA_BI_Utilities_Lables_Wooden_Ship")) -and (-not $uexpText.Contains("DA_BI_Utilities_Lables_Wooden_Ship"))
}

$allPass = $true
foreach ($k in $checks.Keys) {
    if (-not [bool]$checks[$k]) {
        $allPass = $false
    }
}
if (-not $allPass) {
    $lines = @("Minimal clone verification failed.", "Patch report:") + $patchReport
    foreach ($k in $checks.Keys) {
        $lines += ("{0}={1}" -f $k, [bool]$checks[$k])
    }
    $reportPath = Join-Path $StageRoot "minimal_clone_report.txt"
    [IO.File]::WriteAllLines($reportPath, $lines)
    throw "Minimal clone verification failed. See $reportPath"
}

$zipExtractRoot = Join-Path $StageRoot "zip_extract"
Ensure-Directory -Path $zipExtractRoot
Expand-Archive -LiteralPath $SbmZipPath -DestinationPath $zipExtractRoot -Force
$sourcePak = Join-Path $zipExtractRoot "SBM_ContainersChest_P\SBM_ContainersChest_P.pak"
if (-not (Test-Path -LiteralPath $sourcePak)) {
    throw "SBM pak not found after zip extract: $sourcePak"
}

$categoryExtractRoot = Join-Path $StageRoot "category_extract"
Write-Step "Extracting and patching build-menu category JSON"
Invoke-Repak -Args @(
    "unpack", "-q", "-f",
    "-o", $categoryExtractRoot,
    "-i", "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json",
    $sourcePak
)

$categoryJsonPath = Join-Path $categoryExtractRoot "R5\Content\UI\HUD\Building\DA_BuildingUICategories.json"
if (-not (Test-Path -LiteralPath $categoryJsonPath)) {
    throw "Category JSON missing after extraction: $categoryJsonPath"
}

$categoryJson = Get-Content -LiteralPath $categoryJsonPath -Raw | ConvertFrom-Json
$utilitiesCategory = ($categoryJson.CategoriesMap.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Utilities"}' } | Select-Object -First 1).Value
if ($null -eq $utilitiesCategory) {
    throw "Could not find UI.Building.Utilities category."
}
$storageGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "ComfortType.Utilities.Storage"}' } | Select-Object -First 1).Value
if ($null -ne $storageGroup) {
    $storageGroup.Items = @($storageGroup.Items | Where-Object {
        $_ -notmatch "DA_BI_Utilities_Storage_WoodenChest_(Alchemy|Clothing|Food|FoodIngridients|Ore|Ship|Trade|Treasure|Weapons|Wood)"
    })
}
$woodLabelsGroup = ($utilitiesCategory.Groups.PSObject.Properties | Where-Object { $_.Name -eq '{"TagName": "UI.Building.Lables.Wood"}' } | Select-Object -First 1).Value
if ($null -eq $woodLabelsGroup) {
    throw "Could not find UI.Building.Lables.Wood group."
}
$textEntry = "/Game/Gameplay/Building/BuildingUtilities/DA_BI_Utilities_Lables_Wooden_Text.DA_BI_Utilities_Lables_Wooden_Text"
$woodLabelsGroup.Items = @($woodLabelsGroup.Items | Where-Object { $_ -ne $textEntry })
$woodLabelsGroup.Items += $textEntry

$targetCategoryJsonPath = Join-Path $contentRoot "UI\HUD\Building\DA_BuildingUICategories.json"
Ensure-Directory -Path (Split-Path -Parent $targetCategoryJsonPath)
$categoryJson | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $targetCategoryJsonPath -Encoding ASCII

$labelTextLocResPath = New-LabelTextLocRes -ContentRoot $contentRoot
$patchReport.Add("Added Label: Text locres probe: $labelTextLocResPath")

$outUtoc = Join-Path $StageRoot ($PackageBaseName + ".utoc")
$outUcas = Join-Path $StageRoot ($PackageBaseName + ".ucas")
$outPak = Join-Path $StageRoot ($PackageBaseName + ".pak")

Write-Step "Building zen container and pak"
Invoke-Retoc -Args @("to-zen", "--version", $EngineVersion, $contentRoot, $outUtoc)
Invoke-Repak -Args @("pack", "-q", "--version", "V3", "--compression", "Zlib", "-m", "../../../R5/Content/", $contentRoot, $outPak)

foreach ($file in @($outUtoc, $outUcas, $outPak)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Build output missing: $file"
    }
    if ((Get-Item -LiteralPath $file).Length -le 0) {
        throw "Build output is empty: $file"
    }
}

$retocList = & retoc list --path $outUtoc 2>&1 | Out-String
if (-not $retocList.Contains("DA_BI_Utilities_Lables_Wooden_Text")) {
    throw "Container verification failed: Text DA missing from utoc list."
}
$categoryText = & repak get $outPak "R5/Content/UI/HUD/Building/DA_BuildingUICategories.json" | Out-String
if (-not $categoryText.Contains("DA_BI_Utilities_Lables_Wooden_Text")) {
    throw "Pak verification failed: Text DA missing from category JSON."
}

$reportPath = Join-Path $StageRoot "minimal_clone_report.txt"
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("build_and_deploy_label_text_minimal_clone_probe.ps1")
$report.Add("Purpose: isolate additive DA package creation from icon and label-key mutations.")
$report.Add("")
$report.Add("Patch report:")
foreach ($line in $patchReport) {
    $report.Add("  $line")
}
$report.Add("")
$report.Add("Checks:")
foreach ($k in $checks.Keys) {
    $report.Add(("  {0}={1}" -f $k, [bool]$checks[$k]))
}
$report.Add("")
$report.Add("Outputs:")
$report.Add("  $outUtoc")
$report.Add("  $outUcas")
$report.Add("  $outPak")
[IO.File]::WriteAllLines($reportPath, $report)
Write-Step "Verification passed. Report: $reportPath"

if ($SkipDeploy) {
    Write-Step "SkipDeploy enabled; stopping before deployment."
    exit 0
}

Write-Step "Deploying minimal clone probe to client ~mods"
Deploy-ProbePackage -TargetModsDir $ModsDir -StageRoot $StageRoot -PackageBaseName $PackageBaseName

if (-not $SkipServerDeploy) {
    Write-Step "Deploying minimal clone probe to server ~mods"
    Deploy-ProbePackage -TargetModsDir $ServerModsDir -StageRoot $StageRoot -PackageBaseName $PackageBaseName
}

Write-Step "Deploy complete"
