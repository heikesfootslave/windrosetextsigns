param(
    [string]$RootDir = "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns",
    [string]$TemplateRoot = "C:\Users\User\Documents\Windrose Addons\PlayerSharedMapPing\_builddeps\UE4SSCPPTemplate",
    [string]$BuildDir = "C:\Users\User\Documents\Windrose Addons\PlayerSharedMapPing\_builddeps\UE4SSCPPTemplate\build_vs_ninja_windrosetextsigns_short3",
    [string]$VsDevCmdPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
    [string]$CMakeExePath = "C:\Program Files\CMake\bin\cmake.exe",
    [string]$NinjaExePath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
    [string]$CmdExePath = "C:\WINDOWS\System32\cmd.exe",
    [int]$HeartbeatSeconds = 60,
    [int]$NoProgressLimit = 3,
    [int]$ConfigureTimeoutMinutes = 5,
    [int]$BuildTimeoutMinutes = 30,
    [switch]$SkipConfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-ProcessPathEnv {
    $processPath = [System.Environment]::GetEnvironmentVariable("PATH", "Process")
    if ($processPath) {
        [System.Environment]::SetEnvironmentVariable("Path", $processPath, "Process")
    }
    [System.Environment]::SetEnvironmentVariable("PATH", $null, "Process")
}

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

function Clear-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        return
    }
    Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction Stop
        } catch {
        }
    }
}

function Require-Path {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Label)
    if (!(Test-Path -LiteralPath $Path)) {
        throw "$Label not found: `"$Path`""
    }
}

function Copy-IfExists {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

function Ensure-MyCppModRegistered {
    param(
        [Parameter(Mandatory = $true)][string]$MyCppModsCMakePath,
        [Parameter(Mandatory = $true)][string]$SubdirectoryPath
    )

    if (!(Test-Path -LiteralPath $MyCppModsCMakePath)) {
        throw "MyCPPMods CMakeLists not found: `"$MyCppModsCMakePath`""
    }

    $line = "add_subdirectory($SubdirectoryPath)"
    $raw = Get-Content -LiteralPath $MyCppModsCMakePath -Raw
    $raw = [regex]::Replace($raw, "(?im)^\s*add_subdirectory\(WindroseTextSigns\)\s*\r?\n?", "")
    $escapedSubdir = [regex]::Escape($SubdirectoryPath)
    $pattern = "(?im)^\s*add_subdirectory\($escapedSubdir\)\s*$"
    if ($raw -notmatch $pattern) {
        $trimmed = $raw.TrimEnd("`r", "`n")
        $newContent = $trimmed + "`r`n" + $line + "`r`n"
        Set-Content -LiteralPath $MyCppModsCMakePath -Value $newContent -Encoding ascii
        Write-Step "Registered MyCPP mod in template CMakeLists: `"$line`""
    } else {
        Write-Step "MyCPP mod already registered in template CMakeLists: `"$line`""
    }
}

