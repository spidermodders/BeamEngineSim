@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set /p VERSION=<VERSION
set ROOT=%CD%
set OUT=%ROOT%\out

set TARGET=%~1
if "%TARGET%"=="" (
    echo BeamEngineSim %VERSION%
    echo.
    echo   1  EngineSim
    echo   2  DSP plugin
    echo   3  BeamNG mod
    echo   4  Installer
    echo   5  Everything
    echo.
    set /p PICK="Build: "
    if "!PICK!"=="1" set TARGET=enginesim
    if "!PICK!"=="2" set TARGET=dsp
    if "!PICK!"=="3" set TARGET=mod
    if "!PICK!"=="4" set TARGET=installer
    if "!PICK!"=="5" set TARGET=all
)

if not exist "%OUT%" mkdir "%OUT%"

if /i "%TARGET%"=="enginesim" call :enginesim || goto failed
if /i "%TARGET%"=="dsp" call :dsp || goto failed
if /i "%TARGET%"=="mod" call :mod || goto failed
if /i "%TARGET%"=="installer" call :installer || goto failed
if /i "%TARGET%"=="all" (
    call :enginesim || goto failed
    call :dsp || goto failed
    call :mod || goto failed
    call :installer || goto failed
)

echo.
echo Build finished. Output: %OUT%
if "%~1"=="" pause
exit /b 0

:failed
echo.
echo Build failed.
if "%~1"=="" pause
exit /b 1


:deps
if not exist Dependencies mkdir Dependencies
if not exist Dependencies\vcpkg\vcpkg.exe (
    git clone https://github.com/microsoft/vcpkg Dependencies\vcpkg || exit /b 1
    call Dependencies\vcpkg\bootstrap-vcpkg.bat -disableMetrics || exit /b 1
)
if not exist Dependencies\vcpkg_installed\x64-windows\include\SDL2 (
    Dependencies\vcpkg\vcpkg.exe install --triplet x64-windows --x-manifest-root="%ROOT%\Dependencies" --x-install-root="%ROOT%\Dependencies\vcpkg_installed" || exit /b 1
)
if not exist Dependencies\static\x64-windows-static\lib\minhook.x64.lib (
    Dependencies\vcpkg\vcpkg.exe install minhook:x64-windows-static --classic --x-install-root="%ROOT%\Dependencies\static" || exit /b 1
)
if not exist Dependencies\winflexbison\win_bison.exe (
    curl -L -o Dependencies\winflexbison.zip https://github.com/lexxmark/winflexbison/releases/download/v2.5.25/win_flex_bison-2.5.25.zip || exit /b 1
    mkdir Dependencies\winflexbison
    tar -xf Dependencies\winflexbison.zip -C Dependencies\winflexbison || exit /b 1
)
exit /b 0


:enginesim
echo.
echo == EngineSim
call :deps || exit /b 1
git submodule update --init || exit /b 1
for %%P in (delta-studio piranha) do (
    pushd EngineSimulator\dependencies\submodules\%%P
    git apply --reverse --check "%ROOT%\EngineSimulator\Patches\%%P.patch" 2>nul || git apply "%ROOT%\EngineSimulator\Patches\%%P.patch" || (popd & exit /b 1)
    popd
)
if not exist EngineSimulator\build\CMakeCache.txt (
    cmake -S EngineSimulator -B EngineSimulator\build -A x64 ^
        -DCMAKE_TOOLCHAIN_FILE="%ROOT%\Dependencies\vcpkg\scripts\buildsystems\vcpkg.cmake" ^
        -DVCPKG_INSTALLED_DIR="%ROOT%\Dependencies\vcpkg_installed" ^
        -DVCPKG_MANIFEST_MODE=OFF ^
        -DBISON_EXECUTABLE="%ROOT%\Dependencies\winflexbison\win_bison.exe" ^
        -DFLEX_EXECUTABLE="%ROOT%\Dependencies\winflexbison\win_flex.exe" ^
        -DDISCORD_ENABLED=OFF ^
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 || exit /b 1
)
cmake --build EngineSimulator\build --config Release --target engine-sim-app || exit /b 1

set STAGE=%OUT%\enginesim
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\bin"
copy /y EngineSimulator\build\Release\engine-sim-app.exe "%STAGE%\bin\" >nul || exit /b 1
copy /y EngineSimulator\build\Release\*.dll "%STAGE%\bin\" >nul || exit /b 1
(echo ../engine& echo ../assets)> "%STAGE%\bin\delta.conf"
robocopy EngineSimulator\dependencies\submodules\delta-studio\engines\basic\fonts "%STAGE%\engine\fonts" /e /njh /njs /nfl /ndl >nul
robocopy EngineSimulator\dependencies\submodules\delta-studio\engines\basic\shaders "%STAGE%\engine\shaders" /e /njh /njs /nfl /ndl >nul
robocopy EngineSimulator\assets "%STAGE%\assets" /e /njh /njs /nfl /ndl >nul
robocopy EngineSimulator\es "%STAGE%\es" /e /njh /njs /nfl /ndl >nul
copy /y EngineSimulator\LICENSE "%STAGE%\LICENSE-EngineSimulator.txt" >nul
exit /b 0


:dsp
echo.
echo == DSP plugin
call :deps || exit /b 1
if not exist AudioPlugin\build\CMakeCache.txt (
    cmake -S AudioPlugin -B AudioPlugin\build -A x64 || exit /b 1
)
cmake --build AudioPlugin\build --config Release || exit /b 1
copy /y AudioPlugin\build\Release\BeamNGBlendLayerDSP.dll "%OUT%\" >nul || exit /b 1
exit /b 0


:mod
echo.
echo == BeamNG mod
if exist "%OUT%\engineSimBridge.zip" del "%OUT%\engineSimBridge.zip"
powershell -NoProfile -Command "Compress-Archive -Path 'EngineSimulatorMod\*' -DestinationPath '%OUT%\engineSimBridge.zip'" || exit /b 1
exit /b 0


:installer
echo.
echo == Installer
for %%F in (enginesim\bin\engine-sim-app.exe BeamNGBlendLayerDSP.dll engineSimBridge.zip) do (
    if not exist "%OUT%\%%F" (
        echo Missing out\%%F, build the other parts first.
        exit /b 1
    )
)
set ISCC=
for %%I in ("%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do (
    if exist %%I set ISCC=%%I
)
if not defined ISCC (
    echo Inno Setup 6 not found. Install it from https://jrsoftware.org/isdl.php
    exit /b 1
)
%ISCC% /Q /DAppVersion=%VERSION% /O"%OUT%" Installer\BeamEngineSim.iss || exit /b 1
exit /b 0
