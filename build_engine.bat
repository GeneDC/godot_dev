@echo off
setlocal enabledelayedexpansion

@python scripts/generate_project.py build_engine && pause
pause