function New-DeploymentZip {
    param(
        [Parameter(Mandatory = $true)][string]$ModRoot,
        [Parameter(Mandatory = $true)][string]$DeploymentsDir,
        [Parameter(Mandatory = $true)][string]$BuiltDllPath,
        [Parameter(Mandatory = $true)][datetime]$BuildStartedUtc
    )

    Ensure-Directory -Path $DeploymentsDir

    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $zipPath = Join-Path $DeploymentsDir ("WindroseTextSigns_" + $stamp + ".zip")
    $stageRoot = Join-Path $DeploymentsDir ("_stage_" + $stamp)
    $stageMod = Join-Path $stageRoot "WindroseTextSigns"

    Ensure-Directory -Path $stageMod

    $enabledPath = Join-Path $ModRoot "enabled.txt"
    $configDir = Join-Path $ModRoot "Config"
    $dllsDir = Join-Path $ModRoot "dlls"
    $mainDll = Join-Path $dllsDir "main.dll"

    if (!(Test-Path -LiteralPath $enabledPath)) {
        throw "Cannot package deployment: missing `"$enabledPath`"."
    }
    if (!(Test-Path -LiteralPath $configDir)) {
        throw "Cannot package deployment: missing `"$configDir`"."
    }
    if (!(Test-Path -LiteralPath $mainDll)) {
        throw "Cannot package deployment: missing `"$mainDll`"."
    }
    if (!(Test-Path -LiteralPath $BuiltDllPath)) {
        throw "Cannot package deployment: missing built DLL `"$BuiltDllPath`"."
    }

    Copy-Item -LiteralPath $enabledPath -Destination (Join-Path $stageMod "enabled.txt") -Force
    Copy-Item -LiteralPath $configDir -Destination (Join-Path $stageMod "Config") -Recurse -Force
    Copy-Item -LiteralPath $dllsDir -Destination (Join-Path $stageMod "dlls") -Recurse -Force

    $readmePath = Join-Path $ModRoot "README.md"
    if (Test-Path -LiteralPath $readmePath) {
        Copy-Item -LiteralPath $readmePath -Destination (Join-Path $stageMod "README.md") -Force
    }
    $changelogPath = Join-Path $ModRoot "CHANGELOG.md"
    if (Test-Path -LiteralPath $changelogPath) {
        Copy-Item -LiteralPath $changelogPath -Destination (Join-Path $stageMod "CHANGELOG.md") -Force
    }

    $builtDllHash = Get-FileHash -LiteralPath $BuiltDllPath -Algorithm SHA256
    $mainDllHash = Get-FileHash -LiteralPath $mainDll -Algorithm SHA256
    if ($builtDllHash.Hash -ne $mainDllHash.Hash) {
        throw "Refusing to package: runtime main.dll hash does not match built DLL hash (possible stale DLL)."
    }

    $builtDllItem = Get-Item -LiteralPath $BuiltDllPath
    $mainDllItem = Get-Item -LiteralPath $mainDll
    $manifestPath = Join-Path $stageMod "DEPLOYMENT_MANIFEST.txt"
    $manifest = @(
        "mod=WindroseTextSigns"
        "packagedUtc=$([DateTime]::UtcNow.ToString("o"))"
        "buildStartedUtc=$($BuildStartedUtc.ToString("o"))"
        "builtDllPath=$BuiltDllPath"
        "builtDllSize=$($builtDllItem.Length)"
        "builtDllLastWriteUtc=$($builtDllItem.LastWriteTimeUtc.ToString("o"))"
        "builtDllSha256=$($builtDllHash.Hash)"
        "runtimeMainDllPath=$mainDll"
        "runtimeMainDllSize=$($mainDllItem.Length)"
        "runtimeMainDllLastWriteUtc=$($mainDllItem.LastWriteTimeUtc.ToString("o"))"
        "runtimeMainDllSha256=$($mainDllHash.Hash)"
    ) -join "`r`n"
    Set-Content -LiteralPath $manifestPath -Value ($manifest + "`r`n") -Encoding ascii

    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $zipPath -CompressionLevel Optimal -Force
    Remove-Item -LiteralPath $stageRoot -Recurse -Force

    $latestPath = Join-Path $DeploymentsDir "LATEST.txt"
    Set-Content -LiteralPath $latestPath -Value ([System.IO.Path]::GetFileName($zipPath)) -Encoding ascii
    Write-Step "Created deployment zip: `"$zipPath`""
}

function Get-ShortPathOrSame {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (!(Test-Path -LiteralPath $Path)) {
        return $Path
    }

    $escaped = $Path.Replace('"', '""')
    $short = & "$CmdExePath" /d /s /c "for %I in (""$escaped"") do @echo %~sI"
    if ($LASTEXITCODE -eq 0 -and $short) {
        $line = ($short | Select-Object -First 1).Trim()
        if ($line.Length -gt 0) {
            return $line
        }
    }
    return $Path
}

function To-CMakePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $Path.Replace("\", "/")
}

function Resolve-LatestWindowsSdkTool {
    param([Parameter(Mandatory = $true)][ValidateSet("rc.exe","mt.exe")] [string]$ToolName)

    $sdkBinRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (!(Test-Path -LiteralPath $sdkBinRoot)) {
        return $null
    }

    $candidates = Get-ChildItem -LiteralPath $sdkBinRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($dir in $candidates) {
        $toolPath = Join-Path $dir.FullName ("x64\" + $ToolName)
        if (Test-Path -LiteralPath $toolPath) {
            return $toolPath
        }
    }
    return $null
}

function New-StampedPath {
    param(
        [Parameter(Mandatory = $true)][string]$Dir,
        [Parameter(Mandatory = $true)][string]$Prefix,
        [Parameter(Mandatory = $true)][string]$Extension
    )
    $stamp = (Get-Date).ToString("yyyyMMdd_HHmmss_fff")
    return (Join-Path $Dir ($Prefix + "_" + $stamp + $Extension))
}

function Get-ChildProcessIds {
    param([Parameter(Mandatory = $true)][int]$RootPid)
    $all = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    if ($all.Count -eq 0) {
        return @($RootPid)
    }

    $childrenByParent = @{}
    foreach ($p in $all) {
        if (-not $childrenByParent.ContainsKey($p.ParentProcessId)) {
            $childrenByParent[$p.ParentProcessId] = New-Object System.Collections.Generic.List[int]
        }
        $childrenByParent[$p.ParentProcessId].Add([int]$p.ProcessId) | Out-Null
    }

    $seen = New-Object System.Collections.Generic.HashSet[int]
    $queue = New-Object System.Collections.Generic.Queue[int]
    $queue.Enqueue($RootPid)
    $null = $seen.Add($RootPid)

    while ($queue.Count -gt 0) {
        $procId = $queue.Dequeue()
        if ($childrenByParent.ContainsKey($procId)) {
            foreach ($child in $childrenByParent[$procId]) {
                if ($seen.Add($child)) {
                    $queue.Enqueue($child)
                }
            }
        }
    }

    $result = New-Object System.Collections.Generic.List[int]
    foreach ($item in $seen) {
        $result.Add([int]$item) | Out-Null
    }
    return @($result)
}

function Get-ProcessTreeCpuTotal {
    param([Parameter(Mandatory = $true)][int]$RootPid)
    $pids = Get-ChildProcessIds -RootPid $RootPid
    $cpuTotal = 0.0
    foreach ($procId in $pids) {
        try {
            $p = Get-Process -Id $procId -ErrorAction Stop
            $cpuTotal += [double]$p.CPU
        } catch {
        }
    }
    return $cpuTotal
}

function Get-FirstFailureLineFromLogs {
    param(
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath
    )

    $patterns = @(
        "fatal error C[0-9]{4}",
        "\berror C[0-9]{4}\b",
        "CMake Error",
        "FAILED:",
        "ninja:\s+build stopped:\s+subcommand failed",
        "cannot open include file",
        "unrecognized character escape sequence",
        "syntax error"
    )

    foreach ($path in @($StdoutPath, $StderrPath)) {
        if (!(Test-Path -LiteralPath $path)) {
            continue
        }
        foreach ($line in (Get-Content -LiteralPath $path -ErrorAction SilentlyContinue)) {
            foreach ($pattern in $patterns) {
                if ($line -match $pattern) {
                    return $line.Trim()
                }
            }
        }
    }
    return $null
}

function Invoke-CmdScriptWithWatchdog {
    param(
        [Parameter(Mandatory = $true)][string]$ScriptPath,
        [Parameter(Mandatory = $true)][string]$StepName,
        [Parameter(Mandatory = $true)][int]$TimeoutMinutes,
        [Parameter(Mandatory = $true)][string]$LogsDir,
        [string]$SuccessProbePath = "",
        [datetime]$SuccessProbeNotBeforeUtc = [datetime]::MinValue
    )

    Require-Path -Path $ScriptPath -Label "$StepName script"
    Ensure-Directory -Path $LogsDir

    $stdoutPath = New-StampedPath -Dir $LogsDir -Prefix ($StepName + "_stdout") -Extension ".log"
    $stderrPath = New-StampedPath -Dir $LogsDir -Prefix ($StepName + "_stderr") -Extension ".log"

    Write-Step "$StepName start; script=`"$ScriptPath`""
    Write-Step "$StepName logs: stdout=`"$stdoutPath`" stderr=`"$stderrPath`""

    $cmdInvoke = "call ""$ScriptPath"""
    $proc = Start-Process `
        -FilePath $CmdExePath `
        -ArgumentList @("/d", "/s", "/c", $cmdInvoke) `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath

    $start = Get-Date
    $noProgressCount = 0
    $lastStdoutLen = 0L
    $lastStderrLen = 0L
    $lastCpu = 0.0
    $lastCpu = Get-ProcessTreeCpuTotal -RootPid $proc.Id

    while (-not $proc.HasExited) {
        Start-Sleep -Seconds $HeartbeatSeconds
        $elapsed = ((Get-Date) - $start)
        if ($elapsed.TotalMinutes -ge $TimeoutMinutes) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "$StepName timed out after $TimeoutMinutes minutes."
        }

        $stdoutLen = 0L
        $stderrLen = 0L
        if (Test-Path -LiteralPath $stdoutPath) { $stdoutLen = (Get-Item -LiteralPath $stdoutPath).Length }
        if (Test-Path -LiteralPath $stderrPath) { $stderrLen = (Get-Item -LiteralPath $stderrPath).Length }

        $cpu = Get-ProcessTreeCpuTotal -RootPid $proc.Id

        $hasProgress = ($stdoutLen -gt $lastStdoutLen) -or ($stderrLen -gt $lastStderrLen) -or ($cpu -gt $lastCpu)
        if ($hasProgress) {
            $noProgressCount = 0
        } else {
            $noProgressCount++
        }

        $lastStdoutLen = $stdoutLen
        $lastStderrLen = $stderrLen
        $lastCpu = $cpu

        Write-Step "$StepName heartbeat: elapsed=$([int]$elapsed.TotalSeconds)s stdout=$stdoutLen stderr=$stderrLen cpu=$cpu noProgress=$noProgressCount"

        if ($noProgressCount -ge $NoProgressLimit) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "$StepName stopped for no progress at heartbeat limit $NoProgressLimit."
        }
    }

    $proc.WaitForExit()
    $proc.Refresh()
    $exitCode = $proc.ExitCode
    if ($null -eq $exitCode) {
        $probeAcceptable = $false
        if ($SuccessProbePath -and (Test-Path -LiteralPath $SuccessProbePath)) {
            $probeItem = Get-Item -LiteralPath $SuccessProbePath
            if ($SuccessProbeNotBeforeUtc -gt [datetime]::MinValue -and $probeItem.LastWriteTimeUtc -lt $SuccessProbeNotBeforeUtc) {
                Write-Step "$StepName probe exists but is stale: `"$SuccessProbePath`" lastWriteUtc=$($probeItem.LastWriteTimeUtc.ToString("o")) required>=$($SuccessProbeNotBeforeUtc.ToString("o"))"
                $probeAcceptable = $false
            } else {
                $probeAcceptable = $true
            }
        }
        if ($probeAcceptable) {
            Write-Step "$StepName exit code unavailable; continuing because fresh success probe exists at `"$SuccessProbePath`""
            $exitCode = 0
        } else {
            $exitCode = -999
        }
    }

    $failureLine = Get-FirstFailureLineFromLogs -StdoutPath $stdoutPath -StderrPath $stderrPath
    if ($exitCode -ne 0) {
        if ($failureLine) {
            throw "$StepName failed with exit code $exitCode. First failure: $failureLine. See logs: `"$stdoutPath`", `"$stderrPath`""
        }
        throw "$StepName failed with exit code $exitCode. See logs: `"$stdoutPath`", `"$stderrPath`""
    }

    if ($failureLine) {
        throw "$StepName reported success exit code but failure pattern was detected in logs: $failureLine. See logs: `"$stdoutPath`", `"$stderrPath`""
    }

    Write-Step "$StepName completed successfully"
}

