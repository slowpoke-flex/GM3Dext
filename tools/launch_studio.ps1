param(
    [switch]$CheckOnly
)

$logPath = "C:\GM3Dext\build\studio.log"
if (Test-Path $logPath) { Remove-Item $logPath -Force }

$proc = Start-Process -FilePath "C:\GM3Dext\build\GM3DStudio.exe" -ArgumentList @("--log", $logPath) -PassThru
Start-Sleep -Seconds 2

if ($proc.HasExited) {
    Write-Output "Studio exited immediately with code: $($proc.ExitCode)"
} else {
    Write-Output "Studio is running with PID: $($proc.Id)"
}

if (Test-Path $logPath) {
    Write-Output "--- Log Content ---"
    Get-Content $logPath
}
