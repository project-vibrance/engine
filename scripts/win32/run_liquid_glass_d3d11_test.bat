@echo off
setlocal

if "%~3"=="" (
    echo Usage: run_liquid_glass_d3d11_test.bat VCVARSALL BUILD_DIR SHADER_DIR
    exit /b 2
)

set "VCVARSALL=%~1"
set "TEST_BUILD_DIR=%~2"
set "SHADER_DIR=%~3"
if not exist "%TEST_BUILD_DIR%" mkdir "%TEST_BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%VCVARSALL%" x64
if errorlevel 1 exit /b %errorlevel%

cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /MT ^
    /Fo"%TEST_BUILD_DIR%\\" ^
    /Fd"%TEST_BUILD_DIR%\test_liquid_glass_d3d11.pdb" ^
    /Fe"%TEST_BUILD_DIR%\test_liquid_glass_d3d11.exe" ^
    "%~dp0test_liquid_glass_d3d11.cpp" ^
    "%~dp0..\..\engine\src\platform\win32\liquid_glass_d3d11.cpp" ^
    /link /NOLOGO d3d11.lib dxgi.lib
if errorlevel 1 exit /b %errorlevel%

"%TEST_BUILD_DIR%\test_liquid_glass_d3d11.exe" "%SHADER_DIR%" "%TEST_BUILD_DIR%\preview.ppm"
exit /b %errorlevel%