Normalize-ProcessPathEnv

Require-Path -Path $RootDir -Label "RootDir"
Require-Path -Path $TemplateRoot -Label "TemplateRoot"
Require-Path -Path $VsDevCmdPath -Label "VsDevCmd"
Require-Path -Path $CMakeExePath -Label "CMake"
Require-Path -Path $NinjaExePath -Label "Ninja"
Require-Path -Path $CmdExePath -Label "cmd.exe"

$TemplateRootShort = Get-ShortPathOrSame -Path $TemplateRoot
$BuildDirShort = Get-ShortPathOrSame -Path $BuildDir
$RcExePath = Resolve-LatestWindowsSdkTool -ToolName "rc.exe"
$MtExePath = Resolve-LatestWindowsSdkTool -ToolName "mt.exe"
$RustcExePath = "C:\Users\User\.cargo\bin\rustc.exe"
$CargoExePath = "C:\Users\User\.cargo\bin\cargo.exe"

$TemplateRootShortCMake = To-CMakePath -Path $TemplateRootShort
$BuildDirShortCMake = To-CMakePath -Path $BuildDirShort
$NinjaExePathCMake = To-CMakePath -Path $NinjaExePath
$RcExePathCMake = To-CMakePath -Path $RcExePath
$MtExePathCMake = To-CMakePath -Path $MtExePath
$SdkX64Dir = Split-Path -Parent $RcExePath
$SdkVersionDir = Split-Path -Parent $SdkX64Dir
$SdkBinDir = Split-Path -Parent $SdkVersionDir
$WindowsSdkVersion = Split-Path -Leaf $SdkVersionDir
$WindowsSdkDir = Split-Path -Parent $SdkBinDir
if (-not $WindowsSdkDir.EndsWith("\")) {
    $WindowsSdkDir = $WindowsSdkDir + "\"
}

if (-not $RcExePath) {
    throw "Unable to find rc.exe under `"C:\Program Files (x86)\Windows Kits\10\bin`"."
}
if (-not $MtExePath) {
    throw "Unable to find mt.exe under `"C:\Program Files (x86)\Windows Kits\10\bin`"."
}
if (!(Test-Path -LiteralPath $RustcExePath)) {
    throw "Unable to find rustc.exe at `"$RustcExePath`"."
}
if (!(Test-Path -LiteralPath $CargoExePath)) {
    throw "Unable to find cargo.exe at `"$CargoExePath`"."
}

Write-Step "RcExePath=`"$RcExePath`""
Write-Step "MtExePath=`"$MtExePath`""
Write-Step "RustcExePath=`"$RustcExePath`""
Write-Step "CargoExePath=`"$CargoExePath`""
Write-Step "WindowsSdkDir=`"$WindowsSdkDir`""
Write-Step "WindowsSdkVersion=`"$WindowsSdkVersion`""

Write-Step "Resolved short paths for configure/build tree"
Write-Step "TemplateRootShort=`"$TemplateRootShort`""
Write-Step "BuildDirShort=`"$BuildDirShort`""

$buildModDir = Join-Path $TemplateRoot "MyCPPMods\WindroseTextSigns"
$myCppModsCMakePath = Join-Path $TemplateRoot "MyCPPMods\CMakeLists.txt"
$buildScriptsDir = Join-Path $RootDir "build_scripts"
$buildLogsDir = Join-Path $RootDir "build_logs"
$deploymentsDir = Join-Path $RootDir "Deployments"
$runtimeDll = Join-Path $RootDir "dlls\main.dll"

$configureCmdPath = Join-Path $buildScriptsDir "configure_windrosetextsigns_short3.cmd"
$buildCmdPath = Join-Path $buildScriptsDir "build_windrosetextsigns_short3.cmd"

Ensure-Directory -Path $buildModDir
Ensure-Directory -Path $buildScriptsDir
Ensure-Directory -Path $buildLogsDir
Ensure-Directory -Path $BuildDir
Ensure-Directory -Path (Join-Path $RootDir "dlls")

Write-Step "Syncing mod files to template workspace"
Copy-IfExists -Source (Join-Path $RootDir "Source") -Destination $buildModDir
Copy-IfExists -Source (Join-Path $RootDir "Config") -Destination $buildModDir
Copy-IfExists -Source (Join-Path $RootDir "enabled.txt") -Destination (Join-Path $buildModDir "enabled.txt")
Copy-IfExists -Source (Join-Path $RootDir "README.md") -Destination (Join-Path $buildModDir "README.md")
Copy-IfExists -Source (Join-Path $RootDir "CHANGELOG.md") -Destination (Join-Path $buildModDir "CHANGELOG.md")
Ensure-MyCppModRegistered -MyCppModsCMakePath $myCppModsCMakePath -SubdirectoryPath "WindroseTextSigns/Source"

$configureCmdContent = @"
@echo off
setlocal
call "$VsDevCmdPath" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not defined VCToolsInstallDir (echo FAIL: missing VCToolsInstallDir & exit /b 91)
set "RC_EXE_WIN=$RcExePath"
set "MT_EXE_WIN=$MtExePath"
set "RUSTC_EXE_WIN=$RustcExePath"
set "CARGO_EXE_WIN=$CargoExePath"
if not exist "%RC_EXE_WIN%" (echo FAIL: rc.exe missing at "%RC_EXE_WIN%" & exit /b 101)
if not exist "%MT_EXE_WIN%" (echo FAIL: mt.exe missing at "%MT_EXE_WIN%" & exit /b 102)
if not exist "%RUSTC_EXE_WIN%" (echo FAIL: rustc.exe missing at "%RUSTC_EXE_WIN%" & exit /b 104)
if not exist "%CARGO_EXE_WIN%" (echo FAIL: cargo.exe missing at "%CARGO_EXE_WIN%" & exit /b 105)
set "WINDOWS_SDK_DIR=$WindowsSdkDir"
set "WINDOWS_SDK_VER=$WindowsSdkVersion"
if not exist "%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64\kernel32.lib" (echo FAIL: missing kernel32.lib in "%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64" & exit /b 100)
if not exist "%VCToolsInstallDir%include\yvals_core.h" (echo FAIL: missing VC include yvals_core.h in "%VCToolsInstallDir%include" & exit /b 106)
if not exist "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt\stdio.h" (echo FAIL: missing SDK UCRT stdio.h in "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt" & exit /b 108)
if not exist "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um\Windows.h" (echo FAIL: missing SDK Windows.h in "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um" & exit /b 107)
set "PATH=C:\Users\User\.cargo\bin;%PATH%"
set "INCLUDE=%VCToolsInstallDir%include;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\shared;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\winrt;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\cppwinrt;%INCLUDE%"
set "LIB=%VCToolsInstallDir%lib\x64;%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\ucrt\x64;%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64;%LIB%"
"$CMakeExePath" ^
  -S "$TemplateRootShortCMake" ^
  -B "$BuildDirShortCMake" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 ^
  -DUE4SS_VERSION_CHECK=OFF ^
  -DCMAKE_MAKE_PROGRAM="$NinjaExePathCMake" ^
  -DCMAKE_RC_COMPILER="$RcExePathCMake" ^
  -DCMAKE_MT="$MtExePathCMake" ^
  -DRust_COMPILER="$RustcExePath"
exit /b %errorlevel%
"@

$buildCmdContent = @"
@echo off
setlocal
call "$VsDevCmdPath" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not defined VCToolsInstallDir (echo FAIL: missing VCToolsInstallDir & exit /b 91)
set "RUSTC_EXE_WIN=$RustcExePath"
set "CARGO_EXE_WIN=$CargoExePath"
if not exist "%RUSTC_EXE_WIN%" (echo FAIL: rustc.exe missing at "%RUSTC_EXE_WIN%" & exit /b 104)
if not exist "%CARGO_EXE_WIN%" (echo FAIL: cargo.exe missing at "%CARGO_EXE_WIN%" & exit /b 105)
set "WINDOWS_SDK_DIR=$WindowsSdkDir"
set "WINDOWS_SDK_VER=$WindowsSdkVersion"
if not exist "%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64\kernel32.lib" (echo FAIL: missing kernel32.lib in "%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64" & exit /b 100)
if not exist "%VCToolsInstallDir%include\yvals_core.h" (echo FAIL: missing VC include yvals_core.h in "%VCToolsInstallDir%include" & exit /b 106)
if not exist "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt\stdio.h" (echo FAIL: missing SDK UCRT stdio.h in "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt" & exit /b 108)
if not exist "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um\Windows.h" (echo FAIL: missing SDK Windows.h in "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um" & exit /b 107)
set "PATH=C:\Users\User\.cargo\bin;%PATH%"
set "INCLUDE=%VCToolsInstallDir%include;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\shared;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\winrt;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\cppwinrt;%INCLUDE%"
set "LIB=%VCToolsInstallDir%lib\x64;%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\ucrt\x64;%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64;%LIB%"
"$CMakeExePath" --build "$BuildDirShortCMake" --config Release --target WindroseTextSigns -j 6
exit /b %errorlevel%
"@

Set-Content -LiteralPath $configureCmdPath -Value $configureCmdContent -Encoding ascii
Set-Content -LiteralPath $buildCmdPath -Value $buildCmdContent -Encoding ascii

if (-not $SkipConfigure) {
    $cmakeCachePath = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path -LiteralPath $cmakeCachePath) {
        Write-Step "Detected existing CMake cache in build dir; clearing stale cache to avoid source mismatch"
        Clear-DirectoryContents -Path $BuildDir
        Ensure-Directory -Path $BuildDir
    }

    $configurePhaseStartUtc = [DateTime]::UtcNow
    $configureSuccessProbe = Join-Path $BuildDir "build.ninja"
    Invoke-CmdScriptWithWatchdog `
        -ScriptPath $configureCmdPath `
        -StepName "configure" `
        -TimeoutMinutes $ConfigureTimeoutMinutes `
        -LogsDir $buildLogsDir `
        -SuccessProbePath $configureSuccessProbe `
        -SuccessProbeNotBeforeUtc $configurePhaseStartUtc
} else {
    Write-Step "SkipConfigure set; configure step not executed"
}

