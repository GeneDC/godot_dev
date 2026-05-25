@echo off
setlocal enabledelayedexpansion

echo === Godot Dev Environment Setup ===
@python scripts/generate_project.py generate && pause
pause