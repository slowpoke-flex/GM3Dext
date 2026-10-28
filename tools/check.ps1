[CmdletBinding()]
param(
    [string] $BuildDirectory = "",
    [switch] $Graphics
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build' }
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }
$vsRoot = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vsRoot) { throw 'Visual Studio C++ build tools were not found.' }
$vcvarsPath = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$cmakePath = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestPath = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninjaPath = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
foreach ($toolPath in @($vcvarsPath, $cmakePath, $ctestPath, $ninjaPath)) {
    if (-not (Test-Path -LiteralPath $toolPath)) { throw "Required tool not found: $toolPath" }
}
# cmd is used only to inherit the compiler environment from vcvarsall.
$command = 'call "{0}" x64 >nul && "{1}" -S "{2}" -B "{3}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="{4}" && "{1}" --build "{3}" && "{5}" --test-dir "{3}" --output-on-failure' -f $vcvarsPath, $cmakePath, $projectRoot, $BuildDirectory, $ninjaPath, $ctestPath
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Graphics) {
    $terrainBuild = 'call "{0}" x64 >nul && "{1}" --build "{2}" --target GM3DTerrainCheck' -f $vcvarsPath, $cmakePath, $BuildDirectory
    & $env:ComSpec /d /s /c $terrainBuild
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $BuildDirectory 'GM3DTerrainCheck.exe')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $BuildDirectory 'GM3DGraphicsSmoke.exe')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
