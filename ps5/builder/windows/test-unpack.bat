@echo off
rem Unpacks the game's compressed files into a temporary folder, then checks
rem which shaders of a working shaders.pack are found (counts only).
rem   test-unpack.bat OLD\shaders.pack OUTPUT\frr-ps5\game
set UNPACKED=%TEMP%\frr-unpacked
if exist "%UNPACKED%" rmdir /s /q "%UNPACKED%"
"%~dp0builder\tools\frr_prep.exe" unpack %2 "%UNPACKED%" | findstr /v PROGRESS
"%~dp0python\python.exe" "%~dp0builder\scripts\analyse_shaders.py" %1 %2 "%UNPACKED%"
rmdir /s /q "%UNPACKED%"
pause
