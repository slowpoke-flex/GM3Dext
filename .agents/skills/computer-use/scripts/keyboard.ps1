<#
.SYNOPSIS
    Sends keystrokes or text to the active window or a target window on the interactive desktop.
.PARAMETER Text
    Plain text to type.
.PARAMETER Keys
    SendKeys format keys (e.g. "{ENTER}", "{ESC}", "^s" for Ctrl+S, "%{F4}" for Alt+F4, "{F5}").
.PARAMETER TargetTitle
    Optional window title to focus before sending keys.
#>
param(
    [string]$Text = "",
    [string]$Keys = "",
    [string]$TargetTitle = ""
)

$csharpCode = @"
using System;
using System.Threading;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public class KeyboardControllerHelper {
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
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public static bool Send(string targetTitle, string keys, string plainText) {
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault == IntPtr.Zero) return false;

        bool result = false;
        Thread t = new Thread(() => {
            if (!SetThreadDesktop(hDefault)) return;

            if (!string.IsNullOrEmpty(targetTitle)) {
                IntPtr targetHwnd = IntPtr.Zero;
                EnumDesktopWindows(hDefault, (hWnd, lParam) => {
                    if (IsWindowVisible(hWnd)) {
                        StringBuilder sb = new StringBuilder(256);
                        GetWindowText(hWnd, sb, 256);
                        if (sb.ToString().IndexOf(targetTitle, StringComparison.OrdinalIgnoreCase) >= 0) {
                            targetHwnd = hWnd;
                            return false;
                        }
                    }
                    return true;
                }, IntPtr.Zero);

                if (targetHwnd != IntPtr.Zero) {
                    ShowWindow(targetHwnd, 9); // SW_RESTORE
                    SetForegroundWindow(targetHwnd);
                    Thread.Sleep(100);
                }
            }

            if (!string.IsNullOrEmpty(keys)) {
                SendKeys.SendWait(keys);
                result = true;
            } else if (!string.IsNullOrEmpty(plainText)) {
                // Escape SendKeys special characters: + ^ % ~ { } [ ] ( )
                string escaped = "";
                foreach (char c in plainText) {
                    if ("+^%~{}[]()".IndexOf(c) >= 0) {
                        escaped += "{" + c + "}";
                    } else {
                        escaped += c;
                    }
                }
                SendKeys.SendWait(escaped);
                result = true;
            }
        });
        t.Start();
        t.Join();
        CloseDesktop(hDefault);
        return result;
    }
}
"@

Add-Type -TypeDefinition $csharpCode -ReferencedAssemblies System.Windows.Forms -ErrorAction SilentlyContinue

if ([string]::IsNullOrEmpty($Text) -and [string]::IsNullOrEmpty($Keys)) {
    Write-Error "Please provide -Text or -Keys."
    exit 1
}

$ok = [KeyboardControllerHelper]::Send($TargetTitle, $Keys, $Text)
if ($ok) {
    Write-Output "SUCCESS: Sent input."
} else {
    Write-Error "Failed to send input."
    exit 1
}
