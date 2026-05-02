@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not defined VCToolsInstallDir (echo FAIL: missing VCToolsInstallDir & exit /b 91)
set "RC_EXE_WIN=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe"
set "MT_EXE_WIN=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe"
set "RUSTC_EXE_WIN=C:\Users\User\.cargo\bin\rustc.exe"
set "CARGO_EXE_WIN=C:\Users\User\.cargo\bin\cargo.exe"
if not exist "%RC_EXE_WIN%" (echo FAIL: rc.exe missing at "%RC_EXE_WIN%" & exit /b 101)
if not exist "%MT_EXE_WIN%" (echo FAIL: mt.exe missing at "%MT_EXE_WIN%" & exit /b 102)
if not exist "%RUSTC_EXE_WIN%" (echo FAIL: rustc.exe missing at "%RUSTC_EXE_WIN%" & exit /b 104)
if not exist "%CARGO_EXE_WIN%" (echo FAIL: cargo.exe missing at "%CARGO_EXE_WIN%" & exit /b 105)
set "WINDOWS_SDK_DIR=C:\Program Files (x86)\Windows Kits\10\"
set "WINDOWS_SDK_VER=10.0.26100.0"
if not exist "%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64\kernel32.lib" (echo FAIL: missing kernel32.lib in "%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64" & exit /b 100)
if not exist "%VCToolsInstallDir%include\yvals_core.h" (echo FAIL: missing VC include yvals_core.h in "%VCToolsInstallDir%include" & exit /b 106)
if not exist "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt\stdio.h" (echo FAIL: missing SDK UCRT stdio.h in "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt" & exit /b 108)
if not exist "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um\Windows.h" (echo FAIL: missing SDK Windows.h in "%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um" & exit /b 107)
set "PATH=C:\Users\User\.cargo\bin;%PATH%"
set "INCLUDE=%VCToolsInstallDir%include;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\ucrt;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\shared;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\um;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\winrt;%WINDOWS_SDK_DIR%Include\%WINDOWS_SDK_VER%\cppwinrt;%INCLUDE%"
set "LIB=%VCToolsInstallDir%lib\x64;%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\ucrt\x64;%WINDOWS_SDK_DIR%Lib\%WINDOWS_SDK_VER%\um\x64;%LIB%"
"C:\Program Files\CMake\bin\cmake.exe" ^
  -S "C:/Users/User/DOCUME~1/WINDRO~1/PLAYER~1/_BUILD~1/UE4SSC~1" ^
  -B "C:/Users/User/DOCUME~1/WINDRO~1/PLAYER~1/_BUILD~1/UE4SSC~1/BU8808~1" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 ^
  -DUE4SS_VERSION_CHECK=OFF ^
  -DCMAKE_MAKE_PROGRAM="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" ^
  -DCMAKE_RC_COMPILER="C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" ^
  -DCMAKE_MT="C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe" ^
  -DRust_COMPILER="C:\Users\User\.cargo\bin\rustc.exe"
exit /b %errorlevel%
