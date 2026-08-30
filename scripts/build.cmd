@echo off
setlocal
set CONFIG=%~1
set PLATFORM=%~2
if "%CONFIG%"=="" set CONFIG=Debug
if "%PLATFORM%"=="" set PLATFORM=x64
where msbuild.exe >nul 2>&1 || (
  echo ERROR: msbuild.exe no esta disponible. Ejecute este script desde LaunchBuildEnv.cmd del EWDK.
  exit /b 2
)
pushd "%~dp0.."
msbuild.exe RootkitLab.sln /m /t:Rebuild /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:SignMode=Off /v:minimal
set RC=%ERRORLEVEL%
popd
exit /b %RC%

