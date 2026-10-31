# GM3Dext: High-Performance 3D Engine for GameMaker
*Developer Wiki & Architecture Guide (For Developers & AI Agents)*

## 1. Project Overview & Vision
`GM3Dext` is a high-performance native dynamic library (DLL) written in modern C++20 designed to bring cutting-edge 3D capabilities to the GameMaker Engine. 

### Core Tech Stack:
- **Language**: C++20 (MSVC 19.x, 64-bit).
- **Build System**: CMake 3.20+ with Ninja multi-threaded build generator.
- **Physics Engine**: **Jolt Physics** (MIT License, industry-standard multi-threaded rigid body physics used in AAA titles like Horizon Forbidden West).
- **Graphics Pipeline**: **Direct3D 11 / PBR & GPU Compute** (DirectX 11 natively supported on Windows SDK without heavy external dependencies, rock-solid stability in GameMaker environment).
- **Data Exchange**: **Zero-Copy Shared Memory Protocol** via GameMaker raw byte buffers (`buffer_create` / `buffer_get_address`).

---

## 2. Zero-Copy Shared Memory Protocol

### Why Zero-Copy?
Standard GameMaker extensions suffer severe latency when calling DLL functions hundreds or thousands of times per frame (overhead of marshaling, stack frames, GML VM calls). 
With Zero-Copy:
1. GameMaker allocates a contiguous byte buffer once (`buffer_create(size, buffer_fixed, 1)`).
2. GameMaker passes the raw pointer (`buffer_get_address(buf)`) to DLL during `init_extension`.
3. GameMaker writes transforms and entity states directly into memory sequentially (`buffer_write`).
4. In `update_framework(dt)`, the DLL directly reads this memory block, streams it straight into GPU Instance Buffers via a single `memcpy` / `Map(WRITE_DISCARD)`, steps Jolt Physics, and writes physics transforms back into the same memory.
5. GameMaker reads back updated coordinates with `buffer_peek`.

### Binary Memory Layout (Strict 1-byte packing)

#### SharedBufferHeader (Exactly 64 bytes)
```
Offset  Size  Field            Type         Description
0x00    4     magic            uint32_t     0x474D3344 ('GM3D')
0x04    4     version          uint32_t     Protocol version (1)
0x08    4     entity_count     uint32_t     Active entity count in current frame
0x0C    4     max_entities     uint32_t     Buffer capacity (max objects)
0x10    12    camera_pos       float[3]     Camera Position (X, Y, Z)
0x1C    12    camera_target    float[3]     Camera LookAt Target (X, Y, Z)
0x28    4     camera_fov       float        Vertical Field of View (degrees)
0x2C    4     camera_near      float        Near clipping plane
0x30    4     camera_far       float        Far clipping plane
0x34    4     flags            uint32_t     Global scene flags (debug draw, shadows, wireframe)
0x38    8     reserved         uint8_t[8]   Alignment padding to 64 bytes
```

#### SharedEntityData (Exactly 64 bytes - 1 CPU Cache Line)
```
Offset  Size  Field            Type         Description
0x00    12    pos              float[3]     World Position (X, Y, Z)
0x0C    16    rot              float[4]     Rotation Quaternion (QX, QY, QZ, QW)
0x1C    12    scale            float[3]     Scale (SX, SY, SZ)
0x28    4     entity_id        uint32_t     Unique GameMaker instance ID
0x2C    2     mesh_id          uint16_t     Mesh ID (0=Cube, 1=Sphere, 2=Plane, etc.)
0x2E    2     material_id      uint16_t     PBR Material ID
0x30    4     flags            uint32_t     Bitmask (Active, CastShadow, IsRigidBody, IsKinematic)
0x34    4     physics_id       uint32_t     Jolt BodyID handle
0x38    4     custom_param1    float        Emissive intensity / user scalar
0x3C    4     custom_param2    float        Roughness override / extra param
```

---

## 3. GameMaker C ABI Integration Rules
- Windows Calling Convention: `__cdecl` with `extern "C"`.
- In GameMaker Extension Editor:
  - All scalar numbers (`float`, `double`, `int`, `uint`) must be received and returned as `double` (`ty_real`).
  - All pointers (`buffer_get_address`, `window_handle`) must be received as `const char*` / `void*` (`ty_string` / `Pointer`).
  - Return codes: Return `1.0` (`GMResult::OK`) on success, negative double on errors (`-1.0`, `-2.0`, etc.).

---

## 4. Subsystems Architecture

### PhysicsWorld (Jolt Physics)
- **Time Stepping**: Fixed timestep (60 Hz accumulator) with delta time clamping to prevent spiral of death.
- **Job System**: `JobSystemThreadPool` utilizing `std::thread::hardware_concurrency() - 1`.
- **Temp Allocator**: 16 MB preallocated scratch memory for constraint solver and collision queries.
- **Layers**:
  - `NON_MOVING` (Static environment, terrain, walls)
  - `MOVING` (Dynamic props, physics debris, projectiles)
  - `TRIGGER` (Sensors, triggers)
- **Raycasting**: High-speed raycasts for player line-of-sight and ground checks.

### RenderWorld (Direct3D 11 & PBR)
- **Device & Context**: D3D11 Device (Feature Level 11_0+) with DXGI SwapChain.
- **Window Attachment**: Bound to GameMaker `HWND` (or dedicated high-framerate child window overlay).
- **PBR Shading**:
  - Cook-Torrance microfacet BRDF (GGX distribution, Smith geometry, Schlick Fresnel approximation).
  - Directional sunlight with Orthographic Shadow Mapping.
  - Point lights with attenuation.
- **GPU Instancing**:
  - Contiguous streaming of `SharedEntityData` array directly into Dynamic Vertex Buffer (`D3D11_MAP_WRITE_DISCARD`).
  - Batching by `(mesh_id, material_id)` into `DrawIndexedInstanced` calls.
- **GPU Particle System**:
  - Compute Shader simulation (`DispatchCompute`) updating particle buffers in GPU VRAM.

---

## 5. Build Environment & Commands
- **Compiler**: Visual Studio 2026 Developer Command Prompt (`vcvarsall.bat x64`).
- **CMake & Ninja**:
  ```bat
  cmake -B build -S . -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release
  ```
- Resulting DLL: `c:\GM3Dext\build\GM3Dext.dll`

---

## 6. License Compliance & Open Source Assets
All third-party code integrated into this framework must be permissive:
- Jolt Physics: MIT License (Copyright Jorrit Rouwe).
- Direct3D 11 / HLSL: Microsoft Windows SDK.
- Fast OBJ Loader / Math helpers: MIT / Public Domain (Unlicense / Zlib).

---

## 7. Exported C API Reference (59 Functions)
| Exported Name | Calling Conv | Arguments | Return | Description |
|:---|:---|:---|:---|:---|
| `init_extension` | `__cdecl` | `(void* buf, double size, void* hwnd)` | `double` | Initializes D3D11 device, Jolt world & binds memory |
| `shutdown_extension` | `__cdecl` | `()` | `double` | Releases all GPU/Physics resources cleanly |
| `update_framework` | `__cdecl` | `(double delta_time)` | `double` | Ticks physics (60Hz fixed), syncs transforms, renders frame |
| `create_physics_body` | `__cdecl` | `(shape, motion, mass, x, y, z, sx, sy, sz)` | `double` | Spawns Jolt Box/Sphere/Capsule/Cylinder body |
| `destroy_physics_body`| `__cdecl` | `(body_id)` | `double` | Destroys Jolt body by ID |
| `physics_add_impulse` | `__cdecl` | `(body_id, ix, iy, iz)` | `double` | Applies instantaneous linear impulse vector |
| `physics_add_torque`  | `__cdecl` | `(body_id, tx, ty, tz)` | `double` | Applies rotational angular torque vector |
| `physics_add_force`   | `__cdecl` | `(body_id, fx, fy, fz)` | `double` | Applies continuous directional force |
| `physics_set_velocity`| `__cdecl` | `(body_id, vx, vy, vz)` | `double` | Sets linear velocity directly |
| `physics_set_gravity` | `__cdecl` | `(gx, gy, gz)` | `double` | Dynamically updates global physics gravity vector |
| `physics_set_friction`| `__cdecl` | `(body_id, friction)` | `double` | Adjusts surface friction coefficient for body |
| `physics_set_restitution`| `__cdecl` | `(body_id, restitution)` | `double` | Adjusts bounciness / restitution coefficient |
| `load_mesh_obj`       | `__cdecl` | `(const char* filepath)`| `double` | Loads Wavefront OBJ model into GPU VRAM (returns `mesh_id`) |
| `load_mesh_glb`       | `__cdecl` | `(const char* filepath)`| `double` | Loads binary glTF 2.0 (.glb) model into GPU VRAM (returns `mesh_id`) |
| `load_mesh_model`     | `__cdecl` | `(const char* filepath)`| `double` | Auto-detects extension (.glb/.gltf/.obj) and loads mesh |
| `anim_play`           | `__cdecl` | `(entity_id, anim_index, speed, loop)` | `double` | Starts playing a skeletal animation on an entity |
| `anim_stop`           | `__cdecl` | `(entity_id)` | `double` | Stops skeletal animation and resets to bind pose |
| `anim_set_time`       | `__cdecl` | `(entity_id, time)` | `double` | Seeks animation to specific timestamp (seconds) |
| `anim_get_count`      | `__cdecl` | `(mesh_id)` | `double` | Returns total embedded animations in 3D model |
| `anim_get_name`       | `__cdecl` | `(mesh_id, anim_index)` | `const char*` | Returns animation clip name (e.g. "Dance_Loop") |
| `anim_get_duration`   | `__cdecl` | `(mesh_id, anim_index)` | `double` | Returns animation duration in seconds |
| `physics_raycast` | `__cdecl` | `(fx, fy, fz, tx, ty, tz)` | `double` | Casts ray; returns hit body_id (or 0 if no hit) |
| `physics_get_raycast_x`| `__cdecl` | `()` | `double` | Gets X coordinate of last raycast intersection |
| `physics_get_raycast_y`| `__cdecl` | `()` | `double` | Gets Y coordinate of last raycast intersection |
| `physics_get_raycast_z`| `__cdecl` | `()` | `double` | Gets Z coordinate of last raycast intersection |
| `get_framework_version`| `__cdecl` | `()` | `double` | Returns protocol version (`1.0`) |

### Additional exports

| Exported Name | Calling Conv | Arguments | Return | Description |
|:---|:---|:---|:---|:---|
| `texture_load` | `__cdecl` | `(const char* filepath)` | `double` | Loads a PBR albedo texture, returns its `texture_id` |
| `texture_set_normal_map` | `__cdecl` | `(target_tex_id, normal_tex_id)` | `double` | Attaches a normal map to an existing texture id |
| `light_set_sun` | `__cdecl` | `(dx, dy, dz, r, g, b, intensity)` | `double` | Directional sun direction, colour and intensity |
| `light_set_ambient` | `__cdecl` | `(r, g, b, intensity)` | `double` | Global ambient / skylight fill |
| `light_point_set` | `__cdecl` | `(index, x, y, z, radius, r, g, b, intensity)` | `double` | Writes one of the 16 point lights without shadows |
| `light_point_set_ex` | `__cdecl` | `(index, x, y, z, radius, r, g, b, intensity, cast_shadow)` | `double` | Point light with an explicit shadow flag |
| `light_point_clear` | `__cdecl` | `()` | `double` | Disables every point light |
| `render_set_shadow_settings` | `__cdecl` | `(enabled, strength, softness, coverage)` | `double` | Shadow strength, PCF softness and world coverage |
| `render_set_shadow_quality` | `__cdecl` | `(quality, csm_enabled, max_shadow_lights)` | `double` | PCF tap quality, cascade toggle, shadowed point-light budget |
| `physics_set_transform` | `__cdecl` | `(body_id, x, y, z, qx, qy, qz, qw)` | `double` | Teleports a body to an explicit position and rotation |
| `render_set_sky_color` | `__cdecl` | `(r, g, b)` | `double` | Clear colour and fog tint |
| `render_set_fog` | `__cdecl` | `(start, end, r, g, b)` | `double` | Linear distance fog range and colour |
| `show_f5_menu` | `__cdecl` | `(current_mode, invert_y)` | `double` | Runs one frame of the built-in F5 showcase menu |
| `set_gizmo` | `__cdecl` | `(enabled, x, y, z, sx, sy, sz, axis_len)` | `double` | Draws the 3D transform gizmo at a position |
| `render_set_bloom` | `__cdecl` | `(enabled, intensity, threshold)` | `double` | HDR bloom toggle, strength and bright-pass threshold |
| `particle_spawn_burst` | `__cdecl` | `(type, x, y, z, count, speed, size, r, g, b)` | `double` | Emits a spherical particle burst |
| `particle_spawn_ring` | `__cdecl` | `(type, x, y, z, radius, count, r, g, b)` | `double` | Emits a ring of particles |
| `particle_clear` | `__cdecl` | `()` | `double` | Removes all live particles |
| `terrain_generate` | `__cdecl` | `(grid_size, world_size, height_scale, seed)` | `double` | Builds the procedural terrain mesh and heightfield collider |
| `terrain_get_height` | `__cdecl` | `(x, z)` | `double` | Bilinear terrain elevation at a world position |
| `terrain_apply_edit_data` | `__cdecl` | `(const void* data, size_bytes)` | `double` | Uploads a sculpted height patch |
| `material_set_pbr` | `__cdecl` | `(mat_id, r, g, b, a, roughness, metallic, emissive)` | `double` | Defines a custom Cook-Torrance material |
| `material_set_textures` | `__cdecl` | `(mat_id, albedo_tex_id, normal_tex_id)` | `double` | Binds albedo and normal maps to a material |
| `material_set_uv` | `__cdecl` | `(mat_id, tiling_u, tiling_v, offset_u, offset_v)` | `double` | UV tiling and offset for a material |
| `material_set_alpha` | `__cdecl` | `(mat_id, alpha_mode, alpha_cutoff)` | `double` | Opaque / mask / blend mode and cutoff |
| `material_clear_all` | `__cdecl` | `()` | `double` | Resets the custom material table |
| `grass_set_enabled` | `__cdecl` | `(enabled)` | `double` | Toggles the instanced grass pass |
| `grass_set_wind` | `__cdecl` | `(dir_x, dir_z, speed, strength)` | `double` | Wind direction, speed and sway strength |
| `grass_rebuild` | `__cdecl` | `(density, seed)` | `double` | Regenerates the grass placement buffer |
| `grass_get_count` | `__cdecl` | `()` | `double` | Returns the live blade count |
| `grass_clear` | `__cdecl` | `()` | `double` | Removes all painted grass |
| `grass_load_file` | `__cdecl` | `(const char* filepath)` | `double` | Loads a painted grass buffer from disk |
| `grass_save_file` | `__cdecl` | `(const char* filepath)` | `double` | Writes the painted grass buffer to disk |

`src/dllmain.cpp` is the source of truth for this list. All 59 exports are registered in `3DTEST/extensions/GM3Dext/GM3Dext.yy`, and the GML wrapper library (`3DTEST/scripts/scr_gm3d/scr_gm3d.gml`, mirrored byte-for-byte into `gml_example/scr_gm3d.gml` by `tools/sync_gml_sdk.ps1`) wraps every one of them.

---

## 8. Built-in Procedural Mesh Catalog & Custom 3D Model Loading (.GLB / .OBJ)
- **Mesh ID 0**: Unit Cube (24 vertices, 36 indices, normals & UVs)
- **Mesh ID 1**: Smooth UV Sphere (Rings x Sectors tesselation)
- **Mesh ID 2**: Cylinder (Side walls + circular end caps)
- **Mesh ID 3**: Ground Plane / Floor (50x50 units with procedural grid shader)
- **Mesh ID 4**: Capsule (Body cylinder + dual hemispherical caps)
- **Mesh ID 100+**: Custom user models loaded at runtime:
  - `gm3d_mesh_load(filepath)`: Universal model loader (auto-detects `.glb`, `.gltf`, or `.obj`).
  - `gm3d_mesh_load_glb(filepath)`: Fast binary glTF 2.0 parser via embedded `cgltf` (MIT License).
  - `gm3d_mesh_load_obj(filepath)`: Wavefront OBJ parser.
  - Examples in project:
    - `datafiles/gem.obj`: Faceted crystal gem.
    - `datafiles/UAL1_Standard.glb`: Unreal Engine mannequin model (~8,546 vertices, 65 bones, 43 animations).
    - `datafiles/UAL1_Standard_RM.glb`: Root-motion mannequin variant.

---

## 9. Skeletal Animation System (Hardware GPU Skinning & Multi-Track Interpolation)
- **glTF 2.0 Skinning Parser**:
  - Extracts 65-joint hierarchy with inverse bind matrices and bone parent-child topology.
  - Per-vertex `uint4 joints` and `float4 weights` (normalized 4-bone skinning per vertex).
- **CPU Multi-Track Keyframe Sampler**:
  - Binary search time lookup with `XMVectorLerp` (translation/scale) and `XMQuaternionSlerp` (rotation).
  - Topological evaluation of global bone matrices: $M_{global}[j] = M_{local}[j] \cdot M_{global}[parent(j)]$.
  - Final skinning transform: $M_{skin}[j] = InverseBindMatrix[j] \cdot M_{global}[j]$.
- **Hardware GPU Vertex Skinning**:
  - Dynamic constant buffer `b1` with 128 $4 \times 4$ matrices (`sizeof = 8192` bytes).
  - For rigid bodies (cubes, spheres, gems): `BoneMatrices[0] = Identity`, `Joints={0,0,0,0}, Weights={1,0,0,0}`.
  - Both main forward PBR pass AND directional shadow map pass (`VS_Shadow`) evaluate GPU skinning, allowing soft shadows to bend and deform in real time with the character!

---

## 9. Real-Time Directional Shadow Mapping (PCF)
- **Shadow Map Resolution**: $2048 \times 2048$ Depth texture (`DXGI_FORMAT_R32_TYPELESS` / `D32_FLOAT`).
- **Pass 1 (Shadow Pass)**:
  - Orthographic Light Projection matrix positioned along Sun vector.
  - Dedicated depth-only vertex shader `VS_Shadow` with no pixel shader for hardware-accelerated Z-only rasterization.
  - Hardware slope-scaled depth bias (`DepthBias = 50`, `SlopeScaledDepthBias = 1.5f`) prevents shadow acne.
- **Pass 2 (Forward PBR Pass)**:
  - 16-tap ($4 \times 4$ kernel) Percentage-Closer Filtering (`SampleCmpLevelZero`) in pixel shader.
  - Soft contact shadows underneath all dynamic tumbling bodies and casting onto ground and each other.

---

## 10. GameMaker Documentation Tools (MCP & CLI)
Integrated in `tools/gamemaker-mcp` (MIT License):
- Provides instant offline documentation search and function lookups for 2,750 GML functions.
- Query CLI command:
  ```bat
  node tools/gamemaker-mcp/src/cli.js lookup <function_name>
  node tools/gamemaker-mcp/src/cli.js search <keyword>
  ```
- MCP server entry point: `node tools/gamemaker-mcp/src/gamemaker-docs-server.js`

---

## 11. GameMaker Project Architecture & Live Demo (`3DTEST`)

### Project Structure
- `3DTEST/extensions/GM3Dext/GM3Dext.yy`: Official extension declaration linking `GM3Dext.dll` with 20 C ABI functions typed for the GML VM.
- `3DTEST/scripts/scr_gm3d/scr_gm3d.gml`: High-level GML API (`gm3d_init`, `gm3d_camera_set`, `gm3d_body_create_box`, `gm3d_body_create_sphere`, `gm3d_body_create_cylinder`, `gm3d_body_create_capsule`, `gm3d_body_apply_impulse`, `gm3d_body_apply_torque`, `gm3d_body_apply_force`, `gm3d_body_set_velocity`, `gm3d_set_gravity`, `gm3d_body_set_friction`, `gm3d_body_set_restitution`, `gm3d_mesh_load`, `gm3d_mesh_load_glb`, `gm3d_mesh_load_obj`, `gm3d_raycast`, `gm3d_cleanup`).
- `3DTEST/objects/obj_3d_entity/`: Lightweight entity holding 3D transform `(x, y, z, qx, qy, qz, qw, scale)`, `mesh_id`, and `physics_body_id`.
- `3DTEST/objects/obj_3d_demo/`: Complete playable interactive demonstration:
  - **Create Event**: Initializes engine with 5,000 entity capacity, configures camera, creates static ground plane, loads custom `gem.obj`, loads glTF 2.0 binary models `UAL1_Standard.glb` and `UAL1_Standard_RM.glb`, spawns standing mannequins (Cobalt Blue and Gold) with Jolt rigid bodies, and spawns a 5x5 physics pyramid stack with real-time shadow casting.
  - **Step Event**:
    - Right-mouse drag / Arrow keys: Orbit camera.
    - Mouse wheel: Zoom in/out.
    - Left-click: Shoot heavy physics cannonball.
    - SPACE: Spawn dynamic falling bodies (cubes, spheres, cylinders).
    - C: Spawn custom OBJ Crystal Gem (`gem.obj`) with physical box collision.
    - M: Spawn dynamic Unreal Mannequin (`.glb`) with physical box collision and impulse/torque.
    - G: Cycle gravity between Earth ($-9.81$), Zero-G ($0.0$), and Moon ($-1.62$).
    - T: Apply angular torque whirlwind (spins all active bodies in 3D).
    - Zero-copy sync, fixed-step Jolt tick & D3D11 PBR render with PCF shadows.
  - **Draw GUI Event**: HUD showing real FPS, step FPS, active entity count, current gravity mode, and controls.
  - **CleanUp Event**: Safely invokes `gm3d_cleanup()`.
