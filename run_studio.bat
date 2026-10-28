@echo off
cd /d "%~dp0"
if exist "build\GM3DStudio.exe" (
    start "" "build\GM3DStudio.exe"
) else (
    echo [ERROR] build\GM3DStudio.exe not found! Please compile the project first.
    pause
)
