@echo off
cd /d "%~dp0"
if not exist "build\GM3DStudio.exe" (
    echo [INFO] build\GM3DStudio.exe not found. Compiling project first...
    call build.bat
)
if exist "build\GM3DStudio.exe" (
    start "" "build\GM3DStudio.exe"
) else (
    echo [ERROR] Build failed! Please inspect the compiler output above.
    pause
)
