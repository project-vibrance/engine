@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "TEMPLATE_DIR=%SCRIPT_DIR%templates\vibrance-app"

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage

set "TARGET_DIR=%~f1"
set "SDK_DIR=%~f2"
set "BUILD_TYPE=%~3"
if not defined BUILD_TYPE set "BUILD_TYPE=Debug"
if /I "%BUILD_TYPE%"=="debug" set "BUILD_TYPE=Debug"
if /I "%BUILD_TYPE%"=="release" set "BUILD_TYPE=Release"
if not "%BUILD_TYPE%"=="Debug" if not "%BUILD_TYPE%"=="Release" (
    echo Error: build type must be Debug or Release.
    exit /b 2
)

if not exist "%TEMPLATE_DIR%\CMakeLists.txt" (
    echo Error: starter template is missing: "%TEMPLATE_DIR%"
    exit /b 2
)
if not exist "%SDK_DIR%\lib\cmake\vibrance_engine\vibrance_engineConfig.cmake" (
    echo Error: "%SDK_DIR%" is not an installed vibrance-engine SDK prefix.
    echo Expected: "%SDK_DIR%\lib\cmake\vibrance_engine\vibrance_engineConfig.cmake"
    exit /b 2
)

if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
if errorlevel 1 exit /b %errorlevel%

for /f "delims=" %%F in ('dir /b /a "%TARGET_DIR%" 2^>nul') do (
    echo Error: target directory must be empty: "%TARGET_DIR%"
    exit /b 2
)

echo Creating the starter project in: "%TARGET_DIR%"
cmake -E copy_directory "%TEMPLATE_DIR%" "%TARGET_DIR%"
if errorlevel 1 goto :fail

set "BUILD_DIR=%TARGET_DIR%\build\%BUILD_TYPE%"
set "INSTALL_DIR=%TARGET_DIR%\install"

echo Configuring %BUILD_TYPE% against: "%SDK_DIR%"
cmake -S "%TARGET_DIR%" -B "%BUILD_DIR%" -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    "-DCMAKE_PREFIX_PATH=%SDK_DIR%"
if errorlevel 1 goto :fail

cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"
if errorlevel 1 goto :fail

echo.
echo Starter application created successfully.
echo Source: "%TARGET_DIR%"
echo Run:    "%INSTALL_DIR%\bin\hello_vibrance.exe"
exit /b 0

:usage
echo Usage: %~nx0 TARGET_DIRECTORY ENGINE_SDK [Debug^|Release]
echo Example: %~nx0 C:\dev\hello-vibrance C:\dev\vibrance-sdk Debug
exit /b 2

:fail
set "SETUP_EXIT_CODE=%errorlevel%"
if "%SETUP_EXIT_CODE%"=="0" set "SETUP_EXIT_CODE=1"
echo Error: starter setup failed with exit code %SETUP_EXIT_CODE%.
exit /b %SETUP_EXIT_CODE%
