@echo off
setlocal

if "%~5"=="" (
    echo Usage: build_liquid_dwm_extension.bat VCVARSALL ARCH SOURCE_DIR OUTPUT OBJECT_DIR SDK_INCLUDE_ROOT SDK_UM_LIB SDK_UCRT_LIB
    exit /b 2
)
set "VCVARSALL=%~1"
set "TARGET_ARCH=%~2"
set "SOURCE_DIR=%~3"
set "EXTENSION_OUTPUT=%~4"
set "OBJECT_DIR=%~5"
set "DETOURS_DIR=%~dp0..\..\engine\third_party\detours"
set "SDK_INCLUDE_ROOT=%~6"
set "SDK_UM_LIBRARY_DIR=%~7"
set "SDK_UCRT_LIBRARY_DIR=%~8"
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

cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /Zi /MT /c ^
    %SDK_INCLUDE_FLAGS% /I"%SOURCE_DIR%" /I"%DETOURS_DIR%" ^
    /Fo"%OBJECT_DIR%\liquid_glass_dwm_extension.obj" ^
    /Fd"%OBJECT_DIR%\liquid_glass_dwm_extension.pdb" ^
    "%SOURCE_DIR%\liquid_glass_dwm_present.cpp"
if errorlevel 1 exit /b %errorlevel%

cl.exe /nologo /std:c++20 /EHsc /permissive- /utf-8 /W4 /O2 /Zi /MT /c ^
    %SDK_INCLUDE_FLAGS% /I"%SOURCE_DIR%" ^
    /Fo"%OBJECT_DIR%\liquid_glass_d3d11.obj" ^
    /Fd"%OBJECT_DIR%\liquid_glass_dwm_extension.pdb" ^
    "%SOURCE_DIR%\liquid_glass_d3d11.cpp"
if errorlevel 1 exit /b %errorlevel%

cl.exe /nologo /EHsc /W3 /O2 /MT /c ^
    %SDK_INCLUDE_FLAGS% /I"%DETOURS_DIR%" ^
    /Fo"%OBJECT_DIR%\\" ^
    "%DETOURS_DIR%\detours.cpp" ^
    "%DETOURS_DIR%\modules.cpp" ^
    "%DETOURS_DIR%\disasm.cpp" ^
    "%DETOURS_DIR%\disolx86.cpp" ^
    "%DETOURS_DIR%\disolx64.cpp" ^
    "%DETOURS_DIR%\disolia64.cpp" ^
    "%DETOURS_DIR%\disolarm.cpp" ^
    "%DETOURS_DIR%\disolarm64.cpp"
if errorlevel 1 exit /b %errorlevel%

link.exe /DLL /NOLOGO /DEBUG:FULL ^
    /OUT:"%EXTENSION_OUTPUT%" ^
    /PDB:"%OBJECT_DIR%\liquid_glass_dwm_extension_link.pdb" ^
    %SDK_LIBRARY_FLAGS% ^
    "%OBJECT_DIR%\liquid_glass_dwm_extension.obj" ^
    "%OBJECT_DIR%\liquid_glass_d3d11.obj" ^
    "%OBJECT_DIR%\detours.obj" ^
    "%OBJECT_DIR%\modules.obj" ^
    "%OBJECT_DIR%\disasm.obj" ^
    "%OBJECT_DIR%\disolx86.obj" ^
    "%OBJECT_DIR%\disolx64.obj" ^
    "%OBJECT_DIR%\disolia64.obj" ^
    "%OBJECT_DIR%\disolarm.obj" ^
    "%OBJECT_DIR%\disolarm64.obj" ^
    advapi32.lib bcrypt.lib d3d11.lib user32.lib
exit /b %errorlevel%
