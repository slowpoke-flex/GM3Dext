// Viewport panel, camera, picking and the overlay gizmos drawn on top of the render.
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

void StudioApp::update_camera(float delta_time) {
    ImGuiIO& io = ImGui::GetIO();

    if (m_viewport_hovered || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        // Free-Fly Camera Mode: Hold Right Mouse Button
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                float direction[3];
                if (viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction)) {
                    m_cam_yaw = std::atan2(direction[0], direction[2]) * 57.2957795f;
                    m_cam_pitch = -std::asin(std::clamp(direction[1], -1.0f, 1.0f)) * 57.2957795f;
                }
            }
            float mouse_dx = io.MouseDelta.x;
            float mouse_dy = io.MouseDelta.y;

            m_cam_yaw += mouse_dx * 0.25f;
            m_cam_pitch += mouse_dy * 0.25f;
            m_cam_pitch = std::clamp(m_cam_pitch, -88.0f, 88.0f);

            float yawRad = m_cam_yaw * 0.0174532925f;
            float pitchRad = m_cam_pitch * 0.0174532925f;

            float fx = sinf(yawRad) * cosf(pitchRad);
            float fy = -sinf(pitchRad);
            float fz = cosf(yawRad) * cosf(pitchRad);

            float rx = cosf(yawRad);
            float ry = 0.0f;
            float rz = -sinf(yawRad);

            float speed = m_cam_speed * delta_time;
            if (io.KeyShift) speed *= 2.5f;

            if (ImGui::IsKeyDown(ImGuiKey_W)) { m_cam_pos[0] += fx * speed; m_cam_pos[1] += fy * speed; m_cam_pos[2] += fz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_S)) { m_cam_pos[0] -= fx * speed; m_cam_pos[1] -= fy * speed; m_cam_pos[2] -= fz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_D)) { m_cam_pos[0] += rx * speed; m_cam_pos[1] += ry * speed; m_cam_pos[2] += rz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_A)) { m_cam_pos[0] -= rx * speed; m_cam_pos[1] -= ry * speed; m_cam_pos[2] -= rz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) { m_cam_pos[1] += speed; }
            if (ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_C)) { m_cam_pos[1] -= speed; }

            m_cam_target[0] = m_cam_pos[0] + fx * 10.0f;
            m_cam_target[1] = m_cam_pos[1] + fy * 10.0f;
            m_cam_target[2] = m_cam_pos[2] + fz * 10.0f;
        }

        // Move along the actual look direction, preserving orientation.
        if (m_viewport_hovered && io.MouseWheel != 0.0f) {
            float direction[3];
            if (viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction)) {
                const float zoom = io.MouseWheel * 3.5f;
                for (int i = 0; i < 3; ++i) {
                    const float delta = direction[i] * zoom;
                    m_cam_pos[i] += delta;
                    m_cam_target[i] += delta;
                }
            }
        }


    }

    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput && !io.KeyCtrl && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        focus_selected();
    }
}

void StudioApp::focus_selected() {
    if (m_selected_entity_id < 0 && m_selected_entity_ids.empty()) return;

    float center[3] = { 0.0f, 0.0f, 0.0f };
    float max_extent = 1.0f;
    size_t count = 0;

    std::vector<uint32_t> target_ids;
    if (!m_selected_entity_ids.empty()) {
        target_ids.assign(m_selected_entity_ids.begin(), m_selected_entity_ids.end());
    } else if (m_selected_entity_id >= 0) {
        target_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }

    for (uint32_t id : target_ids) {
        Entity* e = m_scene.find_entity(id);
        if (!e) continue;
        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *e, wpos, wrot, wscale);
        center[0] += wpos[0];
        center[1] += wpos[1];
        center[2] += wpos[2];
        const float radius = e->is_point_light ? 1.0f : std::max({std::abs(wscale[0]), std::abs(wscale[1]), std::abs(wscale[2]), 0.5f});
        max_extent = std::max(max_extent, radius);
        ++count;
    }

    if (count == 0) return;
    center[0] /= static_cast<float>(count);
    center[1] /= static_cast<float>(count);
    center[2] /= static_cast<float>(count);

    float direction[3];
    if (!viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction)) {
        float radYaw = m_cam_yaw * 0.0174532925f;
        float radPitch = m_cam_pitch * 0.0174532925f;
        direction[0] = cosf(radPitch) * sinf(radYaw);
        direction[1] = sinf(radPitch);
        direction[2] = cosf(radPitch) * cosf(radYaw);
    }

    const float distance = std::max(3.5f, max_extent * 2.8f);
    for (int i = 0; i < 3; ++i) {
        m_cam_target[i] = center[i];
        m_cam_pos[i] = center[i] - direction[i] * distance;
        m_cam_velocity[i] = 0.0f;
    }
    m_cam_zoom_velocity = 0.0f;
    m_cam_yaw = std::atan2(direction[0], direction[2]) * 57.2957795f;
    m_cam_pitch = -std::asin(std::clamp(direction[1], -1.0f, 1.0f)) * 57.2957795f;
}

