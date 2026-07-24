@echo off
setlocal

if "%~6"=="" (
    echo Usage: build_composition_bridge.bat VCVARSALL ARCH SOURCE INCLUDE_DIR OUTPUT OBJECT_DIR
    exit /b 2
)

set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "BRIDGE_SOURCE=%~3"
set "BRIDGE_INCLUDE=%~4"
set "BRIDGE_OUTPUT=%~5"
set "OBJECT_DIR=%~6"

if not exist "%VCVARSALL%" exit /b 3
if not exist "%OBJECT_DIR%" mkdir "%OBJECT_DIR%"
if errorlevel 1 exit /b %errorlevel%

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
    /I"%BRIDGE_INCLUDE%" ^
    /Fo"%OBJECT_DIR%\composition_bridge.obj" ^
    /Fd"%OBJECT_DIR%\vibrance_win32_composition.pdb" ^
    /Fe"%BRIDGE_OUTPUT%" ^
    "%BRIDGE_SOURCE%" ^
    /link ^
    /NOLOGO ^
    d2d1.lib ^
    d3d11.lib ^
    dxgi.lib ^
    dwmapi.lib ^
    windowsapp.lib ^
    CoreMessaging.lib

exit /b %errorlevel%
