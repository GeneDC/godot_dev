@echo off
pushd godot
call scons platform=windows dev_build=yes -j%NUMBER_OF_PROCESSORS%
popd
pause
