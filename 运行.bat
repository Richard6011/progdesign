@echo off
setlocal
cd /d "%~dp0"
if not exist "build\debug\NoteCollision.exe" powershell -NoProfile -ExecutionPolicy Bypass -File tools\Development.ps1 -Action Build
if not exist "build\debug\NoteCollision.exe" exit /b 1
set "PATH=%~dp0.local\qt\bin;%~dp0.local\mingw\bin;%PATH%"
set "QT_PLUGIN_PATH=%~dp0.local\qt\plugins"
start "" "build\debug\NoteCollision.exe"
