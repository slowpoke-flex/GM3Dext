# GM3Dext: High-Performance 3D Engine & Studio for GameMaker

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Graphics: Direct3D 11](https://img.shields.io/badge/Graphics-Direct3D%2011-0078D7.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atcon-direct3d-11-graphics)
[![Physics: Jolt Physics](https://img.shields.io/badge/Physics-Jolt%20Physics-orange.svg)](https://github.com/jrouwe/JoltPhysics)
[![Target: GameMaker LTS](https://img.shields.io/badge/Target-GameMaker%20LTS-brightgreen.svg)](https://gamemaker.io/)

**GM3Dext** is a native 3D engine and standalone world editor for **GameMaker LTS**. It ships as two binaries built from one C++20 core: `GM3Dext.dll` (a C-ABI extension called from GML) and `GM3DStudio.exe` (a Dear ImGui editor that authors the scenes the extension plays back).

## Key architectural features

- **Zero-copy shared memory protocol**: GameMaker allocates one contiguous byte buffer. Transforms, camera state and instance data travel between GML and the engine through that buffer, with no marshalling and no per-frame VM calls.
- **DirectX 11 PBR pipeline**:
  - Cook-Torrance microfacet BRDF with base colour, roughness, metallic, emissive and normal mapping.
  - Directional sun shadows: 2-cascade CSM plus a 6-face omnidirectional point-light atlas, filtered with a rotated Vogel-disk PCF kernel.
  - HDR bloom (bright-pass, two-pass Gaussian blur) and ACES filmic tonemapping.
  - Procedural sky dome with a radiant sun disk, height-aware exponential fog.
  - Instanced batching grouped by `(mesh_id, material_id, texture_id)`.
- **Jolt Physics**: multi-threaded rigid bodies with box, sphere, capsule, cylinder and convex-hull shapes, a heightfield terrain collider, raycasts, and fixed-timestep interpolation for stutter-free motion at high refresh rates.
- **Skeletal animation**: hardware GPU skinning, multi-track interpolation, up to 128 joints per skin.
- **Procedural content**: multi-biome terrain with splat mapping and a sculpting brush, procedural pine trees, and an instanced grass/foliage system with wind sway, subsurface scattering and a painting brush.
- **Photographic CC0 materials**: seven PBR sets (albedo + normal, from [ambientCG](https://ambientcg.com), CC0 1.0) used as the built-in material palette and as the tiled terrain detail layers. The engine falls back to procedural textures when the `assets/textures` folder is absent.
- **GameMaker integration**: 59 exported functions, a `--log`-friendly C ABI, and a GML loader generator that the Studio emits alongside the scene.

## GM3D Studio

- Full DCC-style docking layout: viewport, hierarchy, inspector, materials, environment, status bar.
- True parent-child transform hierarchy with world-transform-preserving reparenting and cycle guards.
- Multi-selection, grouping (`Ctrl+G`), alignment, surface snapping (`G`), recursive duplication (`Ctrl+D`) and multi-level undo/redo.
- PBR material editor with a live Cook-Torrance preview sphere, texture slots, UV tiling/offset and alpha modes.
- glTF 2.0 / GLB / OBJ import with automatic material extraction and diagnostic reporting.
- Terrain generation, raise/lower/smooth/flatten brushes, PFM heightmap import/export, and a Unity-style foliage painting brush.
- Resilient asset library with thumbnails, category filters and one-click relinking of moved files.
- GameMaker LTS exporter: writes the scene JSON, the GML loader and staged assets into a project folder.

## Building from source

### Requirements

- Windows 10 / 11 (64-bit)
- MSVC v143+ (Visual Studio 2022 or 2026 Build Tools) with C++20 support
- Windows 10 / 11 SDK (Direct3D 11, DXGI, D3DCompiler)
- CMake 3.20+ and Ninja

### Fast build

```cmd
build.bat
```

This produces, in `build/`:

| Binary | Purpose |
|:---|:---|
| `GM3Dext.dll` | Core GameMaker native extension |
| `GM3DStudio.exe` | Standalone 3D world editor |
| `GM3DCheck.exe` | Scene / project / export validator with no GPU dependency |
| `GM3DShaderCheck.exe` | Compiles every embedded HLSL blob at startup |
| `GM3DGraphicsSmoke.exe` | Headless Direct3D 11 device and frame smoke test |
| `SceneSerializerTests.exe` | Scene, hierarchy, material and export unit tests |
| `EngineLifecycleTests.exe` | Init / update / shutdown lifecycle tests against the DLL |
| `FrustumMathTests.exe` | Culling math tests (frustum planes, AABB transform) |

Extra arguments are forwarded to Ninja, so `build.bat GM3DStudio` builds a single target.

### Texture assets

`assets/textures/` holds the CC0 PBR sets (4 MB total) and is copied next to `GM3DStudio.exe` by CMake. To refresh or re-download them:

```powershell
python tools\fetch_textures.py
```

Sources and licences are listed in `assets/textures/CREDITS.md`. The sets are optional at runtime: without the folder the engine uses its built-in procedural textures, so the DLL still runs inside a GameMaker project that ships no assets.

### Tests

```cmd
ctest --test-dir build --output-on-failure
```

`ctest` is not on `PATH` in a plain shell; use `tools/check.ps1`, which locates Visual Studio with `vswhere`, configures, builds and runs CTest. Add `-Graphics` to also rebuild `GM3DTerrainCheck` and run the WARP terrain parity and hidden-window Direct3D checks.

## Using the extension from GameMaker

The extension exports a `__cdecl` C ABI whose arguments and return values are `double` (or raw pointers for buffers and strings):

```gml
// Initialize the engine with the shared buffer and the game window handle
init_extension(buffer_get_address(shared_buffer), buffer_get_size(shared_buffer), window_handle());

// Lighting
light_set_sun(0.577, 0.707, 0.408, 1.0, 0.95, 0.90, 2.8);
light_set_ambient(0.22, 0.28, 0.38, 1.6);
render_set_bloom(1.0, 0.85, 1.0);

// Custom PBR material
material_set_pbr(mat_id, 0.9, 0.8, 0.2, 1.0, 0.25, 0.90, 0.0);
material_set_uv(mat_id, 2.0, 2.0, 0.0, 0.0);

// Tick the engine once per frame
update_framework(delta_time / 1000000.0);
```

The canonical list of exports is `src/dllmain.cpp`; function registration for GameMaker lives in `3DTEST/extensions/GM3Dext/GM3Dext.yy`. Both must be updated together, and functions with more than four arguments must take all-`ty_real` (double) parameters.

## Known limitations

- The grass API (`grass_*`) and `material_clear_all` exist in the DLL but are not yet registered in `GM3Dext.yy`, so GML cannot call them.
- Mesh colliders are not implemented; the editor simulates them with a cylinder proxy and reports this in diagnostics.
- Skins with more than 128 joints are rejected, and glTF morph targets are not supported.
- External dependencies referenced by a `.gltf` are not packaged by the exporter; use a self-contained `.glb`.
- Windows with Direct3D 11 only. There is no Direct3D 12, Vulkan or cross-platform backend.

## Trademark disclaimer and license

- **Disclaimer**: *GameMaker* is a registered trademark of Opera Norway AS (formerly YoYo Games Ltd). GM3Dext is an independent, community-driven open-source project and is not affiliated with, endorsed by, or sponsored by Opera Norway AS or YoYo Games Ltd.
- **License**: released under the [MIT License](LICENSE). Third-party libraries (`JoltPhysics`, `imgui`, `imguizmo`, `cgltf`, `nlohmann_json`) belong to their respective authors under permissive MIT/Zlib/Apache licenses.
