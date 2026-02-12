@echo off
setlocal enabledelayedexpansion

echo === Godot Dev Environment Setup ===

set "ENGINE_PROJ=godot\godot.vcxproj"

echo [1/3] Generating Godot Engine VS Project...
if exist "%ENGINE_PROJ%" (
    echo     Godot Engine VS Project already exists. ^(Delete %ENGINE_PROJ% if you need to force a refresh^)
) else (
    pushd godot
	call scons platform=windows vsproj=yes dev_build=yes -j%NUMBER_OF_PROCESSORS%
    if %ERRORLEVEL% neq 0 (
        echo ERROR: SCons engine build failed.
        popd
        pause
        exit /b %ERRORLEVEL%
    )
    popd
)

echo [2/3] Generating Master Solution and Extension Projects...
if not exist "build" mkdir build
cmake -B build -G "Visual Studio 17 2022"
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake generation failed.
    pause
    exit /b %ERRORLEVEL%
)

:: 3. Completion
echo [3/3] Setup Complete!
echo.
echo The master solution is located at: build\godot_dev.sln
pause