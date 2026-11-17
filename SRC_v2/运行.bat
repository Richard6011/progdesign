@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist NoteCollisionV2.exe powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" -Action Build
if exist NoteCollisionV2.exe start "" "%~dp0NoteCollisionV2.exe"
