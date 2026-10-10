@echo off
setlocal
if "%~6"=="" exit /b 2
set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "TEST_SOURCE=%~3"
set "SDK_INCLUDE=%~4"
set "TEST_OUTPUT=%~5"
set "OBJECT_DIR=%~6"
if not exist "%VCVARSALL%" exit /b 3
if not exist "%OBJECT_DIR%" mkdir "%OBJECT_DIR%"
if errorlevel 1 exit /b %errorlevel%
set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%VCVARSALL%" %TARGET_ARCH%
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /MT ^
    /I"%SDK_INCLUDE%" /Fo"%OBJECT_DIR%\native_test.obj" ^
    /Fe"%TEST_OUTPUT%" "%TEST_SOURCE%" ^
    /link /NOLOGO windowsapp.lib ole32.lib oleaut32.lib uiautomationcore.lib user32.lib uuid.lib
exit /b %errorlevel%
