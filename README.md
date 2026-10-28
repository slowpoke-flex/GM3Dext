# GM3Dext: High-Performance 3D Engine & Studio for GameMaker

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Graphics: Direct3D 11](https://img.shields.io/badge/Graphics-Direct3D%2011-0078D7.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atcon-direct3d-11-graphics)
[![Physics: Jolt Physics](https://img.shields.io/badge/Physics-Jolt%20Physics-orange.svg)](https://github.com/jrouwe/JoltPhysics)
[![Target: GameMaker LTS](https://img.shields.io/badge/Target-GameMaker%20LTS-brightgreen.svg)](https://gamemaker.io/)

**GM3Dext** is a high-performance native 3D engine and standalone world editor designed to bring modern 3D graphics, AAA multi-threaded physics, and visual scene authoring to **GameMaker LTS** and independent game developers.

---

## ⚡ Key Architectural Features

- **Zero-Copy Shared Memory Protocol**: GameMaker allocates a contiguous raw byte buffer once. Transforms, camera states, and instance data are exchanged between GameMaker and the C++ engine via direct memory mapping without marshaling or VM call overhead.
- **DirectX 11 PBR Graphics Pipeline**:
  - Physically Based Rendering (PBR) with Cook-Torrance microfacet BRDF (base color, roughness, metallic, emissive, normal mapping).
  - Real-time 2048x2048 hardware-filtered PCF directional sun shadows.
  - Dynamic HDR bloom post-processing pipeline (bright-pass extraction, 2-pass Gaussian blur, ACES filmic tonemapping).
  - Procedural atmospheric sky dome with radiant sun disk and distance fog.
  - Instanced multi-batching grouped by `(mesh_id, material_id, texture_id)`.
- **AAA Jolt Physics**: Multi-threaded rigid body physics engine with box, sphere, capsule, cylinder, and convex hull collision shapes.
- **GM3D Studio**:
  - Full-featured standalone 3D world editor powered by Dear ImGui and ImGuizmo.
  - True parent-child transform hierarchy with 4x4 matrix decomposition and invariant world transforms.
  - Multi-selection manipulation, group parenting (`Ctrl+G`), unparenting (`Ctrl+Shift+G`), alignment, and surface snapping (`G`).
  - Recursive duplication (`Ctrl+D`) and deletion (`Del`) with automatic ID remapping.
  - Dedicated **PBR Material Editor** with real-time 3D Cook-Torrance shaded preview sphere, texture slots, UV tiling/offset, and alpha blending modes.
  - glTF 2.0 / GLB model loading and automatic material extraction with diagnostic limitation reporting.
  - Procedural terrain generator, real-time sculpting brush (Raise, Lower, Smooth, Flatten), and PFM heightmap import/export.
  - Resilient asset library with category filtering, thumbnail previews, and one-click auto-relink.
  - Multi-level Undo/Redo command history (`Ctrl+Z`, `Ctrl+Y`) with crash protection and atomic scene serialization.

---

## 🛠️ Building from Source

### Requirements
- **Windows 10 / 11 (64-bit)**
- **MSVC v143+ (Visual Studio 2022 or 2026 Build Tools)** with C++20 support
- **Windows 10/11 SDK** (DirectX 11, DXGI, D3DCompiler)
- **CMake 3.20+** and **Ninja**

### Fast Build via Command Line
Run the included `build.bat` script from the project root:
```cmd
build.bat
```

This compiles:
- `build/GM3Dext.dll` — Core GameMaker native extension.
- `build/GM3DStudio.exe` — Standalone 3D World Editor.
- `build/GM3DShaderCheck.exe` — Automated HLSL shader validator.
- `build/SceneSerializerTests.exe` — Hierarchy, materials, and scene unit tests.
- `build/GM3DGraphicsSmoke.exe` — Direct3D 11 headless graphics smoke test.

---

## 🎮 GameMaker LTS Integration

The extension exports a clean C-ABI (`__cdecl`) with `double` and raw pointer arguments for GML:
```gml
// Initialize engine with shared buffer and window handle
init_extension(buffer_get_address(shared_buffer), buffer_get_size(shared_buffer), window_handle());

// Set up lighting
set_sun_light(0.577, 0.707, 0.408, 1.0, 0.95, 0.90, 2.8);
set_ambient_light(0.22, 0.28, 0.38, 1.6);
render_set_bloom(1.0, 0.85, 1.0);

// Configure custom PBR material
material_set_pbr(mat_id, 0.9, 0.8, 0.2, 1.0, 0.25, 0.90, 0.0);
material_set_uv(mat_id, 2.0, 2.0, 0.0, 0.0);

// Update engine every frame in Step or Draw
update_framework(delta_time / 1000000.0);
```

---

## ⚖️ Trademark Disclaimer & License

- **Disclaimer**: *GameMaker is a registered trademark of Opera Norway AS (formerly YoYo Games Ltd). GM3Dext is an independent, community-driven open-source project and is not affiliated with, endorsed by, or sponsored by Opera Norway AS or YoYo Games Ltd.*
- **License**: Released under the [MIT License](LICENSE). Third-party libraries (`JoltPhysics`, `imgui`, `imguizmo`, `cgltf`, `nlohmann_json`) belong to their respective authors under permissive MIT/Zlib/Apache licenses.
