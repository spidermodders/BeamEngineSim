@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set /p MSG="Commit message: "
if "%MSG%"=="" (
    echo No message, nothing done.
    pause
    exit /b 1
)

set /p CURRENT=<VERSION
for /f "tokens=1-3 delims=." %%a in ("%CURRENT%") do set /a PATCH=%%c+1 & set NEXT=%%a.%%b.!PATCH!
git ls-remote --exit-code --tags origin v%CURRENT% >nul 2>&1 || set NEXT=%CURRENT%

echo.
echo Release version, Enter for %NEXT%, or "n" to only commit.
set /p VER="Version: "
if "%VER%"=="" set VER=%NEXT%
if /i "%VER%"=="n" set VER=

if defined VER (
    <nul set /p "=%VER%" > VERSION
    call Build.bat all || goto failed
)

git add -A
git commit -m "%MSG%" || goto failed
git push origin HEAD || goto failed

if defined VER (
    gh release create v%VER% "out\BeamEngineSim-Setup-%VER%.exe" "out\engineSimBridge.zip" --target main --title "BeamEngineSim %VER%" --notes "%MSG%" || goto failed
    echo Released v%VER%.
)

echo Done.
pause
exit /b 0

:failed
echo Failed.
pause
exit /b 1
