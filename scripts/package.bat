@echo off
REM Package build script for Presence For Plex (Windows)
REM Usage: scripts\package.bat [build_type]
REM Example: scripts\package.bat Release

setlocal EnableDelayedExpansion

REM Default values
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

set BUILD_DIR=build\release
set GENERATORS=NSIS;ZIP

echo === Presence For Plex Package Builder ===
echo Build Type: %BUILD_TYPE%
echo Build Directory: %BUILD_DIR%
echo.

REM Step 1: Configure
echo [1/4] Configuring CMake...
cmake -B %BUILD_DIR% -S . -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DBUILD_TESTING=OFF ^
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

if errorlevel 1 (
    echo Error: CMake configuration failed
    exit /b 1
)

REM Step 2: Build
echo [2/4] Building...
cmake --build %BUILD_DIR% --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo Error: Build failed
    exit /b 1
)

REM Step 3: Create packages
echo [3/4] Creating packages...
cd %BUILD_DIR%

REM Create packages directory
if not exist packages mkdir packages

REM Run CPack
cpack -C %BUILD_TYPE% -G "%GENERATORS%"

if errorlevel 1 (
    echo Error: Packaging failed
    exit /b 1
)

echo ✓ Packaging successful

REM Step 4: List packages
echo [4/4] Generated packages:
echo.

dir /b packages\* 2>nul || (
    REM Fallback: search in build directory
    dir /b *.exe *.zip 2>nul
)

echo.
echo === Packaging Complete ===
echo Packages are located in: %BUILD_DIR%\packages\

endlocal
