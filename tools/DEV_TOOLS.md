# GM3D developer checks

Run from PowerShell:

```powershell
.\tools\check.ps1
.\tools\check.ps1 -Graphics
```

The script configures and builds the local release build and runs CTest. `-Graphics` additionally compares a regional terrain texture update against a full rebuild on a Direct3D WARP device, then runs two Direct3D initialization, render and shutdown cycles using a hidden window. The latter requires a working Windows graphics driver. It does not copy the extension into a GameMaker project.

If `build.bat` reports `ninja: error: manifest 'build.ninja' still dirty after 100 tries`, the build directory has a cache from a different generator (usually a stray `cmake -B build` that picked Visual Studio). Delete `build/` and run `tools\check.ps1` again; patching timestamps only hides it.

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

## Running inside the GameMaker LTS Runner

`GM3DStudio`, `GM3DGraphicsSmoke` and the unit tests never load the extension into the host process, so the last thing they cannot prove is that the DLL behaves inside a real GameMaker game. `tools\run_in_runner.ps1` closes that gap:

```powershell
.\tools\run_in_runner.ps1              # build GM3Dext.dll, then build and run 3DTEST
.\tools\run_in_runner.ps1 -SkipBuild   # reuse the existing build\GM3Dext.dll
.\tools\run_in_runner.ps1 -Wait        # block until the game window is closed
```

It does not open the IDE. It calls `Igor.exe` from the installed runtime - the same command-line builder the IDE invokes for F5 - with `Windows Run`, after:

