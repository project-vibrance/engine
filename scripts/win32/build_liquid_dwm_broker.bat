@echo off
setlocal
if "%~4"=="" (
    echo Usage: build_liquid_dwm_broker.bat VCVARSALL ARCH OUTPUT OBJECT_DIR SDK_INCLUDE_ROOT SDK_UM_LIB SDK_UCRT_LIB
    exit /b 2
)
set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "BROKER_OUTPUT=%~3"
for %%I in ("%BROKER_OUTPUT%") do set "OBSERVER_OUTPUT=%%~dpIliquid_glass_dwm_observer.exe"
set "OBJECT_DIR=%~4"
set "SDK_INCLUDE_ROOT=%~5"
set "SDK_UM_LIBRARY_DIR=%~6"
set "SDK_UCRT_LIBRARY_DIR=%~7"
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
if not "%SDK_INCLUDE_ROOT%"=="" set "SDK_INCLUDE_FLAGS=/I"%SDK_INCLUDE_ROOT%\ucrt" /I"%SDK_INCLUDE_ROOT%\shared" /I"%SDK_INCLUDE_ROOT%\um" /I"%SDK_INCLUDE_ROOT%\winrt""
if not "%SDK_UM_LIBRARY_DIR%"=="" set "SDK_LIBRARY_FLAGS=/LIBPATH:"%SDK_UM_LIBRARY_DIR%""
if not "%SDK_UCRT_LIBRARY_DIR%"=="" set "SDK_LIBRARY_FLAGS=%SDK_LIBRARY_FLAGS% /LIBPATH:"%SDK_UCRT_LIBRARY_DIR%""

cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /MT ^
    %SDK_INCLUDE_FLAGS% /I"%~dp0..\..\engine\src\platform\win32" ^
    /Fo"%OBJECT_DIR%\liquid_glass_dwm_broker.obj" ^
    /Fd"%OBJECT_DIR%\liquid_glass_dwm_broker.pdb" ^
    /Fe"%BROKER_OUTPUT%" ^
    "%~dp0liquid_glass_dwm_broker.cpp" ^
    /link %SDK_LIBRARY_FLAGS% advapi32.lib bcrypt.lib dbghelp.lib ole32.lib oleaut32.lib shell32.lib taskschd.lib user32.lib uuid.lib
if errorlevel 1 exit /b %errorlevel%
REM The scheduled observer has no console; the broker retains console/pipeline IO.
link.exe /NOLOGO /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup ^
    /OUT:"%OBSERVER_OUTPUT%" "%OBJECT_DIR%\liquid_glass_dwm_broker.obj" ^
    %SDK_LIBRARY_FLAGS% advapi32.lib bcrypt.lib dbghelp.lib ole32.lib oleaut32.lib shell32.lib taskschd.lib user32.lib uuid.lib
exit /b %errorlevel%