- `3DTEST/rooms/Room1/Room1.yy`: Primary room containing an instance of `obj_3d_demo`.

### Validation
Validated via GameMaker LTS 2026 Core Resources ProjectTool:
```bat
"C:\ProgramData\GameMakerStudio2-LTS2026\Plugins\@gm-tools-project-tool-win-x64\ProjectTool.exe" PROJECT OPEN SOURCE="C:\GM3Dext\3DTEST\3DTEST.yyp"
```
Result: `ProjectTool Successful` (0 errors).

---

## 12. Key Pitfalls & Solutions Log (Reference for Future Work)

### 1. Back-Face Culling vs Two-Sided Shading
- **Symptom**: Cylinders and custom OBJ models appeared hollow, with end-caps or side walls invisible under certain angles.
- **Root Cause**: 
  1. Default Direct3D 11 `CullMode = D3D11_CULL_BACK` culls any triangle whose vertex order is Counter-Clockwise (CCW).
  2. Standard Wavefront OBJ exporters (Blender, Maya) default to CCW winding.
  3. The procedural cylinder had inverted winding on its top/bottom caps.
- **Fix**:
  1. Set `rd.CullMode = D3D11_CULL_NONE` and `shadowRd.CullMode = D3D11_CULL_NONE`.
  2. In `PS_Main`, added `bool isFrontFace : SV_IsFrontFace` and inverted normal $N = -N$ for back-faces. This guarantees full PBR microfacet lighting on both sides of any polygon.
  3. Aligned procedural cylinder and capsule indices to Clockwise (CW) outward winding.
  4. Corrected `gem.obj` facet winding to outward CW.

### 2. GameMaker DLL ABI (>4 Arguments Constraint)
- In GML extensions, DLL functions with more than 4 arguments must have identical argument types (all `ty_real` / `double`).

### 3. HLSL Matrix Layout
- In D3D11 constant buffers, matrices are column-major by default. Declaring `row_major float4x4` and compiling with `D3DCOMPILE_PACK_MATRIX_ROW_MAJOR` matches C++ memory layout.

### 4. glTF Skeletal Skinning Matrix Conventions & Identity Invariant
- **Symptom**: Skinned character vertices exploded into stretched origami-like spikes upon loading.
- **Root Cause**:
  1. In glTF, inverse bind matrices are stored as 16 consecutive floats in **column-major** order ($c_{00}, c_{10}, c_{20}, c_{30}, \dots$).
  2. In DirectXMath / HLSL row-vector conventions, reading consecutive floats directly into a row-major `XMFLOAT4X4` treats the memory layout as ($r_{00}, r_{01}, r_{02}, r_{03}, \dots$).
  3. This sequential reading automatically performs the transpose $(M_{col})^T = M_{row}$, which is mathematically required because row-vector transformations satisfy $(M \cdot v)^T = v^T \cdot M^T$.
  4. Manually calling `XMMatrixSet(m[0], m[4], m[8], m[12], ...)` performed an unnecessary second transpose, resulting in a column-major matrix being multiplied by a row-major matrix ($M_{skin} = M_{invBind} \times M_{global}$). This produced an error norm of $682.42$ instead of $0.0$ in bind pose.
  5. In bind pose, every bone's skinning matrix $M_{skin}[j] = M_{invBind}[j] \times M_{global}[j]$ **must identically equal the Identity matrix ($I$)** ($M_{skin} \cdot v_{bind} = v_{bind}$).
- **Fix**:
  1. In `MeshManager.cpp`, copy glTF inverse bind floats directly into `XMFLOAT4X4` via `memcpy`.
  2. In `AnimationData.h`, evaluate $M_{skin} = M_{invBind} \times M_{global}$. In bind pose, the error norm drops from $682.42$ to $0.000062$ (machine epsilon).
  3. Pre-size and pad `out_skin_matrices` to 128 bones with Identity matrices to prevent uninitialized memory in `m_bone_constant_buffer` when mapped with `D3D11_MAP_WRITE_DISCARD`.
  4. Implemented memoized recursive topological traversal in `SkeletonData::evaluate_pose` to correctly evaluate hierarchies regardless of bone index order.

---

## 13. Texture Management & PBR Material Texturing (`TextureManager`)

### Architecture
- **Hardware Integration**: Built directly on **Windows Imaging Component (WIC)** (`wincodec.h` / `windowscodecs.lib`), supporting standard image formats (PNG, JPG, BMP, TIFF) from disk files or memory streams with zero external dependencies.
- **Hardware Mipmapping**: Automatic GPU-accelerated mipmap generation via `context->GenerateMips(srv.Get())` on creation, eliminating distance shimmering and aliasing.
- **Anisotropic Filtering**: 16x Anisotropic sampler with `WRAP` addressing for razor-sharp textures at extreme oblique angles.
- **Batching & Instancing**: GPU instanced rendering batches items by `(mesh_id, texture_id)`, minimizing Direct3D 11 state changes.
- **Built-in Procedural Textures**:
  - `0`: Default Pure White (1x1 neutral fallback for solid PBR materials).
  - `1`: UV Checkerboard (512x512 high-contrast Amber/Slate testing grid).
  - `2`: Sci-Fi Tech Grid (512x512 Dark Slate with glowing Cyan lines).
  - `3`: Weathered Wooden Planks (512x512 procedural timber boards).
  - `4`: Industrial Hazard Metal (512x512 yellow/charcoal safety stripes).
- **GML API**:
  - `gm3d_texture_load(filepath)` -> loads any custom PNG/JPG into GPU VRAM and returns a handle.
  - Setting `entity.texture_id = id` immediately applies the texture to that object.

---

## 14. Dynamic Lighting System (Directional Sun & Dynamic Point Lights)

### Architecture & Shading Pipeline
- **Directional Sunlight (Sun)**:
  - Dynamically configurable direction vector $\vec{D}_{sun}$, RGB color $\mathbf{C}_{sun}$, and intensity $I_{sun}$.
  - Dynamically recalculates the orthographic shadow map camera view-projection matrix whenever the sun angle shifts:
    $$\mathbf{P}_{lightEye} = \mathbf{P}_{focus} - \vec{D}_{sun} \times \text{distance}$$
  - Full 2048x2048 hardware 16-tap PCF (Percentage-Closer Filtering) for soft, anti-aliased shadow penumbrae.
- **Dynamic Point Lights (Up to 16 Concurrent Lights)**:
  - Configurable position $(X, Y, Z)$, effective radius $R$, RGB color, and intensity.
  - Constant buffer `LightConstants : register(b2)` packed cleanly to 528 bytes (16-byte aligned):
    ```hlsl
    struct PointLightData {
        float3 position;
        float radius;
        float3 color;
        float intensity;
    };
    cbuffer LightConstants : register(b2) {
        PointLightData g_point_lights[16];
        uint g_point_light_count;
        float3 g_light_pad;
    };
    ```
- **Cook-Torrance Microfacet BRDF Loop**:
  - Each point light computes physical inverse-square falloff with windowed cutoff to smoothly reach zero at the radius boundary:
    $$\text{dist} = \|\mathbf{P}_{light} - \mathbf{P}_{world}\|,\quad \text{normDist} = \frac{\text{dist}}{R},\quad \text{win} = \max(0, 1 - \text{normDist}^2),\quad \text{atten} = \frac{\text{win}^2}{\text{dist}^2 + 1}$$
  - Full microfacet evaluation (GGX normal distribution $D$, Schlick-Smith geometric shadowing $G$, and Schlick Fresnel $F$):
    $$f_r = k_d \frac{c_{albedo}}{\pi} + \frac{D \cdot G \cdot F}{4 (\vec{N} \cdot \vec{V})(\vec{N} \cdot \vec{L})}$$
    $$\text{radiance} = \mathbf{C}_{light} \cdot I_{light} \cdot \text{atten}$$
    $$L_{out} += f_r \cdot \text{radiance} \cdot \max(0, \vec{N} \cdot \vec{L})$$
- **GML API**:
  - `gm3d_light_set_sun(dx, dy, dz, r, g, b, intensity)`: Updates sun direction and color.
  - `gm3d_light_set_ambient(r, g, b, intensity)`: Sets global ambient fill light.
  - `gm3d_light_point_set(index, x, y, z, radius, r, g, b, intensity)`: Configures a dynamic point light (indices 0..15).
  - `gm3d_light_point_clear()`: Disables all dynamic point lights.

---

## 15. Atmosphere, Distance Fog & Playable Third-Person Character Controller

### Atmospheric Horizon Fog & Sky Clear Color
- **Sky Background Clear Color**:
  - Direct3D 11 swapchain viewport clear color dynamically controlled via `render_set_sky_color(r, g, b)`.
  - Seamlessly unified with the pixel shader's horizon fog color to eliminate any edge artifacts or cutoff at the camera clipping boundary.
- **Distance Fog Model**:
  - Shader constant buffer `SceneConstants : register(b0)` passes `FogColor`, `FogStart`, and `FogEnd`.
  - Linear smooth saturation falloff:
    $$\text{dist} = \|\mathbf{P}_{world} - \mathbf{P}_{camera}\|,\quad \text{fogFactor} = \text{saturate}\left(\frac{\text{dist} - \text{fogStart}}{\text{fogEnd} - \text{fogStart}}\right)$$
    $$C_{final} = \text{lerp}(C_{lit}, \mathbf{C}_{fog}, \text{fogFactor})$$
  - Day mode features a soft atmospheric blue horizon (`0.38, 0.52, 0.68`).
  - Night mode features a deep midnight mist (`0.03, 0.04, 0.07`), where dynamic point lights pierce dramatically through the gloom.

### Playable Third-Person Character Controller Architecture
- **Dual Camera / Control Modes**:
  - **Mode 0 (Free Orbit Camera)**: Classic scene inspection camera with free spherical panning, projectile shooting (LMB), and physics cube/sphere spawning.
  - **Mode 1 (Third-Person Playable Character)**: Full over-the-shoulder third-person gameplay with responsive WASD movement relative to camera orientation.
- **Kinematic Character Physics**:
  - Player character uses a Jolt kinematic capsule collider (`GM3D_MOTION_KINEMATIC`).
  - Synced to the DLL via `physics_set_transform(body_id, x, y, z, qx, qy, qz, qw)`.
  - Pushes dynamic rigid bodies smoothly out of the way.
- **Animation State Machine**:
  - **Idle**: `Idle_Loop` (Anim 9) when stationary.
  - **Walk**: `Walk_Loop` (Anim 42) at 4.8 m/s.
  - **Sprint**: `Sprint_Loop` (Anim 36) at 9.5 m/s when holding Shift.
  - **Jump**: Vertical jump velocity on Space key with gravity acceleration.
  - **Heavy Punch**: `Punch_Cross` (Anim 24) on Left Click or 'E', applying explosive physical shockwave impulses to all nearby dynamic rigid bodies!

---

## 16. Tangent-Free Normal Mapping & Procedural Surface Micro-Details

### Architecture & Mathematical Foundations
To avoid bloating the vertex layout or requiring custom vertex tangent streams that could break skinned mesh deformations or procedural meshes, GM3D employs **Christian Schüler's Screen-Space Tangent Reconstruction** in HLSL:

$$\vec{dp}_1 = \frac{\partial \mathbf{P}}{\partial x},\quad \vec{dp}_2 = \frac{\partial \mathbf{P}}{\partial y}$$
$$\vec{duv}_1 = \frac{\partial \mathbf{UV}}{\partial x},\quad \vec{duv}_2 = \frac{\partial \mathbf{UV}}{\partial y}$$
$$\vec{dp}_{2\perp} = \vec{dp}_2 \times \vec{N},\quad \vec{dp}_{1\perp} = \vec{N} \times \vec{dp}_1$$
$$\vec{T} = \vec{dp}_{2\perp} \cdot duv_{1.x} + \vec{dp}_{1\perp} \cdot duv_{2.x}$$
$$\vec{B} = \vec{dp}_{2\perp} \cdot duv_{1.y} + \vec{dp}_{1\perp} \cdot duv_{2.y}$$
$$\text{invmax} = \frac{1}{\sqrt{\max(\|\vec{T}\|^2, \|\vec{B}\|^2)}},\quad \text{TBN} = [\vec{T} \cdot \text{invmax},\; \vec{B} \cdot \text{invmax},\; \vec{N}]$$

### Procedural Normal Map Generation & Auto-Sobel Filtering
- **Flat Normal Fallback**: A default 1x1 normal map `[128, 128, 255, 255]` ($\vec{N} = (0, 0, 1)$) is bound whenever an albedo texture has no normal map.
- **Sobel Height-to-Normal Generator**: Automatically calculates $\frac{\partial H}{\partial x}$ and $\frac{\partial H}{\partial y}$ across image luminance for any loaded or procedural texture, converting height deltas into normalized tangent-space normal vectors:
  $$\vec{N}_{tangent} = \text{normalize}\left(\left[-S_x \cdot \text{strength},\; -S_y \cdot \text{strength},\; 1.0\right]\right)$$
- **Built-in Procedural Normal Maps**:
  - `Texture 1 (UV Checkerboard)`: 3D beveled tile seams with subtle stone micro-grain (`strength = 2.8`).
  - `Texture 2 (Tech Grid)`: Recessed cybernetic grooves, circuit trenches, and bolt insets (`strength = 3.5`).
  - `Texture 3 (Wood Planks)`: Deep vertical board grooves, directional wood fiber striations, and steel nail depressions (`strength = 4.0`).
  - `Texture 4 (Hazard Metal)`: Industrial raised diagonal grip treads, tread plate bevels, and border rivets (`strength = 3.5`).
- **Dual Slot Pixel Shader Binding**:
  - Slot 1 (`t1`): Albedo Map.
  - Slot 2 (`t2`): Normal Map.
  - Bound atomically with `m_context->PSSetShaderResources(1, 2, srvs)` to preserve high-performance instanced rendering batches.
- **GML API**:
  - `gm3d_texture_set_normal(target_tex_id, normal_tex_id)`: Explicitly links a normal map texture to any albedo slot.

---

## 17. In-Game 3D Scene Editor, 3D Axis Gizmo & Scene Serialization

### In-Game 3D Scene Editor Architecture
- **Interactive Mode Toggle (`Tab`)**:
  - Toggles between real-time gameplay mode and interactive 3D scene editing.
- **Raycast & Bounding Selection**:
  - Unprojects the 2D mouse cursor $(mx, my)$ into a normalized 3D world ray using camera view/projection frustum trigonometry:
    $$\vec{D} = \text{normalize}\left(\vec{F} + \vec{R} \cdot (ndc_x \cdot \text{aspect} \cdot \tan\frac{\theta}{2}) + \vec{U} \cdot (ndc_y \cdot \tan\frac{\theta}{2})\right)$$
  - Casts a ray through Jolt Physics via `physics_raycast(...)`.
  - Fallback nearest ray-to-bounding-sphere intersection ensures any visual object (even non-physics models) can be selected.
- **3D World-to-Screen Gizmo Projection**:
  - Direct3D view coordinates projected to 2D screen coordinates using `gm3d_world_to_screen(...)`:
    $$vz = \mathbf{P}_{rel} \cdot \vec{F},\quad vx = \mathbf{P}_{rel} \cdot \vec{R},\quad vy = \mathbf{P}_{rel} \cdot \vec{U}$$
    $$sx = \frac{W}{2} + \left(\frac{vx}{vz \cdot \tan\frac{\theta}{2} \cdot \text{aspect}}\right) \cdot \frac{W}{2},\quad sy = \frac{H}{2} - \left(\frac{vy}{vz \cdot \tan\frac{\theta}{2}}\right) \cdot \frac{H}{2}$$
  - Renders red (+X), green (+Y), and blue (+Z) axis vectors directly over the selected entity with distance-adaptive scaling.
  - Displays a live HUD tooltip showing Entity ID, Mesh ID, Material ID, Texture ID, 3D Coordinates $(x, y, z)$, and Scale.
- **Real-Time Entity Manipulation**:
  - **Position**: `I / K` (Z forward/back), `J / L` (X left/right), `U / O` (Y up/down) or Numpad keys. Automatically updates Jolt rigid body transforms via `gm3d_body_set_transform`.
  - **Scale**: `+ / -` keys smoothly scale the entity and its collision boundary.
  - **Material Cycle**: `[` and `]` cycle between 6 PBR presets (Concrete, Ruby, Gold, Emerald, Cobalt, Amethyst).
  - **Texture Cycle**: `;` and `'` cycle between 5 textured surfaces (Procedural, Checker, Tech Grid, Wood, Hazard).
  - **Duplicate**: `Insert` or `Ctrl+D` clones the entity at an offset with active physics.
  - **Delete**: `Del` cleanly removes both the GML instance and its native Jolt rigid body.
- **Scene Serialization (`scene_data.json`)**:
  - `Ctrl+S`: Exports all scene entities (mesh, material, texture, transform, scale, physics flags) to a human-readable, formatted `scene_data.json` file.
  - `Ctrl+O` / `Ctrl+L`: Loads and reconstructs the scene from `scene_data.json`, repopulating both the GameMaker entity graph and native Jolt rigid bodies.

---

## 18. Multi-Mode Showcase Architecture, Visual UI Inspector & Left-Handed Projection

### Top Navigation Bar & 5 Dedicated Showcase Modes
The engine provides instant live transitions across 5 dedicated showcase modes via `build_scene(mode)`:
1. **🏝️ Ocean Island (`MODE_ISLAND = 0`)**:
   - Vast azure ocean water plane ($260 \times 260\text{ m}$, `material_id = 4`).
   - Central raised sand island plateau ($24 \times 0.8 \times 24\text{ m}$, `material_id = 2`).
   - Perimeter gem boulders, ancient columns, and dynamic rigid bodies.
   - Third-person playable character with physical shockwave attacks (`Punch_Cross`), sprinting, jumping, and inverted camera Y-axis.
2. **🛠️ 3D Scene Editor (`MODE_EDITOR = 1`)**:
   - Full 3D viewport with auto-selected entity, projecting interactive 3D Gizmos.
   - 265 px wide visual UI panel with clickable on-screen buttons (no hotkeys required).
   - Real-time spawner, material/texture pallete, transform controls, clone/delete, and JSON serialization.
3. **⚙️ Jolt Physics Lab (`MODE_PHYSICS = 2`)**:
   - Stacked pyramid of 25 dynamic rigid bodies.
   - Dancing mannequins.
   - Live on-screen buttons for explosive physical shockwaves, zero-gravity / earth-gravity toggles, and impulse generators.
4. **💡 Lighting & Atmosphere Studio (`MODE_LIGHTING = 3`)**:
   - Midnight atmosphere with deep fog.
   - Central obsidian altar with rotating crystal.
   - 3 dynamic point lights orbiting smoothly (amber, cyan, magenta) with live radius/intensity controls.
5. **🔮 Materials & Normal Map Gallery (`MODE_MATERIAL = 4`)**:
   - 5 pedestals showcasing Sobel-filtered normal maps (Checker, Tech Grid, Wood Planks, Hazard Metal, Amethyst Crystal).
   - Sweeping grazing-angle light to accentuate tangent-space micro-relief.

### Screen-Space UI & Direct3D 11 Left-Handed Matrix Projection
- **DPI-Safe Mouse Interaction**: All visual buttons use `device_mouse_x_to_gui(0)` and `device_mouse_y_to_gui(0)` ensuring pixel-perfect clicks regardless of window scaling.
- **D3D11 Left-Handed Alignment**: `gm3d_world_to_screen` and `gm3d_screen_to_ray` are mathematically aligned with Direct3D 11 Left-Handed matrices (`ComputeLookAtLH` and `PerspectiveFovLH`), guaranteeing that 3D Gizmo axis arrows match world coordinates.
- **Inverted Y-Axis**: Configured with `invert_cam_y = true` by default and switchable via an on-screen toggle button.

