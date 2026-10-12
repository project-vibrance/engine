@echo off
setlocal
set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "TEST_SOURCE=%~3"
set "NATIVE_OBJECT_DIR=%~4"
set "TEST_OUTPUT=%~5"
set "OBJECT_DIR=%~6"
set "SDK_INCLUDE_ROOT=%~7"
set "SDK_UM_LIBRARY_DIR=%~8"
set "SDK_UCRT_LIBRARY_DIR=%~9"
if not exist "%VCVARSALL%" exit /b 3
if not exist "%OBJECT_DIR%" mkdir "%OBJECT_DIR%"
set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%VCVARSALL%" %TARGET_ARCH%
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /MT ^
    /I"%SDK_INCLUDE_ROOT%\ucrt" /I"%SDK_INCLUDE_ROOT%\shared" ^
    /I"%SDK_INCLUDE_ROOT%\um" /I"%~dp0..\..\engine\third_party\detours" ^
    /Fo"%OBJECT_DIR%\dwm_hook_tests.obj" /Fe"%TEST_OUTPUT%" "%TEST_SOURCE%" ^
    /link /NOLOGO /LIBPATH:"%SDK_UM_LIBRARY_DIR%" /LIBPATH:"%SDK_UCRT_LIBRARY_DIR%" ^
    "%NATIVE_OBJECT_DIR%\liquid_glass_d3d11.obj" ^
    "%NATIVE_OBJECT_DIR%\detours.obj" "%NATIVE_OBJECT_DIR%\modules.obj" ^
    "%NATIVE_OBJECT_DIR%\disasm.obj" "%NATIVE_OBJECT_DIR%\disolx86.obj" ^
    "%NATIVE_OBJECT_DIR%\disolx64.obj" "%NATIVE_OBJECT_DIR%\disolia64.obj" ^
    "%NATIVE_OBJECT_DIR%\disolarm.obj" "%NATIVE_OBJECT_DIR%\disolarm64.obj" ^
    advapi32.lib bcrypt.lib d3d11.lib dxgi.lib user32.lib wtsapi32.lib
exit /b %errorlevel%
