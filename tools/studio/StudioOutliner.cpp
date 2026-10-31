// Scene hierarchy panel and every operation on the selection it drives.
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

void StudioApp::draw_outliner_entity_node(Entity& e, const std::string& filter, int& pending_action) {
    std::vector<uint32_t> children = m_scene.get_children(e.id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (is_entity_selected(e.id)) flags |= ImGuiTreeNodeFlags_Selected;

    std::string icon = e.is_group ? "[Group] " : (e.is_point_light ? "[Light] " : "[Mesh] ");
    std::string label = icon + e.name + " [" + std::to_string(e.id) + "]";

    bool node_open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(e.id)), flags, "%s", label.c_str());

    if (ImGui::IsItemClicked()) {
        bool ctrl = ImGui::GetIO().KeyCtrl;
        bool shift = ImGui::GetIO().KeyShift;
        select_entity(e.id, ctrl, shift);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        focus_selected();
    }

    // Drag source: dragging this entity to reparent
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("GM3D_ENTITY_HIERARCHY", &e.id, sizeof(e.id));
        ImGui::Text("%s", label.c_str());
        ImGui::EndDragDropSource();
    }

    // Drag target: drop another entity onto this entity to make it a child
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ENTITY_HIERARCHY")) {
            if (payload->DataSize == sizeof(uint32_t)) {
                uint32_t dropped_id = *static_cast<const uint32_t*>(payload->Data);
                set_entity_parent(dropped_id, e.id);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ASSET")) {
            if (payload->DataSize == sizeof(size_t)) {
                size_t idx = *static_cast<const size_t*>(payload->Data);
                if (idx < m_assets.size() && !m_assets[idx].is_model) {
                    apply_texture_asset(m_assets[idx].path);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!is_entity_selected(e.id)) select_entity(e.id, false, false);
        if (ImGui::MenuItem("Focus Camera (F)")) focus_selected();
        ImGui::Separator();
        if (ImGui::MenuItem("Create Child Group")) {
            Entity group;
            group.id = m_scene.next_entity_id++;
            group.name = "Group_" + std::to_string(group.id);
            group.is_group = true;
            group.mesh_id = 0;
            group.parent_id = e.id;
            m_scene.entities.push_back(group);
            select_entity(group.id, false, false);
        }
        if (ImGui::MenuItem("Group Selected", "Ctrl+G")) group_selected_entities();
        if (e.parent_id != 0 && ImGui::MenuItem("Unparent", "Ctrl+Shift+G")) set_entity_parent(e.id, 0);
        if (ImGui::MenuItem("Snap to Surface", "G")) snap_selected_to_surface();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pending_action = 1;
        if (ImGui::MenuItem("Delete", "Del")) pending_action = 2;
        ImGui::EndPopup();
    }

    if (node_open) {
        for (uint32_t child_id : children) {
            Entity* child = m_scene.find_entity(child_id);
            if (child) draw_outliner_entity_node(*child, filter, pending_action);
        }
        ImGui::TreePop();
    }
}

