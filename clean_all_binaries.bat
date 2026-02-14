@echo off
echo === Cleaning Extension Binaries ===
echo [1/5] Clean godot-cpp binaries
if exist "godot-cpp\bin" (
	echo - Deleting: "godot-cpp\bin"
	rd /s /q "godot-cpp\bin"
)

echo [2/5] Clean build folder binaries
if exist "build\bin" (
	echo - Deleting: "build\bin"
	rd /s /q "build\bin"
)

echo [3/5] Clean deployed binaries in the Godot project
if exist "project\bin" (
	echo - Deleting: "project\bin"
	rd /s /q "project\bin"
)

echo [4/5] Clean Engine folder binaries, vcxproj, and scons temporary files
if exist "godot\" (
	pushd "godot"
	if exist ".sconsign.dblite" (
		echo - Deleting: ".sconsign.dblite"
		del /q .sconsign.dblite
	)
	if exist "godot.vcxproj" (
		echo - Deleting: "godot.vcxproj"
		del /q godot.vcxproj
	)
	if exist "bin\" (
		echo - Deleting: "godot\bin"
		rd /s /q "bin"
	)
	popd
) else (
	echo - "godot" folder doesn't exist!
)

echo [5/5] Clean Extension binaries
if exist "extensions\" (
	pushd "extensions"
	for /d %%d in (*) do (
		if exist "%%d\bin\" (
			echo - Deleting: "%%d\bin"
			rd /s /q "%%d\bin"
		)
	)
	popd
) else (
	echo - "extensions" folder doesn't exist!
)

echo Clean Complete!
echo Run generate_project_files.bat to regenerate engine build.
pause