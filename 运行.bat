@echo off
cd /d "%~dp0"
if not exist build\debug\NoteCollision.exe powershell -NoProfile -ExecutionPolicy Bypass -File tools\Development.ps1 -Action Build
if exist build\debug\NoteCollision.exe start "" build\debug\NoteCollision.exe
