import os
import subprocess
import sys
import shutil
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent

config_path = ROOT_DIR / "build_config.txt"

def load_config():
    # Fallback defaults
    config = {"ENABLE_TRACY": "OFF", "BUILD_DIR": "build", "ENGINE_BIN_DIR": "bin\\standard"}
    
    if not os.path.exists(config_path):
        return config

    with open(config_path, "r") as f:
        for line in f:
            if line.strip() and not line.startswith("#"):
                key, val = line.replace(" ", "").strip().split("=")
                if key == "ENABLE_TRACY":
                    config["ENABLE_TRACY"] = val.upper()


    if config["ENABLE_TRACY"] == "ON":
        config["BUILD_DIR"] += "_tracy"
        config["ENGINE_BIN_DIR"] = "bin\\tracy"

    return config

def engine_scons(config: dict[str, str], vsproj: bool):
    
    engine_dir = ROOT_DIR / "godot"
    engine_proj = os.path.join(engine_dir, "godot.vcxproj")
    
    if vsproj and os.path.exists(engine_proj):
        print(f"    Godot Engine VS Project already exists. (Delete {engine_proj} if you need to force a refresh)")
        return

    num_processors = os.cpu_count() or 4
    
    # Build the SCons command based on configuration
    scons_cmd = [
        "scons",
        "platform=windows",
        "dev_build=yes",
        f"-j{num_processors}"
    ]
    
    if vsproj:
        scons_cmd.append("vsproj=yes")
    
    if config["ENABLE_TRACY"] == "ON":
        tracy_path = os.path.abspath(os.path.join(ROOT_DIR, "thirdparty", "tracy"))

        scons_cmd.append("profiler=tracy")
        scons_cmd.append(f"profiler_path={tracy_path}")

    print(f"    Running SCons: {' '.join(scons_cmd)} at {engine_dir}")

    try:
        result = subprocess.run(
            scons_cmd,
            cwd=engine_dir,
            shell=True
        )
        
        if result.returncode != 0:
            print("ERROR: SCons engine build failed.")
            sys.exit(result.returncode)
            
    except Exception as e:
        print(f"ERROR: Failed to launch SCons. {e}")
        sys.exit(1)

    # Only continue if we have build artifacts
    if vsproj:
        return
    
    print(f"    Post-Build: Moving build artifacts subfolder: {config["ENGINE_BIN_DIR"]}")
    
    files_moved = 0
    
    engine_bin_dir = engine_dir / "bin"
    engine_target_bin_dir = engine_dir / config["ENGINE_BIN_DIR"]
    
    os.makedirs(engine_target_bin_dir, exist_ok=True)
    
    if os.path.exists(engine_bin_dir):
        for item in os.listdir(engine_bin_dir):
            source_path = os.path.join(engine_bin_dir, item)
            
            # Skip folders
            if os.path.isdir(source_path):
                continue
                
            # Move files to target bin dir
            if os.path.isfile(source_path):
                destination_path = os.path.join(engine_target_bin_dir, item)
                
                # Clear out old matching files in the target subfolder before overwriting
                if os.path.exists(destination_path):
                    os.remove(destination_path)
                    
                shutil.move(source_path, destination_path)
                files_moved += 1
                
    if files_moved > 0:
        print(f"    Moved {files_moved} files.")
    else:
        print("ERROR: SCons finished, but no build files were found in godot/bin/")
        sys.exit(1)

def generate_engine_project(config: dict[str, str]):
    print("Generating Godot Engine VS Project...")
    engine_scons(config, True)
    
def build_engine(config: dict[str, str]):
    print("Building Godot Engine...")
    engine_scons(config, False)

def generate_solution(config: dict[str, str]):   
    print("Generating Master Solution with Extension and Godot Projects...")
    print(f"--- Configuring CMake (Tracy: {config['ENABLE_TRACY']}) ---")
    os.makedirs(config["BUILD_DIR"], exist_ok=True)
    
    cmd = [
        "cmake", "-B", config["BUILD_DIR"], 
        "-G", "Visual Studio 17 2022", "-A", "x64",
        f"-DENABLE_TRACY={config['ENABLE_TRACY']}",
        f"-DENGINE_BIN_DIR={config['ENGINE_BIN_DIR']}"
    ]
    print(f"    Running CMake: {' '.join(cmd)}")
    subprocess.run(cmd)
    
    print(f"The master solution is located at: {config['BUILD_DIR']}/godot_dev.sln")

def open_solution(config: dict[str, str]):
    print(f"--- Opening Solution in \\{config['BUILD_DIR']} ---")
    sln_path = ROOT_DIR / config["BUILD_DIR"] / "godot_dev.sln"
    os.startfile(sln_path)

if __name__ == "__main__":
    config = load_config()
    
    action = sys.argv[1] if len(sys.argv) > 1 else "generate"
    if action == "generate":
        generate_engine_project(config)
        generate_solution(config)
    elif action == "open":
        open_solution(config)
    elif action == "build_engine":
        build_engine(config)