void StudioApp::draw_outliner_panel() {
    ImGui::Begin("Scene Hierarchy", &m_show_hierarchy);
    ImGui::TextUnformatted("SCENE HIERARCHY");
    ImGui::TextDisabled("%zu objects (%zu selected)", m_scene.entities.size(), m_selected_entity_ids.size());
    ImGui::Spacing();

    // Top action bar
    const float half_btn_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    if (ImGui::Button("+ Add Entity", ImVec2(half_btn_w, 28))) {
        ImGui::OpenPopup("AddEntityPopup");
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus (F)", ImVec2(half_btn_w, 28))) {
        focus_selected();
    }
    HelpTooltip("Frame the 3D camera on selected entities (Shortcut: F).");

    ImGui::BeginDisabled(m_selected_entity_ids.empty() && m_selected_entity_id < 0);
    if (ImGui::Button("Group (Ctrl+G)", ImVec2(half_btn_w, 26))) {
        group_selected_entities();
    }
    HelpTooltip("Group all selected objects under a new parent transform.");
    ImGui::SameLine();
    if (ImGui::Button("Unparent", ImVec2(half_btn_w, 26))) {
        unparent_selected_entities();
    }
    HelpTooltip("Detach selected objects from their parents back to root level.");
    ImGui::EndDisabled();

    if (ImGui::BeginPopup("AddEntityPopup")) {
        if (ImGui::MenuItem("Cube")) add_entity(0, "Cube");
        if (ImGui::MenuItem("Sphere")) add_entity(1, "Sphere");
        if (ImGui::MenuItem("Plane (Ground)")) add_entity(3, "Plane");
        if (ImGui::MenuItem("Cylinder")) add_entity(2, "Cylinder");
        if (ImGui::MenuItem("Capsule")) add_entity(4, "Capsule");
        if (ImGui::MenuItem("Pine Tree")) add_entity(6, "PineTree");
        if (ImGui::MenuItem("Point Light")) add_point_light();
        ImGui::Separator();
        if (ImGui::MenuItem("Empty Group Node")) {
            Entity group;
            group.id = m_scene.next_entity_id++;
            group.name = "Group_" + std::to_string(group.id);
            group.is_group = true;
            group.mesh_id = 0;
            m_scene.entities.push_back(group);
            select_entity(group.id, false, false);
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    static char search_buf[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search objects...", search_buf, sizeof(search_buf));
    HelpTooltip("Filter objects by name. Drag objects onto each other to parent them.");
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string filter = lower(search_buf);
    int pending_action = 0;

    ImGui::BeginChild("EntityList", ImVec2(0, 0), true);

    if (filter.empty()) {
        for (size_t i = 0; i < m_scene.entities.size(); ++i) {
            auto& e = m_scene.entities[i];
            if (e.parent_id == 0) {
                draw_outliner_entity_node(e, filter, pending_action);
            }
        }
    } else {
        for (size_t i = 0; i < m_scene.entities.size(); ++i) {
            auto& e = m_scene.entities[i];
            if (lower(e.name).find(filter) == std::string::npos) continue;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (is_entity_selected(e.id)) flags |= ImGuiTreeNodeFlags_Selected;

            std::string icon = e.is_group ? "[Group] " : (e.is_point_light ? "[Light] " : "[Mesh] ");
            std::string label = icon + e.name + " [" + std::to_string(e.id) + "]";
            bool node_open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(e.id)), flags, "%s", label.c_str());

            if (ImGui::IsItemClicked()) {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;
                select_entity(e.id, ctrl, shift);
            }
            if (ImGui::BeginPopupContextItem()) {
                if (!is_entity_selected(e.id)) select_entity(e.id, false, false);
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pending_action = 1;
                if (ImGui::MenuItem("Delete", "Del")) pending_action = 2;
                ImGui::EndPopup();
            }
            if (node_open) ImGui::TreePop();
        }
    }

    // Drag-and-drop to empty area at bottom: unparent to root level!
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID("EntityList_UnparentZone"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ENTITY_HIERARCHY")) {
            if (payload->DataSize == sizeof(uint32_t)) {
                uint32_t dropped_id = *static_cast<const uint32_t*>(payload->Data);
                set_entity_parent(dropped_id, 0);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        if (!ImGui::GetIO().KeyCtrl) deselect_all();
    }

    ImGui::EndChild();
    if (pending_action == 1) duplicate_selected_entity();
    if (pending_action == 2) delete_selected_entity();

    ImGui::End();
}

Entity* StudioApp::selected_entity() {
    for (auto& entity : m_scene.entities)
        if (static_cast<int32_t>(entity.id) == m_selected_entity_id) return &entity;
    return nullptr;
}

void StudioApp::add_entity(uint16_t mesh_id, const std::string& name) {
    Entity e;
    e.id = m_scene.next_entity_id++;
    e.name = name + "_" + std::to_string(e.id);
    e.mesh_id = mesh_id;

    float direction[3] = {0.0f, 0.0f, 1.0f};
    viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction);
    for (int i = 0; i < 3; ++i) e.position[i] = m_cam_pos[i] + direction[i] * 6.0f;

    m_scene.entities.push_back(e);
    select_entity(e.id, false, false);
}

void StudioApp::add_point_light() {
    size_t count = 0;
    for (const auto& entity : m_scene.entities) if (entity.is_point_light) ++count;
    if (count >= 16) {
        m_asset_message = "A scene can contain up to 16 point lights.";
        m_asset_message_pending = true;
        return;
    }
    add_entity(0, "Point Light");
    if (auto* entity = selected_entity()) {
        entity->is_point_light = true;
        entity->cast_shadow = true;
        entity->physics_type = 0;
        m_terrain_brush_enabled = false;
        m_gizmo_operation = GizmoOperation::Translate;
    }
}

void StudioApp::delete_selected_entity() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    std::unordered_set<uint32_t> to_delete;
    for (uint32_t id : m_selected_entity_ids) {
        to_delete.insert(id);
        std::vector<uint32_t> descendants;
        m_scene.get_all_descendants(id, descendants);
        for (uint32_t desc_id : descendants) to_delete.insert(desc_id);
    }

    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) {
        for (const auto& e : m_scene.entities) {
            if (to_delete.count(e.id) && e.physics_body_id != 0) {
                pw->destroy_body(e.physics_body_id);
            }
        }
    }

    m_scene.entities.erase(
        std::remove_if(m_scene.entities.begin(), m_scene.entities.end(),
            [&to_delete](const Entity& e) { return to_delete.count(e.id) > 0; }),
        m_scene.entities.end()
    );

    deselect_all();
}

