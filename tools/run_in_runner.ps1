# Builds GM3Dext.dll and runs the 3DTEST project in the real GameMaker LTS Runner.
#
# The editor is not involved: Igor (the runtime's command-line builder, the same tool the IDE
# calls when you press F5) compiles the project to output\<project>\<project>.win and launches
# the Runner on it. That is the only way to check the extension inside the host process, where
# the adapter, window handling and shared-memory ABI are the real ones.
#
#   .\tools\run_in_runner.ps1                 # build the DLL, then build and run the game
#   .\tools\run_in_runner.ps1 -SkipBuild      # reuse build\GM3Dext.dll
#   .\tools\run_in_runner.ps1 -Wait           # block until the game window is closed
#
# Two things that are easy to get wrong and are handled here:
#   * The extension DLL is copied into the project before Igor builds, otherwise the build
#     packages whatever stale copy happened to be there.
#   * assets\textures is copied next to the built game. The engine looks for it in the game's
#     working directory; without it every CC0 material silently falls back to the procedural
#     textures, which is easy to mistake for a rendering bug.
param(
    [switch]$SkipBuild,
    [switch]$Wait,
    [string]$Project = "3DTEST\3DTEST.yyp"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Fail($message) { Write-Host "FAILED: $message" -ForegroundColor Red; exit 1 }

# 1. Extension DLL
if (-not $SkipBuild) {
    Write-Host "Building GM3Dext.dll ..."
    & cmd /c "`"$root\build.bat`" GM3Dext" | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "build.bat GM3Dext returned $LASTEXITCODE" }
}
$dll = Join-Path $root 'build\GM3Dext.dll'
if (-not (Test-Path $dll)) { Fail "missing $dll" }
Copy-Item $dll (Join-Path $root '3DTEST\extensions\GM3Dext\GM3Dext.dll') -Force

# 2. Runtime and the logged-in user folder Igor needs for its licence
$runtimeRoot = 'C:\ProgramData\GameMakerStudio2-LTS2026\Cache\runtimes'
$runtime = Get-ChildItem $runtimeRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'bin\igor\windows\x64\Igor.exe') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $runtime) { Fail "no GameMaker runtime with Igor.exe under $runtimeRoot" }
$igor = Join-Path $runtime.FullName 'bin\igor\windows\x64\Igor.exe'

$userRoot = Join-Path $env:APPDATA 'GameMakerStudio2-LTS2026'
$user = Get-ChildItem $userRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'licence.plist') } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $user) { Fail "no GameMaker user folder with licence.plist under $userRoot" }

# 3. CC0 textures next to the built game (the engine resolves them from the working directory)
$projectPath = Join-Path $root $Project
$outDir = Join-Path $root ("output\" + [System.IO.Path]::GetFileNameWithoutExtension($Project))
$assetSource = Join-Path $root 'assets\textures'
if (Test-Path $assetSource) {
    $assetTarget = Join-Path $outDir 'assets\textures'
    New-Item -ItemType Directory -Force -Path $assetTarget | Out-Null
    Copy-Item (Join-Path $assetSource '*') $assetTarget -Recurse -Force
} else {
    Write-Host "warning: assets\textures is missing, the game will use procedural textures" -ForegroundColor Yellow
}

# 4. Build and launch through Igor (same path the IDE uses for F5)
$existing = Get-Process Runner -ErrorAction SilentlyContinue
if ($existing) {
    Fail "a GameMaker game is already running (PID $($existing.Id)); close it first so the log file and the DLL copy are not taken"
}

$log = Join-Path $root 'build\igor.log'
$errLog = Join-Path $root 'build\igor.err'
Remove-Item $log, $errLog -ErrorAction SilentlyContinue
Write-Host "Runtime: $($runtime.Name)"
Write-Host "Building and running $Project ..."

$arguments = @(
    "-project=`"$projectPath`"",
    "-user=`"$($user.FullName)`"",
    "-runtime=VM",
    "-rp=`"$($runtime.FullName)`"",
    "--", "Windows", "Run"
)
$process = Start-Process -FilePath $igor -ArgumentList $arguments `
    -RedirectStandardOutput $log -RedirectStandardError $errLog -PassThru

Write-Host "Igor started (PID $($process.Id)); the engine log is build\igor.log" -ForegroundColor Green
Write-Host "Take a window screenshot with:"
Write-Host "  powershell -File .agents\skills\antigravity-computer-use\scripts\screenshot.ps1 -WindowTitle `"GM3D Island`" -Path build\runner.png"

if ($Wait) {
    Write-Host "Waiting for the game to exit ..."
    $process.WaitForExit()
    Write-Host "Igor exited with code $($process.ExitCode). Last lines:"
    if (Test-Path $log) { Get-Content $log -Tail 15 }
}
