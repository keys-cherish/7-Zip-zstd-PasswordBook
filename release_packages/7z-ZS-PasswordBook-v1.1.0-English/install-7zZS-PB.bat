@echo off
chcp 65001 >nul 2>&1
:: 7-Zip ZS Password Book Edition - Installation Script
:: This script registers 7-Zip ZS Password Book to Windows
:: Run as Administrator

setlocal EnableDelayedExpansion

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
:: Script is in the same folder as 7zFM.exe
set "SCRIPT_DIR=%~dp0"
:: Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: Check if 7zFM.exe exists in the same directory
if not exist "%SCRIPT_DIR%\7zFM.exe" (
    echo ERROR: 7zFM.exe not found in %SCRIPT_DIR%
    echo Please make sure this script is in the same folder as 7zFM.exe
    pause
    exit /b 1
)

set "INSTALL_DIR=%SCRIPT_DIR%"

echo Installation Directory: %INSTALL_DIR%
echo.

:: Create App Paths registry entry (for Windows Search)
echo [1/5] Registering application for Windows Search...
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\7zFM-PB.exe" /ve /d "%INSTALL_DIR%\7zFM.exe" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\7zFM-PB.exe" /v "Path" /d "%INSTALL_DIR%" /f >nul

:: Create Uninstall registry entry (for Windows Apps list / Control Panel)
echo [2/5] Registering in Windows Apps list...
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "DisplayName" /d "7-Zip ZS Password Book" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "DisplayIcon" /d "%INSTALL_DIR%\7zFM.exe,0" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "InstallLocation" /d "%INSTALL_DIR%" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "Publisher" /d "7-Zip ZS Password Book Edition" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "DisplayVersion" /d "24.09-v1.5.6-R7-PB-1.1.0" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "NoModify" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "NoRepair" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "UninstallString" /d "\"%INSTALL_DIR%\uninstall-7zZS-PB.bat\"" /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\7-Zip-ZS-PasswordBook" /v "QuietUninstallString" /d "\"%INSTALL_DIR%\uninstall-7zZS-PB.bat\" /S" /f >nul

:: Register shell extension for context menu
echo [3/5] Registering shell context menu...

:: Files - Open with 7-Zip ZS PB
reg add "HKCR\*\shell\7-Zip-ZS-PB" /ve /d "Open with 7-Zip ZS PB" /f >nul
reg add "HKCR\*\shell\7-Zip-ZS-PB" /v "Icon" /d "%INSTALL_DIR%\7zFM.exe,0" /f >nul
reg add "HKCR\*\shell\7-Zip-ZS-PB\command" /ve /d "\"%INSTALL_DIR%\7zFM.exe\" \"%%1\"" /f >nul

:: Files - Add to archive (compress)
reg add "HKCR\*\shell\7-Zip-ZS-PB-Compress" /ve /d "Add to archive (7-Zip ZS PB)" /f >nul
reg add "HKCR\*\shell\7-Zip-ZS-PB-Compress" /v "Icon" /d "%INSTALL_DIR%\7zG.exe,0" /f >nul
reg add "HKCR\*\shell\7-Zip-ZS-PB-Compress\command" /ve /d "\"%INSTALL_DIR%\7zG.exe\" a \"%%1\"" /f >nul

:: Directory - Open with 7-Zip ZS PB
reg add "HKCR\Directory\shell\7-Zip-ZS-PB" /ve /d "Open with 7-Zip ZS PB" /f >nul
reg add "HKCR\Directory\shell\7-Zip-ZS-PB" /v "Icon" /d "%INSTALL_DIR%\7zFM.exe,0" /f >nul
reg add "HKCR\Directory\shell\7-Zip-ZS-PB\command" /ve /d "\"%INSTALL_DIR%\7zFM.exe\" \"%%1\"" /f >nul

:: Directory - Add to archive (compress folder)
reg add "HKCR\Directory\shell\7-Zip-ZS-PB-Compress" /ve /d "Add to archive (7-Zip ZS PB)" /f >nul
reg add "HKCR\Directory\shell\7-Zip-ZS-PB-Compress" /v "Icon" /d "%INSTALL_DIR%\7zG.exe,0" /f >nul
reg add "HKCR\Directory\shell\7-Zip-ZS-PB-Compress\command" /ve /d "\"%INSTALL_DIR%\7zG.exe\" a \"%%1\"" /f >nul

:: Directory Background - Compress current folder
reg add "HKCR\Directory\Background\shell\7-Zip-ZS-PB-Compress" /ve /d "Compress this folder (7-Zip ZS PB)" /f >nul
reg add "HKCR\Directory\Background\shell\7-Zip-ZS-PB-Compress" /v "Icon" /d "%INSTALL_DIR%\7zG.exe,0" /f >nul
reg add "HKCR\Directory\Background\shell\7-Zip-ZS-PB-Compress\command" /ve /d "\"%INSTALL_DIR%\7zG.exe\" a \"%%V\"" /f >nul

:: Create Start Menu shortcut
echo [4/5] Creating Start Menu shortcut...
set "STARTMENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs"
powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%STARTMENU%\7-Zip ZS Password Book.lnk'); $s.TargetPath = '%INSTALL_DIR%\7zFM.exe'; $s.WorkingDirectory = '%INSTALL_DIR%'; $s.Description = '7-Zip ZS File Manager with Password Book'; $s.IconLocation = '%INSTALL_DIR%\7zFM.exe,0'; $s.Save()"

:: Create Desktop shortcut (optional)
echo [5/5] Creating Desktop shortcut...
powershell -NoProfile -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([Environment]::GetFolderPath('CommonDesktopDirectory') + '\7-Zip ZS Password Book.lnk'); $s.TargetPath = '%INSTALL_DIR%\7zFM.exe'; $s.WorkingDirectory = '%INSTALL_DIR%'; $s.Description = '7-Zip ZS File Manager with Password Book'; $s.IconLocation = '%INSTALL_DIR%\7zFM.exe,0'; $s.Save()"

echo.
echo ================================================
echo Installation Complete!
echo ================================================
echo.
echo You can now:
echo   1. Search "7-Zip ZS Password Book" in Windows Search
echo   2. Find it in Start Menu and Desktop
echo   3. Right-click files/folders to "Open with 7-Zip ZS PB"
echo   4. Run 7zFM.exe directly from: %INSTALL_DIR%
echo.
echo This version can coexist with standard 7-Zip or 7-Zip ZS.
echo.
echo To uninstall, run uninstall-7zZS-PB.bat as Administrator
echo or use "Apps & Features" in Windows Settings.
echo.
pause
