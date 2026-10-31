@echo off
set VSLANG=1033
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist "build\build.ninja" (
    echo [INFO] Configuring CMake with Ninja Release...
    cmake.exe -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
)
ninja.exe -C build %*
