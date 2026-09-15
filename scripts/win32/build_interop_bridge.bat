@echo off
setlocal

if "%~6"=="" (
    echo Usage: build_interop_bridge.bat VCVARALL ARCH SOURCE INCLUDE_DIR OUTPUT OBJECT_DIR
    exit /b 2
)

set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "BRIDGE_SOURCE=%~3"
set "SDK_INCLUDE=%~4"
set "BRIDGE_OUTPUT=%~5"
set "OBJECT_DIR=%~6"

if not exist "%VCVARSALL%" (
    echo MSVC environment script was not found: %VCVARSALL%
    exit /b 3
)

if not exist "%OBJECT_DIR%" mkdir "%OBJECT_DIR%"
if errorlevel 1 exit /b %errorlevel%

REM IDEs sometimes inherit a half-initialised Visual Studio environment. Clear
REM its sentinels so vcvarsall computes paths from its own installation.
set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="

call "%VCVARSALL%" %TARGET_ARCH%
if errorlevel 1 exit /b %errorlevel%

cl.exe ^
    /nologo ^
    /std:c++20 ^
    /EHsc ^
    /permissive- ^
    /utf-8 ^
    /W4 ^
    /O2 ^
    /MT ^
    /LD ^
    /I"%SDK_INCLUDE%" ^
    /Fo"%OBJECT_DIR%\interop_bridge.obj" ^
    /Fd"%OBJECT_DIR%\vibrance_win32_interop.pdb" ^
    /Fe"%BRIDGE_OUTPUT%" ^
    "%BRIDGE_SOURCE%" ^
    /link ^
    /NOLOGO ^
    windowsapp.lib ^
    mmdevapi.lib ^
    ole32.lib ^
    shell32.lib ^
    gdi32.lib ^
    user32.lib ^
    uuid.lib

exit /b %errorlevel%