1. copying `build\GM3Dext.dll` into `3DTEST\extensions\GM3Dext\` (Igor packages whatever is there, so a stale copy is easy to ship by accident);
2. copying `assets\textures` next to the built game in `output\3DTEST\`.

Igor writes the build and the game's own stdout to `build\igor.log`. The line to look for is `Entering main loop`; `[GM3Dext] Engine initialized successfully!` above it comes from the project's `gm3d_init` call.

Two traps this script exists to avoid:

- **Missing `assets\textures`.** The engine resolves the folder from the game's working directory. Without it every CC0 material silently falls back to the procedurally generated one: the paved plaza becomes a flat pale surface with faint grid lines, boulders lose their granite albedo. It looks exactly like a rendering regression and is not one.
- **Invisible `std::cout`.** Inside the Runner the extension's stdout is not captured by the log - only GML `show_debug_message` output is. Set `GM3D_DEBUG_BATCHES=<path>` before launching to make `RenderWorld` append one line per draw batch with mesh, material, texture, preset sentinel and roughness to that file:

  ```powershell
  $env:GM3D_DEBUG_BATCHES = "C:\GM3Dext\build\batches.txt"
  .\tools\run_in_runner.ps1 -SkipBuild
  ```

  Igor inherits the variable, so the Runner does too. Without a variable like this the only evidence available in the runner is pixels.

A hybrid laptop note: the game runs on the adapter Windows picks for the **Runner process**, not for this repository's executables. `NvOptimusEnablement` lives in `tools\studio\main.cpp` and therefore only covers the Studio; inside GameMaker the discrete GPU has to be requested for the runtime's `Runner.exe` in Windows Settings -> Graphics.


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

## GameMaker documentation MCP

`tools/gamemaker-mcp/` is a vendored copy of `@petah/gamemaker-mcp`: a Model Context Protocol server over the GameMaker manual (3072 tracked files, the markdown lives in `md/`). It is documentation only - it cannot open the IDE, build a project or send input - but it answers GML API questions from the same manual the editor's help refers to.

`opencode.json` at the repository root registers it, so any OpenCode session in this repository gets these tools:

| Tool | Purpose |
|:---|:---|
| `lookup_gamemaker_function` | Function reference by name (`function_name`) |
| `search_gamemaker_docs` | Free-text search over the manual (`query`) |
| `list_functions_by_category` | All functions of a category |
| `get_markdown_file` | One manual page as markdown (`file_path`) |
| `init_gamemaker_agent` | The bundled overview/instructions, `init.txt` |

Check the connection with `opencode mcp list` (expects `✓ gamemaker-docs connected`). The CLI works too, without OpenCode:

```powershell
node tools\gamemaker-mcp\src\cli.js lookup keyboard_check_pressed
node tools\gamemaker-mcp\src\cli.js search "buffer_write"
```

**Local patch.** Upstream guards its entry point with `if (import.meta.url === \`file://${process.argv[1]}\`)`, which never matches on Windows: `process.argv[1]` is a backslash path with no `file://` prefix. `main()` was therefore never called, the process idled on stdin and never answered MCP `initialize` - the server looked "connected but useless" in every Windows client. `src/gamemaker-docs-server.js` now builds a file URL for the comparison. Re-apply that edit if the package is ever updated from npm.

## MCP servers installed machine-wide

`%USERPROFILE%\.config\opencode\opencode.json` registers five servers for every OpenCode session, not just this repository. The GML manual server above stays project-local because the markdown tree lives in this checkout.

| Server | Kind | Tools | Purpose |
|:---|:---|---:|:---|
| `microsoft-learn` | remote | 3 | Official D3D11/DXGI/Win32/HLSL documentation. No key. |
| `context7` | remote | 2 | Current docs/examples for libraries (cgltf, Jolt, ImGui). Free tier, rate-limited without a key. |
| `deepwiki` | remote | 3 | `ask_wiki_question` / `read_wiki_*` over a GitHub repo. Useful for reading Jolt or cgltf instead of guessing. |
| `gamemaker` | local | 22 of 225 | Project-aware GameMaker tooling: validation, static analysis, guarded edits. Read-only by default. |
| `gamemaker-run` | local, per project | 17 of 58 | Runs a GameMaker project in an isolated copy and observes it: input, screenshots, runtime state. |
| `renderdoc` | local | 15 | GPU frame capture analysis: draw calls, shader source and constant buffers, texture pixels, pipeline state. |

Check them all with `opencode mcp list` (seven lines including `gamemaker-docs` and `gamemaker-run`). Run it twice if `renderdoc` reports `pending` - the Python server needs a few seconds to answer `initialize`.

### Tool filtering

Two servers register far more tools than this project uses (225 and 58). `%USERPROFILE%\.config\opencode\mcp\mcp-filter.mjs` is a transparent stdio proxy: it forwards every message unchanged except the response to `tools/list`, which it filters to `MCP_ALLOW`; a call to a filtered-out tool is answered locally with `-32601`. `MCP_LABEL` only names the server in its stderr line. Both servers run through it:

```
node mcp-filter.mjs <command> [args...]
```

### gamemaker

`npm install -g gamemaker-mcp@latest` installs version 2.0.0 (MIT). Two things are configured around it:

- **Pinned project.** It auto-detects the `.yyp` of the workspace, which in a session opened at `C:\GM3Dext-SDF` binds it to the frozen checkout. `GAMEMAKER_PROJECT` is pinned to `C:\GM3Dext\3DTEST\3DTEST.yyp` instead, with `GAMEMAKER_MCP_MODE=read-only` and `GAMEMAKER_MCP_ALLOW_BUILD=0`.
- **Tool filter.** 22 of its 225 tools are exposed:

  project introspection (`gm_project_info`, `gm_project_validate`, `gm_project_statistics`, `gm_project_health_score`, `gm_asset_list`, `gm_asset_read`, `gm_file_read`, `gm_code_search`, `gm_runtime_detect`), static analysis (`gm_gml_analyze`, `gm_gml_validate_snippet`, `gm_shader_analyze`, `gm_symbol_references`, `gm_asset_find_references`, `gm_draw_state_audit`, `gm_gml_profile_check`, `gm_gml_dead_code_detect`, `gm_dependency_graph`), build/edit (`gm_project_compile_errors`, `gm_job_status`, `gm_gml_patch_preview`, `gm_gml_patch`).

  Editing needs `GAMEMAKER_MCP_MODE=workspace-write`; compiling additionally needs `GAMEMAKER_MCP_ALLOW_BUILD=1`. Change the map in `opencode.json`, not in the server.

### gamemaker-run

`uv tool install morx-gms2-mcp` (0.6.2, Windows only, Python 3.10+) gives a server that copies the bound project into an isolated directory, compiles it through Igor, starts the real Runner and connects to an injected GML bridge. That bridge is why it can do what shell automation cannot: `send_game_input` / `send_game_mouse` / `trigger_game_event` reach the game without needing foreground focus, and `capture_game` reads `DrawGUIEnd`.

It is configured per project, not machine-wide, because it pins one `.yyp`:

```powershell
morx-gms2-mcp bind --project C:\GM3Dext\MeadowDemo
morx-gms2-mcp check --project C:\GM3Dext\MeadowDemo
```

`check` reports validator errors (it caught five stale `parent` references after renaming the project copy - every `.yy` keeps `"parent": {"name": ..., "path": "<project>.yyp"}` and all of them have to be rewritten). The server entry lives in the repository `opencode.json` with `--allow-run`, the runtime root and the user directory; `--write` is deliberately absent, so it cannot edit the project, only run and observe it.

## Meadow scene generator

`tools/meadow_scene.py` writes `scenes/meadow.json`: hills from three sine bands, a violet
grass carpet, one `UAL1_Standard.glb` character and a hazy lavender sky. The Studio renders it
headless, and the script prints the camera line to use:

```powershell
python tools\meadow_scene.py
build\GM3DStudio.exe --scene scenes\meadow.json --camera -4,12.62,-11.86,4,16.34,2 --screenshot shot.png --frames 40
```

Why the file is a few hundred kilobytes and not a few megabytes: no baked grass clumps (the
procedural carpet is rebuilt from the `grass_*` fields), and a `splatmap` of `255,0,0,0`
everywhere, which is what makes the terrain honour the scene's layer colours at all. See
`DEV_WIKI.md` §42 for the four renderer knobs this scene needed.

## Scene capture and startup automation

### renderdoc

```powershell
winget install --id BaldurKarlsson.RenderDoc --exact --silent
git clone https://github.com/halby24/RenderDocMCP C:\GameDev\tools\renderdoc-mcp
cd C:\GameDev\tools\renderdoc-mcp
uv tool install .
python scripts\install_extension.py
```

The extension lands in `%APPDATA%\qrenderdoc\extensions\renderdoc_mcp_bridge`. Enable it once in RenderDoc under **Tools > Manage Extensions > RenderDoc MCP Bridge**; the server talks to the open RenderDoc instance over `127.0.0.1:19876`, so with RenderDoc closed every tool returns "Cannot connect to RenderDoc MCP Bridge" - that is the expected state, not a broken install.

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

## Authoring guide

`docs\STUDIO_GUIDE.md` (Russian) is the user-facing manual: Studio panels and hotkeys, the
terrain/grass/lighting/material workflow, what the exporter writes, and the GameMaker-side
wiring (extension `.yy` table, `scr_gm3d`, `scr_gm3d_load_scene`, datafiles, the entity buffer
and `update_framework`). Read it before changing the exporter, because the scene JSON is the
contract between the two halves.
