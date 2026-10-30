---
name: computer-use
description: >-
  Automate Windows desktop GUI interactions: capture full-screen or window screenshots,
  inspect and focus running application windows, control the mouse (move, click, drag),
  and send keyboard input/shortcuts. Use when testing GUI applications, interacting with
  the desktop or external windows, capturing screenshots for visual validation, or performing
  desktop automation tasks.
---

# Computer Use — Windows Desktop & GUI Automation

This skill provides native Windows desktop automation tools for taking screenshots, inspecting window hierarchies, controlling mouse position and clicks, and sending keystrokes and hotkeys to any desktop application.

## Helper Scripts

All scripts are located in:
`C:\Users\Revo\.gemini\config\skills\computer-use\scripts\`

### 1. Capture Screenshots (`screenshot.ps1`)

Captures the interactive desktop or crops directly to a target application window:

```powershell
# Full desktop screenshot
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\screenshot.ps1" -Path "C:\path\to\capture.png"

# Specific window screenshot by title substring
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\screenshot.ps1" -WindowTitle "GM3D Studio" -Path "C:\path\to\studio.png"
```

After capturing, use the `read_file` tool to inspect the image.

---

### 2. Manage & Inspect Windows (`windows.ps1`)

Lists all visible desktop windows with their HWND, Process ID, coordinates, dimensions, and window titles, or focuses a target window:

```powershell
# List all visible desktop windows
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\windows.ps1" -Action list

# Filter list by title substring
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\windows.ps1" -Action list -Title "GameMaker"

# Bring a window to the foreground and focus it
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\windows.ps1" -Action focus -Title "GM3D Studio"
```

---

### 3. Mouse Control (`mouse.ps1`)

Moves the mouse, clicks (left, right, double), drags, or queries current cursor position:

```powershell
# Get current cursor coordinates
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\mouse.ps1" -Action getpos

# Move cursor
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\mouse.ps1" -Action move -X 500 -Y 300

# Left click at coordinates
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\mouse.ps1" -Action click -X 500 -Y 300

# Right click
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\mouse.ps1" -Action rightclick -X 500 -Y 300

# Double click
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\mouse.ps1" -Action doubleclick -X 500 -Y 300

# Drag and drop
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\mouse.ps1" -Action drag -X 200 -Y 200 -EndX 600 -EndY 400
```

---

### 4. Keyboard Input & Hotkeys (`keyboard.ps1`)

Types plain text or sends key combinations (using .NET SendKeys format):

```powershell
# Type plain text into active window
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\keyboard.ps1" -Text "MyScene"

# Focus target window and send hotkey (e.g. Ctrl+S to save)
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\keyboard.ps1" -TargetTitle "GM3D Studio" -Keys "^s"

# Send Play simulation toggle (P) or common hotkeys:
# Enter: "{ENTER}", Esc: "{ESC}", Tab: "{TAB}", Alt+F4: "%{F4}", F5: "{F5}"
powershell -ExecutionPolicy Bypass -File "C:\Users\Revo\.gemini\config\skills\computer-use\scripts\keyboard.ps1" -Keys "{ENTER}"
```

---

## Standard Automation Workflow

1. **Launch App**: Start the desktop application via `run_command` (e.g. `Start-Process "build\GM3DStudio.exe"`).
2. **Find & Focus Window**: Run `windows.ps1 -Action list` to locate its title and bounds, then `-Action focus`.
3. **Capture & Inspect**: Run `screenshot.ps1 -WindowTitle ...` to view the initial GUI state.
4. **Interact**: Use `mouse.ps1 -Action click -X ... -Y ...` or `keyboard.ps1` to trigger actions.
5. **Verify**: Take another screenshot to visually confirm that the action succeeded.