---

## 19. Native Interactive F5 Menu & Hotkey Control System

### Architecture & Rationale
When Direct3D 11 rendering takes exclusive ownership of the HWND presentation swapchain via DXGI (`m_swap_chain->Present`), standard GameMaker 2D surfaces can become occluded by the DWM presentation queue. To guarantee 100% visibility, unconstrained by graphics backbuffer states, GM3D features a **Native Win32 User32 Popup Menu** invoked via `F5`:

- **Win32 User32 Top-Level Presentation**:
  - `show_f5_menu(current_mode, invert_y)` uses `CreatePopupMenu()` and `TrackPopupMenu(...)`.
  - Native Windows popup menus are rendered directly by the Windows Desktop Window Manager (DWM) as top-level window overlays, guaranteeing they appear on top of any DirectX 11, Vulkan, or OpenGL surface.
  - Clean English ASCII strings with native mouse hover, clickable items, and keyboard navigation.
- **Menu Items & Capabilities**:
  - `[1] Ocean Island`: Switches to third-person playable character exploring ocean & plateau.
  - `[2] 3D Scene Editor`: Switches to interactive 3D scene builder.
  - `[3] Jolt Physics Sandbox`: Switches to rigid body pyramid sandbox.
  - `[4] Lighting & Fog Studio`: Switches to studio lighting with orbiting colored lights.
  - `[5] Material Showcase`: Switches to normal map exhibition gallery.
  - `Invert Mouse Y-Axis [ENABLED / DISABLED]`: Toggles vertical camera look inversion.
  - `Trigger Physical Shockwave`: Applies outward radial impulses to all dynamic rigid bodies.
  - `Cycle Gravity Mode`: Cycles between Earth ($9.81\text{ m/s}^2$), Zero-G ($0$), and Moon ($1.62\text{ m/s}^2$).
  - `Spawn Dynamic Cube / Sphere / Dancing Mannequin`: Spawns dynamic cubes, spheres, or dancing mannequins.
- **Direct Hotkeys (Instant Switching)**:
  - `1`, `2`, `3`, `4`, `5`: Instant mode switching without opening the menu.
  - `Y`: Instant camera Y-axis inversion toggle.
  - `F5`: Opens the full interactive menu at cursor position.
- **Dynamic Window Title Caption**:
  - `window_set_caption(...)` continuously reflects current active mode, Y-axis inversion status, FPS, and hotkey hints.

---

## 20. Direct3D 11 Native 3D Transform Gizmo & Selection Framing

### Architecture & Guaranteed Visibility
Because the engine renders directly to the OS window via an exclusive Direct3D 11 swapchain, rendering the 3D Gizmo directly inside the native D3D11 graphics pipeline (`RenderWorld::draw_gizmo`) guarantees 100% visibility regardless of window manager state:
- **Depth State (`DepthEnable = FALSE`)**:
  - The Gizmo is rendered with depth testing disabled (`m_gizmo_depth_stencil_state`), ensuring that arrows, center pivot, and selection bounding frames are **always drawn in front** of all scene geometry.
- **Unlit Emissive Shader Materials (`matId >= 100`)**:
  - `matId = 100`: Radiant Red (+X Axis)
  - `matId = 101`: Radiant Green (+Y Axis)
  - `matId = 102`: Radiant Blue (+Z Axis)
  - `matId = 103`: Radiant Yellow (Pivot Center Hub)
  - `matId = 104`: Golden Amber (12-edge Selection Bounding Frame)
- **Distance-Adaptive Scaling**:
  - Scales dynamically with camera distance ($d \times 0.075$) to maintain a consistent, comfortable size in the viewport whether zooming in or out.
- **Editor Controls**:
  - `Tab`: Cycle active selected object in the scene.
  - `I / K`: Translate along Z-axis (forward/backward).
  - `J / L`: Translate along X-axis (left/right).
  - `U / O`: Translate along Y-axis (up/down).
  - `+ / -`: Smoothly scale selected object.
  - `[ / ]`: Cycle PBR material palette.
  - `Del`: Delete entity.
  - `Ctrl+D / Insert`: Duplicate entity with active Jolt physics.

---

## 21. GPU Magic Particle System & Animated Ocean Water Shader

### GPU Magic Particle System (`ParticleManager`)
- **Direct3D 11 Instanced Billboards**:
  - High-performance instanced billboard rendering capable of simulating tens of thousands of particles at maximum refresh rates.
  - Camera-facing quad orientation computed analytically in the Vertex Shader.
- **Additive Blending (`D3D11_BLEND_SRC_ALPHA`, `D3D11_BLEND_ONE`)**:
  - Particles accumulate light exponentially where they overlap, creating intense, blindingly vibrant magical radiance.
  - Depth read-only state (`DepthWriteMask = ZERO`) prevents z-fighting while preserving proper occlusions behind world geometry.
- **5 Magic Effect Presets**:
  1. `PARTICLE_FIRE (0)`: Rising flames with swirling glowing embers and smoke dissipation.
  2. `PARTICLE_ARCANE (1)`: Celestial cyan & purple swirling energy vortex with inward/outward spiral motion.
  3. `PARTICLE_HOLY (2)`: Sparkling golden divine light motes with floating ascent.
  4. `PARTICLE_FROST (3)`: Glacial blue crystal shards with glitter and decelerating drift.
  5. `PARTICLE_SHOCKWAVE (4)`: Ground-aligned horizontal expanding energy rings triggered by physical impacts.
- **Player Character Spell Casting (`MODE_ISLAND`)**:
  - **LMB Punch**: Heavy physical punch accompanied by an expanding ground shockwave ring and fiery impact burst.
  - **Key `Q`**: Fire Blast (explosive fireburst with physics body knockback).
  - **Key `E`**: Arcane Nova (radial purple/cyan energy burst).
  - **Key `R`**: Holy Radiance (ascending golden aura rays).
  - **Key `F`**: Frost Blizzard (crystalline icy spray).
  - **Sprinting**: Generates dynamic footstep particles and water splashes.

### Animated Ocean Water Shader
- Dual-frequency procedural animated Gerstner wave displacement in HLSL.
- Dynamic normal perturbation simulating 3D wave facets and specular sun glints.
- Deep azure to shallow aqua color gradients based on wave height.
- White sea foam on wave crests and shore boundaries.

---

## 22. Direct3D 11 HDR Post-Processing Pipeline (Bloom & ACES Tone Mapping)

### Architecture & Pass Flow
The engine features a native high dynamic range (HDR) rendering pipeline designed to make magical particle effects, glowing materials, and specular sunlight glints bloom vividly:

1. **HDR Offscreen Render Target**:
   - Format: `DXGI_FORMAT_R16G16B16A16_FLOAT` (64-bit floating-point buffer).
   - Entire 3D scene (PBR geometry, shadows, ocean water) and GPU magic particles are rendered in unconstrained float values exceeding 1.0.
2. **Post-Processing Passes (Zero Vertex Buffer Overhead)**:
   - Uses a fullscreen procedural triangle shader (`VS_Fullscreen` with `uint vertexID : SV_VertexID`), bypassing CPU vertex buffer allocation and state validation.
3. **Pass 1: Bright Pass Downsample / Bloom Extraction (`PS_ExtractBloom`)**:
   - Viewport: Quarter resolution ($\frac{W}{4} \times \frac{H}{4}$) into ping-pong texture `m_bloom_tex[0]`.
   - Computes perceptual luminance: $L = 0.2126R + 0.7152G + 0.0722B$.
   - Isolates bright areas exceeding `BloomThreshold` ($L > 1.0$), scaling the excess smoothly.
4. **Pass 2: Separable Horizontal Gaussian Blur (`PS_BlurH`)**:
   - 9-tap horizontal blur filtering sampled across texel offsets into `m_bloom_tex[1]`.
5. **Pass 3: Separable Vertical Gaussian Blur (`PS_BlurV`)**:
   - 9-tap vertical blur filtering sampled across texel offsets back into `m_bloom_tex[0]`.
6. **Pass 4: Composite, ACES Filmic Tone Mapping & Color Grading (`PS_Composite`)**:
   - Viewport: Full screen ($W \times H$) into the backbuffer swapchain (`m_render_target_view`).
   - Blends pure linear HDR scene buffer with blurred bloom: $C_{\text{final}} = (C_{\text{HDR}} + C_{\text{bloom}} \times \text{Intensity} \times 0.45) \times \text{Exposure}$.
   - Applies standard Stephen Hill / Narkowicz ACES filmic tone map curve directly from linear HDR (eliminating double-tonemapping washed-out artifacts):
     $$\text{ACESFilm}(x) = \text{saturate}\left(\frac{x(2.51x + 0.03)}{x(2.43x + 0.59) + 0.14}\right)$$
   - **Cinematic Contrast S-Curve**: Enhances perceptual shadow depth via cubic Hermite contrast ($\text{smoothstep}(0, 1, C)$).
   - **Vibrance & Saturation Enhancement**: Enriches natural landscape pigments by $+22\%$ without skin-tone blowouts:
     $$L = 0.2126R + 0.7152G + 0.0722B, \quad C_{\text{vibrant}} = \text{lerp}(L, C, 1.22)$$
   - **Shadow & Highlight Split-Toning**: Cool azure shadows ($[0.96, 0.98, 1.05]$) with warm radiant sunlight highlights ($[1.02, 1.01, 0.96]$).
   - **Subtle Optical Vignette**: Radial edge falloff ($1.0 - \|\vec{UV} - 0.5\|^2 \times 0.45$) focusing the composition on the player.
   - Performs a single accurate hardware gamma correction: $C_{\text{display}} = C^{1 / 2.2}$.
7. **Controls**:
   - Exported function: `gm3d_render_set_bloom(enabled, intensity, threshold)`.
   - In-game toggle: Key `B`, top-right GUI button, or F5 Native Menu.

---

## 23. Procedural 16-bit PCM Audio Synthesizer & Spring-Arm Camera Collision

### Procedural 16-bit PCM Audio Synthesizer
To provide crisp, instantaneous physical and magical audio feedback without requiring external audio asset files or third-party audio engines, GM3D synthesizes raw 16-bit PCM waveforms directly into GameMaker memory buffers (`audio_create_buffer_sound`):
- **`snd_punch`**: Physical strike with rapid descending pitch (140 Hz $\to$ 40 Hz) blended with impact white noise.
- **`snd_fire`**: Roaring flame explosion combining filtered noise with a 65 Hz sub-bass rumble.
- **`snd_arcane`**: Crystalline celestial chord combining three harmonic sine frequencies (523 Hz + 1046 Hz + 1568 Hz).
- **`snd_holy`**: Ascending radiant light chord (440 Hz $\to$ 880 Hz).
- **`snd_frost`**: High icy crackle (1800 Hz shimmer modulated by 45 Hz AM tremolo) and blizzard wind.
- **`snd_shockwave`**: Deep sub-bass floor rumble (55 Hz $\to$ 25 Hz).

### Spring-Arm Camera Collision Detection
In both third-person character tracking (`MODE_ISLAND`) and free orbit inspection (`MODES 1..4`), a physics raycast (`gm3d_raycast`) is shot each frame from the focal target to the camera's desired position:
- If an obstacle (boulder, wall, pedestal, or rigid body) obstructs the line-of-sight, the camera is smoothly pulled inward along the ray by a clamped interpolation factor.
- An inward offset margin (0.35m) ensures the camera clipping plane never penetrates into or intersects geometry.

---

## 24. Procedural Multi-Biome Terrain & Jolt Physics HeightField Integration

GM3D incorporates a high-performance procedural terrain synthesis pipeline inspired by contemporary mountain landscape generation algorithms (analytic derivative-damped fBm and ridged multifractals), paired with 100% physically accurate collision via Jolt Physics `HeightFieldShape`.

### Algorithmic Architecture

1. **Value Noise with Analytic Derivatives**:
   - Quintic Hermite interpolation curve $S(t) = t^3 (t(6t - 15) + 10)$ guarantees $C^2$ continuous second derivatives.
   - Analytic gradients $(\frac{\partial n}{\partial x}, \frac{\partial n}{\partial z})$ are computed directly during noise evaluation, avoiding numerical finite-difference jitter.

2. **Derivative-Damped fBm Erosion (10 Octaves)**:
   - Successive octaves are damped by the accumulated analytical derivative vector length:
     $$\text{amp} = \text{amp} \times \frac{1.0}{1.0 + \gamma \cdot \|\vec{d}\|^2}$$
   - Simulates hydraulic erosion along steep slopes, carving realistic gullies, valleys, and riverbeds.

3. **Ridged Multifractals (7 Octaves)**:
   - Mountain ranges are formed by folding noise values: $r = 1.0 - |n|$.
   - Squaring the signal produces razor-sharp ridges, rugged cliffs, and natural peaks reaching elevations up to $32.0\text{m}$.

4. **Central Plaza Flattening & Island Perimeter Falloff**:
   - The central sanctuary zone ($r < 14\text{m}$) is smoothly flattened via a cubic Hermite transition to establish an architectural foundation for ancient stone columns and magical shrines.
   - A cosine falloff along the perimeter ($r > 85\text{m}$) gracefully slopes all shores into the surrounding infinite ocean plane ($Y = 0.0\text{m}$).

### Direct3D 11 Terrain Mesh Generation

- **Grid Resolution**: $128 \times 128$ vertex grid ($16,384$ vertices, $32,258$ triangles, $96,774$ indices).
- **Physical Extents**: $240.0\text{m} \times 240.0\text{m}$, centered at $(0, 0, 0)$.
- **Vertex Normals**: Evaluated via central-difference gradient vectors to ensure smooth diffuse lighting and specular reflections under sun and point lights.
- **Mesh Registration**: Uploaded to Direct3D 11 GPU buffers and cached under `GM3D_MESH_TERRAIN` (Mesh ID 5).

### Procedural Multi-Biome Splatting, Cavity AO & 2K Normal Map

A ultra-high-resolution $2048 \times 2048$ RGBA albedo map and matching $2048 \times 2048$ Sobel normal map are procedurally baked in native C++ at runtime using NewEra3D-inspired hypsometric shading and multi-scale cavity ambient occlusion:
- **Coastal Shoreline Sand** ($Y \in [0.0\text{m}, 2.4\text{m}]$): Warm golden beach sand with damp tide-line darkening near sea level.
- **Lowland Alpine Grassland & Moss** ($Y \in [1.6\text{m}, 9.0\text{m}]$): Rich, deep olive and emerald vegetation with organic multi-octave tonal patchiness.
- **Subalpine Highland Meadow & Sedge** ($Y \in [7.5\text{m}, 16.5\text{m}]$): Golden-green dry alpine turf and highland sedge.
- **High Mountain Scree & Slate Rock** ($Y \in [14.0\text{m}, 25.5\text{m}]$): Weathered mineral granite and fractured shale scree.
- **Sheer Cliff Bedrock with Geological Strata** ($\text{slope} > 0.36$, $\sim 20^\circ$): Exposed dark basalt bedrock modulated by horizontal sedimentation beds.
- **Alpine Summit Snow** ($Y > 25.5\text{m}$ on gentle slopes $\text{slope} < 0.60$): Soft alpine snow caps on high peaks with continuous Hermite blending (no threshold holes or noise cutouts).
- **Multi-Scale Cavity Ambient Occlusion**: Neighborhood curvature is sampled at radius $R = 4.5\text{m}$. Depressions, crevices, gullies, and ravines are darkened in proportion to depth ($\text{cavity} \in [0.42, 1.08]$), delivering rich volumetric depth.
- **Ancient Plaza Stone**: Flagstone/cobblestone patterning with subtle mortar joint tinting across the central sanctuary.
- **Texture Registration**: Uploaded directly as DXGI 2D textures and cached under `GM3D_TEX_TERRAIN` (Texture ID 5).

### Jolt Physics HeightField Integration

- The raw elevation grid is packed into 16-bit Jolt `HeightFieldShapeSettings` with physical coordinate offsets matching the visual D3D11 mesh:
  - `mOffset = Vec3(-world_size * 0.5f, 0.0f, -world_size * 0.5f)`
  - `mScale = Vec3(cell_size, 1.0f, cell_size)`
- Instantiated as a static collision body in the Jolt Physics simulation world.
- Supports physical interactions with all dynamic rigid bodies (such as spherical granite boulders rolling down mountain ravines) and spring-arm camera raycasts.

### Sub-Nanosecond Bilinear Elevation Query

- **Function**: `gm3d_terrain_get_height(world_x, world_z)`
- Computes cell indices and performs bilinear filtering across adjacent height grid samples in $O(1)$ time ($< 5\text{ns}$ overhead).
- Allows the playable character controller to track undulating ground contours, step over small obstacles, and launch into airborne state when walking off sheer drops.

---

## 25. Procedural Atmospheric Sky Dome, Height Fog & Environmental Foliage

GM3D features a comprehensive environmental enhancement suite providing cinematic sky scattering, physical elevation mist, procedural terrain micro-texturing, and instanced vegetation.

### 1. Procedural Atmospheric Sky Dome & Radiant Sun Disk
- **Rendering Pass**: Rendered into the HDR offscreen buffer prior to opaque geometry using a fullscreen procedural triangle (`SV_VertexID` at depth $Z/W = 1.0$) with depth write disabled and `D3D11_COMPARISON_LESS_EQUAL`.
- **Analytical Unprojection**: Unprojects NDC coordinates back to world space via `InvViewProj` ($XMMatrixInverse$), eliminating all 3D mesh geometry, clipping boundaries, or vertex buffers.
- **Atmospheric Rayleigh Scattering**:
  - Vertical gradient transitioning smoothly from celestial deep azure at the zenith ($V_y = 1.0$) to luminous atmospheric haze at the horizon ($V_y = 0.0$).
- **Radiant Sun Disk & Solar Corona**:
  - Emissive HDR core radiance ($\cos^{512} \times 12.0$) isolated by `PS_ExtractBloom` to generate wide lens glare and bloom.
  - Wide Mie scattering halo ($\cos^8 \times 0.35$) modulated by `SunColor`.

### 2. Height-Aware Exponential Fog
- GM3D implements dual-component **Height-Aware Fog**:
  $$\text{Fog}_{\text{height}} = \exp\left(-\max(0, Y) \cdot 0.12\right)$$
  $$\text{Fog}_{\text{total}} = \text{saturate}\left(\text{Fog}_{\text{distance}}^2 \cdot \text{Fog}_{\text{height}}\right)$$
- Lowland valleys and shorelines accumulate dense mystical morning haze.
- Alpine mountain peaks ($Y > 18.0\text{m}$) pierce above the mist layer into crystal clear blue sky, preserving razor-sharp ridge silhouettes and contrast.
- Foreground and midground ($< 95\text{m}$) remain 100% crystal clear.

### 3. Distance-Faded Procedural Micro-Detail Terrain Tiling & Cliff Strata
- Eliminates low-resolution texture stretching by combining macro-biome albedo maps with world-space high-frequency micro-grain.
- **Distance-Based Mip Fading**: Procedural normal perturbation and micro-contrast are active within $15\text{m}$ under the player's feet and smoothly blend to zero by $30\text{m}$, completely eliminating high-frequency moiré patterns and ripple aliasing from aerial and orbit camera perspectives.
- **Natural Biome Elevation Ramping**: Alpine snow is restricted to high summits ($Y > 22.5\text{m}$), leaving mid-mountain slopes ($Y \in [4\text{m}, 14\text{m}]$) as lush meadows and slate rock scree.
- Evaluates surface slope steepness to blend dark granite rock strata onto sheer cliff walls ($> 18^\circ$).

### 4. Procedural Stylized Pine Trees (`GM3D_MESH_PINE_TREE`)
- Built-in Mesh ID 6 (`GM3D_MESH_PINE_TREE`): Low-poly stylized pine tree composed of a cylindrical trunk and three stacked conical foliage tiers ($40$ vertices, $48$ triangles).
- Material ID 6 differentiates trunk bark from deep evergreen needle foliage.
- Valleys are populated with 100+ instanced trees, casting real-time soft PCF shadows and providing immediate visual scale for surrounding mountains.

---

## 26. 3D Editor Transform Gizmo & Pipeline Isolation

GM3D provides an in-engine interactive 3D transformation gizmo and object selection bounding box for level editing and debugging.

