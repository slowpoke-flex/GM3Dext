$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

$cmd = 'call "{0}" x64 >nul && "{1}" --build build --target GM3DStudio' -f $vcvars, $cmake
& $env:ComSpec /d /s /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "GM3DStudio built successfully."
