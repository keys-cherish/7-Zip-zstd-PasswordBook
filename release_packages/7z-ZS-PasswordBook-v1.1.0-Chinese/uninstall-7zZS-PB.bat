@echo off
:: 7-Zip ZS Password Book Edition - Uninstall Script
:: Run as Administrator

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

set "INSTALL_DIR=%~dp0"
set "INSTALL_DIR=%INSTALL_DIR:~0,-1%"

echo Removing registry entries...
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\7zFM-PB.exe" /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /f >nul 2>&1

echo Removing Start Menu shortcut...
set "STARTMENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs"
del "%STARTMENU%\7-Zip ZS Password Book.lnk" >nul 2>&1

echo.
echo ================================================
echo Uninstall Complete!
echo ================================================
echo.
echo Registry entries and shortcuts have been removed.
echo You can now delete the installation folder manually:
echo   %INSTALL_DIR%
echo.
pause