void StudioApp::resize_viewport_texture(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == m_viewport_width && height == m_viewport_height && m_viewport_tex)) {
        return;
    }

    m_viewport_width = std::max(64u, width);
    m_viewport_height = std::max(64u, height);

    m_viewport_srv.Reset();
    m_viewport_rtv.Reset();
    m_viewport_tex.Reset();

    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_viewport_width;
    td.Height = m_viewport_height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = rw->get_device()->CreateTexture2D(&td, nullptr, m_viewport_tex.GetAddressOf());
    if (FAILED(hr)) return;

    hr = rw->get_device()->CreateRenderTargetView(m_viewport_tex.Get(), nullptr, m_viewport_rtv.GetAddressOf());
    if (FAILED(hr)) return;

    hr = rw->get_device()->CreateShaderResourceView(m_viewport_tex.Get(), nullptr, m_viewport_srv.GetAddressOf());
    if (FAILED(hr)) return;
}

void StudioApp::draw_viewport_panel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport", &m_show_viewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    m_viewport_focused = ImGui::IsWindowFocused();
    m_viewport_hovered = ImGui::IsWindowHovered();

    // Compact toolbar keeps the viewport usable at narrow widths.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.085f, 0.105f, 1.0f));
    const bool compact_toolbar = ImGui::GetContentRegionAvail().x < 700.0f;
    const float toolbar_height = compact_toolbar ? 2.0f * ImGui::GetFrameHeightWithSpacing() + 12.0f : 43.0f;
    ImGui::BeginChild("ViewportToolbar", ImVec2(0, toolbar_height), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (m_simulating) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.22f, 0.20f, 1));
    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.52f, 0.29f, 1));
    if (ImGui::Button(m_simulating ? "Stop (P)" : "Play (P)", ImVec2(75, 26))) {
        if (m_simulating) stop_simulation(); else start_simulation();
    }
    ImGui::PopStyleColor();
    HelpTooltip("Start or stop the physics preview. Shortcut: P. Stopping restores the original scene state.");
    ImGui::SameLine();
    if (ImGui::Button("+ Add", ImVec2(60, 26))) ImGui::OpenPopup("ViewportAdd");
    HelpTooltip("Add a primitive, point light, or imported model to the scene.");
    if (ImGui::BeginPopup("ViewportAdd")) {
        if (ImGui::MenuItem("Cube")) add_entity(0, "Cube");
        if (ImGui::MenuItem("Sphere")) add_entity(1, "Sphere");
        if (ImGui::MenuItem("Cylinder")) add_entity(2, "Cylinder");
        if (ImGui::MenuItem("Plane")) add_entity(3, "Plane");
        if (ImGui::MenuItem("Capsule")) add_entity(4, "Capsule");
        if (ImGui::MenuItem("Pine tree")) add_entity(6, "PineTree");
        if (ImGui::MenuItem("Point light")) add_point_light();
        ImGui::Separator();
        if (ImGui::MenuItem("Import model...")) import_model_dialog();
        ImGui::EndPopup();
    }
    auto tool_button = [&](const char* text, bool selected) {
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.68f, 1.0f));
        bool pressed = ImGui::Button(text, ImVec2(56, 26));
        if (selected) ImGui::PopStyleColor();
        return pressed;
    };
    ImGui::SameLine();
    if (tool_button("Move", !m_terrain_brush_enabled && m_gizmo_operation == GizmoOperation::Translate)) { m_terrain_brush_enabled = false; m_gizmo_operation = GizmoOperation::Translate; }
    HelpTooltip("Move the selected object with the gizmo. Shortcut: W.");
    ImGui::SameLine();
    if (tool_button("Rotate", !m_terrain_brush_enabled && m_gizmo_operation == GizmoOperation::Rotate)) { m_terrain_brush_enabled = false; m_gizmo_operation = GizmoOperation::Rotate; }
    HelpTooltip("Rotate the selected object with the gizmo. Shortcut: E.");
    ImGui::SameLine();
    if (tool_button("Scale", !m_terrain_brush_enabled && m_gizmo_operation == GizmoOperation::Scale)) { m_terrain_brush_enabled = false; m_gizmo_operation = GizmoOperation::Scale; }
    HelpTooltip("Scale the selected object with the gizmo. Shortcut: R.");
    ImGui::SameLine();
    if (tool_button("Sculpt", m_terrain_brush_enabled)) {
        m_terrain_brush_enabled = !m_terrain_brush_enabled;
        m_show_environment = true;
        m_select_terrain_tab = true;
    }
    HelpTooltip("Toggle terrain sculpting and open the brush settings.");
    if (!compact_toolbar) ImGui::SameLine();
    if (ImGui::Button(m_gizmo_mode == GizmoMode::Local ? "Local" : "World", ImVec2(54, 26)))
        m_gizmo_mode = (m_gizmo_mode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
    HelpTooltip("Switch gizmo axes between local object space and world space.");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_gizmo_snap);
    HelpTooltip("Snap gizmo transformations to fixed increments.");
    if (m_gizmo_snap) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(68.0f);
        const char* snap_steps[] = { "0.1m", "0.25m", "0.5m", "1.0m", "2.0m", "5.0m" };
        const float snap_vals[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f };
        int cur_snap_idx = 3;
        for (int i = 0; i < 6; ++i) {
            if (fabsf(m_snap_translation[0] - snap_vals[i]) < 0.05f) { cur_snap_idx = i; break; }
        }
        if (ImGui::Combo("##SnapStep", &cur_snap_idx, snap_steps, 6)) {
            m_snap_translation[0] = m_snap_translation[1] = m_snap_translation[2] = snap_vals[cur_snap_idx];
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus (F)", ImVec2(72, 26))) {
        focus_selected();
    }
    HelpTooltip("Center and frame the camera on selected objects (Shortcut: F).");
    ImGui::SameLine();
    ImGui::Checkbox("Colliders", &m_show_collider_gizmos);
    HelpTooltip("Display emerald green Jolt physics collider wireframes on selected objects.");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // 2. 3D Render Image Viewport
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(1.0f, avail.x);
    avail.y = std::max(1.0f, avail.y);
    uint32_t w = static_cast<uint32_t>(avail.x);
    uint32_t h = static_cast<uint32_t>(avail.y);

    if (w > 64 && h > 64 && (w != m_viewport_width || h != m_viewport_height)) {
        resize_viewport_texture(w, h);
    }

    ImVec2 viewportMin = ImGui::GetCursorScreenPos();

    if (m_viewport_srv) {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_srv.Get()), avail);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ASSET")) {
                if (payload->DataSize == sizeof(size_t)) {
                    const size_t index = *static_cast<const size_t*>(payload->Data);
                    if (index < m_assets.size()) {
                        const auto asset = m_assets[index];
                        if (!(asset.is_model ? add_model_asset(asset.path, false) : apply_texture_asset(asset.path))) {
                            m_asset_message = "Could not use asset. Select an object for textures and stop simulation.";
                            m_asset_message_pending = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    update_terrain_brush(viewportMin.x, viewportMin.y, avail.x, avail.y);
    const bool light_clicked = draw_light_markers(viewportMin.x, viewportMin.y, avail.x, avail.y);

    // 3. 3D Mouse Picking (Click to select entity directly in viewport)
    if (!light_clicked && !m_terrain_brush_enabled && m_viewport_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            ImVec2 mpos = ImGui::GetMousePos();
            pick_entity_at_mouse(mpos.x - viewportMin.x, mpos.y - viewportMin.y, avail.x, avail.y);
        }
    }

    // 4. ImGuizmo 3D Transform Gizmo & Selection Bounds Overlay
    if (!m_terrain_brush_enabled && m_selected_entity_id >= 0 && m_viewport_srv) {
        Entity* sel = nullptr;
        for (auto& e : m_scene.entities) {
            if (static_cast<int32_t>(e.id) == m_selected_entity_id) {
                sel = &e;
                break;
            }
        }

        if (sel) {
            float view[16], proj[16];
            ComputeLookAtLH(view, m_cam_pos[0], m_cam_pos[1], m_cam_pos[2],
                                  m_cam_target[0], m_cam_target[1], m_cam_target[2],
                                  0.0f, 1.0f, 0.0f);

            float aspect = (avail.y > 0.0f) ? (avail.x / avail.y) : 1.777f;
            ComputePerspectiveLH(proj, 60.0f, aspect, 0.1f, 800.0f);

            // Draw glowing 3D selection bounding boxes and collider wireframes for all selected objects
            for (uint32_t sel_id : m_selected_entity_ids) {
                Entity* ent = m_scene.find_entity(sel_id);
                if (ent && !ent->is_point_light && !ent->is_group) {
                    draw_selection_bounds(*ent, view, proj, viewportMin.x, viewportMin.y, avail.x, avail.y);
                    if (m_show_collider_gizmos && ent->physics_type != 0) {
                        draw_collider_bounds(*ent, view, proj, viewportMin.x, viewportMin.y, avail.x, avail.y);
                    }
                }
            }
            if (m_selected_entity_ids.empty() && !sel->is_point_light && !sel->is_group) {
                draw_selection_bounds(*sel, view, proj, viewportMin.x, viewportMin.y, avail.x, avail.y);
                if (m_show_collider_gizmos && sel->physics_type != 0) {
                    draw_collider_bounds(*sel, view, proj, viewportMin.x, viewportMin.y, avail.x, avail.y);
                }
            }

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(viewportMin.x, viewportMin.y, avail.x, avail.y);

            float modelMatrix[16];
            ComputeWorldMatrix(m_scene, *sel, modelMatrix);

            float prev_pos[3], prev_rot[3], prev_scale[3];
            ImGuizmo::DecomposeMatrixToComponents(modelMatrix, prev_pos, prev_rot, prev_scale);

            ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
            if (!sel->is_point_light && m_gizmo_operation == GizmoOperation::Rotate) op = ImGuizmo::ROTATE;
            if (!sel->is_point_light && m_gizmo_operation == GizmoOperation::Scale)  op = ImGuizmo::SCALE;

            ImGuizmo::MODE mode = (m_gizmo_mode == GizmoMode::Local) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            float* snap = nullptr;
            if (m_gizmo_snap) {
                if (op == ImGuizmo::TRANSLATE) snap = m_snap_translation;
                else if (op == ImGuizmo::ROTATE) snap = &m_snap_rotation;
                else if (op == ImGuizmo::SCALE) snap = &m_snap_scale;
            }

            if (ImGuizmo::Manipulate(view, proj, op, mode, modelMatrix, nullptr, snap)) {
                float new_pos[3], new_rot[3], new_scale[3];
                ImGuizmo::DecomposeMatrixToComponents(modelMatrix, new_pos, new_rot, new_scale);

                float delta_pos[3] = { new_pos[0] - prev_pos[0], new_pos[1] - prev_pos[1], new_pos[2] - prev_pos[2] };
                float delta_rot[3] = { new_rot[0] - prev_rot[0], new_rot[1] - prev_rot[1], new_rot[2] - prev_rot[2] };
                float delta_scale[3] = {
                    (prev_scale[0] > 1e-4f) ? (new_scale[0] / prev_scale[0]) : 1.0f,
                    (prev_scale[1] > 1e-4f) ? (new_scale[1] / prev_scale[1]) : 1.0f,
                    (prev_scale[2] > 1e-4f) ? (new_scale[2] / prev_scale[2]) : 1.0f
                };

                ComputeLocalTransform(m_scene, sel->parent_id, modelMatrix, sel->position, sel->rotation, sel->scale);

                // Apply delta to all other selected entities in world space
                for (uint32_t other_id : m_selected_entity_ids) {
                    if (other_id == sel->id) continue;
                    Entity* other = m_scene.find_entity(other_id);
                    if (!other) continue;
                    if (m_scene.is_descendant_of(other->id, sel->id)) continue;

                    float ow_pos[3], ow_rot[3], ow_scale[3], ow_mat[16];
                    ComputeWorldTransform(m_scene, *other, ow_pos, ow_rot, ow_scale, ow_mat);

                    if (m_gizmo_operation == GizmoOperation::Translate) {
                        ow_mat[12] += delta_pos[0];
                        ow_mat[13] += delta_pos[1];
                        ow_mat[14] += delta_pos[2];
                    } else if (m_gizmo_operation == GizmoOperation::Rotate) {
                        ow_rot[0] += delta_rot[0];
                        ow_rot[1] += delta_rot[1];
                        ow_rot[2] += delta_rot[2];
                        BuildMatrix(ow_pos, ow_rot, ow_scale, ow_mat);
                    } else if (m_gizmo_operation == GizmoOperation::Scale) {
                        ow_scale[0] *= delta_scale[0];
                        ow_scale[1] *= delta_scale[1];
                        ow_scale[2] *= delta_scale[2];
                        BuildMatrix(ow_pos, ow_rot, ow_scale, ow_mat);
                    }
                    ComputeLocalTransform(m_scene, other->parent_id, ow_mat, other->position, other->rotation, other->scale);
                }
            }
        }
    }

    ImGui::End();
}

bool StudioApp::draw_light_markers(float min_x, float min_y, float width, float height) {
    if (width <= 0 || height <= 0 || !m_viewport_srv) return false;
    float view[16], proj[16];
    ComputeLookAtLH(view, m_cam_pos[0], m_cam_pos[1], m_cam_pos[2],
        m_cam_target[0], m_cam_target[1], m_cam_target[2], 0, 1, 0);
    ComputePerspectiveLH(proj, 60.0f, width / height, 0.1f, 800.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(min_x, min_y), ImVec2(min_x + width, min_y + height), true);
    bool clicked = false;
    for (const auto& light : m_scene.entities) {
        if (!light.is_point_light) continue;
        const float x = light.position[0], y = light.position[1], z = light.position[2];
        const float vx = x * view[0] + y * view[4] + z * view[8] + view[12];
        const float vy = x * view[1] + y * view[5] + z * view[9] + view[13];
        const float vz = x * view[2] + y * view[6] + z * view[10] + view[14];
        if (vz <= 0.1f || vz > 800.0f) continue;
        const ImVec2 point(min_x + (0.5f + vx * proj[0] / vz * 0.5f) * width,
                           min_y + (0.5f - vy * proj[5] / vz * 0.5f) * height);
        if (point.x < min_x || point.x > min_x + width || point.y < min_y || point.y > min_y + height) continue;
        const bool selected = static_cast<int32_t>(light.id) == m_selected_entity_id;
        const ImU32 color = light.visible ? IM_COL32(255, 207, 92, 245) : IM_COL32(140, 145, 150, 190);
        draw->AddCircleFilled(point, 7.0f, IM_COL32(20, 25, 31, 220));
        draw->AddCircle(point, selected ? 11.0f : 8.0f, color, 20, selected ? 2.5f : 1.5f);
        draw->AddCircleFilled(point, 3.0f, color);
        const ImVec2 mouse = ImGui::GetMousePos();
        const float dx = mouse.x - point.x, dy = mouse.y - point.y;
        if (!m_terrain_brush_enabled && m_viewport_hovered && dx * dx + dy * dy <= 144.0f &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            m_selected_entity_id = static_cast<int32_t>(light.id);
            m_gizmo_operation = GizmoOperation::Translate;
            clicked = true;
        }
    }
    draw->PopClipRect();
    return clicked;
}

void StudioApp::pick_entity_at_mouse(float mouseX, float mouseY, float viewportW, float viewportH) {
    if (viewportW <= 0.0f || viewportH <= 0.0f) return;

    float ray[3];
    if (!viewport_ray(m_cam_pos, m_cam_target, mouseX, mouseY, viewportW, viewportH, ray)) return;
    const float dirX = ray[0], dirY = ray[1], dirZ = ray[2];

    float closestT = 1e9f;
    int32_t hitEntityId = -1;

    for (const auto& e : m_scene.entities) {
        if (!e.visible || e.editor_grid || e.is_point_light || e.is_group) continue;

        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, e, wpos, wrot, wscale);

        float maxScale = std::max({ std::abs(wscale[0]), std::abs(wscale[1]), std::abs(wscale[2]) });
        float radius = maxScale * 0.75f;

        float ocX = m_cam_pos[0] - wpos[0];
        float ocY = m_cam_pos[1] - wpos[1];
        float ocZ = m_cam_pos[2] - wpos[2];

        float b = ocX * dirX + ocY * dirY + ocZ * dirZ;
        float c = (ocX * ocX + ocY * ocY + ocZ * ocZ) - radius * radius;
        float discr = b * b - c;

        if (discr >= 0.0f) {
            float t = -b - sqrtf(discr);
            if (t <= 0.1f) t = -b + sqrtf(discr);
            if (t > 0.1f && t < closestT) {
                closestT = t;
                hitEntityId = static_cast<int32_t>(e.id);
            }
        }
    }

    bool ctrl = ImGui::GetIO().KeyCtrl;
    if (hitEntityId != -1) {
        select_entity(static_cast<uint32_t>(hitEntityId), ctrl, false);
    } else if (!ctrl) {
        deselect_all();
    }
}

