// Terrain rebuild, layer sync and the sculpt/paint brush.
//
// Split out of StudioApp.cpp; the bodies are unchanged.
#include "StudioApp.h"
#include "StudioShared.h"
#include "SceneSerializer.h"
#include "ViewportMath.h"
#include "Heightmap.h"
#include "core/EngineContext.h"
#include "render/RenderWorld.h"
#include "physics/PhysicsWorld.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ImGuizmo.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iostream>

namespace GM3D::Studio {

void StudioApp::refresh_terrain_albedo() {
    if (!m_terrain_albedo_dirty) return;
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    if (rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(),
        m_terrain_albedo_bounds[0], m_terrain_albedo_bounds[1], m_terrain_albedo_bounds[2], m_terrain_albedo_bounds[3])) {
        m_terrain_albedo_dirty = false;
        m_terrain_albedo_elapsed = 0.0f;
    }
}

void StudioApp::update_terrain_brush(float min_x, float min_y, float width, float height) {
    auto* rw = EngineContext::instance().get_render_world();
    auto* pw = EngineContext::instance().get_physics_world();
    if (!rw || !pw) return;
    auto& terrain = rw->get_terrain_system();
    const bool active = m_terrain_brush_enabled && m_scene.has_terrain && !m_simulating && terrain.is_generated();
    if (m_terrain_albedo_dirty) {
        m_terrain_albedo_elapsed += m_delta_time;
        const float interval = (m_terrain_brush_mode == TerrainBrushMode::Paint) ? 0.06f : 0.2f;
        if (m_terrain_albedo_elapsed >= interval) refresh_terrain_albedo();
    }
    if (m_terrain_stroke && (!active || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        refresh_terrain_albedo();
        if (m_terrain_brush_mode != TerrainBrushMode::Paint && m_terrain_brush_mode != TerrainBrushMode::Grass) {
            terrain.update_collider(*pw);
            rw->get_grass_system().snap_to_terrain(terrain, rw->get_device());
        }
        m_terrain_stroke = false;
        m_terrain_has_previous_hit = false;
    }
    if (!active || width <= 0 || height <= 0 || !m_viewport_srv) {
        m_terrain_has_previous_hit = false; return;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    if (!m_viewport_hovered || mouse.x < min_x || mouse.x >= min_x + width || mouse.y < min_y || mouse.y >= min_y + height || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_terrain_has_previous_hit = false; return;
    }

    float ray[3];
    if (!viewport_ray(m_cam_pos, m_cam_target, mouse.x - min_x, mouse.y - min_y, width, height, ray)) {
        m_terrain_has_previous_hit = false; return;
    }
    const float dx = ray[0], dy = ray[1], dz = ray[2];
    const auto& cfg = terrain.get_config();
    const float half = cfg.world_size * 0.5f;
    const float step = std::max(0.25f, std::min(2.0f, cfg.world_size / (cfg.grid_size - 1) * 0.5f));
    bool previous_valid = false, hit = false;
    float previous_t = 0, previous_d = 0, hit_x = 0, hit_z = 0;
    for (float t = 0.1f; t <= 800.0f; t += step) {
        float x = m_cam_pos[0] + dx * t, z = m_cam_pos[2] + dz * t;
        if (x < -half || x > half || z < -half || z > half) { previous_valid = false; continue; }
        float d = m_cam_pos[1] + dy * t - terrain.get_height(x, z);
        if (previous_valid && previous_d >= 0 && d <= 0) {
            float lo = previous_t, hi = t;
            for (int i = 0; i < 10; ++i) {
                float mid = (lo + hi) * 0.5f;
                float mx = m_cam_pos[0] + dx * mid, mz = m_cam_pos[2] + dz * mid;
                if (m_cam_pos[1] + dy * mid > terrain.get_height(mx, mz)) lo = mid; else hi = mid;
            }
            hit_x = m_cam_pos[0] + dx * hi; hit_z = m_cam_pos[2] + dz * hi; hit = true; break;
        }
        previous_valid = true; previous_t = t; previous_d = d;
    }
    if (!hit) { m_terrain_has_previous_hit = false; return; }

    float view[16], proj[16];
    ComputeLookAtLH(view, m_cam_pos[0], m_cam_pos[1], m_cam_pos[2], m_cam_target[0], m_cam_target[1], m_cam_target[2], 0, 1, 0);
    ComputePerspectiveLH(proj, 60.0f, width / height, 0.1f, 800.0f);
    auto project = [&](float x, float y, float z, ImVec2& out) {
        float vx = x * view[0] + y * view[4] + z * view[8] + view[12];
        float vy = x * view[1] + y * view[5] + z * view[9] + view[13];
        float vz = x * view[2] + y * view[6] + z * view[10] + view[14];
        float w = vz * proj[11];
        if (w <= 0.1f) return false;
        out = ImVec2(min_x + (vx * proj[0] / w * 0.5f + 0.5f) * width,
                     min_y + (0.5f - vy * proj[5] / w * 0.5f) * height);
        return true;
    };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(min_x, min_y), ImVec2(min_x + width, min_y + height), true);
    ImU32 ring_col = IM_COL32(255, 210, 60, 255);
    if (m_terrain_brush_mode == TerrainBrushMode::Paint) {
        ring_col = (m_terrain_paint_layer < 0) ? IM_COL32(255, 130, 80, 255) : IM_COL32(80, 235, 165, 255);
    } else if (m_terrain_brush_mode == TerrainBrushMode::Grass) {
        const bool is_erasing = ImGui::IsKeyDown(ImGuiKey_LeftShift) || m_grass_erase_mode;
        ring_col = is_erasing ? IM_COL32(255, 80, 60, 255) : IM_COL32(50, 240, 120, 255);
    }
    ImVec2 last; bool have_last = false;
    for (int i = 0; i <= 64; ++i) {
        const float angle = 6.283185307f * i / 64.0f;
        float x = hit_x + cosf(angle) * m_terrain_brush_radius, z = hit_z + sinf(angle) * m_terrain_brush_radius;
        ImVec2 point;
        bool valid = x >= -half && x <= half && z >= -half && z <= half && project(x, terrain.get_height(x, z) + 0.12f, z, point);
        if (valid && have_last) draw->AddLine(last, point, ring_col, 2.0f);
        last = point; have_last = valid;
    }
    draw->PopClipRect();

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!m_terrain_stroke) {
            if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
            m_terrain_flatten_height = terrain.get_height(hit_x, hit_z);
            m_terrain_stroke = true;
        }
        const float from_x = m_terrain_has_previous_hit ? m_terrain_previous_hit[0] : hit_x;
        const float from_z = m_terrain_has_previous_hit ? m_terrain_previous_hit[1] : hit_z;
        m_terrain_previous_hit[0] = hit_x; m_terrain_previous_hit[1] = hit_z;
        m_terrain_has_previous_hit = true;
        const float left = std::min(from_x, hit_x) - m_terrain_brush_radius;
        const float top = std::min(from_z, hit_z) - m_terrain_brush_radius;
        const float right = std::max(from_x, hit_x) + m_terrain_brush_radius;
        const float bottom = std::max(from_z, hit_z) + m_terrain_brush_radius;

        if (m_terrain_brush_mode == TerrainBrushMode::Grass) {
            const bool is_erasing = ImGui::IsKeyDown(ImGuiKey_LeftShift) || m_grass_erase_mode;
            if (is_erasing) {
                rw->get_grass_system().erase_brush(hit_x, hit_z, m_terrain_brush_radius, rw->get_device());
            } else {
                rw->get_grass_system().paint_brush(hit_x, hit_z, m_terrain_brush_radius, m_grass_brush_density,
                                                   std::min(m_delta_time, 0.05f), terrain,
                                                   m_scene.grass_width, m_scene.grass_height,
                                                   m_grass_brush_tint, rw->get_device());
                m_scene.grass_enabled = true;
                rw->set_grass_enabled(true);
            }
            const auto& insts = rw->get_grass_system().get_instances();
            m_scene.grass_clumps.resize(insts.size());
            for (size_t ci = 0; ci < insts.size(); ++ci) {
                m_scene.grass_clumps[ci].x = insts[ci].px;
                m_scene.grass_clumps[ci].y = insts[ci].py;
                m_scene.grass_clumps[ci].z = insts[ci].pz;
                m_scene.grass_clumps[ci].rot = insts[ci].rotation;
                m_scene.grass_clumps[ci].sx = insts[ci].scale_x;
                m_scene.grass_clumps[ci].sy = insts[ci].scale_y;
                m_scene.grass_clumps[ci].r = insts[ci].r;
                m_scene.grass_clumps[ci].g = insts[ci].g;
                m_scene.grass_clumps[ci].b = insts[ci].b;
            }
            m_scene.grass_enabled = !m_scene.grass_clumps.empty();
        } else if (m_terrain_brush_mode == TerrainBrushMode::Paint) {
            sync_terrain_layers();
            if (apply_terrain_paint_segment(terrain.splatmap_mut(), cfg.grid_size, cfg.world_size, from_x, from_z, hit_x, hit_z,
                                            m_terrain_brush_radius, m_terrain_brush_strength,
                                            std::min(m_delta_time, 0.05f), m_terrain_paint_layer)) {
                if (!m_terrain_albedo_dirty) {
                    m_terrain_albedo_bounds[0] = left; m_terrain_albedo_bounds[1] = top;
                    m_terrain_albedo_bounds[2] = right; m_terrain_albedo_bounds[3] = bottom;
                    m_terrain_albedo_dirty = true;
                } else {
                    m_terrain_albedo_bounds[0] = std::min(m_terrain_albedo_bounds[0], left);
                    m_terrain_albedo_bounds[1] = std::min(m_terrain_albedo_bounds[1], top);
                    m_terrain_albedo_bounds[2] = std::max(m_terrain_albedo_bounds[2], right);
                    m_terrain_albedo_bounds[3] = std::max(m_terrain_albedo_bounds[3], bottom);
                }
                m_scene.terrain_splatmap = terrain.splatmap();
            }
        } else {
            auto& heights = terrain.heights_mut();
            if (apply_terrain_brush_segment(heights, cfg.grid_size, cfg.world_size, from_x, from_z, hit_x, hit_z,
                                            m_terrain_brush_radius, m_terrain_brush_strength,
                                            std::min(m_delta_time, 0.05f), m_terrain_brush_mode, m_terrain_flatten_height)) {
                rw->update_terrain_mesh_region(left, top, right, bottom);
                if (!m_terrain_albedo_dirty) {
                    m_terrain_albedo_bounds[0] = left; m_terrain_albedo_bounds[1] = top;
                    m_terrain_albedo_bounds[2] = right; m_terrain_albedo_bounds[3] = bottom;
                    m_terrain_albedo_dirty = true;
                } else {
                    m_terrain_albedo_bounds[0] = std::min(m_terrain_albedo_bounds[0], left);
                    m_terrain_albedo_bounds[1] = std::min(m_terrain_albedo_bounds[1], top);
                    m_terrain_albedo_bounds[2] = std::max(m_terrain_albedo_bounds[2], right);
                    m_terrain_albedo_bounds[3] = std::max(m_terrain_albedo_bounds[3], bottom);
                }
                m_scene.terrain_heights = heights;
            }
        }
    }
}

