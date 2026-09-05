@echo off
setlocal
pushd "%~dp0"

REM Usage: mingwBuild.bat [Debug|Release] [SDK install prefix] [x64|arm64|all]
REM Shortcut: mingwBuild.bat [Debug|Release] [x64|arm64|all]
REM All relative build paths are anchored to this repository, even when this
REM script is called by the sibling application build.
set "BUILD_TYPE=Debug"
if /I "%~1"=="release" set "BUILD_TYPE=Release"
if /I "%~1"=="debug" set "BUILD_TYPE=Debug"

set "INSTALL_ARG=%~2"
set "TARGET_ARCH=%~3"
if not defined TARGET_ARCH if /I "%INSTALL_ARG%"=="x64" set "TARGET_ARCH=x64"
if not defined TARGET_ARCH if /I "%INSTALL_ARG%"=="arm64" set "TARGET_ARCH=arm64"
if not defined TARGET_ARCH if /I "%INSTALL_ARG%"=="all" set "TARGET_ARCH=all"
if /I "%INSTALL_ARG%"=="x64" set "INSTALL_ARG="
if /I "%INSTALL_ARG%"=="arm64" set "INSTALL_ARG="
if /I "%INSTALL_ARG%"=="all" set "INSTALL_ARG="
if not defined TARGET_ARCH set "TARGET_ARCH=%VIBRANCE_WINDOWS_ARCHITECTURE%"
if not defined TARGET_ARCH set "TARGET_ARCH=x64"
if /I "%TARGET_ARCH%"=="amd64" set "TARGET_ARCH=x64"
if /I "%TARGET_ARCH%"=="aarch64" set "TARGET_ARCH=arm64"
if /I "%TARGET_ARCH%"=="all" (
    if defined INSTALL_ARG (
        call "%~f0" "%BUILD_TYPE%" "%INSTALL_ARG%\windows-x64" x64
        if errorlevel 1 goto :fail
        call "%~f0" "%BUILD_TYPE%" "%INSTALL_ARG%\windows-arm64" arm64
        if errorlevel 1 goto :fail
    ) else (
        call "%~f0" "%BUILD_TYPE%" "%~dp0install" x64
        if errorlevel 1 goto :fail
        call "%~f0" "%BUILD_TYPE%" "%~dp0install\windows-arm64" arm64
        if errorlevel 1 goto :fail
    )
    popd
    endlocal
    exit /b 0
)
if /I not "%TARGET_ARCH%"=="x64" if /I not "%TARGET_ARCH%"=="arm64" (
    echo Error: architecture must be x64, arm64, or all.
    popd
    endlocal
    exit /b 2
)

set "INSTALL_DIR=%INSTALL_ARG%"
if not defined INSTALL_DIR if /I "%TARGET_ARCH%"=="arm64" set "INSTALL_DIR=%~dp0install\windows-arm64"
if not defined INSTALL_DIR set "INSTALL_DIR=%~dp0install"

if not exist "build" mkdir "build"
> "build\.active_build" echo %TARGET_ARCH%\%BUILD_TYPE%

echo Building vibrance-engine for Windows %TARGET_ARCH% with configuration: %BUILD_TYPE%
echo Installing vibrance-engine SDK to: %INSTALL_DIR%

if /I "%TARGET_ARCH%"=="arm64" (
    set "TARGET_DIR=build\windows-arm64\%BUILD_TYPE%"
) else (
    set "TARGET_DIR=build\%BUILD_TYPE%"
)
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

echo Configuring engine CMake for %BUILD_TYPE%...
if /I "%TARGET_ARCH%"=="arm64" (
    cmake -S . -B "%TARGET_DIR%" -G "MinGW Makefiles" ^
        "-DCMAKE_TOOLCHAIN_FILE=%~dp0cmake\toolchains\windows-arm64-llvm-mingw.cmake" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DVIBRANCE_WINDOWS_ARCHITECTURE=arm64 ^
        "-DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%"
) else (
    cmake -S . -B "%TARGET_DIR%" -G "MinGW Makefiles" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DVIBRANCE_WINDOWS_ARCHITECTURE=x64 ^
        "-DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%"
)
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
