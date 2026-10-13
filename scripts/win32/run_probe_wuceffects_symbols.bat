@echo off
setlocal
if "%~3"=="" exit /b 2
set "VCVARSALL=%~1"
set "PROBE_BUILD_DIR=%~2"
set "PDB_CACHE_DIR=%~3"
if not exist "%PROBE_BUILD_DIR%" mkdir "%PROBE_BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%
set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%VCVARSALL%" x64
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /EHsc /W4 /MT ^
    /Fo"%PROBE_BUILD_DIR%\probe_wuceffects_symbols.obj" ^
    /Fe"%PROBE_BUILD_DIR%\probe_wuceffects_symbols.exe" ^
    "%~dp0probe_wuceffects_symbols.cpp" /link /NOLOGO dbghelp.lib
if errorlevel 1 exit /b %errorlevel%
"%PROBE_BUILD_DIR%\probe_wuceffects_symbols.exe" "%PDB_CACHE_DIR%"
exit /b %errorlevel%
