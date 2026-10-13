@echo off
setlocal
set "VSINSTALLDIR="
set "VSCMD_VCVARSALL_INIT="
set "VisualStudioVersion="
call "%~1" %~2
if errorlevel 1 exit /b 1
if not exist "%~dp4" mkdir "%~dp4"
cl /nologo /std:c++20 /EHsc /O2 /MT /permissive- /utf-8 /DWIN32_LEAN_AND_MEAN /I"%~5\um" /I"%~5\shared" /I"%~5\winrt" /I"%~5\ucrt" "%~3" /Fo"%~4.obj" /Fe"%~4" /link /LIBPATH:"%~6" /LIBPATH:"%~7" windowsapp.lib runtimeobject.lib ole32.lib uuid.lib propsys.lib shell32.lib advapi32.lib
exit /b %errorlevel%
