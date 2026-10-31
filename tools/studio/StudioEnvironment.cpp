// Environment panel: terrain, lighting and the tab host for the asset browser.
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
#include "ImGuizmo.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace GM3D::Studio {

void StudioApp::draw_environment_panel() {
    ImGui::Begin("Environment & Lighting", &m_show_environment);
    if (ImGui::BeginTabBar("WorldTools")) {
        const auto terrain_flags = m_select_terrain_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Terrain", nullptr, terrain_flags)) {
            m_select_terrain_tab = false;
            ImGui::TextUnformatted("TERRAIN SCULPT & PAINT");
            ImGui::TextDisabled("Shape and paint your landscape in the viewport");
            ImGui::Spacing();
            ImGui::BeginDisabled(m_simulating);
            if (ImGui::Checkbox("Enable terrain", &m_scene.has_terrain)) rebuild_terrain();
            HelpTooltip("Show or hide terrain and its physics collider. Sculpting is disabled during simulation.");
            ImGui::EndDisabled();
            if (m_simulating) ImGui::TextWrapped("Stop simulation to edit terrain.");
            if (m_scene.has_terrain) {
                if (m_scene.terrain_layers.size() < 4) m_scene.init_default_terrain_layers();
                ImGui::Spacing();
                ImGui::BeginDisabled(m_simulating);
                const bool brush_was_enabled = m_terrain_brush_enabled;
                if (brush_was_enabled) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.68f, 1));
                if (ImGui::Button(brush_was_enabled ? "Brush active (LMB in Viewport)" : "Enable terrain brush", ImVec2(-1, 34)))
                    m_terrain_brush_enabled = !m_terrain_brush_enabled;
                if (brush_was_enabled) ImGui::PopStyleColor();
                HelpTooltip("Use the left mouse button in the viewport to sculpt or paint. Turn this off to select objects.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Brush mode");
                const char* names[] = { "Raise", "Lower", "Smooth", "Flatten", "Paint Layer", "Grass Brush" };
                const float half_width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
                for (int i = 0; i < 6; ++i) {
                    if (i % 2) ImGui::SameLine();
                    const bool selected = static_cast<int>(m_terrain_brush_mode) == i;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.68f, 1));
                    if (ImGui::Button(names[i], ImVec2(half_width, 32))) {
                        m_terrain_brush_mode = static_cast<TerrainBrushMode>(i);
                        m_terrain_brush_enabled = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                    const char* help[] = {
                        "Raise terrain under the brush.",
                        "Lower terrain under the brush.",
                        "Smooth nearby height differences.",
                        "Move heights toward the height sampled at the start of the stroke.",
                        "Paint terrain material layers onto the landscape.",
                        "Paint or erase procedural 3D grass foliage directly on the terrain (Unity style)."
                    };
                    HelpTooltip(help[i]);
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("Radius");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##BrushRadius", &m_terrain_brush_radius, 1.0f, 50.0f, "%.1f m");
                HelpTooltip("Radius of the brush in world units.");
                ImGui::TextUnformatted("Strength");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##BrushStrength", &m_terrain_brush_strength, 0.1f, 20.0f, "%.1f");
                HelpTooltip("Controls how quickly the brush changes terrain.");

                if (m_terrain_brush_mode == TerrainBrushMode::Grass) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.24f, 0.94f, 0.47f, 1.0f), "[Grass Brush Mode Active]");
                    ImGui::Checkbox("Erase Grass (or hold Shift)", &m_grass_erase_mode);
                    HelpTooltip("When active (or holding Shift), dragging in the viewport removes grass clumps.");

                    ImGui::TextUnformatted("Clump Density Flow");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##GrassDensityFlow", &m_grass_brush_density, 1.0f, 60.0f, "%.0f clumps/s");
                    HelpTooltip("Rate at which grass clumps sprout under the brush.");

                    ImGui::TextUnformatted("Grass Tint");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::ColorEdit3("##GrassTint", m_grass_brush_tint);

                    auto* rw_clumps = EngineContext::instance().get_render_world();
                    uint32_t live_clumps = rw_clumps ? rw_clumps->get_grass_system().get_instance_count() : 0;
                    ImGui::TextDisabled("Painted clumps: %u", live_clumps);

                    if (ImGui::Button("Clear All Grass", ImVec2(-1, 26))) {
                        if (rw_clumps) rw_clumps->clear_grass();
                        m_scene.grass_clumps.clear();
                        m_scene.grass_enabled = false;
                    }
                    HelpTooltip("Remove all painted grass from the scene.");
                    ImGui::Spacing();
                }
                ImGui::TextWrapped("Drag on the terrain to sculpt or paint. Flatten uses the height at the start of the stroke.");
                ImGui::BeginDisabled(!m_history.can_undo() || m_terrain_stroke);
                if (ImGui::Button("Undo   Ctrl+Z", ImVec2(-1, 30))) apply_history(false);
                HelpTooltip("Undo the most recent scene change. Redo with Ctrl+Y or Ctrl+Shift+Z.");
                ImGui::EndDisabled();
                ImGui::Spacing();

                if (ImGui::CollapsingHeader("Material Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Select active paint layer or customize colors:");
                    bool layer_props_changed = false;
                    for (int li = 0; li < 4; ++li) {
                        ImGui::PushID(li);
                        auto& layer = m_scene.terrain_layers[li];
                        bool is_active_layer = (m_terrain_paint_layer == li && m_terrain_brush_mode == TerrainBrushMode::Paint);
                        if (is_active_layer) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.32f, 1.0f));
                        std::string paint_btn = (m_terrain_paint_layer == li ? "[Active] " : "Paint ") + layer.name + "##SelectLayer";
                        if (ImGui::Button(paint_btn.c_str(), ImVec2(-1, 26))) {
                            m_terrain_paint_layer = li;
                            m_terrain_brush_mode = TerrainBrushMode::Paint;
                            m_terrain_brush_enabled = true;
                        }
                        if (is_active_layer) ImGui::PopStyleColor();

                        ImGui::SetNextItemWidth(110.0f);
                        if (ImGui::ColorEdit3("##LayerCol", layer.color, ImGuiColorEditFlags_NoInputs)) {
                            layer_props_changed = true;
                        }
                        ImGui::SameLine();
                        char lname[64];
                        strncpy_s(lname, layer.name.c_str(), sizeof(lname) - 1);
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 75.0f);
                        if (ImGui::InputText("##LayerName", lname, sizeof(lname))) {
                            layer.name = lname;
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70.0f);
                        if (ImGui::DragFloat("##LayerScale", &layer.scale, 0.05f, 0.2f, 4.0f, "x%.1f")) {
                            layer_props_changed = true;
                        }
                        HelpTooltip("Pattern tiling scale for this terrain layer.");
                        ImGui::Spacing();
                        ImGui::PopID();
                    }

                    const bool is_erase = (m_terrain_paint_layer == -1 && m_terrain_brush_mode == TerrainBrushMode::Paint);
                    if (is_erase) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.32f, 0.18f, 1.0f));
                    if (ImGui::Button("Erase Paint (Restore Auto-Biome)", ImVec2(-1, 26))) {
                        m_terrain_paint_layer = -1;
                        m_terrain_brush_mode = TerrainBrushMode::Paint;
                        m_terrain_brush_enabled = true;
                    }
                    if (is_erase) ImGui::PopStyleColor();
                    HelpTooltip("Brush erases painted layer weights and restores procedural hypsometric biomes.");

                    if (!m_scene.terrain_splatmap.empty()) {
                        if (ImGui::Button("Clear All Painted Layers", ImVec2(-1, 24))) {
                            m_history.observe(m_scene);
                            m_scene.terrain_splatmap.clear();
                            if (auto* rw = EngineContext::instance().get_render_world()) {
                                rw->get_terrain_system().set_splatmap(m_scene.terrain_splatmap);
                                const float half = m_scene.terrain_world_size * 0.5f;
                                rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(), -half, -half, half, half);
                            }
                            m_history.observe(m_scene);
                        }
                    }

                    if (layer_props_changed) {
                        sync_terrain_layers();
                        if (!m_scene.terrain_splatmap.empty()) {
                            if (auto* rw = EngineContext::instance().get_render_world()) {
                                const float half = m_scene.terrain_world_size * 0.5f;
                                rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(), -half, -half, half, half);
                            }
                        }
                    }
                }

                if (ImGui::CollapsingHeader("Heightmap")) {
                    ImGui::TextWrapped("Import or export grayscale PFM files (32-bit heights in world units). Square maps: 32 to 512 pixels.");
                    if (ImGui::Button("Import heightmap...", ImVec2(-1, 30))) import_heightmap_dialog();
                    HelpTooltip("Replace the terrain heights and resolution. Undo with Ctrl+Z.");
                    if (ImGui::Button("Export heightmap...", ImVec2(-1, 30))) export_heightmap_dialog();
                    HelpTooltip("Save the current terrain heights, including the generated terrain, to a grayscale PFM file.");
                }
                if (ImGui::CollapsingHeader("Terrain generation")) {
                    ImGui::TextDisabled("Changes apply when regenerated.");
                    ImGui::TextUnformatted("Grid Resolution"); ImGui::SetNextItemWidth(-1);
                    const char* grid_labels[] = { "64 x 64 (Fast)", "128 x 128 (Standard)", "256 x 256 (High Detail)", "512 x 512 (Ultra Large)" };
                    const int grid_values[] = { 64, 128, 256, 512 };
                    int current_grid_idx = 1;
                    for (int gi = 0; gi < 4; ++gi) {
                        if (m_terrain_grid_input == grid_values[gi]) { current_grid_idx = gi; break; }
                    }
                    if (ImGui::Combo("##TerrainGridSize", &current_grid_idx, grid_labels, 4)) {
                        m_terrain_grid_input = grid_values[current_grid_idx];
                    }
                    ImGui::TextUnformatted("Seed"); ImGui::SetNextItemWidth(-1);
                    ImGui::InputInt("##TerrainSeed", &m_terrain_seed_input);
                    ImGui::TextUnformatted("Height scale"); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##TerrainHeight", &m_terrain_height_input, 0.5f, 4.0f, 80.0f, "%.1f m");
                    ImGui::TextUnformatted("World size"); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##TerrainSize", &m_terrain_world_input, 2.0f, 60.0f, 600.0f, "%.1f m");
                    if (ImGui::Button("Regenerate terrain...", ImVec2(-1, 30))) ImGui::OpenPopup("Regenerate terrain?");
                }
                if (ImGui::BeginPopupModal("Regenerate terrain?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Regeneration replaces the current sculpted terrain.");
                    ImGui::TextUnformatted("Save the scene first if you want to keep it.");
                    ImGui::Spacing();
                    if (ImGui::Button("Regenerate", ImVec2(130, 0))) {
                        m_scene.terrain_grid_size = static_cast<uint32_t>(std::clamp(m_terrain_grid_input, 32, 512));
                        m_scene.terrain_seed = static_cast<uint32_t>(m_terrain_seed_input);
                        m_scene.terrain_height_scale = std::clamp(m_terrain_height_input, 4.0f, 80.0f);
                        m_scene.terrain_world_size = std::clamp(m_terrain_world_input, 60.0f, 600.0f);
                        m_scene.terrain_heights.clear();
                        m_scene.terrain_splatmap.clear();
                        rebuild_terrain();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                // Procedural Grass & Foliage Controls
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Procedural Grass & Foliage", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto* rw_ui = EngineContext::instance().get_render_world();
                    uint32_t blade_count = rw_ui ? rw_ui->get_grass_system().get_instance_count() : 0;
                    ImGui::TextDisabled("Active grass clumps: %u", blade_count);

                    bool grass_toggled = ImGui::Checkbox("Enable Grass", &m_scene.grass_enabled);
                    if (grass_toggled && rw_ui) {
                        rw_ui->set_grass_enabled(m_scene.grass_enabled);
                    }
                    HelpTooltip("Toggle procedural instanced 3D grass across terrain.");

                    if (ImGui::Button("Paint Grass Tool", ImVec2(-1, 28))) {
                        m_terrain_brush_mode = TerrainBrushMode::Grass;
                        m_terrain_brush_enabled = true;
                    }
                    HelpTooltip("Activate the Unity-style grass brush to paint clumps directly in the viewport.");

                    if (ImGui::Button("Fill Terrain (Procedural)", ImVec2(-1, 28))) {
                        if (rw_ui) {
                            GM3D::GrassConfig gcfg = rw_ui->get_grass_system().get_config();
                            gcfg.enabled = true;
                            gcfg.density = m_scene.grass_density;
                            gcfg.base_height = m_scene.grass_height;
                            gcfg.base_width = m_scene.grass_width;
                            gcfg.max_distance = m_scene.grass_max_distance;
                            gcfg.wind_speed = m_scene.grass_wind_speed;
                            gcfg.wind_strength = m_scene.grass_wind_strength;
                            rw_ui->get_grass_system().build_from_terrain(rw_ui->get_device(), rw_ui->get_terrain_system(), gcfg);
                            rw_ui->set_grass_enabled(true);
                            m_scene.grass_enabled = true;

                            const auto& insts = rw_ui->get_grass_system().get_instances();
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
                        }
                    }
                    HelpTooltip("Auto-populate whole terrain using procedural splatmap distribution.");

                    if (ImGui::Button("Clear All Grass", ImVec2(-1, 26))) {
                        if (rw_ui) rw_ui->clear_grass();
                        m_scene.grass_clumps.clear();
                        m_scene.grass_enabled = false;
                    }
                    HelpTooltip("Remove all grass instances from the scene.");

                    if (ImGui::Button("Snap Grass to Terrain Heights", ImVec2(-1, 26))) {
                        if (rw_ui) {
                            rw_ui->get_grass_system().snap_to_terrain(rw_ui->get_terrain_system(), rw_ui->get_device());
                            const auto& insts = rw_ui->get_grass_system().get_instances();
                            for (size_t ci = 0; ci < insts.size() && ci < m_scene.grass_clumps.size(); ++ci) {
                                m_scene.grass_clumps[ci].y = insts[ci].py;
                            }
                        }
                    }
                    HelpTooltip("Re-clamp all grass roots to current terrain elevation.");

                    ImGui::Spacing();
                    ImGui::TextUnformatted("Height");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##GrassHeight", &m_scene.grass_height, 0.3f, 2.0f, "%.2f m")) {
                        if (rw_ui) rw_ui->get_grass_system().get_config_mut().base_height = m_scene.grass_height;
                    }

                    ImGui::TextUnformatted("Clump Width");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##GrassWidth", &m_scene.grass_width, 0.1f, 0.6f, "%.2f m")) {
                        if (rw_ui) rw_ui->get_grass_system().get_config_mut().base_width = m_scene.grass_width;
                    }

                    ImGui::TextUnformatted("Visibility Distance");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##GrassMaxDist", &m_scene.grass_max_distance, 40.0f, 180.0f, "%.0f m")) {
                        if (rw_ui) rw_ui->get_grass_system().get_config_mut().max_distance = m_scene.grass_max_distance;
                    }

                    ImGui::TextUnformatted("Wind Sway Speed");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##GrassWindSpeed", &m_scene.grass_wind_speed, 0.5f, 5.0f, "%.1fx")) {
                        if (rw_ui) rw_ui->get_grass_system().get_config_mut().wind_speed = m_scene.grass_wind_speed;
                    }

                    ImGui::TextUnformatted("Wind Strength");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat("##GrassWindStrength", &m_scene.grass_wind_strength, 0.05f, 1.0f, "%.2f")) {
                        if (rw_ui) rw_ui->get_grass_system().get_config_mut().wind_strength = m_scene.grass_wind_strength;
                    }
                }
                ImGui::EndDisabled();
            } else {
                ImGui::Spacing();
                ImGui::TextWrapped("Enable terrain to use sculpting tools.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lighting")) {
            ImGui::TextUnformatted("ENVIRONMENT");
            ImGui::TextDisabled("Light, atmosphere and fog");
            ImGui::Spacing();
            auto slider = [](const char* label, float* value, float min, float max) {
                ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
                std::string id = std::string("##") + label;
                ImGui::SliderFloat(id.c_str(), value, min, max);
            };
            auto color = [](const char* label, float* value) {
                ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
                std::string id = std::string("##") + label;
                ImGui::ColorEdit3(id.c_str(), value);
            };
            if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
                slider("Elevation", &m_scene.sun_pitch, 5, 85);
                slider("Azimuth", &m_scene.sun_yaw, 0, 360);
                color("Sun color", m_scene.sun_color);
                slider("Sun intensity", &m_scene.sun_intensity, 0.1f, 5);
            }
            if (ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                size_t count = 0;
                size_t shadowed_count = 0;
                for (const auto& entity : m_scene.entities) {
                    if (entity.is_point_light) {
                        ++count;
                        if (entity.visible && entity.cast_shadow) ++shadowed_count;
                    }
                }
                const size_t active_shadowed = std::min<size_t>(shadowed_count, static_cast<size_t>(std::clamp(m_scene.max_shadow_lights, 0, 4)));
                ImGui::TextDisabled("%zu / 16 lights (%zu / %d casting shadows)", count, active_shadowed, m_scene.max_shadow_lights);
                if (count > 16) ImGui::TextWrapped("Only the first 16 enabled lights are rendered.");
                if (ImGui::Button("+ Add point light", ImVec2(-1, 30))) add_point_light();
                ImGui::TextWrapped("Select a light marker or a light in the hierarchy to edit color, radius, and shadows.");
            }
            if (ImGui::CollapsingHeader("Shadows (Sun CSM & Point Lights)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Enable shadows", &m_scene.shadows_enabled);
                ImGui::BeginDisabled(!m_scene.shadows_enabled);
                ImGui::Checkbox("Cascaded Sun Shadows (2-Cascade CSM)", &m_scene.csm_enabled);
                HelpTooltip("Split sun shadows into a high-resolution near cascade and a wide far cascade with smooth transition.");

                ImGui::TextUnformatted("Shadow Quality (Atlas Resolution)");
                const char* q_names[] = { "Low (1024x1024)", "Medium (2048x2048)", "High (4096x4096)" };
                int q_idx = std::clamp(m_scene.shadow_quality - 1, 0, 2);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##ShadowQuality", &q_idx, q_names, IM_ARRAYSIZE(q_names))) {
                    m_scene.shadow_quality = q_idx + 1;
                }
                HelpTooltip("Controls the GPU shadow atlas resolution for both Sun CSM and Point Light 6-face shadows.");

                ImGui::TextUnformatted("Max Shadowed Point Lights");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##MaxShadowLights", &m_scene.max_shadow_lights, 0, 4, "%d lights");
                HelpTooltip("Maximum number of point lights allowed to render 6-face omnidirectional shadows simultaneously (0..4).");

                slider("Shadow strength", &m_scene.shadow_strength, 0.0f, 1.0f);
                HelpTooltip("Adjust how dark shadows appear.");
                slider("Shadow softness", &m_scene.shadow_softness, 0.5f, 6.0f);
                HelpTooltip("Control the PCF filtering radius of shadow edges.");
                slider("Near cascade coverage", &m_scene.shadow_coverage, 20.0f, 240.0f);
                HelpTooltip("Set the world area covered by Cascade 0. Cascade 1 covers 3.2x this area automatically.");
                ImGui::EndDisabled();
            }
            if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
                color("Ambient color", m_scene.ambient_color);
                slider("Ambient intensity", &m_scene.ambient_intensity, 0.1f, 3);
                color("Sky color", m_scene.sky_color);
                slider("Fog start", &m_scene.fog_start, 0, 500);
                slider("Fog end", &m_scene.fog_end, 10, 1000);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Assets")) {
            draw_assets_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace GM3D::Studio
