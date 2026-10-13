@echo off
cd /d "%~dp0"
set "OUT=out\build\release"
if not exist "%OUT%\Black.exe" call build.bat || exit /b 1
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
set /p SDK=<"%OUT%\sdk_dir.txt"
set "SDK=%SDK:/=\%"
rem One name for every release, so releases/latest/download/BLACK-XBERecomp.zip stays a working link
set "ZIP=BLACK-XBERecomp.zip"
del /q "%ZIP%" 2>nul
rem an empty assets folder beside the executable, where the game (files or .iso) goes
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
7z a "%ZIP%" ".\%OUT%\Black.exe" ".\%OUT%\release\assets" "%SDK%\THIRD_PARTY.txt"
