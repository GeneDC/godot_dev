@echo off
echo === Cleaning Godot Dev Environment ===

echo 1. Wipe CMake build folder
if exist "build" rd /s /q build

:: TODO put this godot-cpp clean in a more aggressive clean script
::echo 2. Wipe godot-cpp binaries
::if exist "godot-cpp\bin" rd /s /q godot-cpp\bin

echo 3. Wipe deployed binaries in the Godot project
if exist "project\bin" rd /s /q project\bin

:: TODO put this engine clean in a more aggressive clean script
::echo Wipe SCons temporary files in the engine folder
::pushd godot
::if exist ".sconsign.dblite" del /q .sconsign.dblite
::popd

echo Clean Complete!
echo Run generate_project_files.bat to regenerate.
pause