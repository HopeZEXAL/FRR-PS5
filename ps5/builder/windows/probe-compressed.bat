@echo off
rem The layout of the game's compressed files (sizes and header words only).
rem   probe-compressed.bat OUTPUT\frr-ps5\game
"%~dp0python\python.exe" "%~dp0builder\scripts\probe_compressed.py" %1
pause