void StudioApp::duplicate_selected_entity() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    std::vector<uint32_t> root_selected;
    for (uint32_t id : m_selected_entity_ids) {
        bool has_selected_ancestor = false;
        for (uint32_t other : m_selected_entity_ids) {
            if (id != other && m_scene.is_descendant_of(id, other)) {
                has_selected_ancestor = true;
                break;
            }
        }
        if (!has_selected_ancestor) root_selected.push_back(id);
    }

    std::unordered_map<uint32_t, uint32_t> old_to_new;
    std::vector<Entity> new_entities;
    std::vector<uint32_t> newly_created_roots;

    for (uint32_t root_id : root_selected) {
        std::vector<uint32_t> subtree;
        subtree.push_back(root_id);
        m_scene.get_all_descendants(root_id, subtree);

        for (uint32_t orig_id : subtree) {
            const Entity* orig = m_scene.find_entity(orig_id);
            if (!orig) continue;

            Entity copy = *orig;
            copy.id = m_scene.next_entity_id++;
            copy.physics_body_id = 0;
            old_to_new[orig_id] = copy.id;

            if (orig_id == root_id) {
                copy.name += "_Copy";
                copy.position[0] += 1.0f;
                copy.position[2] += 1.0f;
                newly_created_roots.push_back(copy.id);
            }
            new_entities.push_back(copy);
        }
    }

    for (auto& new_ent : new_entities) {
        if (new_ent.parent_id != 0) {
            auto it = old_to_new.find(new_ent.parent_id);
            if (it != old_to_new.end()) {
                new_ent.parent_id = it->second;
            }
        }
    }

    for (auto& new_ent : new_entities) {
        m_scene.entities.push_back(new_ent);
    }

    m_selected_entity_ids = newly_created_roots;
    m_selected_entity_id = newly_created_roots.empty() ? -1 : static_cast<int32_t>(newly_created_roots.front());
}

bool StudioApp::is_entity_selected(uint32_t id) const {
    return std::find(m_selected_entity_ids.begin(), m_selected_entity_ids.end(), id) != m_selected_entity_ids.end();
}

void StudioApp::select_entity(uint32_t id, bool multi, bool range) {
    if (id == 0) {
        deselect_all();
        return;
    }
    if (range && m_selected_entity_id > 0) {
        int idx1 = -1, idx2 = -1;
        for (int i = 0; i < static_cast<int>(m_scene.entities.size()); ++i) {
            if (static_cast<int32_t>(m_scene.entities[i].id) == m_selected_entity_id) idx1 = i;
            if (m_scene.entities[i].id == id) idx2 = i;
        }
        if (idx1 >= 0 && idx2 >= 0) {
            int start = std::min(idx1, idx2);
            int end = std::max(idx1, idx2);
            for (int i = start; i <= end; ++i) {
                uint32_t curr_id = m_scene.entities[i].id;
                if (!is_entity_selected(curr_id)) m_selected_entity_ids.push_back(curr_id);
            }
            m_selected_entity_id = static_cast<int32_t>(id);
            return;
        }
    }
    if (multi) {
        auto it = std::find(m_selected_entity_ids.begin(), m_selected_entity_ids.end(), id);
        if (it != m_selected_entity_ids.end()) {
            m_selected_entity_ids.erase(it);
            if (m_selected_entity_id == static_cast<int32_t>(id)) {
                m_selected_entity_id = m_selected_entity_ids.empty() ? -1 : static_cast<int32_t>(m_selected_entity_ids.back());
            }
        } else {
            m_selected_entity_ids.push_back(id);
            m_selected_entity_id = static_cast<int32_t>(id);
        }
    } else {
        m_selected_entity_ids = { id };
        m_selected_entity_id = static_cast<int32_t>(id);
    }
}

void StudioApp::deselect_all() {
    m_selected_entity_ids.clear();
    m_selected_entity_id = -1;
}

void StudioApp::set_entity_parent(uint32_t child_id, uint32_t new_parent_id) {
    if (child_id == 0 || child_id == new_parent_id) return;
    Entity* child = m_scene.find_entity(child_id);
    if (!child) return;

    if (new_parent_id != 0) {
        if (m_scene.is_descendant_of(new_parent_id, child_id)) {
            m_asset_message = "Cannot parent an object to its own descendant.";
            m_asset_message_pending = true;
            return;
        }
    }

    if (child->parent_id == new_parent_id) return;

    if (!ReparentEntityPreservingWorld(m_scene, child_id, new_parent_id)) {
        m_asset_message = "Cannot preserve this object's world transform under that parent. Check for zero or rotated non-uniform scale.";
        m_asset_message_pending = true;
    }
}

