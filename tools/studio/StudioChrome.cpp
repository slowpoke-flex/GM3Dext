// Menu bar, dockspace layout and status bar.
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

void StudioApp::draw_dockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float status_bar_h = m_show_status_bar ? 28.0f : 0.0f;
    ImVec2 dockspace_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - status_bar_h);
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(dockspace_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("GM3D_DockSpace_Host", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("GM3D_DockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Initial Professional 3D Layout Setup (Blender / Unreal Style)
    // Defer initialization until the main window has received its full work size (>= 400px)
    static bool s_layout_initialized = false;
    if (!s_layout_initialized && dockspace_size.x >= 400.0f) {
        s_layout_initialized = true;
        m_request_reset_layout = true;
    }

    if (m_request_reset_layout) {
        m_request_reset_layout = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, nullptr, &dock_main);
        ImGuiID dock_right_down = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.52f, nullptr, &dock_right);
        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.24f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Viewport", dock_main);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_right);
        ImGui::DockBuilderDockWindow("Inspector", dock_right_down);
        ImGui::DockBuilderDockWindow("Materials", dock_right_down);
        ImGui::DockBuilderDockWindow("Environment & Lighting", dock_left);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

void StudioApp::draw_main_menu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) new_project_dialog();
            if (ImGui::MenuItem("Open Project...")) open_project_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("New Scene", "Ctrl+N") && confirm_scene_replacement()) new_scene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O") && confirm_scene_replacement()) open_scene_dialog();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) save_scene();
            if (ImGui::MenuItem("Save Scene As...")) save_scene_as_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Export for GameMaker LTS...", "Ctrl+E")) export_gamemaker_dialog();
            if (ImGui::MenuItem("Import 3D Model (.glb, .obj)...", "Ctrl+I")) import_model_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) request_close();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_simulating && m_history.can_undo())) apply_history(false);
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_simulating && m_history.can_redo())) apply_history(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) duplicate_selected_entity();
            if (ImGui::MenuItem("Delete", "Del", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) delete_selected_entity();
            ImGui::Separator();
            if (ImGui::MenuItem("Group Selected", "Ctrl+G", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) group_selected_entities();
            if (ImGui::MenuItem("Unparent Selected", "Ctrl+Shift+G", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) unparent_selected_entities();
            if (ImGui::MenuItem("Snap to Surface", "G", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) snap_selected_to_surface();
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                m_selected_entity_ids.clear();
                for (const auto& e : m_scene.entities) m_selected_entity_ids.push_back(e.id);
                if (!m_selected_entity_ids.empty()) m_selected_entity_id = static_cast<int32_t>(m_selected_entity_ids.back());
            }
            if (ImGui::MenuItem("Deselect All", "Escape", false, !m_selected_entity_ids.empty())) deselect_all();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Add Cube")) add_entity(0, "Cube");
            if (ImGui::MenuItem("Add Sphere")) add_entity(1, "Sphere");
            if (ImGui::MenuItem("Add Cylinder")) add_entity(2, "Cylinder");
            if (ImGui::MenuItem("Add Plane")) add_entity(3, "Plane");
            if (ImGui::MenuItem("Add Capsule")) add_entity(4, "Capsule");
            if (ImGui::MenuItem("Add Pine Tree")) add_entity(6, "PineTree");
            if (ImGui::MenuItem("Add Point Light")) add_point_light();
            ImGui::Separator();
            if (ImGui::MenuItem("Import 3D Model (.glb, .obj)...")) import_model_dialog();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Gizmo")) {
            if (ImGui::MenuItem("Translate", "W", m_gizmo_operation == GizmoOperation::Translate)) m_gizmo_operation = GizmoOperation::Translate;
            if (ImGui::MenuItem("Rotate", "E", m_gizmo_operation == GizmoOperation::Rotate)) m_gizmo_operation = GizmoOperation::Rotate;
            if (ImGui::MenuItem("Scale", "R", m_gizmo_operation == GizmoOperation::Scale)) m_gizmo_operation = GizmoOperation::Scale;
            ImGui::Separator();
            if (ImGui::MenuItem("Local Space", nullptr, m_gizmo_mode == GizmoMode::Local)) m_gizmo_mode = GizmoMode::Local;
            if (ImGui::MenuItem("World Space", nullptr, m_gizmo_mode == GizmoMode::World)) m_gizmo_mode = GizmoMode::World;
            ImGui::Separator();
            ImGui::MenuItem("Enable Snapping", nullptr, &m_gizmo_snap);
            ImGui::MenuItem("Show Collider Wireframes", nullptr, &m_show_collider_gizmos);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("3D Viewport", nullptr, &m_show_viewport);
            ImGui::MenuItem("Scene Hierarchy", nullptr, &m_show_hierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_show_inspector);
            ImGui::MenuItem("Materials", nullptr, &m_show_materials);
            ImGui::MenuItem("Environment & Lighting", nullptr, &m_show_environment);
            ImGui::MenuItem("Status Bar", nullptr, &m_show_status_bar);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout to Default")) {
                m_show_viewport = true;
                m_show_hierarchy = true;
                m_show_inspector = true;
                m_show_materials = true;
                m_show_environment = true;
                m_show_status_bar = true;
                m_request_reset_layout = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void StudioApp::draw_status_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float bar_height = 28.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - bar_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, bar_height));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 1.0f));

    if (ImGui::Begin("StatusBar", nullptr, flags)) {
        ImGuiIO& io = ImGui::GetIO();

        if (m_simulating) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.25f, 1.0f), "[SIMULATING JOLT PHYSICS]");
        } else {
            ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.45f, 1.0f), "[READY]");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::Text("Entities: %zu", m_scene.entities.size());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (m_selected_entity_id >= 0) {
            std::string selName = "None";
            for (const auto& e : m_scene.entities) {
                if (static_cast<int32_t>(e.id) == m_selected_entity_id) {
                    selName = e.name;
                    break;
                }
            }
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Selected: %s (ID: %d)", selName.c_str(), m_selected_entity_id);
        } else {
            ImGui::TextDisabled("No Selection (Click in Viewport to select)");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Cam: (%.1f, %.1f, %.1f)", m_cam_pos[0], m_cam_pos[1], m_cam_pos[2]);

        // Right aligned stats
        char right_text[192];
        auto* rw_status = EngineContext::instance().get_render_world();
        const char* gpu_name = rw_status ? rw_status->get_adapter_name() : nullptr;
        snprintf(right_text, sizeof(right_text), "FPS: %.1f (%.2f ms) | %s | DirectX 11 PBR",
                 io.Framerate, 1000.0f / (io.Framerate > 0 ? io.Framerate : 1.0f),
                 (gpu_name && gpu_name[0]) ? gpu_name : "Unknown GPU");
        float right_width = ImGui::CalcTextSize(right_text).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > right_width + 10.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - right_width - 15.0f);
            ImGui::TextColored(ImVec4(0.68f, 0.74f, 0.82f, 1.0f), "%s", right_text);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace GM3D::Studio