### Pipeline Execution & Hazard Prevention
- **Render Pass Ordering**: The gizmo is drawn directly into the offscreen HDR scene render target (`m_hdr_rtv`) immediately after the main opaque geometry pass and before GPU particle rendering and HDR post-processing.
- **Strict State Management**:
  - `draw_gizmo()` explicitly binds `m_vertex_shader`, `m_pixel_shader`, `m_input_layout`, and primitive topology `D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST`.
  - Constant buffers (`m_constant_buffer` and `m_bone_constant_buffer`) are rebound to eliminate any state leakage from previous passes.
  - Upon completion, `m_depth_stencil_state` is restored to guarantee depth buffer integrity.
- **Single-Pass Batched Instancing**:
  - Rather than issuing individual draw calls and buffer mappings for each axis and edge, all 19 constituent gizmo elements (12 bounding box edges, 1 center pivot cube, 6 arrow shafts and arrowheads) are collected into a single CPU array.
  - Copied into `m_instance_buffer` via a single `D3D11_MAP_WRITE_DISCARD` operation and dispatched in a single `DrawIndexedInstanced(cube_mesh->index_count, 19, 0, 0, 0)` call.
- **Shader Early-Out Emissive Bypass**:
  - At the very top of `PS_Main`, material identifiers $\ge 100$ branch immediately to return pure unlit emissive colors:
    - `100`: Bright Red (+X Axis)
    - `101`: Bright Green (+Y Axis)
    - `102`: Bright Blue (+Z Axis)
    - `103`: Bright Yellow (Pivot Hub)
    - `104`: Amber Gold (Selection Bounding Box)
  - This bypass avoids texture sampling, normal mapping, or lighting math on gizmo geometry, completely preventing GPU pipeline invalidation or device timeout (TDR).

---

## 27. Scene Interaction Overhaul, 6DOF Spectator Camera & FPS Diagnostics

GM3D provides an intuitive, high-performance interaction framework across all application modes.

### 1. Viewport Interaction & Mouse Capture
- **Mouse Look Locking (`Tab` or `M`)**: Toggles hardware mouse cursor capture via `display_mouse_set()` centered on the window HWND, hiding the cursor (`cr_none`) and providing standard first-person/third-person 360-degree continuous mouse-look without bumping against window edges.
- **Escape Key (`Esc`)**: Instantly releases mouse lock and restores the system cursor (`cr_default`) for desktop interaction.
- **Hold RMB Look**: When cursor is unlocked, holding the Right Mouse Button (`mb_right`) enables smooth camera orbit and mouse-look.

### 2. Ocean Island Camera Modes (Key `C`)
- **Mode 1: 6DOF Free-Fly Spectator Drone**:
  - `W` / `S`: Fly forward and backward along the 3D line of sight.
  - `A` / `D`: Strafe horizontally left and right.
  - `Space` / `E`: Fly upwards (+Y).
  - `Q` / `Ctrl`: Fly downwards (-Y).
  - `Shift`: High-speed sprint boost ($65.0\text{m/s}$ vs $22.0\text{m/s}$).
  - **Ground Collision Clearance**: Automatically clamps minimum elevation to $\text{terrain\_height} + 0.6\text{m}$ to prevent clipping into subterranean void.
- **Mode 2: Third-Person Character Controller**:
  - `W`, `A`, `S`, `D`: Locomotion oriented relative to camera azimuth.
  - `Shift`: Sprinting with biome-aware footstep particles (ocean spray, valley dust, summit snow puffs).
  - `Space`: Physics-driven jump with terrain elevation tracking.
  - `LMB`: Heavy punch combo with forward shockwave and physics force impulse.
  - `Q`: Fire Blast spell (directional fiery burst with physics knockback).
  - `E`: Arcane Nova spell (radial shockwave blasting surrounding rigid bodies).
  - `R`: Holy Radiance heal.
  - `F`: Frost Blizzard blast.

### 3. 3D Scene Editor Overhaul (`MODE_EDITOR`)
- **Arrow Keys**: Move selected object along X (Left/Right) and Z (Up/Down) axes.
- **PageUp / PageDown** (or `O` / `U`): Elevate selected object along Y axis.
- **Shift + Mouse Wheel**: Precision vertical elevation adjustment.
- **Mouse Wheel**: Camera orbit zoom ($2.5\text{m} \dots 90.0\text{m}$).
- **Q / E Keys**: Continuous yaw rotation of the selected entity.
- **Smooth Target Tracking**: Camera orbit center (`cur_target_x/y/z`) automatically smoothly lerps to the active selected entity, keeping whatever is being edited in clear focus.
- **Tab Key**: Cycles through all entities in the scene.
- **Viewport Raycast Selection**: Left-clicking any object in the 3D viewport selects it with an active 3D bounding box and coordinate gizmo.

### 4. Real-Time FPS & Performance Diagnostics
- Because native Direct3D 11 swap chains present directly to the window backbuffer, FPS is cleanly and continuously reported directly in the OS window caption:
  `window_set_caption("GM3D Island | FPS: 60 (245) | CAM: TPS | Mouse: LOCKED | LMB: Attack | ...")`
- Reports both fixed target framerate and unthrottled real framerate (`fps_real`), providing immediate performance feedback during testing and profiling.

---

## 28. Accurate Jolt Collision Primitives & Continuous Character Collision Sliding

GM3D features a synchronized physical simulation and continuous character collision response framework.

### 1. Shape Dimension Normalization (`EngineContext.cpp`)
- **Root Cause Resolution**: Previously, `create_rigid_body()` populated only `half_extents[3]` without assigning `radius` or `half_height` in `RigidBodyDesc`. Jolt fell back to default $0.5\text{m}$ (or $0.01\text{m}$) radius, causing $2.4\text{m}$ boulders to sink halfway into the terrain and columns to collide as truncated shapes.
- **Normalized Radius & Height Mapping**:
  $$\text{radius} = \frac{S_x}{2}$$
  $$\text{half\_height}_{\text{capsule}} = \max\left(0.01, \frac{S_y - 2 \cdot \text{radius}}{2}\right)$$
  $$\text{half\_height}_{\text{cylinder}} = \frac{S_y}{2}$$
- Guarantee: Dynamic spheres, cylinders, and capsules match visual mesh scale with 1:1 precision. Boulders rest tangentially on terrain without sinking.

### 2. Static World Obstacle Colliders
- **Ancient Columns**: Instantiated as static physical cylinders ($\text{radius} = 0.6\text{m}$, $\text{height} = 3.6\text{m}$) matching the visual cylinder mesh.
- **Procedural Pine Trees**: Trunks are equipped with physical static cylinder collision hulls ($\text{radius} = 0.35\text{m} \times \text{scale}$, $\text{height} = 2.4\text{m} \times \text{scale}$). Boulders rolling down mountain slopes deflect realistically off forest tree trunks.

### 3. Continuous Character Collision & Sliding Resolution
- **Capsule-Aware Horizontal Obstacle Detection**:
  - The playable character ($R = 0.52\text{m}$) evaluates horizontal proximity to all surrounding physical bodies (columns, trees, boulders, crates) within active vertical overlap bounds.
  - Surface normal $\vec{N}$ is derived from the contact vector:
    $$\vec{N} = \frac{\vec{P}_{\text{new}} - \vec{P}_{\text{obstacle}}}{\|\vec{P}_{\text{new}} - \vec{P}_{\text{obstacle}}\|}$$
  - The character is smoothly projected outside the obstacle boundary ($\vec{P} += \vec{N} \cdot (\text{min\_dist} - \text{dist})$), enabling silky smooth sliding around circular pillars and boulder contours without snagging or clipping through geometry.
- **Physical Momentum Transfer**:
  - Running or sprinting into dynamic rigid bodies applies an impulse proportional to movement speed:
    $$\vec{J} = -\vec{N} \cdot F_{\text{push}} \cdot \Delta t$$
  - Players can push boulders across the terrain plaza in real-time.
- **Multi-Surface Vertical Grounding**:
  - In addition to bilinear terrain heightfield queries, the grounding system evaluates analytical dome elevation:
    $$Y_{\text{surface}} = Y_{\text{sphere}} + \sqrt{R^2 - d_{\text{horizontal}}^2}$$
  - Allows players to leap onto and walk across boulders, crates, and elevated platforms.

---

## 29. Atmospheric Lighting Balance, Sky Radiance & Shadow Detail Preservation

GM3D balances high-contrast direct sunlight with natural atmospheric sky fill light, eliminating crushed pitch-black shadows and washed-out skies.

### 1. Two-Tone Hemispheric Skylight & Ground Bounce
- Real-world outdoor shadows are illuminated by the entire celestial sky dome rather than total darkness:
  $$\text{hemi} = \frac{N_y + 1}{2}$$
  $$\vec{L}_{\text{sky}} = \text{lerp}\left(\vec{C}_{\text{groundBounce}}, \vec{C}_{\text{skyDome}}, \text{hemi}\right)$$
  $$\vec{C}_{\text{ambient}} = \vec{C}_{\text{AmbientColor}} \cdot \vec{L}_{\text{sky}} \cdot \vec{C}_{\text{baseColor}}$$
- Upward-facing surfaces in shadow receive rich sky blue fill ($[0.65, 0.85, 1.25]$).
- Crevices and downward-facing cliffs receive warm earth bounce ($[0.24, 0.22, 0.18]$).

### 2. Shadow Detail Preservation in Color Grading
- Hard cubic Hermite curves ($\text{smoothstep}$) crush low luminance values ($< 0.1$) to zero, causing pitch-black shadow artifacts.
- The composite pass uses a balanced filmic contrast blend:
  $$C_{\text{graded}} = \text{lerp}\left(C, C^2 (3 - 2C), 0.25\right)$$
- Paired with soft cool-blue shadow tinting ($[1.02, 1.04, 1.10]$), shadows remain deep and grounded while fully preserving geological cliff textures, vegetation, and tree silhouettes.

### 3. Natural High-Angle Golden Sunlight & Radiant Sky Dome
- Sun elevation is balanced at $\sim 52^\circ$ ($\vec{L} = [-0.45, -0.78, 0.42]$), illuminating valleys and the island plaza without casting excessive $50\text{m}$ shadows across the entire world.
- The procedural sky dome features a saturated Rayleigh gradient transitioning from celestial deep sapphire at the zenith ($[0.08, 0.26, 0.72]$) to glowing cyan-blue at the horizon ($[0.40, 0.62, 0.88]$).
- Dynamic boulders are textured with Weathered Alpine Granite (`matId == 8`, $\text{roughness} = 0.82$, $\text{metallic} = 0.02$).

---

## 30. High-Fidelity Soft Shadows via Rotated Vogel Disk PCF & Normal-Offset Bias

GM3D transitions real-time directional sunlight shadows from a harsh, aliased 4x4 regular grid filter to a filmic, soft-penumbra shadow model powered by **Rotated Vogel Disk PCF (Percentage-Closer Filtering)** and **Slope-Adaptive Normal Offset Bias**.

### 1. Root Cause Analysis: 4x4 Regular-Grid Shadow Aliasing
- **Cartesian Grid Clumping**: The previous shadow filter queried a $4 \times 4$ regular integer grid ($x, y \in \{-1, 0, 1, 2\}$) spaced strictly by 1 shadow texel. This aligned all 16 sample coordinates with the shadow map texel grid axes.
- **Quantization Stair-Stepping**: Because sample offsets were identical for all screen pixels without spatial jitter, shadow silhouettes across diagonal contours, curved boulders, and terrain slopes manifested visible 4-level staircase banding and harsh pixelated edges.
- **Excessive Depth Bias**: Using an arbitrary scalar depth bias ($0.0006 \dots 0.0018$, equivalent to $\sim 15.5\text{cm}$ in light-space world units) caused shadow detachment ("peter-panning") beneath boulders and feet.

### 2. The 16-Tap Vogel Disk Distribution
To eliminate Cartesian directional bias, samples are distributed uniformly on a 2D unit disk according to Fermat's spiral using the Golden Angle ($\phi \approx 2.39996323\text{ rad} \approx 137.50776^\circ$):
$$r_i = \sqrt{\frac{i + 0.5}{N}}, \quad \theta_i = i \cdot \phi \quad \text{for } i \in [0, N-1]$$
$$\vec{O}_i = \left(r_i \cos\theta_i, \; r_i \sin\theta_i\right)$$

- **Uniform Radial & Angular Density**: Every sample sits at a unique radius ($r \in [0.177, 0.984]$) and unique angle, ensuring circularly isotropic blur rather than box-shaped shadow artifacts.

### 3. Screen-Space Interleaved Gradient Noise Rotation
To eliminate concentric ring artifacts and turn sample discretization into smooth photographic penumbras, the Vogel disk is rotated per pixel using **Jimenez Interleaved Gradient Noise** evaluated on window coordinates $\vec{P}_{\text{screen}} = (x, y)$:
$$\text{noise} = \text{frac}\left(52.9829189 \cdot \text{frac}\left(0.06711056 \cdot x + 0.00583715 \cdot y\right)\right)$$
$$\alpha = \text{noise} \cdot 2\pi$$
$$\vec{O}_{i, \text{rotated}} = \begin{pmatrix} \cos\alpha & -\sin\alpha \\ \sin\alpha & \cos\alpha \end{pmatrix} \cdot (\vec{O}_i \cdot R_{\text{filter}})$$

- **Blue-Noise-Like Spatial Dither**: Neighboring pixels sample different angular orientations of the disk. The human visual system integrates this sub-pixel high-frequency spatial variation into a continuous, silky-smooth Gaussian-like penumbra.
- **Hardware 2x2 Bilinear Integration**: Each of the 16 rotated taps calls `SampleCmpLevelZero` on a sampler configured with `D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT`. Each tap bilinearly blends 4 depth texels, yielding $16 \times 4 = 64$ effective depth comparisons per pixel with negligible GPU overhead.

### 4. World-Space Normal Offset Bias & Tight Contact Anchoring
To permanently eliminate shadow acne without detaching shadows from contact surfaces:
- **Normal Offset Geometry**: The receiver coordinate is shifted slightly along the geometric surface normal $\vec{N}_{\text{geom}}$ in world space prior to light projection:
  $$\vec{P}_{\text{biased}} = \vec{P}_{\text{world}} + \vec{N}_{\text{geom}} \cdot \left(0.055 \cdot (1 - \vec{N}_{\text{geom}} \cdot \vec{L})\right)$$
  $$\vec{P}_{\text{shadow}} = \vec{P}_{\text{biased}} \cdot \mathbf{M}_{\text{LightViewProj}}$$
- **Contact-Hardening Depth Bias**: Because the normal offset handles slope geometry and spherical curvature, the scalar depth bias is minimized:
  $$\text{bias} = \max\left(0.00032 \cdot (1 - \vec{N} \cdot \vec{L}), \; 0.00008\right)$$
  - In light-space units ($259\text{m}$ orthographic range), $0.00008$ corresponds to just $2.0\text{cm}$ of bias (down from $15.5\text{cm}$).
  - Result: Shadows firmly anchor to the tangent contact lines of boulders, pillars, and characters with zero acne and zero gap.

### 5. Filter Radius & Real-World Penumbra Scale
- **Penumbra Spread**: $R_{\text{filter}} = 3.6\text{ texels}$.
- In world units: $3.6 \times \frac{110.0\text{m}}{2048} \approx 0.19\text{m}$ ($19\text{cm}$).
- This physical width naturally mimics real Earth solar penumbras (caused by the $0.53^\circ$ angular width of the sun disk), yielding rich, smooth, and soft shadows across all surfaces.

---

## 31. Global Environmental Shadows, World-Space Texel Snapping & Ambient Sky Radiance

GM3D overhauls global environmental shadows, resolving harsh black voids, shadow swimming, and oppressive mountain occlusions.

### 1. Root Cause Analysis: Unpleasant Global Shadows
1. **Pitch-Black Void Shadows**: Shadowed areas dropped to near-zero luminance ($15-17\%$ on ground, $1\%$ on mountain slopes) due to low ambient fill and double contrast compression (ACES toe curve compounded with cubic contrast).
2. **Oppressive Mountain Shadow Walls**: In the island scene, $32\text{m}$ peaks were situated within $19\text{m}$ of the central plaza. Combined with a low sun angle ($50^\circ$), peaks cast a massive $30-50\text{m}$ black shadow completely submerging the player, boulders, and paths.
3. **Shadow Swimming / Texel Crawling**: Moving the camera caused the orthographic shadow rasterization grid to shift by fractional sub-texels, causing all shadow edges across the scene to visibly shimmer and vibrate.

### 2. World-Space Shadow Texel Snapping
To completely eliminate shadow shimmering and crawling as the camera or player moves:
- The light view matrix translation is quantized to integer multiples of the shadow map's physical world texel size:
  $$\text{worldTexelSize} = \frac{\text{orthoSize}}{\text{SHADOW\_MAP\_SIZE}} = \frac{110.0}{2048.0} \approx 0.0537\text{m}$$
  $$\mathbf{V}_{\text{light}}[12] = \left\lfloor \frac{\mathbf{V}_{\text{light}}[12]}{\text{worldTexelSize}} \right\rfloor \cdot \text{worldTexelSize}$$
  $$\mathbf{V}_{\text{light}}[13] = \left\lfloor \frac{\mathbf{V}_{\text{light}}[13]}{\text{worldTexelSize}} \right\rfloor \cdot \text{worldTexelSize}$$
- **Result**: The shadow map rasterization grid is mathematically locked to the static world coordinates. Shadow silhouettes remain rock-solid without shimmering.

### 3. Frustum Concentration ($110\text{m}$ vs $180\text{m}$)
- Orthographic coverage is concentrated from $180\text{m}$ to $110\text{m}$ around the active player focus.
- Physical texel resolution sharpens by $65\%$ (from $8.8\text{cm}$ down to $5.3\text{cm}$ per texel), delivering crystalline silhouettes for the player, dynamic boulders, columns, and terrain contours.

### 4. Radiant Ambient Sky Fill & ACES Shadow Lift
- **Two-Tone Ambient Floor**: In `PS_Main`, an ambient sky irradiance floor is added so that surfaces in full shadow receive natural celestial diffuse illumination:
  $$\vec{E}_{\text{ambientFloor}} = \begin{pmatrix} 0.14 \\ 0.18 \\ 0.26 \end{pmatrix} \cdot \text{hemi} + \begin{pmatrix} 0.06 \\ 0.05 \\ 0.04 \end{pmatrix}$$
  $$\vec{C}_{\text{ambient}} = \left(\vec{C}_{\text{AmbientColor}} \cdot \vec{L}_{\text{sky}} + \vec{E}_{\text{ambientFloor}}\right) \cdot \vec{C}_{\text{baseColor}}$$
- **ACES Toe Protection**: In `PS_Composite`, dark values are prevented from collapsing into pitch-black voids:
  $$\vec{C}_{\text{mapped}} = \max\left(\text{ACESFilm}(\vec{C}), \; \vec{C} \cdot 0.40\right)$$
- Shadowed grass remains lush emerald green ($[0.15, 0.32, 0.14]$), and stone paths retain soft azure-gray tones ($[0.35, 0.40, 0.48]$) at $\sim 40\%$ daylight luminance, matching real-world Mediterranean daylight photography.

### 5. High Midday Sun & Expansive Valley Geometry
- **High Mediterranean Sun**: Sun elevation is raised to $64.2^\circ$ ($\vec{L} = [-0.35, -0.90, 0.25]$, intensity $1.85$). Sunlight bathes the entire island and plaza, shrinking mountain shadows into distant contours.
- **Topographical Valley Expansion**: The central valley is widened from $19\text{m}$ to $35\text{m}$ radius (`smoothstep(0.14f, 0.06f, dist)`). Mountains rise smoothly through rolling subalpine meadows ($6-10\text{m}$) before reaching high crags ($24-28\text{m}$) along the island perimeter ($dist > 0.28$), permanently eliminating oppressive shadow walls over the gameplay area.

---

## 32. GM3D Studio - Standalone 3D World Editor Architecture

GM3D features a dedicated, native standalone 3D world editor application (**`GM3DStudio.exe`**) engineered in C++20 with **Dear ImGui (Docking)**, **ImGuizmo**, and **DirectX 11**, designed to eliminate manual hardcoded coordinate placement in GameMaker.

### 1. High-Level Ecosystem & Pipeline Architecture
- **Separation of Concerns**:
  - **GM3D Studio**: Visual world assembly, interactive 3D translation/rotation/scaling, PBR material assignment, Jolt physics collider configuration, sun/fog atmospheric adjustments, and procedural terrain regeneration.
  - **Scene Interchange Format (`scene.json`)**: Human-readable, Git-friendly structured JSON capturing all entity transforms, tags, render flags, physics colliders, and environmental parameters.
  - **GameMaker (Game Client)**: Loads the level with a single call, binding gameplay logic, state machines, AI, quests, UI, and audio directly to named entities and tags.

