@echo off
rem Builds iPhone Webcam for Windows.
rem   build.cmd [x64|arm64] [version]
rem Output: build\<arch>\app      everything that gets installed (self-contained desktop app,
rem                                engine, CLI, virtual camera source DLL)
rem         build\<arch>\native   CMake build tree (also contains FakePhone/CaptureTest test tools)
setlocal EnableDelayedExpansion

set "ARCH=%~1"
if "%ARCH%"=="" (
  if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (set "ARCH=arm64") else (set "ARCH=x64")
)
if /i not "%ARCH%"=="x64" if /i not "%ARCH%"=="arm64" (
  echo Unknown architecture "%ARCH%". Use x64 or arm64.
  exit /b 1
)
set "VERSION=%~2"
if "%VERSION%"=="" set "VERSION=1.0.0"

rem vcvarsall argument: <host>_<target>, or just <target> when building natively.
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (set "HOST=arm64") else (set "HOST=x64")
if /i "%HOST%"=="%ARCH%" (set "VCVARS_ARCH=%ARCH%") else (set "VCVARS_ARCH=%HOST%_%ARCH%")
set "VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
if /i "%ARCH%"=="arm64" set "VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires %VC_COMPONENT% -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio C++ build tools for %ARCH% not found ^(component %VC_COMPONENT%^).
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %VCVARS_ARCH% >nul || exit /b 1

cd /d "%~dp0"
set "OUT=build\%ARCH%"
cmake -S . -B "%OUT%\native" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%OUT%\native" || exit /b 1

set "DOTNET=dotnet"
where dotnet >nul 2>nul || set "DOTNET=%ProgramFiles%\dotnet\dotnet.exe"
set DOTNET_CLI_TELEMETRY_OPTOUT=1
set DOTNET_NOLOGO=1
if exist "%OUT%\app" rmdir /s /q "%OUT%\app"
"%DOTNET%" publish desktop\IPhoneWebcam.Desktop.csproj -c Release -r win-%ARCH% --self-contained true ^
  -p:Version=%VERSION% -o "%OUT%\app" -v quiet || exit /b 1

for %%f in (IPhoneWebcamEngine.dll IPhoneWebcamSource.dll IPhoneWebcamCli.exe) do (
  copy /y "%OUT%\native\%%f" "%OUT%\app\" >nul || exit /b 1
)
del /q "%OUT%\app\*.pdb" 2>nul

echo.
echo Built iPhone Webcam %VERSION% for %ARCH%: %~dp0%OUT%\app
