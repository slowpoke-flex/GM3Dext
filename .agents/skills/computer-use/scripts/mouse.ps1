<#
.SYNOPSIS
    Controls the mouse cursor on the interactive Windows desktop.
.PARAMETER Action
    click (default), doubleclick, move, rightclick, drag, or getpos.
.PARAMETER X
    X coordinate on the screen.
.PARAMETER Y
    Y coordinate on the screen.
.PARAMETER EndX
    Destination X for drag operation.
.PARAMETER EndY
    Destination Y for drag operation.
#>
param(
    [ValidateSet("click", "doubleclick", "move", "rightclick", "drag", "getpos")]
    [string]$Action = "click",
    [int]$X = -1,
    [int]$Y = -1,
    [int]$EndX = -1,
    [int]$EndY = -1
)

$csharpCode = @"
using System;
using System.Threading;
using System.Runtime.InteropServices;

public class MouseControllerHelper {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X; public int Y; }

    [DllImport("user32.dll")]
    public static extern IntPtr OpenDesktop(string lpszDesktop, uint dwFlags, bool fInherit, uint dwDesiredAccess);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetThreadDesktop(IntPtr hDesktop);

    [DllImport("user32.dll")]
    public static extern bool CloseDesktop(IntPtr hDesktop);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);

    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);

    private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    private const uint MOUSEEVENTF_LEFTUP = 0x0004;
    private const uint MOUSEEVENTF_RIGHTDOWN = 0x0008;
    private const uint MOUSEEVENTF_RIGHTUP = 0x0010;

    public static POINT GetPosition() {
        POINT p = new POINT();
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault != IntPtr.Zero) {
            Thread t = new Thread(() => {
                SetThreadDesktop(hDefault);
                GetCursorPos(out p);
            });
            t.Start();
            t.Join();
            CloseDesktop(hDefault);
        }
        return p;
    }

    public static void Move(int x, int y) {
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault != IntPtr.Zero) {
            Thread t = new Thread(() => {
                SetThreadDesktop(hDefault);
                SetCursorPos(x, y);
            });
            t.Start();
            t.Join();
            CloseDesktop(hDefault);
        }
    }

    public static void Click(int x, int y, bool isRight, bool isDouble) {
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault != IntPtr.Zero) {
            Thread t = new Thread(() => {
                SetThreadDesktop(hDefault);
                if (x >= 0 && y >= 0) SetCursorPos(x, y);
                Thread.Sleep(30);
                if (isRight) {
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, UIntPtr.Zero);
                    Thread.Sleep(20);
                    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, UIntPtr.Zero);
                } else {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
                    Thread.Sleep(20);
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
                    if (isDouble) {
                        Thread.Sleep(100);
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
                        Thread.Sleep(20);
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
                    }
                }
            });
            t.Start();
            t.Join();
            CloseDesktop(hDefault);
        }
    }

    public static void Drag(int startX, int startY, int endX, int endY) {
        IntPtr hDefault = OpenDesktop("default", 0, false, 0x01FF);
        if (hDefault != IntPtr.Zero) {
            Thread t = new Thread(() => {
                SetThreadDesktop(hDefault);
                SetCursorPos(startX, startY);
                Thread.Sleep(50);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
                Thread.Sleep(50);

                int steps = 20;
                for (int i = 1; i <= steps; i++) {
                    int cx = startX + ((endX - startX) * i / steps);
                    int cy = startY + ((endY - startY) * i / steps);
                    SetCursorPos(cx, cy);
                    Thread.Sleep(15);
                }
                Thread.Sleep(50);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
            });
            t.Start();
            t.Join();
            CloseDesktop(hDefault);
        }
    }
}
"@

Add-Type -TypeDefinition $csharpCode -ErrorAction SilentlyContinue

switch ($Action) {
    "getpos" {
        $pt = [MouseControllerHelper]::GetPosition()
        Write-Output "Cursor Position: X=$($pt.X), Y=$($pt.Y)"
    }
    "move" {
        if ($X -lt 0 -or $Y -lt 0) { Write-Error "Provide -X and -Y"; exit 1 }
        [MouseControllerHelper]::Move($X, $Y)
        Write-Output "Moved cursor to ($X, $Y)"
    }
    "click" {
        [MouseControllerHelper]::Click($X, $Y, $false, $false)
        Write-Output "Clicked at ($X, $Y)"
    }
    "rightclick" {
        [MouseControllerHelper]::Click($X, $Y, $true, $false)
        Write-Output "Right-clicked at ($X, $Y)"
    }
    "doubleclick" {
        [MouseControllerHelper]::Click($X, $Y, $false, $true)
        Write-Output "Double-clicked at ($X, $Y)"
    }
    "drag" {
        if ($X -lt 0 -or $Y -lt 0 -or $EndX -lt 0 -or $EndY -lt 0) {
            Write-Error "Provide -X, -Y, -EndX, and -EndY for drag"; exit 1
        }
        [MouseControllerHelper]::Drag($X, $Y, $EndX, $EndY)
        Write-Output "Dragged from ($X, $Y) to ($EndX, $EndY)"
    }
}