### 2. Core Editor Subsystems (`tools/studio/`)
1. **Docking Workspace & Modern Dark UI**:
   - Built on `Dear ImGui` (docking branch) using a cinematic dark theme.
   - Movable, dockable panels: Viewport, Scene Hierarchy (Outliner), Entity Inspector, Environment & Lighting, Performance Stats Overlay.
2. **DirectX 11 Viewport Render-to-Texture**:
   - `RenderWorld` exposes `set_custom_render_target(ID3D11RenderTargetView*)`.
   - When the Viewport dock window is resized, a dedicated D3D11 render target texture dynamically resizes to match, rendering the full HDR PBR world with soft shadows directly inside the ImGui dock panel via `ImGui::Image`.
3. **Interactive 3D Manipulation (`ImGuizmo`)**:
   - Translating, rotating, and scaling entities in 3D world/local space with visual gizmos.
   - Hotkeys: `W` (Translate), `E` (Rotate), `R` (Scale), `Del` (Delete entity), `Ctrl+D` (Duplicate), `Ctrl+S` (Save scene), `Ctrl+O` (Open scene).
   - Grid and angle snapping support (1.0m translation, 15° rotation, 0.5x scale).
4. **Scene Hierarchy (Outliner)**:
   - Search filter bar, entity grouping, right-click context menu (Add, Duplicate, Delete).
5. **Entity Inspector**:
   - **Transform**: Real-time DragFloat coordinates ($X, Y, Z$) and Euler angles.
   - **Render Component**: Mesh selection (Cube, Sphere, Plane, Cylinder, Capsule, Terrain), PBR Material selection (Coral, Gold, Emerald, Water, Amethyst, Pine, Steel, Granite), Texture ID, Cast Shadow toggle.
   - **Physics Component**: Jolt motion type (None, Static, Dynamic, Kinematic), shape selection (Box, Sphere, Capsule, Cylinder, Convex Mesh), Mass, Friction, Restitution.
6. **Environment & Terrain Controller**:
   - Interactive sliders for Sun elevation pitch ($5^\circ$ to $85^\circ$), azimuth yaw, color, and intensity.
   - Ambient sky color and intensity, fog distance boundaries.
   - Procedural island terrain regenerator (seed, height scale, world size).
7. **Scene Serializer (`SceneSerializer.cpp`)**:
   - Powered by `nlohmann/json.hpp`.
   - Native Windows File Dialogs (`GetOpenFileNameW`, `GetSaveFileNameW`) for intuitive scene saving and loading.

### 3. Studio UI & Viewport Ergonomics Overhaul
- **High-Definition System Font**:
  - Replaced legacy bitmap font with smoothed Windows **Segoe UI** (17px, 3x oversampled) with native Latin and Cyrillic character support (`io.Fonts->GetGlyphRangesCyrillic()`).
- **Initial Automated Docking (`ImGui::DockBuilder`)**:
  - Automatically initializes a clean professional 3D layout upon first launch:
    - **Central Dock Node (70% width)**: 3D Viewport with hardware-accelerated DirectX 11 PBR rendering.
    - **Top-Right Dock Node**: Scene Hierarchy (Outliner).
    - **Bottom-Right Dock Node**: Entity Component Inspector.
    - **Left Dock Node**: Environment & Atmospheric Lighting controls.
  - Added `Window -> Reset Layout to Default` in the menu bar to immediately restore docked layout at any time.
- **Blender / Unreal Style Transform Controls**:
  - Colored `[X]` (Red), `[Y]` (Green), `[Z]` (Blue) vector inputs with single-click reset and smooth drag sensitivity.
- **3D Ground Plane & Procedural Space Grid**:
  - Default scene starts with a $240 \times 240\text{m}$ procedural grid floor (`Grid Floor`, mesh ID 3, procedural grid shader), a central golden brass reference cube, and a weathered granite sphere.
  - Top Viewport toolbar provides one-click Gizmo mode switching (`[W] Move`, `[E] Rotate`, `[R] Scale`), coordinate space toggle (`Local / World`), snap toggle, quick entity spawners (`+ Cube`, `+ Sphere`, `+ Cylinder`, `+ Tree`), and camera controls.
- **Dynamic Aspect Ratio Compensation**:
  - `RenderWorld` queries the exact pixel dimensions of the custom viewport render target, dynamically recalculating the camera projection aspect ratio to prevent geometry stretching during panel resizing.

### 4. Professional Viewport & Engine Overhaul (Status Bar, Mouse Picking, Simulation)
- **Docked Bottom Status Bar**:
  - Completely eliminated the floating stats overlay that covered viewport buttons and tabs.
  - Replaced with a docked 28px bottom status bar displaying:
    - Live Simulation State: `[READY]` / `[SIMULATING JOLT PHYSICS]`
    - Entity Count: `Entities: N`
    - Selection Info: `Selected: EntityName (ID: X)`
    - Camera Diagnostics: `Cam: (X, Y, Z)`
    - Right-aligned performance metrics: `FPS: 60.0 (16.6ms) | DirectX 11 PBR | Jolt Physics`.
- **Direct 3D Mouse Picking**:
  - Left-clicking anywhere inside the 3D Viewport automatically casts an unprojected 3D camera ray through screen coordinates and selects the closest intersected entity.
  - Clicking on gizmo handles operates transforms without deselecting.
- **3D Selection Bounding Box Overlay**:
  - Actively selected entities display a glowing golden/amber 3D wireframe bounding box projected onto the viewport draw list, moving and rotating seamlessly with the transform gizmo.
- **Analytic Screen-Space Anti-Aliased Grid**:
  - Ground plane procedural shader upgraded with `fwidth` derivative filtering:
    - 1-meter minor grid lines and 5-meter major grid lines.
    - Vibrant Red $X$-axis ($Z=0$) and Blue $Z$-axis ($X=0$) line guides.
    - Smooth distance falloff without moiré patterns or shimmering at grazing angles.
- **Live Jolt Physics Simulation in Editor (`Play / Reset`)**:
  - Added toolbar toggle `[ > Play Physics ]` / `[ [] Reset Physics ]` (hotkey `P`).
  - Upon play, snapshots current entity state and initializes dynamic rigid bodies in Jolt Physics; entities fall, tumble, bounce, and collide in real-time.
  - Upon reset, restores all entity transforms back to their exact pre-simulation configuration.

### 5. History and scene protection
- `SceneHistory.h` stores scene snapshots, coalesces active edits and excludes transient physics handles and the scene filepath. The default history budget is an estimated 128 MiB. At least two states and a separate saved-state snapshot are retained, so this is not a strict cap on total memory.
- Scene and project saves create a temporary file in the destination directory and replace the target with `MoveFileExW`. Failed loads leave the current scene untouched. There is no automatic `.bak` recovery.
- Scene format version 1 is written; unversioned legacy scenes are accepted, unsupported versions are rejected. The editor prompts for unsaved changes before replacing or closing the scene.

### 6. Project Registry, Persistent Asset Links, 3D Model Thumbnails & Auto-Relinking (Roadmap Item 2)
- **Relative Path Persistence (`SceneSerializer.cpp`, `ProjectSerializer.cpp`)**:
  - All model paths (`Entity::model_path`) and texture paths (`Entity::texture_path`, `Material::albedo_path`, `Material::normal_path`) are serialized relative to the scene or project directory (`gm3d_project.json`), when a relative path is possible. Resources outside the project, or on another drive, can still require relinking after a move.
- **Procedural 3D Wireframe Model Thumbnails & Live File Watcher (`StudioApp.cpp`)**:
  - Generates 96x96 RGBA isometric wireframe preview thumbnails for imported `.obj`/`.glb`/`.gltf` meshes directly from CPU vertex positions (`MeshData::preview_positions`).
  - Automatic filesystem timestamp polling (`std::filesystem::last_write_time`) hot-reloads modified textures and 3D models in place via `MeshManager::replace_mesh` and `TextureManager::replace_texture`.
  - **Auto-Relink Missing Assets**: Scans project/scene directories and `assets/`, `models/`, `textures/` subfolders to automatically repair broken file references.

### 7. True Scene Hierarchy, Parenting, Groups, Multi-Selection & Alignment Tools (Roadmap Item 3)
- **Hierarchical Transform Propagation (`TransformMath.h`)**:
  - `Entity::parent_id` and `Entity::is_group` establish parent-child trees of arbitrary depth with cycle detection (`Scene::is_descendant_of`).
  - `ComputeWorldMatrix` and `ComputeWorldTransform` recursively evaluate $M_{\text{world}} = M_{\text{local}} \times M_{\text{parentWorld}}$.
  - `ReparentEntityPreservingWorld` verifies the reconstructed world matrix before applying a parent change. Singular transforms and transforms that require shear are rejected with a message.
- **Multi-Selection & Group Transforms (`StudioApp.cpp`)**:
  - `Ctrl+Click` and `Shift+Click` range selection in the Outliner and 3D Viewport.
  - Multi-entity gizmo manipulation transforms all selected root entities around their shared world pivot using relative matrix deltas ($M_{\text{newWorld}} = M_{\text{oldWorld}} \times M_{\text{pivot}}^{-1} \times M_{\text{newPivot}}$).
  - Subtree-aware duplication (`Ctrl+D`) and deletion (`Del`) clone or remove entire parent-child hierarchies cleanly.
  - Built-in alignment and distribution tools (`Min`, `Center`, `Max`, `Distribute` along X/Y/Z) and surface snapping (`G` key).

### 8. Custom PBR Materials, Dynamic Shader Buffers & Material Editor (Roadmap Item 4)
- **Persisted PBR Material System (`SceneData.h`, `SceneSerializer.cpp`)**:
  - `struct Material`:
    - Base Color: $RGBA$ floating-point color tint and opacity.
    - Roughness $[0.0, 1.0]$: microfacet roughness ($0.0$ mirror specular, $1.0$ diffuse matte).
    - Metallic $[0.0, 1.0]$: dielectric conductor interpolation ($0.0$ dielectric, $1.0$ full metal).
    - Emissive $[0.0, 10.0]$: HDR bloom radiation boost and custom RGB emissive color.
    - Texture Slots: relative paths and handles for `albedo_path` and `normal_path`.
    - UV Mapping: `uv_tiling[2]` ($U, V$ scale) and `uv_offset[2]` ($U, V$ offset).
    - Alpha Modes: `0 = Opaque`, `1 = Mask (Alpha Cutoff)`, `2 = Blend (Transparent Alpha Blending)`.
    - Built-in Procedural Presets: preserves classic presets $0..8$ (Default White, Coral Red, Gold Brass, Emerald Teal, Water Azure, Amethyst Purple, Pine Wood, Royal Steel, Weathered Granite) with instant conversion to editable custom PBR.
  - Scene Serialization:
    - Serialized in `scene.json` under `"materials": [...]` with relative path resolution.
    - Backward-compatible: loads legacy scenes seamlessly and initializes default materials via `init_default_materials()`.

- **DirectX 11 Shader Pipeline & Constant Buffer (`RenderWorld.h`, `RenderWorld.cpp`)**:
  - Added 64-byte aligned GPU structure `MaterialGPU` matching HLSL `cbuffer MaterialConstants : register(b3)`:
    - Vector 0: `MatBaseColor` (`float4`, RGBA)
    - Vector 1: `MatRoughness`, `MatMetallic`, `MatEmissive`, `MatAlphaCutoff` (16 bytes)
    - Vector 2: `MatUVTiling` (`float2`), `MatUVOffset` (`float2`) (16 bytes)
    - Vector 3: `MatAlphaMode` (`int`), `MatPresetId` (`int`), `MatPad` (`float2`) (16 bytes)
  - Shader Features:
    - Computes transformed UVs: `float2 uv = input.UV * MatUVTiling + MatUVOffset;`.
    - Samples normal maps and diffuse maps with UV coordinates.
    - Alpha cutoff masking: `if (MatAlphaMode == 1 && opacity < MatAlphaCutoff) discard;`.
    - Additive emissive boost: `color += baseColor * MatEmissive;` amplified by ACES HDR bloom.
    - Multi-batch instanced rendering grouped by key `(mesh_id, material_id, texture_id)` with constant buffer and alpha blending switching.

- **glTF 2.0 / GLB Material Extraction & Diagnostic Reports (`MeshManager.cpp`)**:
  - `MeshManager::extract_gltf_materials` parses `cgltf` materials from 3D models.
  - Automatically extracts base color factors, metallic-roughness factors, emissive factors, texture image URIs, and alpha modes.
  - Generates clear diagnostic reports and limitation warnings for advanced glTF extensions (e.g. `KHR_materials_clearcoat`, `KHR_materials_transmission`, `KHR_materials_sheen`, embedded GLB binary textures).
  - Automatically populates scene materials and binds them to imported entities.

- **Interactive Material Editor & 3D PBR Sphere Preview (`StudioApp.cpp`, `StudioApp.h`)**:
  - Dedicated **"Materials"** editor panel docked beside the Inspector.
  - Real-time 3D PBR sphere preview powered by Cook-Torrance GGX shading on a dynamic D3D11 GPU texture, providing instant visual feedback for roughness gloss, metallic specular tint, and Fresnel rim reflections.
  - Material list with color swatches, search filtering, "+ New Material", "Duplicate", and "Delete".
  - Color pickers, sliders, texture slots with thumbnail previews, and UV controls.
  - Inspector rendering integration: dynamic material selection combo, "Edit Material..." shortcut, "+ New from Selection", and multi-selection batch material assignment.

- **GameMaker C-ABI Exported Material API (`dllmain.cpp`, `EngineContext.cpp`)**:
  - `material_set_pbr(mat_id, r, g, b, a, roughness, metallic, emissive)`
  - `material_set_textures(mat_id, albedo_tex_id, normal_tex_id)`
  - `material_set_uv(mat_id, tiling_u, tiling_v, offset_u, offset_v)`
  - `material_set_alpha(mat_id, alpha_mode, alpha_cutoff)`
  - `material_clear_all()`

### 9. Terrain editing
- Sculpt and paint tools use `TerrainBrush.h`; scene snapshots group brush strokes for Undo. The scene stores height samples, four layer settings and RGBA8 paint weights.
- `TerrainSystem` updates affected mesh and albedo regions and rebuilds the collider after a stroke. Grayscale PFM import/export is described in `tools/DEV_TOOLS.md`.
- GameMaker export uses `terrain_apply_edit_data` to restore heights, weights and layer parameters after generation. The generated loader requires matching current extension definitions and DLL; the actual GameMaker LTS round trip remains to be tested.

### 10. 2-Cascade Directional CSM & 6-Face Omnidirectional Point Light Shadow Atlas (Roadmap Item 6)
- **Unified Shadow Depth Atlas (`RenderWorld.h`, `RenderWorld.cpp`)**:
  - Allocates a high-resolution `DXGI_FORMAT_R32_TYPELESS` depth atlas (`m_shadow_map_tex`, $2S \times 2S$, where $S \in \{1024, 2048, 4096\}$ based on Shadow Quality: Low / Medium / High):
    - **Top-Left Quadrant $(0, 0, S \times S)$**: Directional Sun Cascade 0 (Near high-resolution frustum, $75\text{m}$ coverage, $\sim 3.6\text{cm}$ texels at $S=2048$).
    - **Top-Right Quadrant $(S, 0, S \times S)$**: Directional Sun Cascade 1 (Far wide frustum, $240\text{m}$ coverage, world-space texel-snapped).
    - **Bottom Half $(0, S, 2S \times S)$**: Up to 4 shadow-casting Point Lights, each rendered across 6 $90^\circ$ perspective cubemap faces ($+X, -X, +Y, -Y, +Z, -Z$) packed into a $6 \times 2$ tile grid of size $(S/3) \times (S/2)$ per face.
- **Shader Evaluation (`EvaluateDirectionalShadowAtlas`, `EvaluatePointShadow`)**:
  - Automatically selects Cascade 0 or Cascade 1 based on world-space distance from the camera focus ($d < 32\text{m}$), sampling with a 16-tap Rotated Vogel Disk PCF kernel clamped to the quadrant's UV bounds.
  - For point lights with `cast_shadow >= 0.5`, selects the dominant axis face of $\vec{L}_{\text{fragToLight}}$, projects into the corresponding atlas tile, and performs a 4-tap PCF comparison with slope-scaled bias.
- **Editor & C-ABI Controls**:
  - Environment panel controls for Shadow Quality (`Low (1024)`, `Medium (2048)`, `High (4096)`), 2-Cascade CSM toggle, Max Shadowed Point Lights ($0..4$), and per-light `"Cast Shadows"` checkbox.
  - Exported C-ABI functions include `render_set_shadow_settings(enabled, strength, softness, coverage)`, `render_set_shadow_quality(quality, csm_enabled, max_shadow_lights)` and `light_point_set_ex(..., cast_shadow)`.

### 11. Modular Entity Components, Collider/Animator Editors, Play Isolation & GameMaker LTS Exporter (Roadmap Item 7)
- **Modular Component Inspector (`StudioApp::draw_inspector_panel`)**:
  - Organizes entity properties into distinct, collapsible component cards with `+ Add Component` / `[Remove]` workflow:
    1. **Transform Component**: Local Position, Rotation, Scale with colored X/Y/Z reset buttons and read-only World Position display for child entities.
    2. **Mesh Renderer Component**: Primitive / Custom Model selector, PBR Material assignment, Texture slot, and Cast Shadow flag.
    3. **Collider & Rigidbody Component (Jolt)**: Motion Type (`Static`, `Dynamic`, `Kinematic`), Collider Shape (`Box`, `Sphere`, `Capsule`, `Cylinder`), local **Collider Size $(X, Y, Z)$**, **Center Offset $(X, Y, Z)$**, Mass, Friction, Restitution, and `"Reset to Bounds"`.
    4. **Skeletal Animator Component**: Clip selector (`anim_get_count` / `anim_get_name` / `anim_get_duration`), Play/Pause toggle, Loop checkbox, Playback Speed slider, and interactive **Timeline Scrubber**.
    5. **Point Light Component**: Color, Intensity, Attenuation Radius, and Cast Shadows toggle.
- **3D Viewport Collider Wireframe Gizmos (`StudioApp::draw_collider_bounds`)**:
  - Toggleable `"Colliders"` overlay in the viewport toolbar renders emerald-green 3D wireframes (12-edge oriented boxes, 3-axis orthogonal sphere rings, or capsule/cylinder struts and caps) honoring each entity's world transform, `collider_size`, and `collider_offset`.
- **Full Play Mode Isolation (`StudioApp::start_simulation`, `StudioApp::stop_simulation`)**:
  - Snapshots the entire `Scene` (transforms, hierarchy, animations, materials, lighting, terrain) plus the editor camera state (`m_cam_pos`, `m_cam_yaw`, `m_cam_pitch`, `m_cam_fov`, `m_cam_near`, `m_cam_far`).
  - Spawns Jolt bodies using hierarchical world transforms + local `collider_offset` and `collider_size`, converting simulated world positions back into parent-local space each frame.
  - Stopping simulation destroys temporary Jolt bodies, clears GPU particles (`particle_clear`), and restores exact pre-play scene and camera states.
- **One-Click GameMaker LTS Scene & Asset Exporter (`SceneSerializer::export_for_gamemaker`)**:
  - Invoked via `File -> Export for GameMaker LTS...` (`Ctrl+E`).
  - Stages all referenced external 3D models and textures into `<export_dir>/assets/`, exports `terrain.pfm` heightmap, writes `<export_dir>/scene_gm3d.json` with precomputed world transforms (`world_position`, `world_rotation`, `world_scale`, `world_quat`), and generates a ready-to-use GML loader script **`scr_gm3d_load_scene.gml`** (`gm3d_load_scene_json`).

### 12. Verification and outstanding work

The previous benchmark table was removed: its numbers did not identify a reproducible test scene, hardware or captured measurements. Do not treat performance gains as measured until a fixed scene is profiled before and after changes.

