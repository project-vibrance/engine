@echo off
setlocal
set "VCVARSALL=%~1"
set "ROOT=%~dp0..\.."
set "DETOURS=%ROOT%\build\detours-reference"
set "OUT=%ROOT%\build\dwm-probe"
if not exist "%OUT%" mkdir "%OUT%"
set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%VCVARSALL%" x64
if errorlevel 1 exit /b %errorlevel%
if not exist "%DETOURS%\lib.X64\detours.lib" (
    pushd "%DETOURS%\src"
    nmake /nologo
    if errorlevel 1 exit /b %errorlevel%
    popd
)
cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /MT /LD ^
    /I"%DETOURS%\src" /Fo"%OUT%\probe_dwm_present_extension_v4.obj" ^
    /Fe"%OUT%\probe_dwm_present_extension_v4.dll" ^
    "%ROOT%\scripts\win32\probe_dwm_present_extension.cpp" ^
    /link /LIBPATH:"%DETOURS%\lib.X64" detours.lib advapi32.lib bcrypt.lib
exit /b %errorlevel%
