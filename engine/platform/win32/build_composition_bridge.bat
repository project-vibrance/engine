@echo off
setlocal

if "%~6"=="" (
    echo Usage: build_composition_bridge.bat VCVARSALL ARCH SOURCE INCLUDE_DIR OUTPUT OBJECT_DIR SDK_VERSION SDK_INCLUDE_ROOT SDK_UM_LIB SDK_UCRT_LIB
    exit /b 2
)

set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "BRIDGE_SOURCE=%~3"
set "BRIDGE_INCLUDE=%~4"
set "BRIDGE_OUTPUT=%~5"
set "OBJECT_DIR=%~6"
set "SDK_VERSION=%~7"
set "SDK_INCLUDE_ROOT=%~8"
set "SDK_UM_LIBRARY_DIR=%~9"
REM cmd.exe exposes only %%0 through %%9 directly. One shift moves the original
REM argument 10 into %%9; all earlier values were captured above.
shift
set "SDK_UCRT_LIBRARY_DIR=%~9"

if not exist "%VCVARSALL%" exit /b 3
if not exist "%OBJECT_DIR%" mkdir "%OBJECT_DIR%"
if errorlevel 1 exit /b %errorlevel%

set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%VCVARSALL%" %TARGET_ARCH%
if errorlevel 1 exit /b %errorlevel%

set "SDK_INCLUDE_FLAGS="
set "SDK_LIBRARY_FLAGS="
REM C++/WinRT projections are compiler-facing headers and are intentionally
REM taken from the current VS-selected SDK. The Win32/WinRT ABI headers and
REM import libraries below remain pinned to the requested SDK generation.
if not "%SDK_INCLUDE_ROOT%"=="" set "SDK_INCLUDE_FLAGS=/I"%SDK_INCLUDE_ROOT%\ucrt" /I"%SDK_INCLUDE_ROOT%\shared" /I"%SDK_INCLUDE_ROOT%\um" /I"%SDK_INCLUDE_ROOT%\winrt""
if not "%SDK_UM_LIBRARY_DIR%"=="" set "SDK_LIBRARY_FLAGS=/LIBPATH:"%SDK_UM_LIBRARY_DIR%""
if not "%SDK_UCRT_LIBRARY_DIR%"=="" set "SDK_LIBRARY_FLAGS=%SDK_LIBRARY_FLAGS% /LIBPATH:"%SDK_UCRT_LIBRARY_DIR%""

if not "%SDK_INCLUDE_ROOT%"=="" echo Building Composition bridge against Windows SDK %SDK_VERSION%

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
    %SDK_INCLUDE_FLAGS% ^
    /Fo"%OBJECT_DIR%\composition_bridge.obj" ^
    /Fd"%OBJECT_DIR%\vibrance_win32_composition.pdb" ^
    /Fe"%BRIDGE_OUTPUT%" ^
    "%BRIDGE_SOURCE%" ^
    /link ^
    /NOLOGO ^
    %SDK_LIBRARY_FLAGS% ^
    d2d1.lib ^
    d3d11.lib ^
    dxgi.lib ^
    dwmapi.lib ^
    ole32.lib ^
    user32.lib ^
    windowscodecs.lib ^
    windowsapp.lib ^
    CoreMessaging.lib

exit /b %errorlevel%