$buildPhaseStartUtc = [DateTime]::UtcNow
$buildSuccessProbe = Join-Path $BuildDir "MyCPPMods\WindroseTextSigns\Source\WindroseTextSigns.dll"
Invoke-CmdScriptWithWatchdog `
    -ScriptPath $buildCmdPath `
    -StepName "build" `
    -TimeoutMinutes $BuildTimeoutMinutes `
    -LogsDir $buildLogsDir `
    -SuccessProbePath $buildSuccessProbe `
    -SuccessProbeNotBeforeUtc $buildPhaseStartUtc

$dllOutputCandidates = @(
    (Join-Path $BuildDir "MyCPPMods\WindroseTextSigns\Source\WindroseTextSigns.dll"),
    (Join-Path $BuildDir "MyCPPMods\WindroseTextSigns\WindroseTextSigns.dll"),
    (Join-Path $BuildDir "RelWithDebInfo\MyCPPMods\WindroseTextSigns\WindroseTextSigns.dll"),
    (Join-Path $BuildDir "Release\MyCPPMods\WindroseTextSigns\WindroseTextSigns.dll"),
    (Join-Path $BuildDir "WindroseTextSigns.dll")
)

$builtDll = $null
$builtDllFresh = $null
foreach ($candidate in $dllOutputCandidates) {
    if (Test-Path -LiteralPath $candidate) {
        $item = Get-Item -LiteralPath $candidate
        if ($item.LastWriteTimeUtc -ge $buildPhaseStartUtc) {
            if ($null -eq $builtDllFresh -or $item.LastWriteTimeUtc -gt $builtDllFresh.LastWriteTimeUtc) {
                $builtDllFresh = $item
            }
        }
        if ($null -eq $builtDll) {
            $builtDll = $candidate
        }
    }
}

if ($null -eq $builtDllFresh) {
    throw "Build did not produce a fresh WindroseTextSigns DLL for this run. Refusing to package a stale DLL."
}

$builtDll = $builtDllFresh.FullName

if (-not $builtDll) {
    throw "Build completed but DLL was not found in expected paths under `"$BuildDir`"."
}

Write-Step "Copying built DLL to runtime staging: `"$runtimeDll`""
Copy-Item -LiteralPath $builtDll -Destination $runtimeDll -Force

New-DeploymentZip -ModRoot $RootDir -DeploymentsDir $deploymentsDir -BuiltDllPath $builtDll -BuildStartedUtc $buildPhaseStartUtc

Write-Step "Direct game/client deployment is disabled in this script by design. Output is build artifacts + Deployments zip only."

Write-Step "Done"
