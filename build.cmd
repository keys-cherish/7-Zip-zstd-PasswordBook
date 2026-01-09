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

REM Check for clean/rebuild option
if "%1"=="clean" goto :cleanonly
if "%1"=="rebuild" goto :clean

REM Always clean FM and GUI cache to ensure fresh build
echo.
echo Cleaning FM/GUI cache for fresh build...
if exist "%ROOT%\UI\FileManager\%PLATFORM%" rd /s /q "%ROOT%\UI\FileManager\%PLATFORM%"
if exist "%ROOT%\UI\GUI\%PLATFORM%" rd /s /q "%ROOT%\UI\GUI\%PLATFORM%"
goto :build

:cleanonly
echo.
echo Cleaning all build cache...
if exist "%ROOT%\Bundles\Format7zF\%PLATFORM%" rd /s /q "%ROOT%\Bundles\Format7zF\%PLATFORM%"
if exist "%ROOT%\UI\FileManager\%PLATFORM%" rd /s /q "%ROOT%\UI\FileManager\%PLATFORM%"
if exist "%ROOT%\UI\GUI\%PLATFORM%" rd /s /q "%ROOT%\UI\GUI\%PLATFORM%"
echo   [OK] Build cache cleaned.
goto :end

:clean
echo.
echo Cleaning all build cache...
if exist "%ROOT%\Bundles\Format7zF\%PLATFORM%" rd /s /q "%ROOT%\Bundles\Format7zF\%PLATFORM%"
if exist "%ROOT%\UI\FileManager\%PLATFORM%" rd /s /q "%ROOT%\UI\FileManager\%PLATFORM%"
if exist "%ROOT%\UI\GUI\%PLATFORM%" rd /s /q "%ROOT%\UI\GUI\%PLATFORM%"
echo   [OK] Build cache cleaned.

:build
REM Create output directory
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

REM Create data subfolder for password database
if not exist "%OUTDIR%\data" mkdir "%OUTDIR%\data"

REM Create scripts subfolder for install/uninstall scripts  
if not exist "%OUTDIR%\scripts" mkdir "%OUTDIR%\scripts"

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
echo.
echo Usage: build.cmd [clean^|rebuild]
echo   clean   - Clean build cache only
echo   rebuild - Clean cache and rebuild all

REM Copy Lang folder if source exists and target doesn't have zh-cn.txt
if not exist "%OUTDIR%\Lang" mkdir "%OUTDIR%\Lang"
if exist "%~dp0release\Chinese\Lang\zh-cn.txt" (
    if not exist "%OUTDIR%\Lang\zh-cn.txt" (
        echo.
        echo Copying Chinese language file...
        copy "%~dp0release\Chinese\Lang\zh-cn.txt" "%OUTDIR%\Lang\" >nul
        echo   [OK] zh-cn.txt copied
    )
)

REM Copy install scripts to scripts folder if not exist
if exist "%~dp0build_output\install-7zZS-PB.bat" (
    if not exist "%OUTDIR%\scripts\install-7zZS-PB.bat" (
        move "%OUTDIR%\install-7zZS-PB.bat" "%OUTDIR%\scripts\" >nul 2>&1
        move "%OUTDIR%\uninstall-7zZS-PB.bat" "%OUTDIR%\scripts\" >nul 2>&1
    )
)

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
