@echo off
setlocal
REM ===========================================================================
REM  deploy.bat - collect Qt runtime next to MinesweeperUDP.exe
REM
REM  Usage:
REM      deploy.bat                                   auto-detect Qt
REM      deploy.bat E:\MySofts\Qt\6.8.1\mingw_64      explicit Qt kit
REM
REM  Run from build\bin (beside the exe) or from the project root.
REM ===========================================================================

set "SELF_DIR=%~dp0"
set "SELF_DIR=%SELF_DIR:~0,-1%"
set "BIN_DIR=%SELF_DIR%"

if not exist "%BIN_DIR%\MinesweeperUDP.exe" set "BIN_DIR=%SELF_DIR%\build\bin"

if not exist "%BIN_DIR%\MinesweeperUDP.exe" (
    echo [ERROR] MinesweeperUDP.exe not found next to this script.
    echo         Run deploy.bat inside build\bin or in the project root.
    goto :fail
)

set "QT_DIR=%~1"
if "%QT_DIR%"=="" if not "%QTDIR%"=="" set "QT_DIR=%QTDIR%"
if "%QT_DIR%"=="" if exist "E:\MySofts\Qt\6.8.1\mingw_64\bin\windeployqt.exe" set "QT_DIR=E:\MySofts\Qt\6.8.1\mingw_64"
if "%QT_DIR%"=="" if exist "C:\Qt\6.8.1\mingw_64\bin\windeployqt.exe" set "QT_DIR=C:\Qt\6.8.1\mingw_64"
if "%QT_DIR%"=="" if exist "D:\Qt\6.8.1\mingw_64\bin\windeployqt.exe" set "QT_DIR=D:\Qt\6.8.1\mingw_64"

if "%QT_DIR%"=="" goto :no_qt
if not exist "%QT_DIR%\bin\windeployqt.exe" goto :no_qt

set "MINGW_BIN="
for %%D in ("%QT_DIR%\..\..\Tools") do set "TOOLS_DIR=%%~fD"
if not exist "%TOOLS_DIR%" goto :skip_mingw
for /d %%M in ("%TOOLS_DIR%\mingw*_64") do (
    if exist "%%~fM\bin\libstdc++-6.dll" set "MINGW_BIN=%%~fM\bin"
)
:skip_mingw
if "%MINGW_BIN%"=="" if exist "%QT_DIR%\bin\libstdc++-6.dll" set "MINGW_BIN=%QT_DIR%\bin"

set "PATH=%QT_DIR%\bin;%MINGW_BIN%;%PATH%"

echo ============================================================
echo   UdpMinesweeper dependency deployment
echo ------------------------------------------------------------
echo   Target folder : %BIN_DIR%
echo   Qt            : %QT_DIR%
echo   MinGW runtime : %MINGW_BIN%
echo ============================================================
echo.

echo [1/3] Running windeployqt...
"%QT_DIR%\bin\windeployqt.exe" --release --force --compiler-runtime --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --dir "%BIN_DIR%" "%BIN_DIR%\MinesweeperUDP.exe"
echo.

echo [2/3] Copying MinGW compiler runtime...
if "%MINGW_BIN%"=="" (
    echo   [WARN] MinGW runtime folder not found, skipped.
) else (
    for %%L in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll) do (
        if exist "%MINGW_BIN%\%%L" (
            copy /y "%MINGW_BIN%\%%L" "%BIN_DIR%\%%L" >nul
            echo   -^> %%L
        ) else (
            echo   [WARN] %%L not found in %MINGW_BIN%
        )
    )
)
echo.

echo [3/3] Verifying required files...
set "MISSING=0"
for %%F in (
    Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll
    libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll
    platforms\qwindows.dll
) do (
    if exist "%BIN_DIR%\%%F" (
        echo   [ok]   %%F
    ) else (
        echo   [MISS] %%F
        set "MISSING=1"
    )
)

echo.
if "%MISSING%"=="0" (
    echo ============================================================
    echo   DEPLOY SUCCEEDED - this folder is self-contained
    echo   Run MinesweeperUDP.exe on any Windows PC without Qt.
    echo ============================================================
    endlocal
    exit /b 0
)

echo ============================================================
echo   DEPLOY INCOMPLETE - see [MISS] entries above
echo ============================================================
endlocal
exit /b 1

:no_qt
echo [ERROR] Qt installation not found.
echo         Pass the Qt kit path explicitly, e.g.:
echo             deploy.bat E:\MySofts\Qt\6.8.1\mingw_64
goto :fail

:fail
echo.
echo ============================================================
echo   DEPLOY FAILED
echo ============================================================
endlocal
exit /b 1
