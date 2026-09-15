@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"

REM Usage: mingwBuild.bat [Debug|Release] [installed engine SDK prefix]
set "BUILD_TYPE=%~1"
if not defined BUILD_TYPE set "BUILD_TYPE=Debug"
if /I "%BUILD_TYPE%"=="debug" set "BUILD_TYPE=Debug"
if /I "%BUILD_TYPE%"=="release" set "BUILD_TYPE=Release"
if not "%BUILD_TYPE%"=="Debug" if not "%BUILD_TYPE%"=="Release" (
    echo Error: build type must be Debug or Release.
    popd
    exit /b 2
)

set "ENGINE_SDK=%~2"
if not defined ENGINE_SDK set "ENGINE_SDK=%VIBRANCE_ENGINE_ROOT%"
if not defined ENGINE_SDK if exist "..\vibrance-engine\install\windows-x64\lib\cmake\vibrance_engine\vibrance_engineConfig.cmake" (
    set "ENGINE_SDK=%CD%\..\vibrance-engine\install\windows-x64"
)

if not defined ENGINE_SDK goto :missing_sdk
if not exist "%ENGINE_SDK%\lib\cmake\vibrance_engine\vibrance_engineConfig.cmake" goto :invalid_sdk

set "BUILD_DIR=build\%BUILD_TYPE%"
set "INSTALL_DIR=%CD%\install"

echo Configuring hello_vibrance %BUILD_TYPE% against: "%ENGINE_SDK%"
cmake -S . -B "%BUILD_DIR%" -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    "-DCMAKE_PREFIX_PATH=%ENGINE_SDK%"
if errorlevel 1 goto :fail

cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"
if errorlevel 1 goto :fail

echo Build and install completed.
echo Run: "%INSTALL_DIR%\bin\hello_vibrance.exe"
popd
endlocal
exit /b 0

:missing_sdk
echo Error: no installed vibrance-engine SDK was selected.
echo Pass it as argument 2 or set VIBRANCE_ENGINE_ROOT.
echo Example: mingwBuild.bat Debug C:\dev\vibrance-sdk
popd
endlocal
exit /b 2

:invalid_sdk
echo Error: "%ENGINE_SDK%" is not an installed vibrance-engine SDK prefix.
echo Expected: "%ENGINE_SDK%\lib\cmake\vibrance_engine\vibrance_engineConfig.cmake"
popd
endlocal
exit /b 2

:fail
set "BUILD_EXIT_CODE=%errorlevel%"
if "%BUILD_EXIT_CODE%"=="0" set "BUILD_EXIT_CODE=1"
echo Error: build failed with exit code %BUILD_EXIT_CODE%.
popd
endlocal & exit /b %BUILD_EXIT_CODE%
