<#
.SYNOPSIS
    Captures a screenshot of the real interactive Windows desktop (or a specific window) and saves it to PNG.
.PARAMETER Path
    Destination PNG path. Default: $env:TEMP\screenshot_<timestamp>.png.
.PARAMETER WindowTitle
    Optional substring to search for a specific window title and crop to that window.
#>
param(
    [string]$Path = "",
    [string]$WindowTitle = ""
)

if ([string]::IsNullOrWhiteSpace($Path)) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $Path = Join-Path $env:TEMP "screenshot_$timestamp.png"
}

$dir = [System.IO.Path]::GetDirectoryName($Path)
if (![string]::IsNullOrEmpty($dir) -and !(Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

$csharpCode = @"
using System;
using System.Threading;
using System.Text;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;

public class DesktopScreenCapture {
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
    public static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateCompatibleDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int nWidth, int nHeight);

    [DllImport("gdi32.dll")]
    public static extern IntPtr SelectObject(IntPtr hdc, IntPtr hgdiobj);

    [DllImport("gdi32.dll", SetLastError = true)]
    public static extern bool BitBlt(IntPtr hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, IntPtr hdcSrc, int nXSrc, int nYSrc, int dwRop);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteObject(IntPtr hObject);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int nIndex);

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

    public static bool Capture(string path, string targetTitle) {
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault == IntPtr.Zero) return false;

        bool result = false;
        Thread t = new Thread(() => {
            if (!SetThreadDesktop(hDefault)) return;

            int srcX = 0;
            int srcY = 0;
            int width = GetSystemMetrics(0); // SM_CXSCREEN
            int height = GetSystemMetrics(1); // SM_CYSCREEN

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
                    SetForegroundWindow(targetHwnd);
                    Thread.Sleep(100);
                    RECT r;
                    if (GetWindowRect(targetHwnd, out r)) {
                        int w = r.Right - r.Left;
                        int h = r.Bottom - r.Top;
                        if (w > 0 && h > 0) {
                            srcX = r.Left;
                            srcY = r.Top;
                            width = w;
                            height = h;
                        }
                    }
                }
            }

            IntPtr hdcSrc = GetDC(IntPtr.Zero);
            if (hdcSrc == IntPtr.Zero) return;

            IntPtr hdcDest = CreateCompatibleDC(hdcSrc);
            IntPtr hBitmap = CreateCompatibleBitmap(hdcSrc, width, height);
            IntPtr hOld = SelectObject(hdcDest, hBitmap);

            bool ok = BitBlt(hdcDest, 0, 0, width, height, hdcSrc, srcX, srcY, 0x00CC0020);

            SelectObject(hdcDest, hOld);
            DeleteDC(hdcDest);
            ReleaseDC(IntPtr.Zero, hdcSrc);

            if (ok) {
                using (Bitmap bmp = Image.FromHbitmap(hBitmap)) {
                    bmp.Save(path, ImageFormat.Png);
                }
                result = true;
            }
            DeleteObject(hBitmap);
        });

        t.Start();
        t.Join();
        CloseDesktop(hDefault);
        return result;
    }
}
"@

Add-Type -TypeDefinition $csharpCode -ReferencedAssemblies System.Drawing -ErrorAction SilentlyContinue

$ok = [DesktopScreenCapture]::Capture($Path, $WindowTitle)
if ($ok) {
    Write-Output "SUCCESS: Screenshot saved to $Path"
} else {
    Write-Error "Failed to capture screenshot."
    exit 1
}
