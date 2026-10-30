param()
$ErrorActionPreference = 'Stop'

$logPath = "C:\GM3Dext\build\studio.log"
if (Test-Path $logPath) { Remove-Item $logPath -Force }

$proc = Start-Process -FilePath "C:\GM3Dext\build\GM3DStudio.exe" -ArgumentList @("--log", $logPath) -PassThru

Write-Output "Started Studio with PID $($proc.Id). Waiting for window..."
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 500
    if ($proc.HasExited) {
        Write-Output "Process exited prematurely with code $($proc.ExitCode)!"
        break
    }
    if (Test-Path $logPath) {
        $content = Get-Content $logPath -Raw
        if ($content -match "complete, m_running=true" -or $content -match "Entering app.run") {
            Write-Output "Studio initialized and running main loop!"
            break
        }
    }
}

if (Test-Path $logPath) {
    Write-Output "--- Studio Log ---"
    Get-Content $logPath -Tail 25
}

# Take screenshot
$screenshotPath = "C:\GM3Dext\build\studio_screen.png"
& powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\screenshot.ps1" -Path $screenshotPath

# Check window list
& powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\windows.ps1"

# Leave running or kill for test
Write-Output "Test script done. Killing test instance..."
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
