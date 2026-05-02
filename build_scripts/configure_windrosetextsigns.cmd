@echo off
setlocal
call "C:\PROGRA~2\MIB055~1\2022\BUILDT~1\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 (echo FAIL: VsDevCmd errorlevel=%errorlevel% & exit /b 1)
if not defined VCToolsInstallDir (echo FAIL: missing VCToolsInstallDir & exit /b 91)
if not defined WindowsSdkDir set "WindowsSdkDir=C:\PROGRA~2\WI3CF2~1\10\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0"
if not defined UniversalCRTSdkDir set "UniversalCRTSdkDir=%WindowsSdkDir%"
if not defined UCRTVersion set "UCRTVersion=%WindowsSDKVersion%"
if "%WindowsSDKVersion:~-1%"=="\" set "WindowsSDKVersion=%WindowsSDKVersion:~0,-1%"
if "%UCRTVersion:~-1%"=="\" set "UCRTVersion=%UCRTVersion:~0,-1%"
set "LIB=%VCToolsInstallDir%lib\x64;%UniversalCRTSdkDir%lib\%UCRTVersion%\ucrt\x64;%WindowsSdkDir%lib\%WindowsSDKVersion%um\x64;%LIB%"
if not exist "%VCToolsInstallDir%lib\x64\*.lib" (echo FAIL: missing VC libs path "%VCToolsInstallDir%lib\x64" & exit /b 99)
if not exist "%WindowsSdkDir%lib\%WindowsSDKVersion%um\x64\kernel32.lib" (echo FAIL: missing kernel32.lib in "%WindowsSdkDir%lib\%WindowsSDKVersion%um\x64" & exit /b 100)
if not exist "C:\PROGRA~2\WI3CF2~1\10\bin\100261~1.0\x64\rc.exe" (echo FAIL: missing rc.exe at "C:\PROGRA~2\WI3CF2~1\10\bin\100261~1.0\x64\rc.exe" & exit /b 101)
if not exist "C:\PROGRA~2\WI3CF2~1\10\bin\100261~1.0\x64\mt.exe" (echo FAIL: missing mt.exe at "C:\PROGRA~2\WI3CF2~1\10\bin\100261~1.0\x64\mt.exe" & exit /b 102)
if not exist "%VCToolsInstallDir%bin\Hostx64\x64\link.exe" (echo FAIL: missing link.exe at "%VCToolsInstallDir%bin\Hostx64\x64\link.exe" & exit /b 103)
echo INFO: invoking CMake configure
"C:/PROGRA~1/CMake/bin/cmake.exe" ^
  -S "C:/Users/User/DOCUME~1/WINDRO~1/PLAYER~1/_BUILD~1/UE4SSC~1" ^
  -B "C:/Users/User/DOCUME~1/WINDRO~1/PLAYER~1/_BUILD~1/UE4SSC~1/BUB185~1" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 ^
  -DUE4SS_VERSION_CHECK=OFF ^
  -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MIB055~1/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" ^
  -DCMAKE_RC_COMPILER="C:/PROGRA~2/WI3CF2~1/10/bin/100261~1.0/x64/rc.exe" ^
  -DCMAKE_MT="C:/PROGRA~2/WI3CF2~1/10/bin/100261~1.0/x64/mt.exe"
exit /b %errorlevel%
