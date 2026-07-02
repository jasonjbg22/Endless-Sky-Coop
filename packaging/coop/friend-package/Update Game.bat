@echo off
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File ".\Update Game.ps1"
if errorlevel 1 (
	echo.
	echo Update failed. You can still play the current build.
	pause
	exit /b 1
)

echo.
echo Update complete.
pause
