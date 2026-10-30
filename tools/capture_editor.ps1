param(
    [string]$ScreenshotPath = "C:\GM3Dext\build\editor_ui.png",
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'

$csharp = @"
using System;
using System.Runtime.InteropServices;

public class DesktopLauncher {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION {
        public IntPtr hProcess, hThread;
        public int dwProcessId, dwThreadId;
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool CreateProcessW(
        string lpApplicationName,
        string lpCommandLine,
        IntPtr lpProcessAttributes,
        IntPtr lpThreadAttributes,
        bool bInheritHandles,
        uint dwCreationFlags,
        IntPtr lpEnvironment,
        string lpCurrentDirectory,
        ref STARTUPINFO lpStartupInfo,
        out PROCESS_INFORMATION lpProcessInformation
    );

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    public static int Launch(string exePath, string args, string desktopName) {
        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(si);
        si.lpDesktop = desktopName;

        PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
        string cmdLine = "\"" + exePath + "\" " + args;

        bool success = CreateProcessW(null, cmdLine, IntPtr.Zero, IntPtr.Zero, false, 0x00000200, IntPtr.Zero, "C:\\GM3Dext", ref si, out pi);
        if (!success) {
            int err = Marshal.GetLastWin32Error();
            throw new System.ComponentModel.Win32Exception(err);
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pi.dwProcessId;
    }
}
"@

Add-Type -TypeDefinition $csharp

$logPath = "C:\GM3Dext\build\studio.log"
if (Test-Path $logPath) { Remove-Item $logPath -Force }

$studioPid = [DesktopLauncher]::Launch("C:\GM3Dext\build\GM3DStudio.exe", "--log `"$logPath`"", "winsta0\default")
Write-Output "Launched GM3DStudio on winsta0\default (PID: $studioPid). Waiting 4 seconds for window to render..."

Start-Sleep -Seconds 4
Write-Output "Editor is running. Capturing screenshot..."

# List windows
Write-Output "--- Desktop Windows ---"
& powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\windows.ps1"

# Capture screenshot
Write-Output "--- Capturing Screenshot ---"
& powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\screenshot.ps1" -Path $ScreenshotPath

if (-not $KeepRunning) {
    Write-Output "Stopping test instance PID $studioPid..."
    Stop-Process -Id $studioPid -Force -ErrorAction SilentlyContinue
} else {
    Write-Output "Leaving instance PID $studioPid running."
}
