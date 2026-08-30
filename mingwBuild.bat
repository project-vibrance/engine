@echo off
setlocal
pushd "%~dp0"

REM Usage: mingwBuild.bat [Debug|Release] [SDK install prefix]
REM All relative build paths are anchored to this repository, even when this
REM script is called by the sibling application build.
set "BUILD_TYPE=Debug"
if /I "%~1"=="release" set "BUILD_TYPE=Release"
if /I "%~1"=="debug" set "BUILD_TYPE=Debug"

set "INSTALL_DIR=%~2"
if not defined INSTALL_DIR set "INSTALL_DIR=%~dp0install"

if not exist "build" mkdir "build"
> "build\.active_build" echo %BUILD_TYPE%

echo Building vibrance-engine with configuration: %BUILD_TYPE%
echo Installing vibrance-engine SDK to: %INSTALL_DIR%

set "TARGET_DIR=build\%BUILD_TYPE%"
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

echo Configuring engine CMake for %BUILD_TYPE%...
cmake -S . -B "%TARGET_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% "-DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%"
if errorlevel 1 goto :fail

cmake --build "%TARGET_DIR%" --config %BUILD_TYPE%
if errorlevel 1 goto :fail

cmake --install "%TARGET_DIR%" --prefix "%INSTALL_DIR%" --config %BUILD_TYPE%
if errorlevel 1 goto :fail

popd
endlocal
exit /b 0

:fail
set "BUILD_EXIT_CODE=%errorlevel%"
if "%BUILD_EXIT_CODE%"=="0" set "BUILD_EXIT_CODE=1"
popd
endlocal & exit /b %BUILD_EXIT_CODE%
