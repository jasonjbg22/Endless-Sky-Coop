@echo off
setlocal
cd /d "%~dp0"

if not exist "Endless Sky.exe" (
	echo Endless Sky.exe was not found in this folder.
	echo Make sure this file stays next to the game executable.
	pause
	exit /b 1
)

start "" "Endless Sky.exe"
