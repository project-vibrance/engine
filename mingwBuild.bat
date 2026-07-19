if not exist build mkdir build

REM accept first argument as build type (Debug/Release)
SET BUILD_TYPE=Debug
IF NOT "%~1"=="" (
	IF /I "%~1"=="release" SET BUILD_TYPE=Release
	IF /I "%~1"=="Release" SET BUILD_TYPE=Release
	IF /I "%~1"=="debug" SET BUILD_TYPE=Debug
	IF /I "%~1"=="Debug" SET BUILD_TYPE=Debug
)

REM accept an SDK install prefix as the second argument
SET INSTALL_DIR=%~2
IF "%INSTALL_DIR%"=="" SET INSTALL_DIR=%~dp0install

echo %BUILD_TYPE%> build\.active_build

echo Building with configuration: %BUILD_TYPE%
echo Installing vibrance-engine SDK to: %INSTALL_DIR%

set TARGET_DIR=build\%BUILD_TYPE%
if not exist %TARGET_DIR% mkdir %TARGET_DIR%

echo Configuring CMake for %BUILD_TYPE%...
cmake -S . -B %TARGET_DIR% -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%"
if errorlevel 1 exit /b %errorlevel%

cmake --build %TARGET_DIR% --config %BUILD_TYPE%
if errorlevel 1 exit /b %errorlevel%

cmake --install %TARGET_DIR% --prefix "%INSTALL_DIR%" --config %BUILD_TYPE%
