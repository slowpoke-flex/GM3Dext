// Inspector: the object properties panel (transform, collider, animation, materials).
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

void StudioApp::draw_inspector_panel() {
    ImGui::Begin("Inspector", &m_show_inspector);

    if (m_selected_entity_ids.size() > 1) {
        ImGui::TextUnformatted("MULTI-SELECTION INSPECTOR");
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%zu objects selected", m_selected_entity_ids.size());
        ImGui::Spacing();

        if (ImGui::Button("Group Selected (Ctrl+G)", ImVec2(-1, 28))) group_selected_entities();
        if (ImGui::Button("Unparent All (Ctrl+Shift+G)", ImVec2(-1, 26))) unparent_selected_entities();
        if (ImGui::Button("Snap All to Surface (G)", ImVec2(-1, 26))) snap_selected_to_surface();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete All Selected (Del)", ImVec2(-1, 26))) delete_selected_entity();
        ImGui::PopStyleColor();

        ImGui::SeparatorText("Alignment & Distribution");
        ImGui::TextUnformatted("Align X:");
        ImGui::SameLine();
        if (ImGui::Button("Min##AX", ImVec2(50, 0))) align_selected_entities(0, 0);
        ImGui::SameLine();
        if (ImGui::Button("Center##AX", ImVec2(60, 0))) align_selected_entities(0, 1);
        ImGui::SameLine();
        if (ImGui::Button("Max##AX", ImVec2(50, 0))) align_selected_entities(0, 2);

        ImGui::TextUnformatted("Align Y:");
        ImGui::SameLine();
        if (ImGui::Button("Bottom##AY", ImVec2(55, 0))) align_selected_entities(1, 0);
        ImGui::SameLine();
        if (ImGui::Button("Center##AY", ImVec2(60, 0))) align_selected_entities(1, 1);
        ImGui::SameLine();
        if (ImGui::Button("Top##AY", ImVec2(50, 0))) align_selected_entities(1, 2);

        ImGui::TextUnformatted("Align Z:");
        ImGui::SameLine();
        if (ImGui::Button("Min##AZ", ImVec2(50, 0))) align_selected_entities(2, 0);
        ImGui::SameLine();
        if (ImGui::Button("Center##AZ", ImVec2(60, 0))) align_selected_entities(2, 1);
        ImGui::SameLine();
        if (ImGui::Button("Max##AZ", ImVec2(50, 0))) align_selected_entities(2, 2);

        ImGui::Spacing();
        ImGui::TextUnformatted("Distribute Evenly:");
        if (ImGui::Button("Along X", ImVec2(75, 0))) align_selected_entities(0, 3);
        ImGui::SameLine();
        if (ImGui::Button("Along Y", ImVec2(75, 0))) align_selected_entities(1, 3);
        ImGui::SameLine();
        if (ImGui::Button("Along Z", ImVec2(75, 0))) align_selected_entities(2, 3);

        ImGui::End();
        return;
    }

    if (m_selected_entity_id < 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No entity selected.");
        ImGui::End();
        return;
    }

    Entity* sel = nullptr;
    for (auto& e : m_scene.entities) {
        if (static_cast<int32_t>(e.id) == m_selected_entity_id) {
            sel = &e;
            break;
        }
    }

    if (!sel) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Selected entity not found.");
        ImGui::End();
        return;
    }

    if (sel->editor_grid && m_scene.has_terrain)
        ImGui::TextWrapped("Editor grid is hidden while terrain is enabled to avoid overlapping surfaces.");

    ImGui::TextUnformatted("OBJECT PROPERTIES");
    ImGui::TextDisabled("ID %u%s", sel->id, sel->is_group ? " (Group Container)" : "");
    ImGui::Spacing();

    // Hierarchy parent information
    if (sel->parent_id != 0) {
        Entity* parent = m_scene.find_entity(sel->parent_id);
        std::string pname = parent ? (parent->name + " [ID " + std::to_string(parent->id) + "]") : ("[ID " + std::to_string(sel->parent_id) + "]");
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.95f, 1.0f), "Parent: %s", pname.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Unparent")) set_entity_parent(sel->id, 0);
    } else {
        ImGui::TextDisabled("Parent: None (Root Entity)");
    }

    // Name & Tag
    char name_buf[64];
    strncpy_s(name_buf, sel->name.c_str(), sizeof(name_buf));
    ImGui::TextUnformatted("Name"); ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Name", name_buf, sizeof(name_buf))) {
        sel->name = name_buf;
    }

    char tag_buf[64];
    strncpy_s(tag_buf, sel->tag.c_str(), sizeof(tag_buf));
    ImGui::TextUnformatted("Tag"); ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Tag", tag_buf, sizeof(tag_buf))) {
        sel->tag = tag_buf;
    }

    ImGui::Separator();

    if (sel->is_point_light) {
        if (ImGui::CollapsingHeader("Transform Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawVec3Control("Position", sel->position, 0.0f, 0.1f);
            float wpos[3], wrot[3], wscale[3];
            ComputeWorldTransform(m_scene, *sel, wpos, wrot, wscale);
            ImGui::TextDisabled("World: (%.2f, %.2f, %.2f)", wpos[0], wpos[1], wpos[2]);
            if (ImGui::Button("Snap to Surface (G)", ImVec2(-1, 24))) {
                snap_selected_to_surface();
            }
        }
        if (ImGui::CollapsingHeader("Point Light Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled", &sel->visible);
            ImGui::SameLine();
            ImGui::Checkbox("Cast Shadows", &sel->cast_shadow);
            HelpTooltip("Enable 6-face omnidirectional shadow mapping for this point light (up to the scene's Max Shadowed Lights limit).");
            ImGui::TextUnformatted("Color"); ImGui::SetNextItemWidth(-1);
            ImGui::ColorEdit3("##LightColor", sel->light_color);
            ImGui::TextUnformatted("Intensity"); ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##LightIntensity", &sel->light_intensity, 0.5f, 0.0f, 500.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            HelpTooltip("Luminous intensity of this point light.");
            ImGui::TextUnformatted("Radius"); ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##LightRadius", &sel->light_radius, 0.1f, 0.2f, 100.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            HelpTooltip("Maximum distance illuminated and shadowed by this point light.");
        }
        ImGui::End();
        return;
    }

    // Transform Component with Blender/Unreal style colored axes
    if (ImGui::CollapsingHeader("Transform Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawVec3Control("Position", sel->position, 0.0f, 0.1f);
        DrawVec3Control("Rotation", sel->rotation, 0.0f, 0.5f);
        DrawVec3Control("Scale", sel->scale, 1.0f, 0.05f);

        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *sel, wpos, wrot, wscale);
        ImGui::TextDisabled("World: (%.2f, %.2f, %.2f)", wpos[0], wpos[1], wpos[2]);

        if (ImGui::Button("Snap to Surface (G)", ImVec2(-1, 24))) {
            snap_selected_to_surface();
        }
        HelpTooltip("Snap this object onto the terrain or ground plane.");
    }

    // Mesh Renderer Component
    if (ImGui::CollapsingHeader("Mesh Renderer Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Model");
        if (!sel->model_path.empty()) {
            const auto name = path_utf8(std::filesystem::u8path(sel->model_path).filename());
            ImGui::TextWrapped("%s%s", sel->mesh_id ? "" : "Missing: ", name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sel->model_path.c_str());
        } else if (sel->mesh_id > 6) {
            ImGui::TextWrapped("Legacy model ID %u (source path unavailable)", sel->mesh_id);
        }
        const char* meshes[] = { "Cube", "Sphere", "Cylinder", "Plane", "Capsule", "Pine Tree" };
        const uint16_t mesh_ids[] = { 0, 1, 2, 3, 4, 6 };
        int current_mesh = -1;
        for (int i = 0; i < 6; ++i) if (sel->model_path.empty() && sel->mesh_id == mesh_ids[i]) current_mesh = i;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Mesh", current_mesh < 0 ? "Custom model" : meshes[current_mesh])) {
            for (int i = 0; i < 6; ++i) if (ImGui::Selectable(meshes[i], current_mesh == i)) {
                sel->mesh_id = mesh_ids[i];
                sel->model_path.clear();
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(m_simulating);
        if (ImGui::Button("Replace model...", ImVec2(-1, 0))) import_model_dialog(true);
        HelpTooltip("Replace this object's mesh while retaining its transform and other settings.");
        ImGui::EndDisabled();

        ImGui::TextUnformatted("Material");
        if (m_scene.materials.empty()) m_scene.init_default_materials();

        Material* cur_mat = m_scene.find_material(sel->material_id);
        std::string cur_mat_label = cur_mat ? cur_mat->name : ("Material ID: " + std::to_string(sel->material_id));

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##PBRMaterial", cur_mat_label.c_str())) {
            for (const auto& m : m_scene.materials) {
                ImGui::PushID(static_cast<int>(m.id));
                ImVec4 swatch(m.base_color[0], m.base_color[1], m.base_color[2], m.base_color[3]);
                ImGui::ColorButton("##sw", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                ImGui::SameLine();
                bool is_sel = (sel->material_id == m.id);
                if (ImGui::Selectable(m.name.c_str(), is_sel)) {
                    sel->material_id = static_cast<uint16_t>(m.id);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Edit Material...", ImVec2(130, 24))) {
            m_selected_material_id = sel->material_id;
            m_show_materials = true;
            ImGui::SetWindowFocus("Materials");
        }
        HelpTooltip("Open the Materials editor to tweak roughness, metallic, colors, and textures.");
        ImGui::SameLine();

        if (ImGui::Button("+ New##FromSel", ImVec2(70, 24))) {
            Material new_m{};
            if (cur_mat) new_m = *cur_mat;
            new_m.id = m_scene.allocate_material_id();
            if (!new_m.id) { m_asset_message = "No material slots remain."; m_asset_message_pending = true; }
            else {
            new_m.name = "Material " + std::to_string(new_m.id);
            new_m.is_builtin_preset = false;
            m_scene.materials.push_back(new_m);
            sel->material_id = static_cast<uint16_t>(new_m.id);
            m_selected_material_id = new_m.id;
            m_show_materials = true;
            ImGui::SetWindowFocus("Materials");
            }
        }
        HelpTooltip("Create a new custom material based on this one.");

        if (m_selected_entity_ids.size() > 1) {
            char apply_txt[64];
            snprintf(apply_txt, sizeof(apply_txt), "Apply to All Selected (%zu)", m_selected_entity_ids.size());
            if (ImGui::Button(apply_txt, ImVec2(-1, 24))) {
                for (uint32_t eid : m_selected_entity_ids) {
                    for (auto& e : m_scene.entities) {
                        if (e.id == eid) e.material_id = sel->material_id;
                    }
                }
            }
        }

        ImGui::TextUnformatted("Texture");
        const char* builtin_textures[] = { "White", "Checker", "Tech grid", "Wood", "Metal" };
        const bool builtin = sel->texture_path.empty() && sel->texture_id < 5;
        if (!sel->texture_path.empty()) {
            const auto name = path_utf8(std::filesystem::u8path(sel->texture_path).filename());
            ImGui::TextWrapped("%s%s", sel->texture_id ? "" : "Missing: ", name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sel->texture_path.c_str());
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Texture", builtin ? builtin_textures[sel->texture_id] : "Custom texture")) {
            for (uint16_t i = 0; i < 5; ++i) if (ImGui::Selectable(builtin_textures[i], builtin && sel->texture_id == i)) {
                sel->texture_id = i;
                sel->texture_path.clear();
            }
            ImGui::EndCombo();
        }
        if (auto* rw = EngineContext::instance().get_render_world()) {
            auto* preview = rw->get_texture_manager().get_srv(sel->texture_id);
            if (preview) ImGui::Image(reinterpret_cast<ImTextureID>(preview), ImVec2(72, 72));
        }
        ImGui::BeginDisabled(m_simulating || sel->editor_grid);
        if (ImGui::Button("Choose texture...", ImVec2(-1, 0))) import_texture_dialog();
        HelpTooltip("Assign an image to the selected object. You can also drop an image onto the window.");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Images may also be dropped onto the window.");

        ImGui::Checkbox("Cast Shadow", &sel->cast_shadow);
        HelpTooltip("Include this object in the sun's shadow map.");
        ImGui::SameLine();
        ImGui::Checkbox("Visible", &sel->visible);
    }

    // Collider & Rigidbody Component (Jolt Physics)
    if (ImGui::CollapsingHeader("Collider & Rigidbody Component (Jolt)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* p_types[] = { "None (No Physics)", "Static", "Dynamic (Rigidbody)", "Kinematic" };
        ImGui::TextUnformatted("Body Type"); ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##BodyType", &sel->physics_type, p_types, IM_ARRAYSIZE(p_types));

        const char* shapes[] = { "Box", "Sphere", "Capsule", "Cylinder", "Mesh (Model)" };
        ImGui::TextUnformatted("Collider Shape"); ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##Collider", &sel->collider_shape, shapes, IM_ARRAYSIZE(shapes));
        if (sel->collider_shape == 4) {
            HelpTooltip("Uses the model's own triangles, scaled by the object's Transform scale and "
                        "Collider Size. Static and Kinematic bodies collide as a triangle mesh; "
                        "Dynamic bodies use a convex hull of the same geometry.");
        }

        if (sel->physics_type != 0) {
            ImGui::Spacing();
            DrawVec3Control("Col Size", sel->collider_size, 1.0f, 0.05f);
            HelpTooltip("Collider scale multiplier relative to the object's Transform scale.");
            DrawVec3Control("Col Offset", sel->collider_offset, 0.0f, 0.05f);
            HelpTooltip("Collider center offset relative to the object's pivot.");

            if (ImGui::Button("Reset Collider Bounds", ImVec2(-1, 24))) {
                sel->collider_size[0] = 1.0f; sel->collider_size[1] = 1.0f; sel->collider_size[2] = 1.0f;
                sel->collider_offset[0] = 0.0f; sel->collider_offset[1] = 0.0f; sel->collider_offset[2] = 0.0f;
            }
            ImGui::Checkbox("Show Collider Wireframes", &m_show_collider_gizmos);
            ImGui::Spacing();
            if (sel->physics_type == 2) {
                ImGui::DragFloat("Mass (kg)", &sel->mass, 0.1f, 0.01f, 1000.0f);
            }
            ImGui::SliderFloat("Friction", &sel->friction, 0.0f, 1.0f);
            ImGui::SliderFloat("Restitution", &sel->restitution, 0.0f, 1.0f);
        }
    }

    // Skeletal Animator Component
    uint32_t anim_count = 0;
    if (auto* rw = EngineContext::instance().get_render_world()) {
        anim_count = rw->get_animation_count(sel->mesh_id);
        if (anim_count > 0 && !sel->has_animator) {
            sel->has_animator = true;
        }
    }
    if (sel->has_animator || anim_count > 0) {
        if (ImGui::CollapsingHeader("Skeletal Animator Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Animator", &sel->has_animator);
            ImGui::BeginDisabled(!sel->has_animator);

            auto* rw = EngineContext::instance().get_render_world();
            if (anim_count > 0 && rw) {
                sel->anim_clip_index = std::clamp(sel->anim_clip_index, 0, static_cast<int>(anim_count) - 1);
                std::string cur_clip_name = rw->get_animation_name(sel->mesh_id, static_cast<uint32_t>(sel->anim_clip_index));
                float cur_clip_dur = rw->get_animation_duration(sel->mesh_id, static_cast<uint32_t>(sel->anim_clip_index));
                char preview_buf[128];
                snprintf(preview_buf, sizeof(preview_buf), "[%d] %s (%.2fs)", sel->anim_clip_index,
                         cur_clip_name.empty() ? "Clip" : cur_clip_name.c_str(), cur_clip_dur);

                ImGui::TextUnformatted("Animation Clip");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##AnimClip", preview_buf)) {
                    for (uint32_t ci = 0; ci < anim_count; ++ci) {
                        std::string cname = rw->get_animation_name(sel->mesh_id, ci);
                        float cdur = rw->get_animation_duration(sel->mesh_id, ci);
                        char item_buf[128];
                        snprintf(item_buf, sizeof(item_buf), "[%u] %s (%.2fs)", ci, cname.empty() ? "Clip" : cname.c_str(), cdur);
                        if (ImGui::Selectable(item_buf, sel->anim_clip_index == static_cast<int>(ci))) {
                            sel->anim_clip_index = static_cast<int>(ci);
                            sel->anim_time = 0.0f;
                        }
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button(sel->anim_playing ? "Pause" : "Play", ImVec2(90, 26))) {
                    sel->anim_playing = !sel->anim_playing;
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop / Reset", ImVec2(100, 26))) {
                    sel->anim_playing = false;
                    sel->anim_time = 0.0f;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &sel->anim_loop);

                ImGui::SliderFloat("Speed", &sel->anim_speed, 0.1f, 3.0f, "%.2fx");
                if (!sel->anim_playing && cur_clip_dur > 0.001f) {
                    ImGui::SliderFloat("Timeline (s)", &sel->anim_time, 0.0f, cur_clip_dur, "%.2f s");
                }
            } else {
                ImGui::TextDisabled("No embedded skeletal clips in current mesh (or custom runtime clip index):");
                ImGui::InputInt("Clip Index", &sel->anim_clip_index);
                sel->anim_clip_index = std::max(0, sel->anim_clip_index);
                ImGui::Checkbox("Auto-Play", &sel->anim_playing);
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &sel->anim_loop);
                ImGui::SliderFloat("Speed", &sel->anim_speed, 0.1f, 3.0f, "%.2fx");
            }

            ImGui::EndDisabled();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("+ Add Component", ImVec2(-1, 28))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (ImGui::MenuItem("Static Collider (Box)", nullptr, false, sel->physics_type == 0)) {
            sel->physics_type = 1;
            sel->collider_shape = 0;
        }
        if (ImGui::MenuItem("Dynamic Rigidbody (Jolt)", nullptr, false, sel->physics_type != 2)) {
            sel->physics_type = 2;
        }
        if (ImGui::MenuItem("Skeletal Animator", nullptr, false, !sel->has_animator)) {
            sel->has_animator = true;
            sel->anim_playing = true;
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace GM3D::Studio
