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
.\build\MeshColliderTests.exe
.\build\GM3DTerrainCheck.exe  # built by check.ps1 -Graphics
```

`GM3DCheck` reports missing resources, duplicate or invalid IDs, unsupported collider settings and other scene issues. Exit code 0 means no errors (warnings may still be present); 1 means validation or parsing failed; 2 means invalid command arguments. Project checks also validate the last scene if one is configured. Shader checks compile embedded HLSL without launching the editor or creating a graphics device.

The `export` command invokes the same GameMaker exporter as the editor. It can replace an existing scene and loader in the destination folder without a prompt; use a dedicated test folder. It does not generate terrain that is absent from the saved scene.

To capture editor startup and error output in a release build:

```powershell
.\build\GM3DStudio.exe --log "C:\path\gm3d-studio.log"
```

The log is appended and may contain paths to imported resources. It is created only when `--log` is explicitly supplied.

## Texture assets

The photographic materials live in `assets/textures/` (CC0, see `assets/textures/CREDITS.md`). They are fetched and downscaled to 512 px by:

```powershell
python tools\fetch_textures.py            # only missing sets
python tools\fetch_textures.py --force    # re-extract everything
```

The script needs `urllib` and Pillow, caches the source zips in `scratch/texture_cache/` (gitignored) and rewrites `CREDITS.md`. It downloads `_Color`, `_NormalGL` and `_Roughness` only; AO and displacement are dropped because the shader does not sample them.

All three are used: albedo and normal by the material shader, roughness by the terrain detail blend (`DetailGrassR..DetailCobbleR`, t12..t15). The engine logs how many sets came with a roughness map:

At runtime `TextureManager::find_asset_dir` resolves the folder in this order:

1. `%GM3D_ASSET_DIR%` (absolute override, useful for a packaged game)
2. `<folder of the exe>/assets/textures`
3. `<folder of the exe>/../assets/textures` and `../../assets/textures` (a dev build under `build/`)
4. `<working directory>/assets/textures`

CMake copies the folder next to `GM3DStudio.exe` after every build. If no folder is found the engine logs one line and falls back to the built-in procedural textures, so `GM3Dext.dll` still runs inside a GameMaker project without any assets.

## Scene capture and startup automation

The editor can be driven from the command line, which is what most automated checks use:

```powershell
.\build\GM3DStudio.exe --scene "C:\path\scene.json" --screenshot "build\shot.png" --frames 40 --log "build\shot.log"
```

| Flag | Effect |
|:---|:---|
| `--scene <path>` | Opens a scene at startup instead of the default Untitled world |
| `--screenshot <png>` | Saves the whole window (viewport plus UI) to a PNG through WIC, then exits |
| `--frames <n>` | How many frames to render first; default 30. Raise it when terrain or grass must finish building |
| `--grass-demo` | Enables the terrain, generates the procedural grass field and parks the camera in the densest meadow |
| `--log <path>` | Appends stdout/stderr to a file; required to see any console output in a release build |
| `--frame-profile` | Splits every frame into ui / sync / scene / imgui / present and logs the average once per second |
| `--camera px,py,pz,tx,ty,tz` | Sets the camera after the scene is built, for reproducible captures |
| `--no-grass` / `--no-shadows` | A/B switches that really disable the layer (they also clear the scene flag) |
| `--simulate <n>` | Starts the Jolt preview at launch, runs `n` frames headless, prints every body's resting transform and exits |

`--simulate` is how collider changes get verified without a screenshot: the `[physics]` line reports how many mesh colliders were built as triangle meshes, convex hulls or box fallbacks, and the `[simulate]` lines report where each body came to rest.

```powershell
.\build\GM3DStudio.exe --scene tests\fixtures\meshcollider.json --simulate 200 --log build\sim.log
```

The capture is taken from the swap chain before `Present`, so it matches what the user sees. `--screenshot` implies an exit, which makes it safe to call from a script: the process finishes on its own.

## GML SDK sync

`gml_example/scr_gm3d.gml` is a byte-identical copy of `3DTEST/scripts/scr_gm3d/scr_gm3d.gml`:

```powershell
.\tools\sync_gml_sdk.ps1          # copy 3DTEST -> gml_example
.\tools\sync_gml_sdk.ps1 -Check   # exit 1 when they differ
```

`tools\check.ps1` runs the `-Check` form after CTest, so an edit to the SDK that is not mirrored into the example fails the check.

## Performance benchmark

`GM3DStudio.exe` has a built-in benchmark that removes vsync and measures the engine step (scene render submit) plus the wall time until the GPU has consumed the frame:

```powershell
.\build\GM3DStudio.exe --bench 180 --log "build\bench.log"                # default editor scene
.\build\GM3DStudio.exe --bench 180 --bench-stress --log "build\bench.log"  # synthetic stress scene
```

The first 30 frames are warm-up and are ignored. The single `[bench]` line reports `cpu_ms` (time spent inside the engine step), `total_ms` (the same window including `ID3D11DeviceContext::Flush`) and `fps(p50)` derived from `total_ms`. Compare p50 across runs, not the average: a single terrain rebuild or asset import skews the mean.

`--bench-stress` builds a deterministic scene of 576 shadow-casting props on a flat grid plus four shadowed point lights, and fixes the camera. It exists because the default editor scene (three objects) cannot show culling or shadow-pass regressions. `--bench-stress` without `--bench` loads that scene and keeps running, so it can be inspected or screenshotted interactively.

Benchmark numbers are only comparable on the same machine, scene, viewport size and build configuration. Change one variable at a time, and re-run the same build twice before trusting a small difference: run-to-run noise on the stress scene is roughly 1%.

## Terrain heightmaps

The Terrain panel imports and exports grayscale PFM (`Pf`) files with square dimensions from 32 to 512 pixels. Values are 32-bit floating-point world heights; the PFM scale is applied on import. Export uses little-endian floats and includes the generated heightfield even if it has not been sculpted. Import replaces the terrain resolution and heights as one undoable action. Color PFM and PNG heightmaps are not supported.

The GameMaker exporter also includes terrain heights, paint weights and four layer settings in the scene JSON. Its generated loader uses the additive `terrain_apply_edit_data` extension function after `terrain_generate`; it expects the current DLL and matching extension definitions. The packed, little-endian buffer starts with three u32 counts (height floats, weight bytes, layers), followed by f32 heights, RGBA8 weights and five f32 values per layer (RGB, roughness, scale). Counts must be zero or match the terrain resolution, and the layer count must be zero or four. The function returns the updated terrain collider ID or a negative error code. It validates the buffer before changing terrain; as with other native pointer APIs, the caller is responsible for passing a valid buffer address and length.

`GM3DGraphicsSmoke` exercises terrain data restoration through the exported DLL, including a physics raycast and rejection of truncated input. This is not a substitute for compiling and running an exported project in GameMaker LTS.