void StudioApp::draw_selection_bounds(const Entity& entity, const float* view, const float* proj, float minX, float minY, float sizeX, float sizeY) {
    if (entity.mesh_id == 3 && entity.scale[0] > 50.0f) return;
    if (entity.is_group) return;

    float hx = 0.5f;
    float hy = 0.5f;
    float hz = 0.5f;

    float corners[8][3] = {
        { -hx, -hy, -hz },
        {  hx, -hy, -hz },
        {  hx,  hy, -hz },
        { -hx,  hy, -hz },
        { -hx, -hy,  hz },
        {  hx, -hy,  hz },
        {  hx,  hy,  hz },
        { -hx,  hy,  hz }
    };

    float world_matrix[16];
    ComputeWorldMatrix(m_scene, entity, world_matrix);

    auto Project = [&](float lx, float ly, float lz, ImVec2& outPt) -> bool {
        float wx = lx * world_matrix[0] + ly * world_matrix[4] + lz * world_matrix[8]  + world_matrix[12];
        float wy = lx * world_matrix[1] + ly * world_matrix[5] + lz * world_matrix[9]  + world_matrix[13];
        float wz = lx * world_matrix[2] + ly * world_matrix[6] + lz * world_matrix[10] + world_matrix[14];

        float vx = wx * view[0] + wy * view[4] + wz * view[8]  + view[12];
        float vy = wx * view[1] + wy * view[5] + wz * view[9]  + view[13];
        float vz = wx * view[2] + wy * view[6] + wz * view[10] + view[14];
        float vw = wx * view[3] + wy * view[7] + wz * view[11] + view[15];

        if (vw <= 0.1f) return false;

        float cx = vx * proj[0] + vy * proj[4] + vz * proj[8]  + vw * proj[12];
        float cy = vx * proj[1] + vy * proj[5] + vz * proj[9]  + vw * proj[13];
        float cz = vx * proj[2] + vy * proj[6] + vz * proj[10] + vw * proj[14];
        float cw = vx * proj[3] + vy * proj[7] + vz * proj[11] + vw * proj[15];

        if (cw <= 0.1f) return false;

        float ndcX = cx / cw;
        float ndcY = cy / cw;
        if (ndcX < -3.0f || ndcX > 3.0f || ndcY < -3.0f || ndcY > 3.0f) return false;

        outPt.x = minX + (ndcX * 0.5f + 0.5f) * sizeX;
        outPt.y = minY + (-ndcY * 0.5f + 0.5f) * sizeY;
        return true;
    };

    ImVec2 screenPts[8];
    bool valid[8];
    for (int i = 0; i < 8; ++i) {
        valid[i] = Project(corners[i][0], corners[i][1], corners[i][2], screenPts[i]);
    }

    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 boxCol = IM_COL32(255, 175, 45, 220);

    for (int i = 0; i < 12; ++i) {
        int a = edges[i][0];
        int b = edges[i][1];
        if (valid[a] && valid[b]) {
            drawList->AddLine(screenPts[a], screenPts[b], boxCol, 1.5f);
        }
    }
}

