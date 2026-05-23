@echo off
setlocal enabledelayedexpansion

echo === Tracy Project Setup ===

cd thirdparty/tracy

echo       Starting CMake...
cmake -B profiler/build -S profiler -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake generation failed.
    pause
    exit /b %ERRORLEVEL%
)
