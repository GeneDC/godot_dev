@echo off

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
	start build\godot_dev.sln
) else (
    start build_tracy\godot_dev.sln
)

