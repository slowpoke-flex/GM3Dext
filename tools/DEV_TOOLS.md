# GM3D developer checks

Run from PowerShell:

```powershell
.\tools\check.ps1
.\tools\check.ps1 -Graphics
```

The script configures and builds the local release build and runs CTest. `-Graphics` additionally compares a regional terrain texture update against a full rebuild on a Direct3D WARP device, then runs two Direct3D initialization, render and shutdown cycles using a hidden window. The latter requires a working Windows graphics driver. It does not copy the extension into a GameMaker project.

Standalone checks after building:

```powershell
.\build\GM3DCheck.exe scene "C:\path\scene.json"
.\build\GM3DCheck.exe project "C:\path\project.gm3dproject"
.\build\GM3DCheck.exe export "C:\path\scene.json" "C:\path\export-folder"
.\build\GM3DShaderCheck.exe "C:\GM3Dext"
.\build\GM3DGraphicsSmoke.exe
.\build\GM3DTerrainCheck.exe  # built by check.ps1 -Graphics
```

`GM3DCheck` reports missing resources, duplicate or invalid IDs, unsupported collider settings and other scene issues. Exit code 0 means no errors (warnings may still be present); 1 means validation or parsing failed; 2 means invalid command arguments. Project checks also validate the last scene if one is configured. Shader checks compile embedded HLSL without launching the editor or creating a graphics device.

The `export` command invokes the same GameMaker exporter as the editor. It can replace an existing scene and loader in the destination folder without a prompt; use a dedicated test folder. It does not generate terrain that is absent from the saved scene.

To capture editor startup and error output in a release build:

```powershell
.\build\GM3DStudio.exe --log "C:\path\gm3d-studio.log"
```

The log is appended and may contain paths to imported resources. It is created only when `--log` is explicitly supplied.

## Terrain heightmaps

The Terrain panel imports and exports grayscale PFM (`Pf`) files with square dimensions from 32 to 512 pixels. Values are 32-bit floating-point world heights; the PFM scale is applied on import. Export uses little-endian floats and includes the generated heightfield even if it has not been sculpted. Import replaces the terrain resolution and heights as one undoable action. Color PFM and PNG heightmaps are not supported.

The GameMaker exporter also includes terrain heights, paint weights and four layer settings in the scene JSON. Its generated loader uses the additive `terrain_apply_edit_data` extension function after `terrain_generate`; it expects the current DLL and matching extension definitions. The packed, little-endian buffer starts with three u32 counts (height floats, weight bytes, layers), followed by f32 heights, RGBA8 weights and five f32 values per layer (RGB, roughness, scale). Counts must be zero or match the terrain resolution, and the layer count must be zero or four. The function returns the updated terrain collider ID or a negative error code. It validates the buffer before changing terrain; as with other native pointer APIs, the caller is responsible for passing a valid buffer address and length.

`GM3DGraphicsSmoke` exercises terrain data restoration through the exported DLL, including a physics raycast and rejection of truncated input. This is not a substitute for compiling and running an exported project in GameMaker LTS.