void StudioApp::sync_terrain_layers() {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    if (m_scene.terrain_layers.size() < 4) m_scene.init_default_terrain_layers();
    std::vector<TerrainLayer> gpu_layers;
    gpu_layers.reserve(m_scene.terrain_layers.size());
    for (const auto& l : m_scene.terrain_layers) {
        TerrainLayer gl{};
        gl.color[0] = l.color[0];
        gl.color[1] = l.color[1];
        gl.color[2] = l.color[2];
        gl.roughness = l.roughness;
        gl.scale = l.scale;
        gpu_layers.push_back(gl);
    }
    rw->get_terrain_system().set_layers(gpu_layers);
}

void StudioApp::rebuild_terrain() {
    m_terrain_albedo_dirty = false;
    m_terrain_albedo_elapsed = 0.0f;
    auto& ctx = EngineContext::instance();
    auto* rw = ctx.get_render_world();
    auto* pw = ctx.get_physics_world();
    if (!rw || !pw) return;
    m_terrain_stroke = false;
    m_terrain_has_previous_hit = false;
    m_terrain_grid_input = static_cast<int>(m_scene.terrain_grid_size);
    m_terrain_seed_input = static_cast<int>(m_scene.terrain_seed);
    m_terrain_height_input = m_scene.terrain_height_scale;
    m_terrain_world_input = m_scene.terrain_world_size;
    if (m_scene.has_terrain) {
        sync_terrain_layers();
        rw->get_terrain_system().set_splatmap(m_scene.terrain_splatmap);
        rw->generate_terrain(m_scene.terrain_grid_size, m_scene.terrain_world_size,
            m_scene.terrain_height_scale, m_scene.terrain_seed, *pw,
            m_scene.terrain_heights.empty() ? nullptr : &m_scene.terrain_heights);
        GM3D::GrassConfig gcfg = rw->get_grass_system().get_config();
        gcfg.enabled = m_scene.grass_enabled;
        gcfg.density = m_scene.grass_density;
        gcfg.base_height = m_scene.grass_height;
        gcfg.base_width = m_scene.grass_width;
        gcfg.max_distance = m_scene.grass_max_distance;
        gcfg.fade_range = m_scene.grass_fade_range;
        gcfg.wind_speed = m_scene.grass_wind_speed;
        gcfg.wind_strength = m_scene.grass_wind_strength;
        gcfg.wind_dir[0] = m_scene.grass_wind_dir[0];
        gcfg.wind_dir[1] = m_scene.grass_wind_dir[1];
        for (int c = 0; c < 3; ++c) {
            gcfg.color_a[c] = m_scene.grass_color_a[c];
            gcfg.color_b[c] = m_scene.grass_color_b[c];
        }
        rw->get_grass_system().set_config(gcfg);

        if (m_scene.grass_enabled && !m_scene.grass_clumps.empty()) {
            std::vector<GM3D::GrassGPUInstance> insts(m_scene.grass_clumps.size());
            for (size_t ci = 0; ci < m_scene.grass_clumps.size(); ++ci) {
                const auto& c = m_scene.grass_clumps[ci];
                insts[ci].px = c.x; insts[ci].py = c.y; insts[ci].pz = c.z;
                insts[ci].rotation = c.rot;
                insts[ci].scale_x = c.sx; insts[ci].scale_y = c.sy;
                insts[ci].wind_phase = (c.x * 0.13f + c.z * 0.17f);
                insts[ci].r = c.r; insts[ci].g = c.g; insts[ci].b = c.b;
                insts[ci].pad[0] = 0.0f; insts[ci].pad[1] = 0.0f;
            }
            rw->get_grass_system().set_instances(insts, rw->get_device());
            rw->get_grass_system().snap_to_terrain(rw->get_terrain_system(), rw->get_device());
            rw->set_grass_enabled(true);
        } else if (m_scene.grass_enabled) {
            // No painted clumps: fall back to the procedural carpet described by the scene's
            // grass_* fields. This is what keeps a generated meadow scene a few kilobytes of
            // JSON instead of a hundred thousand baked instances.
            rw->get_grass_system().build_from_terrain(rw->get_device(), rw->get_terrain_system(), gcfg);
            rw->set_grass_enabled(true);
        } else {
            rw->get_grass_system().clear();
            rw->set_grass_enabled(false);
        }

        // The Settings panel's "Show grass" toggle is a preview switch: terrain rebuilds would
        // otherwise bring the carpet back.
        if (!m_graphics.show_grass) rw->set_grass_enabled(false);
    } else {
        rw->get_terrain_system().clear(*pw);
        rw->get_grass_system().shutdown();
    }
}

} // namespace GM3D::Studio
