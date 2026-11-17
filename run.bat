@echo off
setlocal
REM ===========================================================================
REM  UdpMinesweeper - one click build script (Windows / MinGW / Qt6 / Ninja)
REM
REM  Usage:
REM      run.bat            incremental build (Release)
REM      run.bat clean      wipe build dir and rebuild
REM      run.bat deploy     build + windeployqt (standalone folder)
REM      run.bat test       build + run all unit tests
REM ===========================================================================

set "QT_ROOT=E:\MySofts\Qt"
set "QT_VER=6.8.1"
set "QT_DIR=%QT_ROOT%\%QT_VER%\mingw_64"
set "MINGW_DIR=%QT_ROOT%\Tools\mingw1310_64"
set "CMAKE_EXE=%QT_ROOT%\Tools\CMake_64\bin\cmake.exe"
set "NINJA_EXE=%QT_ROOT%\Tools\Ninja\ninja.exe"

set "PROJ_DIR=%~dp0"
set "BUILD_DIR=%PROJ_DIR%build"

echo ============================================================
echo   UdpMinesweeper build script
echo ------------------------------------------------------------
echo   Project : %PROJ_DIR%
echo   Qt      : %QT_DIR%
echo   MinGW   : %MINGW_DIR%
echo ============================================================
echo.

if not exist "%CMAKE_EXE%" goto :no_cmake
if not exist "%NINJA_EXE%" goto :no_ninja
if not exist "%MINGW_DIR%\bin\g++.exe" goto :no_gcc
if not exist "%QT_DIR%\bin\qmake.exe" goto :no_qt

set "PATH=%MINGW_DIR%\bin;%QT_DIR%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%QT_DIR%"

if /I "%~1"=="clean" (
    echo [1/4] Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
) else (
    echo [1/4] Build directory: %BUILD_DIR%
)
echo.

echo [2/4] Configuring with CMake + Ninja...
"%CMAKE_EXE%" -S "%PROJ_DIR%." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" "-DCMAKE_CXX_COMPILER=%MINGW_DIR%/bin/g++.exe"
if errorlevel 1 goto :cfg_fail
echo.

echo [3/4] Building...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 goto :build_fail
echo.

if /I "%~1"=="test" (
    echo [4/4] Running unit tests...
    "%BUILD_DIR%\bin\core_tests.exe"
    if errorlevel 1 goto :test_fail
    "%BUILD_DIR%\bin\net_tests.exe"
    if errorlevel 1 goto :test_fail
    "%BUILD_DIR%\bin\ai_tests.exe"
    if errorlevel 1 goto :test_fail
    "%BUILD_DIR%\bin\audio_tests.exe"
    if errorlevel 1 goto :test_fail
    echo   All tests passed.
    goto :done
)

if /I "%~1"=="deploy" (
    echo [4/4] Deploying Qt runtime with windeployqt...
    "%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "%BUILD_DIR%\bin\MinesweeperUDP.exe"
) else (
    echo [4/4] windeployqt skipped. Use "run.bat deploy" for a standalone folder.
)

:done
echo.
echo ============================================================
echo   BUILD SUCCEEDED
echo ------------------------------------------------------------
echo   Game     : %BUILD_DIR%\bin\MinesweeperUDP.exe
echo   Tests    : %BUILD_DIR%\bin\core_tests.exe
echo              %BUILD_DIR%\bin\net_tests.exe
echo              %BUILD_DIR%\bin\ai_tests.exe
echo ------------------------------------------------------------
echo   Play solo / vs AI   : run MinesweeperUDP.exe
echo   Play over LAN       : host opens UDP %PORT%, client joins
echo             with host IP and the same port
echo ============================================================
endlocal
exit /b 0

:no_cmake
echo [ERROR] CMake not found: %CMAKE_EXE%
goto :fail
:no_ninja
echo [ERROR] Ninja not found: %NINJA_EXE%
goto :fail
:no_gcc
echo [ERROR] g++ not found: %MINGW_DIR%\bin\g++.exe
goto :fail
:no_qt
echo [ERROR] Qt not found: %QT_DIR%
goto :fail
:cfg_fail
echo [ERROR] CMake configure failed.
goto :fail
:build_fail
echo [ERROR] Build failed.
goto :fail
:test_fail
echo [ERROR] Unit tests failed.
goto :fail
:fail
echo ============================================================
echo   BUILD FAILED - see messages above
echo ============================================================
endlocal
exit /b 1
