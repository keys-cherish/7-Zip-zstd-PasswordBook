@echo off
chcp 65001 >nul 2>&1
:: 7-Zip ZS Password Book Edition - Uninstall Script
:: Run as Administrator

setlocal EnableDelayedExpansion

echo ================================================
echo 7-Zip ZS Password Book Edition - Uninstaller
echo ================================================
echo.

:: Check for admin rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Please run this script as Administrator!
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

:: Check for silent mode
set "SILENT=0"
if /i "%~1"=="/S" set "SILENT=1"
if /i "%~1"=="-S" set "SILENT=1"
if /i "%~1"=="/silent" set "SILENT=1"

:: Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "INSTALL_DIR=%SCRIPT_DIR%"

if "%SILENT%"=="0" (
    echo Installation Directory: %INSTALL_DIR%
    echo.
    echo This will remove:
    echo   - Registry entries
    echo   - Start Menu shortcut
    echo   - Desktop shortcut
    echo   - Right-click context menu
    echo.
    set /p "CONFIRM=Are you sure you want to uninstall? (Y/N): "
    if /i not "!CONFIRM!"=="Y" (
        echo Uninstall cancelled.
        pause
        exit /b 0
    )
    echo.
)

:: Remove App Paths registry entry
echo [1/5] Removing Windows Search registration...
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\7zFM-PB.exe" /f >nul 2>&1

:: Remove Uninstall registry entry
echo [2/5] Removing from Windows Apps list...
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /f >nul 2>&1

:: Remove shell context menu entries
echo [3/5] Removing shell context menu...
reg delete "HKCR\*\shell\7-Zip-ZS-PB" /f >nul 2>&1
reg delete "HKCR\*\shell\7-Zip-ZS-PB-Compress" /f >nul 2>&1
reg delete "HKCR\Directory\shell\7-Zip-ZS-PB" /f >nul 2>&1
reg delete "HKCR\Directory\shell\7-Zip-ZS-PB-Compress" /f >nul 2>&1
reg delete "HKCR\Directory\Background\shell\7-Zip-ZS-PB-Compress" /f >nul 2>&1

:: Remove Start Menu shortcut
echo [4/5] Removing Start Menu shortcut...
set "STARTMENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs"
del "%STARTMENU%\7-Zip ZS Password Book.lnk" >nul 2>&1

:: Remove Desktop shortcut
echo [5/5] Removing Desktop shortcut...
powershell -NoProfile -Command "Remove-Item ([Environment]::GetFolderPath('CommonDesktopDirectory') + '\7-Zip ZS Password Book.lnk') -Force -ErrorAction SilentlyContinue"

echo.
echo ================================================
echo Uninstall Complete!
echo ================================================
echo.
echo All registry entries and shortcuts have been removed.
echo.
echo The following items are NOT removed (manual cleanup if needed):
echo   - Installation folder: %INSTALL_DIR%
echo   - Password data: %INSTALL_DIR%\data\
echo   - Backup data: %APPDATA%\7-Zip-ZS-PB\
echo.
if "%SILENT%"=="0" (
    echo Press any key to exit...
    pause >nul
)