The full development plan is **not complete**. In particular:
- The grass API, `material_clear_all` and the material/shadow setters are registered in `3DTEST/extensions/GM3Dext/GM3Dext.yy` and wrapped by the GML SDK, and `tools/sync_gml_sdk.ps1 -Check` (run by `tools/check.ps1`) keeps `gml_example/scr_gm3d.gml` identical to the 3DTEST copy. The example project previously lagged behind at 8 wrapper functions; both copies now expose the full set.
- Mesh colliders are not implemented; `Diagnostics.cpp` reports the cylinder proxy that the editor substitutes. Any scene that relies on a mesh collider is physically wrong, not merely approximate.
- `RenderWorld::render_frame` used to have no frustum culling and no transparent depth sorting, and the shadow pass redrew every caster for every cascade and every point-light face. Both were fixed in §33.
- Run an exported scene in GameMaker LTS with a model, material, painted terrain, light, collider and animation. LTS 2026.0.0.23 ran a temporary `3DTEST` copy with a terrain and static cube fixture passed through the actual exporter: the loader created two entities, terrain height was 7, and cleanup succeeded. This smoke test did not verify rendering, imported models, painted layer appearance, lights or animation; the complete runtime round trip remains unverified.
- Check resource import, missing-file relinking, material texture clearing and Undo/Redo in the editor, including project moves and narrow window layouts. Review the remaining UI/UX work.
- Check Play/Stop restoration, hierarchy transforms, shadows and performance on representative scenes. Add missing coverage where necessary.
- Research current GMRT source availability, licensing and compatibility independently using current official sources. GMRT integration has not been verified and must not be assumed compatible with the LTS extension.

### 13. Developer handoff (2026-10-29)

- Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1 -Graphics` from the repository root. It builds the editor and DLL, runs CTest, compares terrain regional updates with full rebuilds on WARP, and runs hidden-window Direct3D smoke checks. See `tools/DEV_TOOLS.md` for individual diagnostics and limitations.
- Material paths are relative in scene files and resolved on load; missing maps appear in diagnostics and the asset library for relinking. Deleting a material reassigns affected objects and is undoable. Material IDs fit the 16-bit renderer interface.
- Reparenting preserves the world transform where representable and refuses invalid, singular or shear-producing cases. Deep hierarchy traversal is cycle guarded.
- Imported glTF skins with more than 128 joints are rejected because the GPU bone buffer has 128 entries. Loop-disabled animation samples clamp to the final frame. Visual animation and shadow verification remains outstanding.
- GameMaker export asks for a folder and warns before replacing scene, loader or terrain files. It checks missing resources, but external glTF dependencies are not packaged; use a self-contained GLB. Export is not fully transactional and failures after writing begins can leave a partial package.
- Repeated exports reuse staged assets with identical content; different files sharing a name receive separate names. Existing unrelated assets are not overwritten.
- `build/`, DLLs, logs and `scratch/` are ignored by Git. Rebuild the DLL and copy it to the example project when needed; `tools/check.ps1` does not copy it automatically. Never assume a local ignored DLL was pushed.

---

### 14. UI/UX Modernization, Transform Interpolation, and Asset Integration (2026-10-30)

- **Per-Monitor V2 DPI Awareness & Desktop Sizing (`main.cpp`, `StudioApp.cpp`)**:
  - Initialized `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` in `wWinMain`.
  - Delivers crystal-clear 1:1 pixel rendering on high-DPI displays (such as standard 1080p laptops at 125% scaling), completely eliminating Desktop Window Manager (DWM) bitmap blurring and window coordinate virtualization clipping.
  - Added desktop work-area detection via `SystemParametersInfoW(SPI_GETWORKAREA, ...)`; on screens $\le 1640 \times 940$, automatically clamps and centers the editor window with generous margins so no panels extend past the monitor edges.

- **Dock Layout Architecture & Responsiveness (`StudioApp.cpp`)**:
  - Eliminated the initial 1-frame uninitialized dock size race condition by deferring `DockBuilderRemoveNode` / `DockBuilderSplitNode` until the main viewport window size is verified ($\ge 400\text{px}$).
  - Assigned optimal 3D DCC dock splits:
    - **Right Dock (Hierarchy & Inspector/Materials)**: 28% ($\sim 420\text{px}$), split vertically into Hierarchy (top) and Inspector / Materials tabs (bottom).
    - **Left Dock (Environment & Lighting)**: 24% ($\sim 360\text{px}$), displaying all three tabs (`Terrain`, `Lighting`, `Assets`) with zero tab-scrolling overflow.
    - **Center Viewport**: $\sim 720\text{px}$ full 3D rendering canvas.

- **Scene Hierarchy & Camera Focus UX (`StudioApp.cpp`, `StudioApp.h`)**:
  - Restructured the top Hierarchy action bar into a 2-row layout with equalized half-width buttons (`+ Add Entity`, `Focus (F)`, `Group (Ctrl+G)`, `Unparent`), ensuring all buttons remain 100% visible regardless of dock resizing.
  - Centralized camera framing logic into `StudioApp::focus_selected()`:
    - Supports single entity or multi-entity bounding center calculations.
    - Instantly triggered via Viewport toolbar button `Focus (F)`, Hierarchy toolbar button `Focus (F)`, 'F' keyboard shortcut, Hierarchy right-click context menu ("Focus Camera (F)"), or double-clicking any entity in the Scene Hierarchy tree.
    - Automatically stops residual camera drift by zeroing out velocity vectors upon focusing.

- **Responsive Materials Panel & Compact Mode (`StudioApp.cpp`)**:
  - Designed an adaptive layout that checks available content width:
    - **Compact Mode ($< 480\text{px}$)**: Replaces the rigid side-by-side pane with a clean top dropdown selector featuring real-time color swatches, an $80 \times 80$ Cook-Torrance GGX PBR preview sphere, and full-width property controls below.
    - **Wide Mode ($\ge 480\text{px}$)**: Retains the master-detail two-column list and property inspector.
  - Docked directly as a tab beside the Inspector in the bottom-right dock node, eliminating floating window occlusion over the 3D viewport.

- **Viewport Toolbar & Interaction Enhancements (`StudioApp.cpp`)**:
  - Quick Snap Increment Selector: Checking `"Snap"` reveals an inline combo box (`0.1m`, `0.25m`, `0.5m`, `1.0m`, `2.0m`, `5.0m`) for rapid grid alignment without opening menus.
  - Color-coded Simulation Controls: Highlights `Play (P)` in emerald green and `Stop (P)` in bold crimson with clear shortcut indicators.
  - Informative Window Title: Displays active scene filename and dirty indicators (`GM3D Studio - [Untitled Scene *]`), cached against redundant per-frame Win32 IPC calls.

- **Smooth Movement & Physics Transform Interpolation (`PhysicsWorld.cpp`, `PhysicsWorld.h`)**:
  - Root cause resolved: Jolt Physics updates at a fixed $60\text{Hz}$ timestep (`FIXED_TIME_STEP`). At variable or high refresh rates ($144\text{Hz}$+), dynamic bodies previously suffered visual micro-stutter due to time step aliasing.
  - Implemented `TransformSample` and `BodyHistory` tracking in `PhysicsWorld`. Dynamic rigid bodies interpolate smoothly across render frames:
    $$\alpha = \text{clamp}\left(\frac{\text{accumulator}}{\text{FIXED\_TIME\_STEP}}, 0.0, 1.0\right)$$
    blending positions via linear interpolation and orientations via spherical linear interpolation (`SLERP`).
  - Added smooth exponential velocity damping (`m_cam_velocity`) and zoom glide (`m_cam_zoom_velocity`) for WASD flight and mouse wheel navigation.

- **GM3Dext Bridge Asset Integration**:
  - Integrated 2.5D PBR shaders (`sh_moba_pbr.vsh`, `sh_moba_pbr.fsh`) and helper functions (`scr_draw_sprite_pbr.gml`) in `gml_example/bridge_pbr_sprites/`.
  - Added `tools/generate_relief_maps.py`, implementing Euclidean Distance Transform (EDT) and Sobel filtering for automated sprite-to-PBR map generation (normals, depth, roughness/metallic/AO masks).
  - Integrated rigged animated model `Skeleton_Warrior.glb` into `3Dreadymodels/`.

---

### 15. Procedural Grass & Foliage System (Architecture Plan)

- **Splatmap & Slope-Driven Density Distribution**:
  - Grass blades and foliage clumps generate procedurally across the terrain surface without requiring manual placement.
  - Spawning density is modulated by:
    - Splatmap Channel Weight: samples the active grass layer (Layer 0, `Alpine Grass`) weight $W_{\text{grass}} \in [0, 1]$.
    - Terrain Slope Angle: computes surface normal steepness from terrain height gradients $\nabla H$; blades suppress on cliff faces and steep inclines ($N_y < 0.75$).
    - Procedural Blue-Noise / White-Noise jitter: avoids unnatural grid alignment patterns.

- **GPU Instancing & Indirect Drawing Pipeline**:
  - High-density foliage (tens to hundreds of thousands of blades) rendered in a single GPU draw call using `DrawIndexedInstanced` (or Compute Shader culling with `DrawInstancedIndirect`).
  - Base geometry: compact 3-to-5 quad clump meshes (6 to 12 triangles per clump) or cross-plane quads with vertex color gradients:
    - Vertex Color Alpha / Y-channel: encodes blade root ($Y=0$, rigid anchor) vs blade tip ($Y=1$, maximum wind deflection).
    - Ambient Occlusion gradient baked into vertex colors: roots grounded with deep contact shadowing ($AO \approx 0.15$), tips receiving full direct irradiance ($AO = 1.0$).

- **Dynamic Wind Sway & Interactive Foliage Physics**:
  - **Vertex Shader Wind Simulation**:
    - Multi-frequency sine/cosine wave harmonic displacements combined with high-speed directional noise texture sampling:
      $$\vec{D}_{\text{wind}} = \vec{W}_{\text{dir}} \cdot \left(\sin(\omega t + \vec{P} \cdot \vec{K}) + 0.35 \sin(2.7 \omega t + \vec{P} \cdot 1.8 \vec{K})\right) \cdot Y^2$$
    - Wind gust turbulence travels realistically across the landscape.
  - **Dynamic Entity Trample / Displacement**:
    - Passes up to 8 interactive sphere colliders (player, enemies, rolling boulders) into constant buffer `cbuffer FoliageInteraction : register(b4)`.
    - Blades within interaction radius smoothly bend outward away from the entity pivot and recover elastically when vacated.

- **Foliage Lighting, Subsurface Scattering (SSS) & LOD Culling**:
  - Two-Sided Shading (`CullMode = NONE`) with inverted backface normals for correct direct sun BRDF evaluation.
  - Thin-Leaf Subsurface Scattering (Translucency): wraps sunlight through the blades when looking toward the sun ($\vec{L} \cdot -\vec{V} > 0$), producing vibrant luminous rim highlights.
  - Distance Fade & Stochastic Culling: blades smoothly scale downward to zero height at the outer visibility radius ($R \approx 80\text{m} - 120\text{m}$), eliminating abrupt popping artifacts and maintaining stable $60\text{+} \text{ FPS}$.

---

### 16. End-to-End Runtime Verification in GameMaker LTS 2026

- **Overview & Test Scope**:
  - Validated complete end-to-end integration between GM3D Studio scene export (`GM3DCheck.exe export`) and the GameMaker LTS 2026 native 64-bit runtime (`runtime-2026.0.0.23`, `Runner.exe`).
  - Target project: `scratch/gm-lts-check/project/3DTEST.yyp`.
  - Representative test scene: `scratch/test-scene/full_test_scene.json` (`LTS Full Showcase`).

- **Scene Assets & Configuration**:
  - **Environment**: Directional sun with 2-cascade Cascaded Shadow Mapping (CSM), distance fog, sky atmosphere.
  - **Procedural Multi-Texture Terrain**: 32x32 heightfield generated analytically with derivative-damped elevation ($H(x,z) = 7.0 + 3.0 \sin(fx) \cos(fz)$), 4-layer splatmap weights (`Alpine Grass`, `Cliff Granite`, `Coastal Sand`, `Cobblestone Path`), exported as `terrain.pfm`.
  - **PBR Material**: Custom PBR armor material bound to albedo `assets/models/skeleton_texture.png` and normal map `GM3Dext_Bridge/out_skeleton/skeleton_norm_0.png`.
  - **Skeletal Mesh Entities**:
    - `Skeleton_Warrior`: Rigged model (`Skeleton_Warrior.glb`) with box collider.
    - `Animated_Mannequin`: Rigged model (`UAL1_Standard.glb`) playing clip 9 (`Idle_Loop`) with capsule collider.
  - **Dynamic Jolt Rigid Bodies**:
    - `Dynamic_Boulder`: Sphere rigid body ($M=2.5$).
    - `Dynamic_Chest`: Box rigid body ($M=1.5$).
  - **Point Lights**: Orange torch light casting dynamic shadows, cyan crystal light for localized ambient fill.

- **GameMaker LTS 2026 Pipeline Execution**:
  - **Asset Compiler (`GMAssetCompiler.dll` / `Igor.exe`)**:
    - Compiled `3DTEST.yyp` under 64-bit Windows target (`Release` build, `vm` runtime).
    - Packaged `scene_gm3d.json`, `terrain.pfm`, and `assets/` into `3DTEST.win`.
    - Total asset compiler time: ~400ms, 0 errors, 0 warnings.
  - **Runtime Execution (`Runner.exe` x64)**:
    - Direct3D 11 hardware device initialized successfully.
    - Extension initialized: `[GM3Dext] Engine initialized successfully! Version: 1`.
    - Scene loader (`scr_gm3d_load_scene.gml`) deserialized `LTS Full Showcase` (5 entities, 4 physics bodies).
    - Verified terrain elevation query: `gm3d_terrain_get_height(0, 0) == 7.0`.
    - 420-frame continuous simulation stepped at 60 FPS:
      - `RigidBody ID=6` (Chest): fell under gravity from $Y=18.0 \to 16.76 \to 13.12$, struck terrain at $Y \approx 7.72$, slid along the slope, and settled at $Y=6.84$.
      - `RigidBody ID=5` (Boulder): fell from $Y=16.0 \to 14.76 \to 11.12$, impacted terrain at $Y=9.04$, rolled down the hill slope to $Y=7.12$.
      - Dynamic physics readback (`buffer_peek` on transform buffers) tracked smooth body coordinates with zero memory drift.
    - Clean test conclusion: `GM3D_LTS_TEST_SUCCESS: All entities rendered, physics stepped, animation ticked!`.
    - Extension shutdown: `[GM3Dext] Engine shut down.`, runner process exited with code 0.

- **Visual Validation & Window Capture**:
  - Automated window capture via `PrintWindow` and `DesktopScreenCapture` Win32 API.
  - Captured verified in-game screenshot: `lts_runtime_shot.png` (1107x652 resolution).
  - Confirmed visual rendering of terrain splatting, dynamic shadow projections, skeletal animated characters, falling physics bodies, and Direct3D 11 viewport stability.

---

### 17. Procedural Instanced Grass & Foliage Engine Implementation

- **Architecture & System Design (`GrassSystem.h`, `GrassSystem.cpp`)**:
  - Engineered a native GPU-instanced foliage rendering subsystem within `RenderWorld`:
    - **Clump Geometry**: 3 intersecting vertical quads in a star pattern ($0^\circ, 60^\circ, 120^\circ$ yaw) generating 12 vertices and 18 indices per clump with smoothed upward/hemispherical normals for lush lighting without harsh backface contrast.
    - **Dual-Stream Vertex Input**: Stream 0 delivers per-vertex attributes (`POSITION`, `NORMAL`, `TEXCOORD0`, `TEXCOORD1` with quadratic wind weight $(y/h)^2$ and baked ground AO gradient). Stream 1 delivers 48-byte aligned per-instance data (`INST_POS`, `INST_ROT`, `INST_SCALE`, `INST_PHASE`, `INST_COLOR`).
    - **Single Draw Call Execution**: Renders up to 120,000 active 3D grass clumps (~720,000 triangles) in a single `DrawIndexedInstanced` call.

- **HLSL Foliage Shader Pipeline (`VS_Grass`, `PS_Grass`)**:
  - **Row-Major Matrix Layout**: Declares `row_major float4x4 g_view_proj;` to strictly conform to GM3D's column-vector / row-major DirectX 11 pipeline.
  - **Dual-Distance Smooth Fading**:
    - Distant Fade: Smoothly collapses clump scale to 0 across $R_{\text{fade}} = [90\text{m}, 120\text{m}]$, discarding out-of-range instances safely beyond the far plane ($Z > 1.0$) with zero fillrate waste.
    - Near-Camera Proximity Fade: Smoothly fades clumps within $1.5\text{m}$ of the camera lens ($\text{nearFade} = \text{saturate}((d - 1.5) / 1.0)$), eliminating close-range camera clipping artifacts.
  - **Dynamic Multi-Frequency Wind Sway**:
    - Harmonics combining high-frequency rustle with traveling low-frequency gusts:
      $$\vec{D}_{\text{wind}} = \vec{W}_{\text{dir}} \cdot (\sin(\omega t + \vec{P} \cdot \vec{K}_1 + \phi) + 0.35 \sin(2.3 \omega t + \vec{P} \cdot \vec{K}_2 + 1.6 \phi)) \cdot (0.65 + 0.35 \text{gust}) \cdot \text{strength} \cdot W_{\text{wind}}$$
    - Quadratic vertical tip displacement with horizontal length conservation ($\Delta y = -|\vec{D}| \cdot 0.22$).
  - **Dynamic Entity Trampling Interaction**:
    - Injects dynamic sphere collider `g_interaction_collider` ($X, Y, Z, \text{radius}$).
    - Blades within interaction radius elastically bend outward away from entity pivots and depress downward.
  - **Realistic Foliage Shading**:
    - Two-Sided Shading (`CullMode = NONE`) with normal flipping on backfaces (`!isFrontFace`).
    - Base-to-tip natural hypsometric color gradient blending alpine emerald green with sunlit golden highlights.
    - Half-Lambert wrapped diffuse lighting ($N \cdot L \times 0.6 + 0.4$) for soft leaf translucency.
    - Thin-Leaf Subsurface Scattering (SSS): wraps backlight into vibrant rim radiance ($\text{pow}(\vec{V} \cdot -\vec{L}, 3.0) \times 0.55$).
    - Ground contact AO shading and atmospheric distance fog integration.

- **Terrain & Splatmap Density Distribution (`build_from_terrain`)**:
  - Automatically distributes clumps across the terrain based on:
    - Slope angle threshold ($N_y \ge 0.72$, eliminating cliff growth).
    - Water elevation check ($Y \ge 1.4\text{m}$ to $23.5\text{m}$).
    - 4-layer splatmap filtering: samples Layer 0 (`Alpine Grass` weight $\ge 0.10$) and suppresses on Layer 3 (`Cobblestone Path` weight $> 0.40$).
    - Hash-based jitter and natural scale/color variation.

- **GameMaker LTS 2026 Verification**:
  - Exported and verified in `Runner.exe` x64: 86,410 procedural 3D grass clumps simulated and rendered stably at 60 FPS in a single draw call with zero memory leaks.

### 17a. Tuft geometry rework (2026-10-31)

The original clump geometry was three crossed quads, each one a barely tapered card `base_width` wide and `base_height` tall. With the shipped defaults (0.22 m wide, 0.85 m tall) every clump rendered as a large pale paddle, and because the clump footprint equalled the blade width there was no tuft structure at all: the field read as scattered cardboard.

Changes in `GrassSystem::init_geometry` and `s_grass_hlsl`:

- **Blade-shaped geometry.** A clump is now 12 thin ribbons of 3 rows each, laid out with a golden-angle yaw and a pseudo-random root offset, height, width and bend. Blade width is 0.045 of the tuft footprint instead of 0.95, so `base_width` now means "tuft diameter" rather than "blade width".
- **Taper to a point.** Row width follows `(1 - t)^0.65`, ending at 15% width, so silhouettes are pointed instead of rectangular.
- **Per-row wind weight** is `t^2` (stiff at the root, loose at the tip) and **AO** runs 0.18 -> 1.0, giving the tuft contact shading.
- **Distance handling.** The vertex shader used to scale the whole tuft by the distance fade, which made far grass sink into the ground. It now thins the blade (`widthFade = 0.55 + 0.45 * fade`) and keeps 80-100% of the height.
- **Colour.** Root is `0.42x` the instance tint instead of `0.65x`, the tip gradient is quadratic, and the sun specular was reduced from 0.18 to 0.10.
- **Defaults** in `SceneData.h`: `grass_height` 0.85 -> 0.55, `grass_width` 0.22 -> 0.40 (tuft diameter), spawn spacing 0.65 -> 0.34 m.

Cost at the shipped density (90,601 clumps): about 1.3 ms of a 3.3 ms frame (303 FPS) on the reference machine, measured with `--grass-demo --bench`.

Note that `build_from_terrain` clamps the spawn grid to 300 steps, so reducing `base_spacing` increases density inside a smaller radius rather than growing the covered area. The grass field therefore covers roughly +/-51 m around the origin at default density.

> Later revisions to blade lighting, tint and density are in §34.

---

### 18. Unity-Style Interactive Foliage Painting (Grass Brush) & Zero-Default Architecture

- **Zero-Default Architecture (Absent by Default)**:
  - Foliage is strictly absent by default (`grass_enabled = false`, instance count = 0).
  - Generating, resizing, or sculpting terrain does not automatically spawn uninvited grass across the map.
  - Clumps only appear when explicitly painted with the Grass Brush or requested via procedural fill.

- **Unity-Style Foliage Painter Tooling (`TerrainBrushMode::Grass`)**:
  - Integrated directly into the Terrain Editor panel alongside height sculptors and splatmap painters.
  - **3D Viewport Brush Projector**:
    - Real-time terrain-conforming ring indicator at cursor raycast intersection.
    - Vibrant Emerald Green (`IM_COL32(50, 240, 120, 255)`) indicates active painting.
    - Bright Coral Red (`IM_COL32(255, 80, 60, 255)`) indicates active erasing (holding `Shift` or checking `Erase mode`).
  - **Brush Dynamics & Poisson Proximity Filtering**:
    - Spawns clumps uniformly within the circular brush disc with rate-controlled density accumulator (`m_grass_brush_density`).
    - Enforces a minimum root distance ($0.22\text{m}$) to prevent dense point over-stacking and preserve frame rates.
    - Evaluates terrain surface normals ($N_y \ge 0.60$) to prevent unnatural sprouting on sheer cliff faces.
    - Adds natural jittered yaw rotations ($[0, 2\pi)$), scale variation, individual wind phase offsets, and subtle tint noise.
  - **Interactive Eraser**:
    - Instantly cleans out all clump roots within the brush radius when dragging with `Shift + LMB` or in Erase mode.
  - **Terrain Re-Sculpting Synchronization (`snap_to_terrain`)**:
    - Automatically updates all existing clump root elevations to match new terrain heights whenever the terrain is sculpted.
  - **Quick Action Controls**:
    - `Paint Grass Tool`: One-click button to activate foliage brush mode.
    - `Clear All Grass`: Resets the entire grass collection to 0 instances and frees GPU buffers.
    - `Fill Terrain (Procedural)`: One-click convenience button to auto-populate the terrain using the splatmap rule-set.
    - `Snap Grass to Terrain Heights`: Re-grounds all foliage clumps to the current terrain mesh.

- **Serialization & Binary Packaging (`grass.bin`)**:
  - **Scene JSON**: Serializes painted clump coordinates, dimensions, rotations, and color tints into `terrain.grass_clumps`.
  - **High-Performance Binary Format (`grass.bin`)**:
    - 4-byte ASCII Magic: `0x53415247` (`'GRAS'`).
    - 4-byte Version: `1`.
    - 4-byte Clump Count: `uint32_t`.
    - Binary Payload: `N * 48` bytes (`GrassGPUInstance` stream).
  - **GameMaker LTS Package Exporter**:
    - Automatically exports `grass.bin` alongside `scene_gm3d.json` and updates `scr_gm3d_load_scene.gml` with fast binary loading (`grass_load_file`).
  - **Exported DLL C-API**:
    - `grass_clear()`
    - `grass_load_file(const char* filepath)`
    - `grass_save_file(const char* filepath)`



---

## 33. Render-side culling, shadow-pass batching and the instance arena (2026-10-31)

The forward renderer drew every active entity in every pass. With two sun cascades and four shadow-casting point lights that is 26 full-scene shadow passes per frame, plus a camera pass with no frustum test at all. Measured on the synthetic `--bench-stress` scene (576 props, 4 shadowed point lights), a frame took **23.0 ms** (43 FPS).

### What was wrong

Profiling with `ID3D11DeviceContext::Flush` inside the pass boundaries showed the GPU finishing the same frame in about **1 ms**. The remaining ~22 ms was CPU time spent inside `Map(D3D11_MAP_WRITE_DISCARD)` on the 600 KB dynamic instance buffer: one call per draw, up to 312 per frame. Every discard makes the driver rename the whole allocation, and the cost scales with buffer size, not with the amount written. Shrinking the same buffer to 120 KB dropped the frame to 1.39 ms without any other change - which is what identified the culprit.

### Changes

1. **Instance arena (`RenderWorld::begin_instance_write`)**. One dynamic buffer of 65536 instances (3.9 MB). The cursor resets per frame; the first write uses `WRITE_DISCARD`, every following write uses `WRITE_NO_OVERWRITE` at a monotonic offset. No region is reused within a frame, so pending draws never read memory that has been rewritten. Draws bind the buffer with a byte offset instead of re-uploading.
2. **Frustum culling in the camera pass.** `GPUMesh` now carries local bounds computed at upload (`MeshManager::create_gpu_mesh`); skinned meshes are inflated by 1.5x because an animated pose can leave the bind-pose volume. `transform_aabb` builds the world AABB from eight transformed corners, so mirrored (negative) scale stays correct. Six-plane tests live in `src/render/FrustumMath.h` and are unit tested by `FrustumMathTests`.
3. **Per-light caster culling.** The shadow pass tests each caster against the cascade or point-light face it is being rendered into, instead of redrawing all of them.
4. **Depth-only fast path.** Shadow draws skip material constant-buffer uploads, texture binds and blend-state changes.
5. **Pose cache.** Skeletal poses are evaluated once per entity per frame and reused by the shadow and camera passes. Previously a skinned entity was re-evaluated for every cascade and every light face.
6. **Identity bone upload hoisting.** The 128 identity matrices that rigid meshes need are uploaded once per pass rather than once per batch.
7. **Transparent sorting.** Batches with `alpha_mode == 2` are drawn after opaque geometry, far to near, and also sorted inside the batch. Previously they were drawn in `unordered_map` order, so overlapping glass could flicker frame to frame.

### Measurements

Same machine, same build configuration, `--bench 180`, viewport 1280x720, vsync off, 30-frame warm-up excluded. p50 of `total_ms`.

| Scene | Before | After | Speed-up |
|:---|---:|---:|---:|
| `--bench-stress` (580 entities, 26 shadow views) | 23.00 ms (43 FPS) | 1.07 ms (937 FPS) | 21x |
| default editor scene (3 entities) | 1.91 ms (522 FPS) | 1.96 ms (511 FPS) | unchanged (2% run-to-run noise) |

The default scene is unchanged by design: three objects fit in the viewport and their cost was already dominated by post-processing.

### Verification

- `FrustumMathTests` covers the plane extraction and the AABB transform (inside, behind, beyond-far, beside, straddling, enclosing boxes; rotation, scale, mirrored scale).
- Rendered output was compared before and after the change with a hidden-window capture of the same scene: the default scene is **pixel-identical** (max channel difference 0). On the stress scene the residual differences are concentrated on silhouette edges and are the same size as the difference between two runs of the *same* build, i.e. run-to-run noise from the point lights settling, not a culling artefact.
- `SceneSerializerTests`, `EngineLifecycleTests`, `GM3DShaderCheck` and `GM3DGraphicsSmoke` still pass.

### Pitfalls for future work

- Never reintroduce a per-draw `Map(WRITE_DISCARD)` on the shared instance buffer. If a new pass needs instance data, call `begin_instance_write` and pass the offset to `IASetVertexBuffers`.
- The arena is 65536 instances per frame. A scene that needs more will silently stop adding instances; check `begin_instance_write` returning `nullptr` before assuming geometry was drawn.
- Frustum planes are extracted from matrix *columns* because the engine uses row-vector arithmetic with row-major storage. Using rows silently culls the wrong half of the world.
- Skinned bounds are heuristic. A model whose animation travels far from its bind pose can be culled early; raise the inflation factor in `create_gpu_mesh` rather than disabling culling for all skinned meshes.

---

## 34. Photographic CC0 materials, linear albedo and the lighting rebalance (2026-10-31)

The editor looked like pastel paint: a bright, desaturated, blue-grey field with pale yellow-green grass. Three independent mistakes were stacking.

### Why it looked washed out

1. **Albedo was gamma corrected twice.** Every texture was sampled and multiplied as if it were linear, but the composite pass ends with `pow(mapped, 1/2.2)`. Photographic or authored sRGB values therefore got corrected a second time, which lifts shadows and flattens saturation.
2. **The blue ambient dominated the sun.** `AmbientColor` defaulted to `(0.45, 0.58, 0.78)` at intensity `1.15` and was then multiplied by a hemispheric term that peaked at `1.35` in blue. Up-facing surfaces received roughly `1.2` in blue from ambient alone, which desaturated every albedo towards white.
3. **Post grading pushed it further.** The composite added `+18%` saturation and a `1.14x` blue shadow tint, which on a blue-lit image just produced pastel grey-blue.

### Lighting changes

| Knob | Before | After | Why |
|:---|:---|:---|:---|
| `SceneData::ambient_color` | `0.45, 0.58, 0.78` | `0.42, 0.45, 0.48` | Neutral fill; the sky tint lives in the hemispheric term |
| `SceneData::ambient_intensity` | `1.15` | `0.55` | Indirect light must not exceed direct sunlight |
| shader `skyLight` | `0.75, 0.95, 1.35` | `0.66, 0.78, 1.00` | Keeps a cool sky without crushing hue |
| post `shadowTint` | `1.04, 1.06, 1.14` | `1.01, 1.02, 1.05` | Removes the blue cast |
| post `satBoost` | `1.18` | `1.05` | Photographs already carry saturation |
| `RenderWorld::m_exposure` | `1.15` | `1.02` | Linear albedo is darker; this rebalances without clipping |

**Conductor fill light.** With `metallic = 0.85` a material has almost no diffuse term, and with no environment probe a metal only showed the sun's specular lobe - the gold cube rendered as a black box. The pixel shader now adds `envProbe * F0 * (1 - roughness) * 2.0 * metallic`, where `envProbe` is a fixed sky/ground hemisphere `(0.52, 0.66, 0.92)` / `(0.30, 0.26, 0.21)`. It is deliberately independent of `AmbientColor`, which is now too dim to serve as a probe.

### Texture pipeline

`assets/textures/` holds seven CC0 texture sets from [ambientCG](https://ambientcg.com) (CC0 1.0, see `assets/textures/CREDITS.md`), generated by `python tools/fetch_textures.py`:

| Folder | Used for |
|:---|:---|
| `Ground037` | terrain layer 0, meadow soil and moss |
| `Rock030` | terrain layer 1, cliff granite; material preset 8 |
| `Ground080` | terrain layer 2, coastal sand |
| `PavingStones131` | terrain layer 3, plaza flagstone |
| `Wood066` | material preset 5, timber |
| `Metal032` | material preset 7, steel |
| `Grass001` | `TexId::TURF`, dense green turf for future materials |

Each set keeps `_Color.jpg`, `_Normal.png` (OpenGL convention) and `_Roughness.jpg` at 512 px; the roughness map is not sampled by the shader yet. Total 4.0 MB.

- **IDs are reserved, not appended.** `src/render/BuiltinTextures.h` fixes `0..13` (procedural debug patterns, baked terrain albedo, the CC0 sets) and moves dynamic ids to `32`, so a texture id stored in a scene file means the same thing across builds.
- **Loading is optional.** `TextureManager::load_builtin_texture_sets` looks for `<exe>/assets/textures`, then `../..`, then the working directory, and honours the `GM3D_ASSET_DIR` environment variable. If nothing is found it logs once and the engine keeps the procedural textures, so the DLL still works dropped into a GameMaker project with no assets.
- **CMake copies the folder** next to `GM3DStudio.exe` post-build, so a copied build directory is self-contained.

### Terrain detail blend

The baked terrain albedo is 2048 px across a 240 m world - about 8 texels per metre. It carries biome colour, but at that density it can never read as a surface. `TerrainSystem::update_detail_splat` bakes a 256x256 RGBA8 weight map (grass / rock / sand / cobble) from the same hypsometric ramps as the albedo, with the painted splat map overriding it, and the pixel shader blends four tiled sets on top:

- Detail UVs are world-space (`WorldPos.xz * 0.40`, about 2.5 m per tile), so the texture never stretches with the mesh.
- Albedo is squared into linear space (`c * c`); normal maps are not.
- Flat ground only samples the XZ projection. Faces steeper than `N.y^2 < 0.80` additionally sample a side projection and cross-fade, so cliffs do not smear.
- The baked albedo is demoted to a low-frequency hue tint, and that tint only follows the grass and sand weights - otherwise the stone plaza picked up a green cast from the meadow palette.

Cost at 1600x900 on the reference machine: the whole grass-demo frame is **1.97 ms p50** (508 FPS), the same as the default three-object scene, and the stress scene is **0.92 ms** (1087 FPS).

### Grass follow-ups

- **Back faces no longer go black.** The grass shader flipped the whole normal on back faces, so the downward copy faced away from the sun and the blade rendered as a black spike. It now flips only the horizontal component and keeps the upward bias.
- **Tint is gamma corrected** with `baseColor *= sqrt(baseColor)` (about gamma 1.5): the tint values are authored, not photographs, and full squaring made blades much darker than the terrain.
- **Density.** The `steps` clamp in `build_from_terrain` went from 300 to 420 and `base_spacing` from 0.34 to 0.30, giving **177,241 clumps** over roughly +/-63 m instead of 90,601 over +/-51 m. The hard instance ceiling and the paint-brush cap both went to 260,000 so the field is not truncated asymmetrically.

### Pitfalls for future work

- Any new albedo texture must be squared (or `pow(x, 2.2)`) before it reaches the lighting maths. Normal, roughness and metallic maps must not be.
- `update_detail_splat` is called from `update_albedo`, so terrain painting refreshes the detail blend automatically. A new code path that changes heights without calling `update_albedo` will leave the detail weights stale.
- Inside `RenderWorld.cpp` the unqualified `TexId::` did not resolve inside member functions even though the header was included (MSVC reported `C2653`); the file uses the fully qualified `GM3D::TexId::` everywhere. Keep it qualified.
- `assets/textures` is required at runtime for the photographic look, not for correctness. Never make a code path depend on it - always keep the procedural branch working.

---

## 35. Why the editor ran at 30 FPS with three objects (2026-10-31)

Report: an empty terrain plus two props rendered at **32 FPS**. The renderer's own benchmark claimed 1.9 ms per frame (522 FPS), so the cost was not in the scene.

### Measuring instead of guessing

`--frame-profile` splits every frame into five sections (UI, scene sync, `EngineContext::update`, ImGui draw, `Present`) and logs the average once per second:

```powershell
.\build\GM3DStudio.exe --frame-profile --bench 300 --log build\prof.log
```

The first run was unambiguous: `ui=0.35 sync=0.00 scene=2.25 imgui=0.26 present=32.63 | total=35.5 ms`.

Three separate bugs were stacking, and only the last one is about the GPU.

### 1. Two presents per frame

`RenderWorld::render()` ended with `m_swap_chain->Present(...)` unconditionally. The editor never renders to the swap chain: it sets `set_custom_render_target(m_viewport_rtv)` and composites the viewport into an ImGui panel, then presents once itself after drawing the UI. So every editor frame presented **twice**: the first call also pushed a back buffer that still contained the *previous* frame's UI.

With vsync on, each present waits for a vblank, so the frame took two intervals - a hard **30 FPS on a 60 Hz display and 72 FPS on a 144 Hz one, regardless of scene contents**. The fix is three lines: skip the present when a custom render target is active.

```cpp
if (!m_custom_render_target) {
    m_swap_chain->Present(m_config.vsync ? 1 : 0, 0);
}
```

### 2. The process was running on the integrated GPU

Hybrid laptops route an unknown executable to the iGPU unless it opts in. `GM3DGraphicsSmoke.exe` still logs `Adapter: Intel(R) UHD Graphics 630 (128 MB)` on a machine whose discrete card is a GTX 1060, which is what the editor was rendering on. A three-line export in the Studio's `main.cpp` fixes the affinity for the whole process:

```cpp
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
```

Afterwards the same log line reads `Adapter: NVIDIA GeForce GTX 1060 with Max-Q Design (2965 MB)`.

### 3. Bitblt swap chain on a cross-adapter surface

The chain was created with `D3D11CreateDeviceAndSwapChain` and `DXGI_SWAP_EFFECT_DISCARD`. The legacy bitblt model makes DWM copy the entire back buffer on every `Present`, and when the device and the desktop live on different adapters that copy crosses the bus: **32.5 ms in Present** for a 1600x900 window. The chain is now created with `IDXGIFactory2::CreateSwapChainForHwnd` and `DXGI_SWAP_EFFECT_FLIP_DISCARD` (buffer count 2, `DXGI_SCALING_STRETCH`), with the old path kept as a fallback for pre-Windows-10 hosts.

Since the chain is created from an existing device now, `D3D11CreateDeviceAndSwapChain` is gone; the device comes from `D3D11CreateDevice`, and the adapter is queried back through `IDXGIDevice::GetAdapter` so the name can be logged and shown in the status bar.

### Measurements

Same machine (GTX 1060 Max-Q + Intel UHD 630), 1600x900, editor viewport, three objects.

| Stage | `Present` | Frame | Result |
|:---|---:|---:|:---|
| before | 32.6 ms | 35.5 ms | 28 FPS, vsync-locked to two intervals |
| + Optimus exports | 3.2 ms | 6.3 ms | 158 FPS uncapped (still vsync-locked to 1 interval in use) |
| + single present per frame | 3.2 ms | 3.4 ms | 287 FPS uncapped |
| + flip-model chain | 1.5 ms | 3.0 ms | 329 FPS uncapped, **144 FPS** with vsync (display refresh) |

The engine's own benchmark number moved from 1.9 ms to 0.37 ms for the same reason: it used to include the renderer's internal present.

### Pitfalls for future work

- Never call `Present` from a render path that targets a custom render target. The editor owns its single present; the DLL path (`GM3Dext.dll` inside GameMaker) still presents, because there `m_custom_render_target` is null.
- Keep the bitblt fallback. Flip model needs Windows 10 and a `CreateSwapChainForHwnd` factory; both are checked, and the code degrades instead of failing.
- `ResizeBuffers` must pass the same flags the chain was created with - the flip chain uses `0`, which is what `RenderWorld::resize` already passes.
- `NvOptimusEnablement` only affects the process that exports it, so it lives in `tools/studio/main.cpp`, not in the DLL. Inside GameMaker the host executable decides the adapter.
- The status bar now shows the adapter name next to the FPS, and `[RenderWorld] Adapter:` is printed to the log at startup. Check that line first when a frame-time report looks impossible.

---

## 36. Terrain detail tiling produced pale crawling patches (2026-10-31)

Report and screenshots: large pale patches with straight, triangle-like edges covering the terrain, shimmering whenever the camera moved. They survived `--no-shadows` and disappeared when the detail layer was switched off.

### Root cause

The detail sets are 512 px and were tiled every 2.5 m, which is about **200 texels per metre**. The terrain is normally seen from 1.7 m eye height at a grazing angle; a screen pixel at 30 m covers roughly 7 texels along the view direction and far more across it. The mip chain answers that with a very high mip level, and a 512 px photo at mip 7 is an 8x8 blob - so **every tile became a flat blotch of a slightly different colour**. Because the screen-space derivative jumps from triangle to triangle on a heightfield, the mip transitions land on triangle boundaries, which is why the pattern read as geometry (quads split by a diagonal) rather than as texture.

The 4x4 debug render of `input.UV` (see below) showed a clean gradient, and the baked 256x256 splat weight map dumped to a PPM was smooth, which ruled out both the mesh UVs and the weights.

### Fix

The detail is now only used close up:

```hlsl
float distToCam  = length(input.WorldPos - CameraPos);
float detailFade = saturate(1.0 - (distToCam - 10.0) / 25.0);   // full < 10 m, gone at 35 m
baseColor = lerp(baseColor, detailAlbedo * macroTint, detailFade);
N = normalize(N + float3(dn.x, 0.0, dn.y) * (0.85 * detailFade));
```

Inside 35 m the photographs do their job on the ground under the player; beyond that the smooth baked macro albedo (plus fog) carries the surface, which is exactly the band where minification made the detail unreadable anyway. Fading the normal relief with the same factor matters as much as the albedo: at range it was pure normal-map aliasing.

Trade-off to revisit: the far terrain is now back to the flat macro colour. A proper solution is a second, coarser detail scale (or a low-res "macro variation" set) instead of one 200-texel/m set used at every distance.

### Debugging trap found on the way

`--no-grass` and `--no-shadows` were silently ineffective: `StudioApp::sync_scene_to_engine` re-applies `m_scene.grass_enabled` and `m_scene.shadows_enabled` on every frame, so disabling the render world once was undone immediately. Every "no grass" comparison still had grass in it. The flags now clear the scene field as well. Any future A/B switch has to touch the scene state, not just the renderer.

### Debug aids added

| Flag / variable | Use |
|:---|:---|
| `--camera px,py,pz,tx,ty,tz` | Reproduce an exact view for a screenshot |
| `--no-grass` / `--no-shadows` | Isolate layers in a capture |
| `GM3D_DUMP_SPLAT=1` | Writes `build/splat_dump.ppm` (the terrain weight map) on the first bake |

Temporarily returning a value straight from the pixel shader (`return float4(w.x, w.y, w.z, 1);`) is the quickest way to prove whether a pattern comes from the weights, the albedo or the lighting - but remember that a broken shader still lets the Studio start, so always run `GM3DShaderCheck.exe` after such an experiment.

---

## 37. Sparkling specular shards on the terrain (2026-10-31)

Follow-up to section 36. After the tiling fade the pale patches changed colour but did not go away: the terrain still showed **dithered white shards with straight triangle edges**, shimmering with camera movement.

### What it was not

Each of these was ruled out by measurement, not by reading the code:

| Suspect | Test | Result |
|:---|:---|:---|
| Z-fighting / a second surface | batch dump + depth func set to `ALWAYS` | one batch, one instance; shards survive with the depth test disabled |
| Shadows | `--no-shadows` (which now really disables them) | pixel diff max 17, shards unchanged |
| Grass | `--no-grass` | bit-identical frame |
| Particles | particle pass disabled | bit-identical frame |
| Terrain detail textures | `MatPad.x = 0` | shards unchanged |
| Albedo | terrain albedo replaced by white | faint but present |
| Vertex normals | normals rendered as colour (before and after normal mapping) | smooth |
| World position | `frac(WorldPos * 0.25)` rendered | smooth |
| Depth / mesh UVs | `frac(UV * 4)` and a dump of the splat map | both smooth |

Returning a constant colour from the pixel shader made the shards disappear entirely, so they live in the terrain's own shading.

### The cause

Probing the individual lighting terms (a temporary `GM3D_DEBUG_MODE` switch inside `PS_Main`) isolated it to **specular**: `directLight` showed the shards, `ambient` did not, and `specular` alone reproduced them exactly. Forcing `roughness = 0.85` unconditionally removed them.

The terrain block was therefore not running for part of the surface. It was gated on `(int)input.Param.y == 5`, and rendering that comparison as red/black showed the black shards exactly where the test failed: the per-instance parameter is not reliable per pixel on the heightfield. Those fragments fell through to the default material (roughness 0.45, glossy) instead of the terrain's matte 0.85, and the GGX lobe exploded at grazing angles into bright speckles that ACES clipped to white.

### The fix

The terrain is no longer identified by instance data. `draw_batch_items` stamps the terrain batch with a sentinel in the material constant buffer and pins its surface response there:

```cpp
if (albedo_id == GM3D::TexId::TERRAIN) {
    matProps.preset_id  = -5;      // sentinel matched by PS_Main
    matProps.roughness  = 0.85f;
    matProps.metallic   = 0.01f;
    if (detail_ready) matProps.pad[0] = 0.40f;
}
```

and the pixel shader keys the terrain path on `matId == -5`. Because the flag and the roughness now come from the per-batch constant buffer, every terrain fragment takes the same path regardless of what the instance slot happens to contain.

### Lessons

- When a bug appears only on some pixels of one draw, the first thing to question is **per-instance data**: it is supposed to be constant, so if behaviour varies per pixel either the attribute is not what you think or the pixel is not from that draw. Rendering the predicate itself as a colour answered the question in one frame.
- A glossy default material on a large ground plane is a reliable way to produce suspiciously bright artefacts. Ground should have an explicit roughness in the material constants, not a value assigned inside a shader branch.
- The `--no-grass` / `--no-shadows` / `--camera` flags from section 36 were what made this bisection possible; every dead end above cost one screenshot.

---

## 38. Mesh colliders instead of a cylinder stub (2026-10-31)

Until now the editor's fourth collider shape was a lie: `collider_shape == 4` read "Convex Mesh" in the inspector, but `start_simulation()` clamped the value to `0..3`, so the body was built as a cylinder and `Diagnostics` reported "Mesh collider is not implemented; editor simulation uses a cylinder". This section replaces the stub with real geometry.

### Where the triangles come from

The renderer only kept GPU buffers, so there was no CPU copy to hand to Jolt. `MeshManager` now keeps one:

```cpp
struct CollisionMesh {
    std::vector<float> positions;   // three floats per vertex, mesh-local space
    std::vector<uint32_t> indices;
};
```

It is filled inside `create_gpu_mesh()`, which every loader (OBJ, glTF/GLB, built-in primitives, terrain) funnels through, so imports get collision geometry for free. Three details matter:

- **Cap.** `kMaxCollisionTriangles = 400000`. Above that the CPU copy is dropped and the collider falls back to a box; the alternative was silently doubling the memory of a heavy model that is never simulated.
- **In-place updates.** `update_mesh_vertices()` also rewrites the affected position range, so a sculpted heightfield mesh never leaves a stale copy behind.
- **`replace_mesh()`** moves the copy along with the GPU mesh; without that, replacing a model would keep the old collider.

### Trimesh or convex hull

`RigidBodyDesc` gained `mesh_positions` / `mesh_vertex_count` / `mesh_indices` / `mesh_index_count`, already in shape-local space, and `PhysicsWorld::create_mesh_shape()` decides what to build from the motion type:

| Body type | Shape | Why |
|:---|:---|:---|
| Static / Kinematic | `JPH::MeshShape` (triangle soup) | Level geometry must be exact, including concavity |
| Dynamic | `JPH::ConvexHullShape` of the same vertices | A triangle soup has no volume, so Jolt cannot derive mass properties from it, and a concave dynamic body tunnels through thin geometry |

`create_body()` gained an optional `MeshColliderKind* out_kind`, which reports `Triangles`, `ConvexHull` or `None`. `None` means the request fell back to a box - no mesh bound, an over-cap mesh, a degenerate mesh, or an unwelded duplicate-only triangle list. The editor counts those and shows "N mesh collider(s) fell back to a box".

The scale convention is the same one the primitive shapes use: `vertex * object_scale * collider_size`. With the default `Collider Size` of 1 the collider coincides with the rendered surface, and dragging the multiplier scales the geometry instead of swapping the shape.

### The one-sided problem

The first version built the trimesh with the model's own winding and objects fell straight through it. Jolt's mesh collisions ignore back faces (`CollideShapeSettings::mBackFaceMode` defaults to `EBackFaceMode::IgnoreBackFaces`), and an arbitrary imported model gives no winding guarantee - a flipped triangle becomes a hole. The fix is to emit every triangle in both windings:

```cpp
triangles.emplace_back(a, b, c, 0);
triangles.emplace_back(a, c, b, 0);
```

This doubles the triangle count of a static collider (which is why the source cap is 400k and not 800k) but removes an entire class of "the floor does not collide" reports that would be impossible to diagnose from the editor.

### Verifying it without eyes

Two new tools, both in the tree:

- `--simulate <n>` starts the Jolt preview at launch, runs `n` frames, logs `[physics] mesh colliders: X trimesh, Y convex hull, Z box fallback` plus a `[simulate]` line with each body's resting transform, then exits.
- `build\MeshColliderTests.exe` (CTest name `MeshCollider`) drives `PhysicsWorld` directly - no device, no window - and pins the behaviour that matters: a box dropped in the middle of a disc-shaped trimesh rests on it, the same box 2.5 m off-centre falls past it (a bounding-box collider would hold it up), a dynamic mesh body reports `ConvexHull` and lands flush, and both the empty and the degenerate mesh report a fallback instead of creating a broken body.

Studio numbers for the scene in `tests/fixtures/meshcollider.json` (a 4 m cylinder trimesh platform with a drop sphere at y = 8):

| Body | Collider | Resting y |
|:---|:---|:---|
| Drop On Cap | box on the trimesh cap | 4.23 (cap at 4.0 + half height) |
| Drop Past Rim (x = 2.6) | box, disc radius 2 | 0.23 (floor, not the cap) |
| Convex Hull Drop | dynamic mesh | 0.98 (half of 2 m, flush) |

### Lessons

- A user-facing enum that the code silently clamps is worse than a missing feature: it made a cylinder look like a working mesh collider. The inspector now says "Mesh (Model)" and the tooltip states the static/dynamic rule.
- Keep the CPU geometry next to the GPU geometry at creation time. Reconstructing it later means reading back from a GPU buffer, which needs a staging copy and only works while the mesh is still resident.
- Trimesh colliders are one-sided unless the engine says otherwise. If the winding comes from a third party, build both sides.

---

## 39. Authored roughness on the terrain (2026-10-31)

`tools/fetch_textures.py` had always downloaded `<Set>_Roughness.jpg`, and nothing sampled it: the terrain pinned `roughness = 0.85` in the shader. This section closes that gap and removes the last magic constant from the terrain surface.

### What the maps actually say

Measured over the shipped 512 px sets (mean of the R channel):

| Set | Terrain layer | Roughness mean | p10 / p90 |
|:---|:---|:---|:---|
| Ground037 | grass/soil | 0.46 | 0.40 / 0.52 |
| PavingStones131 | cobble | 0.50 | 0.43 / 0.58 |
| Ground080 | sand | 0.66 | 0.62 / 0.69 |
| Rock030 | rock | 0.69 | 0.61 / 0.76 |

So the authored truth is *glossier* than the 0.85 constant: soil at 0.46 and paving at 0.50 are ordinary values for scanned ground. The constant was a safe guess, not a measurement.

### Implementation

`TextureManager` stores roughness as an R8 texture with a mip chain (`CreateRoughnessSRV`) instead of an RGBA copy - four of these are sampled per terrain pixel, and the map is one channel of linear data. A 1x1 fallback at 0.85 (`m_flat_rough_srv`) covers sets shipped without a map, so the shader never has to branch. `load_builtin_texture_sets` logs how many sets came with one.

The pixel shader blends the four maps with the same splat weights as albedo and normal:

```hlsl
float roughDetail = DetailGrassR.Sample(TextureSampler, uvTop).r * w.x
                  + DetailRockR.Sample(TextureSampler, uvTop).r * w.y
                  + DetailSandR.Sample(TextureSampler, uvTop).r * w.z
                  + DetailCobbleR.Sample(TextureSampler, uvTop).r * w.w;