void StudioApp::group_selected_entities() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    std::vector<uint32_t> targets;
    for (uint32_t id : m_selected_entity_ids) {
        bool has_ancestor = false;
        for (uint32_t other : m_selected_entity_ids) {
            if (id != other && m_scene.is_descendant_of(id, other)) {
                has_ancestor = true;
                break;
            }
        }
        if (!has_ancestor) targets.push_back(id);
    }
    if (targets.empty()) return;

    float center[3] = { 0.0f, 0.0f, 0.0f };
    for (uint32_t id : targets) {
        Entity* e = m_scene.find_entity(id);
        if (!e) continue;
        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *e, wpos, wrot, wscale);
        center[0] += wpos[0];
        center[1] += wpos[1];
        center[2] += wpos[2];
    }
    center[0] /= targets.size();
    center[1] /= targets.size();
    center[2] /= targets.size();

    Entity group;
    group.id = m_scene.next_entity_id++;
    group.name = "Group_" + std::to_string(group.id);
    group.is_group = true;
    group.mesh_id = 0;
    group.visible = true;
    group.position[0] = center[0];
    group.position[1] = center[1];
    group.position[2] = center[2];

    m_scene.entities.push_back(group);

    for (uint32_t id : targets) {
        set_entity_parent(id, group.id);
    }

    m_selected_entity_ids = { group.id };
    m_selected_entity_id = static_cast<int32_t>(group.id);
}

void StudioApp::unparent_selected_entities() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    for (uint32_t id : m_selected_entity_ids) {
        set_entity_parent(id, 0);
    }
}

void StudioApp::snap_selected_to_surface() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    for (uint32_t id : m_selected_entity_ids) {
        Entity* ent = m_scene.find_entity(id);
        if (!ent || ent->is_group) continue;

        float wpos[3], wrot[3], wscale[3], wmat[16];
        ComputeWorldTransform(m_scene, *ent, wpos, wrot, wscale, wmat);

        float surface_y = 0.0f;
        if (m_scene.has_terrain) {
            surface_y = static_cast<float>(EngineContext::instance().terrain_get_height(wpos[0], wpos[2]));
        }

        float bottom_offset = ent->is_point_light ? 0.0f : (wscale[1] * 0.5f);
        wmat[13] = surface_y + bottom_offset;

        ComputeLocalTransform(m_scene, ent->parent_id, wmat, ent->position, ent->rotation, ent->scale);
    }
}

void StudioApp::align_selected_entities(int axis, int mode) {
    if (m_selected_entity_ids.size() < 2) return;
    if (axis < 0 || axis > 2) return;

    std::vector<std::pair<float, Entity*>> items;
    for (uint32_t id : m_selected_entity_ids) {
        Entity* e = m_scene.find_entity(id);
        if (!e) continue;
        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *e, wpos, wrot, wscale);
        items.push_back({ wpos[axis], e });
    }
    if (items.size() < 2) return;

    if (mode == 3) {
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        float min_val = items.front().first;
        float max_val = items.back().first;
        float step = (max_val - min_val) / static_cast<float>(items.size() - 1);
        for (size_t i = 1; i + 1 < items.size(); ++i) {
            Entity* ent = items[i].second;
            float target_val = min_val + step * static_cast<float>(i);
            float wpos[3], wrot[3], wscale[3], wmat[16];
            ComputeWorldTransform(m_scene, *ent, wpos, wrot, wscale, wmat);
            wmat[12 + axis] = target_val;
            ComputeLocalTransform(m_scene, ent->parent_id, wmat, ent->position, ent->rotation, ent->scale);
        }
        return;
    }

    float target_val = items.front().first;
    if (mode == 0) {
        for (const auto& item : items) target_val = std::min(target_val, item.first);
    } else if (mode == 1) {
        float sum = 0.0f;
        for (const auto& item : items) sum += item.first;
        target_val = sum / items.size();
    } else if (mode == 2) {
        for (const auto& item : items) target_val = std::max(target_val, item.first);
    }

    for (const auto& item : items) {
        Entity* ent = item.second;
        float wpos[3], wrot[3], wscale[3], wmat[16];
        ComputeWorldTransform(m_scene, *ent, wpos, wrot, wscale, wmat);
        wmat[12 + axis] = target_val;
        ComputeLocalTransform(m_scene, ent->parent_id, wmat, ent->position, ent->rotation, ent->scale);
    }
}

} // namespace GM3D::Studio
