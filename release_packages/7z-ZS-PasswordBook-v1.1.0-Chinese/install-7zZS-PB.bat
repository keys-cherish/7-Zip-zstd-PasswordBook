@echo off
:: 7-Zip ZS Password Book Edition - Installation Script
:: This script registers 7-Zip ZS Password Book to Windows
:: Run as Administrator

echo ================================================
echo 7-Zip ZS Password Book Edition - Installer
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

:: Get the directory where this script is located
set "INSTALL_DIR=%~dp0"
set "INSTALL_DIR=%INSTALL_DIR:~0,-1%"

echo Installation Directory: %INSTALL_DIR%
echo.

:: Create App Paths registry entry (for Windows Search)
echo Registering application for Windows Search...
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\7zFM-PB.exe" /ve /d "%INSTALL_DIR%\7zFM.exe" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\7zFM-PB.exe" /v "Path" /d "%INSTALL_DIR%" /f >nul

:: Create Start Menu shortcut info in registry
echo Registering in Windows Apps list...
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "DisplayName" /d "7-Zip ZS Password Book" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "DisplayIcon" /d "%INSTALL_DIR%\7zFM.exe" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "InstallLocation" /d "%INSTALL_DIR%" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "Publisher" /d "7-Zip ZS Password Book Edition" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "DisplayVersion" /d "24.09-v1.5.6-R7-PB" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "NoModify" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "NoRepair" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "UninstallString" /d "%INSTALL_DIR%\uninstall-7zZS-PB.bat" /f >nul

:: Create Start Menu shortcut
echo Creating Start Menu shortcut...
set "STARTMENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs"
powershell -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%STARTMENU%\7-Zip ZS Password Book.lnk'); $s.TargetPath = '%INSTALL_DIR%\7zFM.exe'; $s.WorkingDirectory = '%INSTALL_DIR%'; $s.Description = '7-Zip ZS File Manager with Password Book'; $s.Save()"

:: Add to PATH (optional - creates a separate path entry)
echo Adding to system search path...
setx /M PATH "%PATH%;%INSTALL_DIR%" >nul 2>&1

echo.
echo ================================================
echo Installation Complete!
echo ================================================
echo.
echo You can now:
echo   1. Search "7-Zip ZS Password Book" in Windows Search
echo   2. Find it in Start Menu
echo   3. Run 7zFM.exe directly from: %INSTALL_DIR%
echo.
echo This version can coexist with standard 7-Zip or 7-Zip ZS.
echo.
pause
