@echo off
REM 7-Zip ZS Build Script for Visual Studio 2022

echo ============================================
echo 7-Zip ZS Build Script
echo ============================================

REM Set VS environment
call "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Set build variables
set ROOT=%~dp0CPP\7zip
set PLATFORM=x64
set OUTDIR=%~dp0build_output
set OPTS=

REM Create output directory
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo.
echo Building 7z.dll (Core Library)...
cd /d "%ROOT%\Bundles\Format7zF"
nmake /NOLOGO
if %errorlevel% neq 0 (
    echo ERROR: Failed to build 7z.dll
    goto :error
)
copy "%PLATFORM%\7z.dll" "%OUTDIR%\" >nul
echo   [OK] 7z.dll

echo.
echo Building 7zFM.exe (File Manager)...
cd /d "%ROOT%\UI\FileManager"
nmake /NOLOGO
if %errorlevel% neq 0 (
    echo ERROR: Failed to build 7zFM.exe
    goto :error
)
copy "%PLATFORM%\7zFM.exe" "%OUTDIR%\" >nul
echo   [OK] 7zFM.exe

echo.
echo Building 7zG.exe (GUI Module)...
cd /d "%ROOT%\UI\GUI"
nmake /NOLOGO
if %errorlevel% neq 0 (
    echo ERROR: Failed to build 7zG.exe
    goto :error
)
copy "%PLATFORM%\7zG.exe" "%OUTDIR%\" >nul
echo   [OK] 7zG.exe

echo.
echo ============================================
echo Build completed successfully!
echo Output directory: %OUTDIR%
echo ============================================

REM Create Lang folder if it doesn't exist
if not exist "%OUTDIR%\Lang" mkdir "%OUTDIR%\Lang"

REM Create password file if it doesn't exist
if not exist "%OUTDIR%\7z_passwords.txt" (
    echo # 7-Zip ZS Password Book> "%OUTDIR%\7z_passwords.txt"
    echo # One password per line, lines starting with # are comments>> "%OUTDIR%\7z_passwords.txt"
)

echo.
echo NOTE: For Chinese/localization support, copy the Lang folder from
echo       an official 7-Zip installation to: %OUTDIR%\Lang
echo       Download from: https://www.7-zip.org/
echo.

dir "%OUTDIR%"
goto :end

:error
echo.
echo ============================================
echo BUILD FAILED!
echo ============================================
exit /b 1

:end
cd /d "%~dp0"