terrainRoughness = lerp(0.85, clamp(roughDetail, 0.30, 1.0), detailFade);
```

Two deliberate simplifications:

- **Top projection only.** Roughness is a scalar; on a grayscale map the smear of a side projection is invisible, so cliffs do not pay another four fetches.
- **0.30 floor.** The smoothest texels of a photograph are dark rock at ~0.21; letting the ground reach that would put a mirror under the sun at grazing angles, which is exactly the regime that produced the section 37 artefacts.

The blend uses the existing `detailFade` (10 m to 35 m), so beyond that range the terrain is back to the single 0.85 constant and the far surface stays free of per-pixel specular noise.

### The gate that came with it

While editing the block it became obvious the whole detail path ran on *every* terrain pixel: four albedo taps, four normal taps, and on steep ground eight more, all multiplied by a fade that was already zero past 35 m. In a landscape view most of the screen is that far away, so those fetches were pure waste. The block is now behind `if (detailFade > 0.001)`, which makes the near field pay for the new roughness taps and the far field pay for none of them. A 40-frame capture of the same camera before and after is identical apart from grass wind (measured: same build twice differs by mean 0.29/255, the refactor by 0.27/255).

### Verification

- Near meadow and cobble plaza captured before/after: the plaza stones pick up a restrained sheen, the meadow stays matte under the grass, no speckles at 4x zoom on the ground.
- Diff against a build that forces `terrainRoughness = 0.85`: 10.85% of pixels change by more than 2/255, mean 1.5/255. Subtle by design - it is a specular response on a matte surface, not a colour change.
- Camera-shift test (10 cm) on the ground with grass off: no new high-frequency sparkle, so the maps did not bring back section 37.
- Loader: `[TextureManager] Loaded 7 CC0 PBR texture sets ... (7 with roughness maps)`.

### Lessons

- A fetch script that downloads data nobody samples is a trap: it looks like the feature exists. Both `fetch_textures.py` and the loader now agree on what is used.
- Authored maps frequently disagree with the safe constant. Here the constant was 0.85 and the photographs say 0.46-0.69; trusting the data made the plaza read as stone instead of paint.
- If you add fetches to an already-faded path, check whether the path has a distance gate at all. It did not, and the gate pays for the new samples several times over.

---

## 40. First run inside the real GameMaker LTS Runner (2026-10-31)

Everything in sections 33-39 was verified through `GM3DStudio.exe`, `GM3DGraphicsSmoke.exe` and the unit tests - none of which load `GM3Dext.dll` into a host process. This section is the first end-to-end run of the extension inside GameMaker itself, and it produced two findings that no amount of editor testing would have caught.

### How to run it without opening the IDE

`tools\run_in_runner.ps1` drives `Igor.exe` from the installed runtime with `Windows Run` - the same command-line builder the IDE calls on F5:

```powershell
.\tools\run_in_runner.ps1 -SkipBuild
```

It copies `build\GM3Dext.dll` into `3DTEST\extensions\GM3Dext\` first (Igor packages whatever is in the project, so a stale copy ships silently), copies `assets\textures` next to the built game, and writes the build plus the game's own stdout to `build\igor.log`. The line that proves the extension came up is:

```
[GM3Dext] Engine initialized successfully! Version: 1
...
Entering main loop.
```

### Finding 1: without the textures next to the game, the terrain looks broken

The first run rendered a **flat pale surface with faint grid lines** where the plaza should be, and boulders without their granite albedo. That is not a rendering regression:

`TextureManager::find_asset_dir` looks for `assets/textures` relative to the executable and the working directory. Inside the Runner the executable is the runtime's `Runner.exe`, which has no such folder, so every CC0 lookup fails and the engine quietly falls back to the procedural textures.

The proof came from `GM3D_DEBUG_BATCHES`, which dumps what the engine resolved for each draw call:

| Build | Terrain batch line |
|:---|:---|
| Studio (assets found) | `mesh=5 mat=0 tex=5 albedo=5 preset=-5 pad0=0.4 rough=0.85` |
| Runner (assets missing) | `mesh=5 mat=0 tex=5 albedo=5 preset=-5 pad0=0 rough=0.85` |

`pad0` is the detail-blend toggle from section 39: 0.4 means the four CC0 sets are bound, 0 means the shader fell back to the procedural grain. The pale plaza was the baked macro albedo with no detail on top of it.

Copying `assets\textures` into `output\3DTEST\` - which is the game's working directory, and where GameMaker also puts Included Files - restored the paved plaza and the rock albedo. `run_in_runner.ps1` now does this on every run, and a shipped game must include the folder the same way.

### Finding 2: the Runner ran on the integrated GPU

The window reported ~35 FPS at 1107x652 with `option_game_speed=60`, while the Studio hits the display refresh rate on the same scene. `nvidia-smi` showed the GTX 1060 at 0 MiB with no processes, so the game was entirely on the Intel UHD 630.

The `NvOptimusEnablement` export in `tools\studio\main.cpp` only covers the Studio: the adapter hint is read from the **host executable**, and inside GameMaker that is the runtime's `Runner.exe`, which this repository does not build. Forcing the preference for that binary took the game from ~35 FPS to the 60 FPS game-speed cap with 250-800 `fps_real` (about 1.2-4 ms per frame):

```powershell
$runner = 'C:\ProgramData\GameMakerStudio2-LTS2026\Cache\runtimes\runtime-2026.0.0.23\windows\x64\Runner.exe'
Set-ItemProperty 'HKCU:\Software\Microsoft\DirectX\UserGpuPreferences' -Name $runner -Value 'GpuPreference=2;'
```

The same thing is available as Windows Settings -> System -> Display -> Graphics -> add `Runner.exe` -> High performance. Note that this path is per runtime version, so it has to be redone after a runtime update. Keep `NvOptimusEnablement` in the Studio: it is still what gets the editor onto the discrete GPU.

### What the run did prove

- The extension loads, `gm3d_init` succeeds, and the VM game reaches its main loop with no errors in the build log.
- Terrain, the CC0 albedo sets and shadows render the same as in the editor; the paved plaza and granite boulders match the Studio screenshots.
- Jolt steps inside the game: the dynamic boulders roll and settle between screenshots, and the shared-memory transform sync back to GML works.
- 60 FPS at 1107x652 on the discrete GPU; 35 FPS on the integrated one, which is a useful floor for perf work.

### Still not covered by this run

- The demo's keyboard/mouse shortcuts could not be exercised: synthetic `SendKeys`/clicks from the automation scripts do not reach GameMaker's input state. GUI interactive testing needs a human or a real input injection driver.
- Particle systems and spawn-key effects were not triggered for the same reason; they are still only covered by the editor and the shader check.
- The exported `gm3d_*` GML API (59 registered functions) is exercised only as far as the demo calls it.

---

## 41. Splitting the editor out of one 250 KB file (2026-10-31)

`tools/studio/StudioApp.cpp` had grown to **5217 lines / 248 KB** holding the whole editor: every panel, every dialog, the terrain brush, the file handling and the frame loop. Every UI change meant scrolling through it, and there was no way to compile a panel without compiling everything. It is now 678 lines plus eleven focused translation units.

### Where things live now

| File | Lines | Contents |
|:---|:---|:---|
| `StudioApp.cpp` | 678 | Lifecycle: window creation, `initialize`, `run`, `update`, `render`, `sync_scene_to_engine`, style, undo history, `handle_message` |
| `StudioViewport.cpp` | 637 | Viewport panel, camera, picking, selection box, collider gizmos, light markers |
| `StudioMaterials.cpp` | 504 | Material editor panel and its PBR sphere preview |
| `StudioOutliner.cpp` | 494 | Scene hierarchy panel and every selection operation it drives |
| `StudioAssets.cpp` | 481 | Asset browser, import, relink, drag-and-drop, auto-relink |
| `StudioEnvironment.cpp` | 395 | Environment panel (Terrain / Lighting tabs and the asset tab host) |
| `StudioInspector.cpp` | 379 | Object properties panel |
| `StudioSceneFiles.cpp` | 365 | Open/save/new scene and project, export, PFM dialogs |
| `StudioTerrain.cpp` | 273 | Terrain rebuild, layer sync, sculpt/paint brush |
| `StudioAutomation.cpp` | 244 | Benchmarks, grass demo, screenshot capture, frame profiler |
| `StudioChrome.cpp` | 215 | Menu bar, dockspace layout, status bar |
| `StudioSimulation.cpp` | 179 | Jolt preview: body creation, stop, resting-state report |
| `StudioShared.h` | - | `HelpTooltip`, `DrawVec3Control`, `to_utf8`, `path_utf8`, quaternion/matrix helpers |

`StudioShared.h` exists because those helpers used to sit in an anonymous namespace inside the one file. They are now `inline` functions in `GM3D::Studio`, so the ~400 call sites did not change.

### How it was done without changing behaviour

The bodies were *moved*, never edited:

1. A throwaway script parses top-level definitions (signature line to its matching closing brace at column 0) and writes them into a new file with a fixed include preamble. It never touches the text inside a body.
2. One build after each tranche.
3. Pixel comparison against the pre-split build: a `git worktree` of the previous commit was built separately, both binaries rendered the default scene and `tests/fixtures/meshcollider.json` at 1600x900 for 30 frames, and the PNGs were diffed with the `fps` readout masked (that is the only region that differs between two runs of the *same* build).

Both tranches came out **bit-identical**: 0 changed pixels outside the fps box on both scenes. That is the check worth repeating after any follow-up move: it catches a dropped `ImGui::Begin`/`End` pair, a reordered draw call or a lost return value, which a compile cannot.

### Conventions to keep it this way

- A new panel or dialog goes into the file that matches its subject, not back into `StudioApp.cpp`. Adding a file means adding it to `STUDIO_SOURCES` in `CMakeLists.txt`.
- Anything shared between panels goes into `StudioShared.h` (UI helpers) or into the object's own header (`SceneData.h`, `ProjectData.h`), never into the `.cpp`.
- `StudioApp.h` keeps the class declaration; member functions can live in any of the translation units because they are all `StudioApp::` methods.
- The include preamble of each new file is deliberately generous (ImGui, ImGuizmo, engine headers, Win32 dialog headers); trimming it is not worth the build breakage.

### Build wrinkle worth remembering

After a CMake edits, `build.bat` can fail with `ninja: error: manifest 'build.ninja' still dirty after 100 tries`. That means the build directory carries a cache from a different generator (in this session a stray `cmake -B build` had reconfigured it as "Visual Studio 18 2026" while `build.ninja` was left over from Ninja). The fix is to delete `build/` and let `tools\check.ps1` reconfigure with Ninja - do not try to patch the timestamps.
