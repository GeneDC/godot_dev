@echo off
echo === Cleaning Extension Binaries ===

echo [1/2] Clean deployed binaries in the Godot project
if exist "project\bin" (
	echo - Deleting: "project\bin"
	rd /s /q "project\bin"
)

echo [2/2] Clean Extension binaries
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
echo Extensions are ready to be rebuilt.
pause