void StudioApp::draw_collider_bounds(const Entity& entity, const float* view, const float* proj, float minX, float minY, float sizeX, float sizeY) {
    if (entity.is_group || entity.is_point_light || entity.physics_type == 0) return;

    float world_matrix[16];
    ComputeWorldMatrix(m_scene, entity, world_matrix);

    auto Project = [&](float lx, float ly, float lz, ImVec2& outPt) -> bool {
        float ox = lx + entity.collider_offset[0];
        float oy = ly + entity.collider_offset[1];
        float oz = lz + entity.collider_offset[2];

        float wx = ox * world_matrix[0] + oy * world_matrix[4] + oz * world_matrix[8]  + world_matrix[12];
        float wy = ox * world_matrix[1] + oy * world_matrix[5] + oz * world_matrix[9]  + world_matrix[13];
        float wz = ox * world_matrix[2] + oy * world_matrix[6] + oz * world_matrix[10] + world_matrix[14];

        float vx = wx * view[0] + wy * view[4] + wz * view[8]  + view[12];
        float vy = wx * view[1] + wy * view[5] + wz * view[9]  + view[13];
        float vz = wx * view[2] + wy * view[6] + wz * view[10] + view[14];
        float vw = wx * view[3] + wy * view[7] + wz * view[11] + view[15];

        if (vw <= 0.1f) return false;

        float cx = vx * proj[0] + vy * proj[4] + vz * proj[8]  + vw * proj[12];
        float cy = vx * proj[1] + vy * proj[5] + vz * proj[9]  + vw * proj[13];
        float cz = vx * proj[2] + vy * proj[6] + vz * proj[10] + vw * proj[14];
        float cw = vx * proj[3] + vy * proj[7] + vz * proj[11] + vw * proj[15];

        if (cw <= 0.1f) return false;

        float ndcX = cx / cw;
        float ndcY = cy / cw;
        if (ndcX < -3.0f || ndcX > 3.0f || ndcY < -3.0f || ndcY > 3.0f) return false;

        outPt.x = minX + (ndcX * 0.5f + 0.5f) * sizeX;
        outPt.y = minY + (-ndcY * 0.5f + 0.5f) * sizeY;
        return true;
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 col = IM_COL32(45, 235, 115, 220);

    float sx = std::max(0.05f, entity.collider_size[0]);
    float sy = std::max(0.05f, entity.collider_size[1]);
    float sz = std::max(0.05f, entity.collider_size[2]);

    if (entity.collider_shape == 0) {
        // Box collider wireframe
        float hx = 0.5f * sx;
        float hy = 0.5f * sy;
        float hz = 0.5f * sz;
        float corners[8][3] = {
            { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
            { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
        };
        ImVec2 pts[8];
        bool ok[8];
        for (int i = 0; i < 8; ++i) ok[i] = Project(corners[i][0], corners[i][1], corners[i][2], pts[i]);
        const int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        };
        for (int i = 0; i < 12; ++i) {
            if (ok[edges[i][0]] && ok[edges[i][1]]) {
                drawList->AddLine(pts[edges[i][0]], pts[edges[i][1]], col, 1.5f);
            }
        }
    } else if (entity.collider_shape == 1) {
        // Sphere collider wireframe (3 orthogonal rings)
        const int kSegs = 24;
        float r = 0.5f * sx;
        for (int ring = 0; ring < 3; ++ring) {
            ImVec2 prevPt;
            bool prevOk = false;
            for (int s = 0; s <= kSegs; ++s) {
                float a = (static_cast<float>(s) / kSegs) * 6.2831853f;
                float ca = cosf(a) * r;
                float sa = sinf(a) * r;
                float lx = (ring == 0) ? ca : (ring == 1 ? ca : 0.0f);
                float ly = (ring == 0) ? sa : (ring == 1 ? 0.0f : ca);
                float lz = (ring == 0) ? 0.0f : (ring == 1 ? sa : sa);
                ImVec2 curPt;
                bool curOk = Project(lx, ly, lz, curPt);
                if (prevOk && curOk) drawList->AddLine(prevPt, curPt, col, 1.5f);
                prevPt = curPt;
                prevOk = curOk;
            }
        }
    } else if (entity.collider_shape == 4) {
        // Mesh collider: draw the model's local bounds. The real shape is the triangle soup,
        // so this is the extent of the collider rather than a stand-in primitive.
        float lo[3] = { -0.5f, -0.5f, -0.5f };
        float hi[3] = {  0.5f,  0.5f,  0.5f };
        if (auto* rw = EngineContext::instance().get_render_world()) {
            if (const GPUMesh* mesh = rw->get_mesh_manager().get_mesh(entity.mesh_id)) {
                for (int axis = 0; axis < 3; ++axis) {
                    lo[axis] = mesh->bounds_min[axis];
                    hi[axis] = mesh->bounds_max[axis];
                }
            }
        }
        float corners[8][3];
        for (int i = 0; i < 8; ++i) {
            const int ax = i & 1, ay = (i >> 1) & 1, az = (i >> 2) & 1;
            corners[i][0] = (ax ? hi[0] : lo[0]) * sx;
            corners[i][1] = (ay ? hi[1] : lo[1]) * sy;
            corners[i][2] = (az ? hi[2] : lo[2]) * sz;
        }
        ImVec2 pts[8];
        bool ok[8];
        for (int i = 0; i < 8; ++i) ok[i] = Project(corners[i][0], corners[i][1], corners[i][2], pts[i]);
        const int edges[12][2] = {
            {0, 1}, {1, 3}, {3, 2}, {2, 0},
            {4, 5}, {5, 7}, {7, 6}, {6, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7}
        };
        const ImU32 meshCol = IM_COL32(120, 210, 255, 210);
        for (int i = 0; i < 12; ++i) {
            if (ok[edges[i][0]] && ok[edges[i][1]]) {
                drawList->AddLine(pts[edges[i][0]], pts[edges[i][1]], meshCol, 1.5f);
            }
        }
    } else {
        // Capsule / Cylinder wireframe (top & bottom rings + 4 vertical struts)
        const int kSegs = 20;
        float r = 0.5f * sx;
        float hh = 0.5f * sy;
        for (int cap = -1; cap <= 1; cap += 2) {
            float y = cap * hh;
            ImVec2 prevPt;
            bool prevOk = false;
            for (int s = 0; s <= kSegs; ++s) {
                float a = (static_cast<float>(s) / kSegs) * 6.2831853f;
                ImVec2 curPt;
                bool curOk = Project(cosf(a) * r, y, sinf(a) * r, curPt);
                if (prevOk && curOk) drawList->AddLine(prevPt, curPt, col, 1.5f);
                prevPt = curPt;
                prevOk = curOk;
            }
        }
        const float dirs[4][2] = { {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f} };
        for (int d = 0; d < 4; ++d) {
            ImVec2 p0, p1;
            if (Project(dirs[d][0] * r, -hh, dirs[d][1] * r, p0) &&
                Project(dirs[d][0] * r,  hh, dirs[d][1] * r, p1)) {
                drawList->AddLine(p0, p1, col, 1.5f);
            }
        }
    }
}

} // namespace GM3D::Studio
