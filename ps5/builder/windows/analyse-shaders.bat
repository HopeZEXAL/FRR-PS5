@echo off
rem How the shaders in a working shaders.pack relate to those in the game files.
rem   analyse-shaders.bat OLD\shaders.pack OUTPUT\frr-ps5\game
"%~dp0python\python.exe" "%~dp0builder\scripts\analyse_shaders.py" %1 %2
pause
