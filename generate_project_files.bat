@echo off
setlocal enabledelayedexpansion

echo === Godot Dev Environment Setup ===

set "ENGINE_PROJ=godot\godot.vcxproj"

:: Default state
set TRACY_FLAG=OFF
set BUILD_DIR=build

:: Read the config file
for /f "tokens=1,2 delims==" %%A in (build_config.txt) do (
    set "line=%%A"
    :: Ignore comment lines starting with #
    if "!line:~0,1!" neq "#" (
        :: Strip trailing spaces and look for the flag name
        set "var_name=%%A"
        set "var_value=%%B"
        
        :: Clean spaces from variables
        set "var_name=!var_name: =!"
        set "var_value=!var_value: =!"
        
        if /i "!var_name!"=="ENABLE_TRACY" (
            set "TRACY_FLAG=!var_value!"
        )
    )
)

if /i "!TRACY_FLAG!"=="ON" (
    set TRACY_FLAG=ON
    set BUILD_DIR=build_tracy
    echo === Configured with TRACY PROFILER [ENABLED] ===
) else (
    echo === Configured with TRACY PROFILER [DISABLED] ===
)

echo [1/3] Generating Godot Engine VS Project...
if exist "%ENGINE_PROJ%" (
    echo     Godot Engine VS Project already exists. ^(Delete %ENGINE_PROJ% if you need to force a refresh^)
) else (
    pushd godot
	call scons platform=windows vsproj=yes dev_build=yes profiler=tracy profiler_path=thirdparty\tracy -j%NUMBER_OF_PROCESSORS%
    if %ERRORLEVEL% neq 0 (
        echo ERROR: SCons engine build failed.
        popd
        pause
        exit /b %ERRORLEVEL%
    )
    popd
)

if not exist !BUILD_DIR! mkdir !BUILD_DIR!

echo [2/3] Generating Master Solution with Extension and Godot Projects...
echo       Starting CMake...
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -DENABLE_TRACY=!TRACY_FLAG!
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