@echo off
setlocal
call "C:\PROGRA~2\MIB055~1\2022\BUILDT~1\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not defined VCToolsInstallDir exit /b 91
if not defined WindowsSdkDir set "WindowsSdkDir=C:\PROGRA~2\WI3CF2~1\10\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0"
if not defined UniversalCRTSdkDir set "UniversalCRTSdkDir=%WindowsSdkDir%"
if not defined UCRTVersion set "UCRTVersion=%WindowsSDKVersion%"
if "%WindowsSDKVersion:~-1%"=="\" set "WindowsSDKVersion=%WindowsSDKVersion:~0,-1%"
if "%UCRTVersion:~-1%"=="\" set "UCRTVersion=%UCRTVersion:~0,-1%"
set "LIB=%VCToolsInstallDir%lib\x64;%UniversalCRTSdkDir%lib\%UCRTVersion%\ucrt\x64;%WindowsSdkDir%lib\%WindowsSDKVersion%um\x64;%LIB%"
"C:/PROGRA~1/CMake/bin/cmake.exe" --build "C:/Users/User/DOCUME~1/WINDRO~1/PLAYER~1/_BUILD~1/UE4SSC~1/BUB185~1" --config Release --target WindroseTextSigns -j 6
exit /b %errorlevel%
