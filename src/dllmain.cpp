#include "core/GMExport.h"
#include "core/EngineContext.h"
#include "core/SharedMemory.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        EngineContext::instance().shutdown();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
#endif

/*
 ==============================================================================================
                            EXPORTED FUNCTIONS FOR GAMEMAKER
 ==============================================================================================
 GameMaker C ABI Integration Notes:
 - Calling Convention: __cdecl (GM_CALL) with C linkage (extern "C").
 - Arguments: in GameMaker, floating point & integer numbers are passed as `double` (f64).
   Pointer to byte buffer is passed from GML: `buffer_get_address(buff)` as type `ty_string` / `Pointer`.
   Window handle is passed from GML: `window_handle()` as type `ty_string` / `Pointer`.
 - Return value: `double` (status code or result value).
 ==============================================================================================
*/

/**
 * @brief Extension initialization.
 * @param buffer_ptr Pointer to GameMaker raw byte buffer (obtained via buffer_get_address).
 * @param buffer_size Size of the buffer in bytes (obtained via buffer_get_size).
 * @param window_handle GameMaker window handle (obtained via window_handle()).
 */
GM_EXPORT double GM_CALL init_extension(void* buffer_ptr, double buffer_size, void* window_handle) {
    try {
        return EngineContext::instance().initialize(buffer_ptr, buffer_size, window_handle);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

/**
 * @brief Release resources and shutdown engine.
 */
GM_EXPORT double GM_CALL shutdown_extension() {
    try {
        return EngineContext::instance().shutdown();
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

/**
 * @brief Framework main tick: called every frame (in Step Event or Draw Event).
 * @param delta_time Time elapsed between frames in seconds (e.g. delta_time / 1000000.0 from GML).
 */
GM_EXPORT double GM_CALL update_framework(double delta_time) {
    try {
        return EngineContext::instance().update(delta_time);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

/**
 * @brief Create rigid body in physics simulation (Jolt Physics).
 * @return Handle ID of created body for linking to entity in shared buffer.
 */
GM_EXPORT double GM_CALL create_physics_body(double shape, double motion, double mass, 
                                            double px, double py, double pz, 
                                            double sx, double sy, double sz) {
    try {
        return EngineContext::instance().create_rigid_body(shape, motion, mass, px, py, pz, sx, sy, sz);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

/**
 * @brief Destroy rigid body by ID.
 */
GM_EXPORT double GM_CALL destroy_physics_body(double body_id) {
    try {
        return EngineContext::instance().destroy_rigid_body(body_id);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_add_impulse(double body_id, double ix, double iy, double iz) {
    try {
        return EngineContext::instance().add_impulse(body_id, ix, iy, iz);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_add_torque(double body_id, double tx, double ty, double tz) {
    try {
        return EngineContext::instance().add_torque(body_id, tx, ty, tz);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_add_force(double body_id, double fx, double fy, double fz) {
    try {
        return EngineContext::instance().add_force(body_id, fx, fy, fz);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_set_velocity(double body_id, double vx, double vy, double vz) {
    try {
        return EngineContext::instance().set_velocity(body_id, vx, vy, vz);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_set_gravity(double gx, double gy, double gz) {
    try {
        return EngineContext::instance().set_gravity(gx, gy, gz);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_set_friction(double body_id, double friction) {
    try {
        return EngineContext::instance().set_friction(body_id, friction);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL physics_set_restitution(double body_id, double restitution) {
    try {
        return EngineContext::instance().set_restitution(body_id, restitution);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL load_mesh_obj(const char* filepath) {
    try {
        return EngineContext::instance().load_mesh_obj(filepath);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL load_mesh_glb(const char* filepath) {
    try {
        return EngineContext::instance().load_mesh_glb(filepath);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL load_mesh_model(const char* filepath) {
    try {
        return EngineContext::instance().load_mesh_model(filepath);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL physics_raycast(double fx, double fy, double fz, double tx, double ty, double tz) {
    try {
        return EngineContext::instance().raycast(fx, fy, fz, tx, ty, tz);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL physics_get_raycast_x() {
    return EngineContext::instance().get_raycast_x();
}

GM_EXPORT double GM_CALL physics_get_raycast_y() {
    return EngineContext::instance().get_raycast_y();
}

GM_EXPORT double GM_CALL physics_get_raycast_z() {
    return EngineContext::instance().get_raycast_z();
}

/**
 * @brief Get protocol version / DLL build version.
 */
GM_EXPORT double GM_CALL get_framework_version() {
    return static_cast<double>(GM3D_PROTOCOL_VERSION);
}

// ------------------------------------------------------------------------------------------------
// Skeletal Animation Exports
// ------------------------------------------------------------------------------------------------

static thread_local std::string s_anim_name_buf;

GM_EXPORT double GM_CALL anim_play(double entity_id, double anim_index, double speed, double loop) {
    try {
        return EngineContext::instance().anim_play(entity_id, anim_index, speed, loop);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL anim_stop(double entity_id) {
    try {
        return EngineContext::instance().anim_stop(entity_id);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL anim_set_time(double entity_id, double time) {
    try {
        return EngineContext::instance().anim_set_time(entity_id, time);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL anim_get_count(double mesh_id) {
    try {
        return EngineContext::instance().anim_get_count(mesh_id);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT const char* GM_CALL anim_get_name(double mesh_id, double anim_index) {
    try {
        s_anim_name_buf = EngineContext::instance().anim_get_name(mesh_id, anim_index);
        return s_anim_name_buf.c_str();
    } catch (...) {
        return "";
    }
}

GM_EXPORT double GM_CALL anim_get_duration(double mesh_id, double anim_index) {
    try {
        return EngineContext::instance().anim_get_duration(mesh_id, anim_index);
    } catch (...) {
        return 0.0;
    }
}

// ------------------------------------------------------------------------------------------------
// Texture Management Exports
// ------------------------------------------------------------------------------------------------

GM_EXPORT double GM_CALL texture_load(const char* filepath) {
    try {
        return EngineContext::instance().load_texture(filepath);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL texture_set_normal_map(double target_tex_id, double normal_tex_id) {
    try {
        return EngineContext::instance().set_normal_map(target_tex_id, normal_tex_id);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

// ------------------------------------------------------------------------------------------------
// Dynamic Lighting Controls (Sun & Point Lights)
// ------------------------------------------------------------------------------------------------

GM_EXPORT double GM_CALL light_set_sun(double dx, double dy, double dz, double r, double g, double b, double intensity) {
    try {
        return EngineContext::instance().set_sun_light(dx, dy, dz, r, g, b, intensity);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL light_set_ambient(double r, double g, double b, double intensity) {
    try {
        return EngineContext::instance().set_ambient_light(r, g, b, intensity);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL light_point_set(double index, double x, double y, double z, double radius, double r, double g, double b, double intensity) {
    try {
        return EngineContext::instance().set_point_light(index, x, y, z, radius, r, g, b, intensity);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL light_point_clear() {
    try {
        return EngineContext::instance().clear_point_lights();
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

// ------------------------------------------------------------------------------------------------
// Physics Transform & Atmosphere Exports
// ------------------------------------------------------------------------------------------------

GM_EXPORT double GM_CALL physics_set_transform(double body_id, double x, double y, double z, double qx, double qy, double qz, double qw) {
    try {
        return EngineContext::instance().set_body_transform(body_id, x, y, z, qx, qy, qz, qw);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL render_set_sky_color(double r, double g, double b) {
    try {
        return EngineContext::instance().set_sky_color(r, g, b);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL render_set_fog(double start, double end, double r, double g, double b) {
    try {
        return EngineContext::instance().set_fog(start, end, r, g, b);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

// ------------------------------------------------------------------------------------------------
// Native Windows F5 Interactive Menu
// ------------------------------------------------------------------------------------------------

GM_EXPORT double GM_CALL show_f5_menu(double current_mode, double invert_y) {
    try {
        HWND hwnd = static_cast<HWND>(EngineContext::instance().get_window_handle());
        if (!hwnd || !IsWindow(hwnd)) {
            hwnd = GetActiveWindow();
        }
        if (!hwnd || !IsWindow(hwnd)) {
            hwnd = GetForegroundWindow();
        }

        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return -1.0;

        int cur_m = static_cast<int>(current_mode);
        bool inv_y = (static_cast<int>(invert_y) != 0);

        AppendMenuW(hMenu, MF_STRING | (cur_m == 0 ? MF_CHECKED : 0), 101, L"[1] Ocean Island (Playable Character)");
        AppendMenuW(hMenu, MF_STRING | (cur_m == 1 ? MF_CHECKED : 0), 102, L"[2] 3D Scene Editor (Gizmo, Transform, Spawn)");
        AppendMenuW(hMenu, MF_STRING | (cur_m == 2 ? MF_CHECKED : 0), 103, L"[3] Jolt Physics Sandbox (Pyramid, Impulses)");
        AppendMenuW(hMenu, MF_STRING | (cur_m == 3 ? MF_CHECKED : 0), 104, L"[4] Lighting & Fog Studio (Orbiting Lights)");
        AppendMenuW(hMenu, MF_STRING | (cur_m == 4 ? MF_CHECKED : 0), 105, L"[5] Material Showcase (PBR & Normal Maps)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING | (inv_y ? MF_CHECKED : 0), 110, inv_y ? L"Invert Mouse Y-Axis: [ENABLED] (Press Y)" : L"Invert Mouse Y-Axis: [DISABLED] (Press Y)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        if (cur_m == 1) {
            AppendMenuW(hMenu, MF_STRING, 130, L"Editor: Select Next Entity (Tab)");
            AppendMenuW(hMenu, MF_STRING, 131, L"Editor: Move +X (Key L)");
            AppendMenuW(hMenu, MF_STRING, 132, L"Editor: Move -X (Key J)");
            AppendMenuW(hMenu, MF_STRING, 133, L"Editor: Move +Y (Key O)");
            AppendMenuW(hMenu, MF_STRING, 134, L"Editor: Move -Y (Key U)");
            AppendMenuW(hMenu, MF_STRING, 135, L"Editor: Move +Z (Key I)");
            AppendMenuW(hMenu, MF_STRING, 136, L"Editor: Move -Z (Key K)");
            AppendMenuW(hMenu, MF_STRING, 137, L"Editor: Scale Up (Key +)");
            AppendMenuW(hMenu, MF_STRING, 138, L"Editor: Scale Down (Key -)");
            AppendMenuW(hMenu, MF_STRING, 139, L"Editor: Duplicate Object (Ctrl+D)");
            AppendMenuW(hMenu, MF_STRING, 140, L"Editor: Delete Object (Del)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        }

        AppendMenuW(hMenu, MF_STRING, 120, L"Physics Impulse: Radial Shockwave");
        AppendMenuW(hMenu, MF_STRING, 121, L"Toggle Gravity (Earth / Zero-G / Moon)");
        AppendMenuW(hMenu, MF_STRING, 122, L"Spawn Dynamic Cube (Textured)");
        AppendMenuW(hMenu, MF_STRING, 123, L"Spawn Dynamic Sphere (Grid)");
        AppendMenuW(hMenu, MF_STRING, 124, L"Spawn Dancing Mannequin");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, 150, L"Magic Spell: Cast Fire Blast (Key Q)");
        AppendMenuW(hMenu, MF_STRING, 151, L"Magic Spell: Cast Arcane Nova (Key E)");
        AppendMenuW(hMenu, MF_STRING, 152, L"Magic Spell: Cast Holy Radiance (Key R)");
        AppendMenuW(hMenu, MF_STRING, 153, L"Magic Spell: Cast Frost Blizzard (Key F)");
        AppendMenuW(hMenu, MF_STRING, 160, L"Visuals: Toggle HDR Bloom Glow (Key B)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, 999, L"Close Menu (Esc)");

        POINT pt;
        GetCursorPos(&pt);
        if (hwnd) SetForegroundWindow(hwnd);

        int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);

        switch (cmd) {
            case 101: return 0.0; // MODE_ISLAND
            case 102: return 1.0; // MODE_EDITOR
            case 103: return 2.0; // MODE_PHYSICS
            case 104: return 3.0; // MODE_LIGHTING
            case 105: return 4.0; // MODE_MATERIAL
            case 110: return 10.0; // Invert Y toggle
            case 120: return 20.0; // Explosion
            case 121: return 21.0; // Gravity toggle
            case 122: return 22.0; // Spawn cube
            case 123: return 23.0; // Spawn sphere
            case 124: return 24.0; // Spawn mannequin
            case 130: return 30.0; // Select Next Entity
            case 131: return 31.0; // Move +X
            case 132: return 32.0; // Move -X
            case 133: return 33.0; // Move +Y
            case 134: return 34.0; // Move -Y
            case 135: return 35.0; // Move +Z
            case 136: return 36.0; // Move -Z
            case 137: return 37.0; // Scale Up
            case 138: return 38.0; // Scale Down
            case 139: return 39.0; // Duplicate
            case 140: return 40.0; // Delete
            case 150: return 50.0; // Fire Blast
            case 151: return 51.0; // Arcane Nova
            case 152: return 52.0; // Holy Radiance
            case 153: return 53.0; // Frost Blizzard
            case 160: return 60.0; // Bloom toggle
            default:  return -1.0; // Cancel
        }
    } catch (...) {
        return -1.0;
    }
}

GM_EXPORT double GM_CALL set_gizmo(double enabled, double x, double y, double z, double sx, double sy, double sz, double axis_len) {
    try {
        return EngineContext::instance().set_gizmo(enabled, x, y, z, sx, sy, sz, axis_len);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL render_set_bloom(double enabled, double intensity, double threshold) {
    try {
        return EngineContext::instance().set_bloom(enabled, intensity, threshold);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL particle_spawn_burst(double type, double x, double y, double z, double count, double speed, double size, double r, double g, double b) {
    try {
        return EngineContext::instance().particle_spawn_burst(type, x, y, z, count, speed, size, r, g, b);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL particle_spawn_ring(double type, double x, double y, double z, double radius, double count, double r, double g, double b) {
    try {
        return EngineContext::instance().particle_spawn_ring(type, x, y, z, radius, count, r, g, b);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL particle_clear() {
    try {
        return EngineContext::instance().particle_clear();
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL terrain_generate(double grid_size, double world_size, double height_scale, double seed) {
    try {
        return EngineContext::instance().terrain_generate(grid_size, world_size, height_scale, seed);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL terrain_get_height(double x, double z) {
    try {
        return EngineContext::instance().terrain_get_height(x, z);
    } catch (...) {
        return 0.0;
    }
}

GM_EXPORT double GM_CALL material_set_pbr(double mat_id, double r, double g, double b, double a, double roughness, double metallic, double emissive) {
    try {
        return EngineContext::instance().material_set_pbr(mat_id, r, g, b, a, roughness, metallic, emissive);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL material_set_textures(double mat_id, double albedo_tex_id, double normal_tex_id) {
    try {
        return EngineContext::instance().material_set_textures(mat_id, albedo_tex_id, normal_tex_id);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL material_set_uv(double mat_id, double tiling_u, double tiling_v, double offset_u, double offset_v) {
    try {
        return EngineContext::instance().material_set_uv(mat_id, tiling_u, tiling_v, offset_u, offset_v);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL material_set_alpha(double mat_id, double alpha_mode, double alpha_cutoff) {
    try {
        return EngineContext::instance().material_set_alpha(mat_id, alpha_mode, alpha_cutoff);
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}

GM_EXPORT double GM_CALL material_clear_all() {
    try {
        return EngineContext::instance().material_clear_all();
    } catch (...) {
        return GMResult::ERR_GENERAL;
    }
}
