<#
.SYNOPSIS
    Lists, activates, or minimizes windows on the interactive Windows desktop.
.PARAMETER Action
    list (default), focus, or minimize.
.PARAMETER Title
    Window title or substring to filter or target.
#>
param(
    [ValidateSet("list", "focus", "minimize")]
    [string]$Action = "list",
    [string]$Title = ""
)

$csharpCode = @"
using System;
using System.Threading;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class WindowManagerHelper {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern IntPtr OpenDesktop(string lpszDesktop, uint dwFlags, bool fInherit, uint dwDesiredAccess);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetThreadDesktop(IntPtr hDesktop);

    [DllImport("user32.dll")]
    public static extern bool CloseDesktop(IntPtr hDesktop);

    [DllImport("user32.dll")]
    public static extern bool EnumDesktopWindows(IntPtr hDesktop, EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    public class WinInfo {
        public IntPtr Hwnd;
        public uint Pid;
        public string Title;
        public int Left, Top, Width, Height;
    }

    public static List<WinInfo> GetWindows(string filter) {
        var list = new List<WinInfo>();
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault == IntPtr.Zero) return list;

        Thread t = new Thread(() => {
            if (!SetThreadDesktop(hDefault)) return;
            EnumDesktopWindows(hDefault, (hWnd, lParam) => {
                if (IsWindowVisible(hWnd)) {
                    StringBuilder sb = new StringBuilder(512);
                    GetWindowText(hWnd, sb, 512);
                    string title = sb.ToString();
                    if (!string.IsNullOrWhiteSpace(title)) {
                        if (string.IsNullOrEmpty(filter) || title.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0) {
                            RECT r;
                            GetWindowRect(hWnd, out r);
                            uint pid;
                            GetWindowThreadProcessId(hWnd, out pid);
                            list.Add(new WinInfo {
                                Hwnd = hWnd,
                                Pid = pid,
                                Title = title,
                                Left = r.Left,
                                Top = r.Top,
                                Width = r.Right - r.Left,
                                Height = r.Bottom - r.Top
                            });
                        }
                    }
                }
                return true;
            }, IntPtr.Zero);
        });
        t.Start();
        t.Join();
        CloseDesktop(hDefault);
        return list;
    }

    public static bool FocusWindow(string title) {
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault == IntPtr.Zero) return false;
        bool ok = false;
        Thread t = new Thread(() => {
            if (!SetThreadDesktop(hDefault)) return;
            EnumDesktopWindows(hDefault, (hWnd, lParam) => {
                if (IsWindowVisible(hWnd)) {
                    StringBuilder sb = new StringBuilder(512);
                    GetWindowText(hWnd, sb, 512);
                    if (sb.ToString().IndexOf(title, StringComparison.OrdinalIgnoreCase) >= 0) {
                        ShowWindow(hWnd, 9); // SW_RESTORE
                        SetForegroundWindow(hWnd);
                        ok = true;
                        return false;
                    }
                }
                return true;
            }, IntPtr.Zero);
        });
        t.Start();
        t.Join();
        CloseDesktop(hDefault);
        return ok;
    }
}
"@

Add-Type -TypeDefinition $csharpCode -ErrorAction SilentlyContinue

switch ($Action) {
    "list" {
        $wins = [WindowManagerHelper]::GetWindows($Title)
        $wins | ForEach-Object {
            [PSCustomObject]@{
                HWND   = "0x{0:X}" -f $_.Hwnd.ToInt64()
                PID    = $_.Pid
                Bounds = "$($_.Left),$($_.Top) ($($_.Width)x$($_.Height))"
                Title  = $_.Title
            }
        } | Format-Table -AutoSize
    }
    "focus" {
        if ([string]::IsNullOrEmpty($Title)) {
            Write-Error "Please provide -Title to focus a window."
            exit 1
        }
        $res = [WindowManagerHelper]::FocusWindow($Title)
        if ($res) {
            Write-Output "SUCCESS: Window matching '$Title' focused."
        } else {
            Write-Error "Window matching '$Title' not found."
            exit 1
        }
    }
}
