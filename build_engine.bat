@echo off
pushd godot
call scons platform=windows dev_build=yes profiler=tracy profiler_path=thirdparty\tracy -j%NUMBER_OF_PROCESSORS%
popd
pause
