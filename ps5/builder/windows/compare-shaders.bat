@echo off
rem Compares two shaders.pack files (counts and missing shaders only).
rem   compare-shaders.bat OLD\shaders.pack NEW\shaders.pack
"%~dp0python\python.exe" "%~dp0builder\scripts\compare_packs.py" %1 %2
pause
