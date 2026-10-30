param(
    [string]$LogPath = "C:\GM3Dext\build\studio.log"
)

$csharp = @"
using System;
using System.Runtime.InteropServices;

public class ProcessDesktopLauncher {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX;
        public int dwY;
        public int dwXSize;
        public int dwYSize;
        public int dwXCountChars;
        public int dwYCountChars;
        public int dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
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

    public static int LaunchOnDesktop(string exePath, string args, string desktopName) {
        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(si);
        si.lpDesktop = desktopName; // e.g. @"winsta0\default"

        PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
        string cmdLine = "\"" + exePath + "\" " + args;

        // Try with CREATE_BREAKAWAY_FROM_JOB (0x01000000) so process survives parent termination
        uint flags = 0x01000000 | 0x00000200;
        bool success = CreateProcessW(null, cmdLine, IntPtr.Zero, IntPtr.Zero, false, flags, IntPtr.Zero, null, ref si, out pi);
        if (!success) {
            // Fallback without breakaway
            flags = 0x00000200;
            success = CreateProcessW(null, cmdLine, IntPtr.Zero, IntPtr.Zero, false, flags, IntPtr.Zero, null, ref si, out pi);
            if (!success) {
                int err = Marshal.GetLastWin32Error();
                throw new System.ComponentModel.Win32Exception(err);
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return -pi.dwProcessId; // Negative indicates non-breakaway
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pi.dwProcessId; // Positive indicates breakaway succeeded!
    }
}
"@

Add-Type -TypeDefinition $csharp

if (Test-Path $LogPath) { Remove-Item $LogPath -Force }

$pidResult = [ProcessDesktopLauncher]::LaunchOnDesktop("C:\GM3Dext\build\GM3DStudio.exe", "--log `"$LogPath`"", "winsta0\default")
Write-Output "Launched GM3DStudio on winsta0\default with PID $pidResult"

Start-Sleep -Seconds 3
if (Test-Path $LogPath) {
    Write-Output "--- Studio Log ---"
    Get-Content $LogPath -Tail 15
}
