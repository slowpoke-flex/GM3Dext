#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_set>

#include "SceneData.h"
#include "SceneHistory.h"
#include "ProjectData.h"
#include "TerrainBrush.h"
#include "TransformMath.h"
#include "core/SharedMemory.h"

using Microsoft::WRL::ComPtr;

namespace GM3D::Studio {

enum class GizmoOperation {
    Translate = 0,
    Rotate = 1,
    Scale = 2
};

enum class GizmoMode {
    Local = 0,
    World = 1
};

class StudioApp {
public:
    StudioApp();
    ~StudioApp();

    bool initialize(HINSTANCE hInstance, int nCmdShow);
    int run();
    void shutdown();

    LRESULT handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void init_imgui();
    void setup_style();
    void update(float delta_time);
    void render();

    // Viewport Render Target management
    void resize_viewport_texture(uint32_t width, uint32_t height);

    // Camera controller
    void update_camera(float delta_time);

    // UI Panels
    void draw_dockspace();
    void draw_main_menu();
    void draw_viewport_panel();
    void draw_outliner_panel();
    void draw_outliner_entity_node(Entity& e, const std::string& filter, int& pending_action);
    void draw_inspector_panel();
    void draw_environment_panel();
    void draw_status_bar();
    bool draw_light_markers(float min_x, float min_y, float width, float height);
    void draw_assets_tab();
    void draw_material_editor_panel();

    // 3D Selection & Mouse Picking
    void pick_entity_at_mouse(float mouseX, float mouseY, float viewportW, float viewportH);
    void draw_selection_bounds(const Entity& entity, const float* view, const float* proj, float minX, float minY, float sizeX, float sizeY);
    void draw_collider_bounds(const Entity& entity, const float* view, const float* proj, float minX, float minY, float sizeX, float sizeY);

    // GameMaker LTS Scene & Asset Exporter
    void export_gamemaker_dialog();
    bool export_gamemaker_package(const std::string& export_dir, std::string& out_summary);

    // Multi-Selection and Hierarchy
    bool is_entity_selected(uint32_t id) const;
    void select_entity(uint32_t id, bool multi = false, bool range = false);
    void deselect_all();
    void set_entity_parent(uint32_t child_id, uint32_t new_parent_id);
    void group_selected_entities();
    void unparent_selected_entities();
    void align_selected_entities(int axis, int mode); // axis: 0=X, 1=Y, 2=Z; mode: 0=Min, 1=Center, 2=Max, 3=Distribute
    void snap_selected_to_surface();
    void focus_selected();
    void auto_relink_missing_assets();

    // Physics Simulation Mode in Editor
    void start_simulation();
    void stop_simulation();

    // Scene Actions
    void new_scene();
    void open_scene_dialog();
    bool load_scene(const std::string& path);
    void new_project_dialog();
    void open_project_dialog();
    bool save_project_registry();
    bool reload_asset(size_t index);
    void relink_asset(size_t index);
    void update_assets(float delta_time);
    bool save_scene();
    bool save_scene_as_dialog();
    bool confirm_scene_replacement();
    void request_close();
    void apply_history(bool redo);
    void import_model_dialog(bool replace_selected = false);
    void import_texture_dialog();
    uint16_t load_asset(const std::string& path, bool is_model);
    bool add_model_asset(const std::string& path, bool replace_selected);
    bool apply_texture_asset(const std::string& path);
    void process_dropped_files();
    Entity* selected_entity();
    void add_entity(uint16_t mesh_id, const std::string& name);
    void add_point_light();
    void delete_selected_entity();
    void duplicate_selected_entity();

    // Sync Scene <-> Engine
    void sync_scene_to_engine();
    void sync_terrain_layers();
    void rebuild_terrain();
    void import_heightmap_dialog();
    void export_heightmap_dialog();
    void update_terrain_brush(float min_x, float min_y, float width, float height);
    void refresh_terrain_albedo();

private:
    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;
    uint32_t m_window_width = 1600;
    uint32_t m_window_height = 900;
    bool m_running = false;
    bool m_imgui_initialized = false;

    // Viewport D3D11 Texture for ImGui
    ComPtr<ID3D11Texture2D>          m_viewport_tex;
    ComPtr<ID3D11RenderTargetView>   m_viewport_rtv;
    ComPtr<ID3D11ShaderResourceView> m_viewport_srv;
    uint32_t m_viewport_width = 1280;
    uint32_t m_viewport_height = 720;
    bool m_viewport_focused = false;
    bool m_viewport_hovered = false;

    // Material Preview Texture
    ComPtr<ID3D11Texture2D>          m_mat_preview_tex;
    ComPtr<ID3D11ShaderResourceView> m_mat_preview_srv;
    uint32_t m_mat_preview_last_id = UINT32_MAX;
    uint32_t m_mat_preview_hash = 0;
    void update_material_preview_texture(const Material& mat);

    // Scene & Selection
    Scene m_scene;
    SceneHistory m_history;
    int32_t m_selected_entity_id = -1;
    std::vector<uint32_t> m_selected_entity_ids;
    bool m_request_reset_layout = false;
    bool m_simulating = false;
    Scene m_simulation_backup;
    bool m_simulation_backup_valid = false;
    std::vector<uint32_t> m_simulation_bodies;
    float m_sim_backup_cam_pos[3] = {};
    float m_sim_backup_cam_target[3] = {};
    float m_sim_backup_cam_yaw = 0.0f;
    float m_sim_backup_cam_pitch = 0.0f;
    float m_sim_backup_cam_orbit_dist = 22.0f;

    // Gizmo State
    GizmoOperation m_gizmo_operation = GizmoOperation::Translate;
    GizmoMode m_gizmo_mode = GizmoMode::World;
    bool m_gizmo_snap = false;
    bool m_show_collider_gizmos = true;
    float m_snap_translation[3] = { 1.0f, 1.0f, 1.0f };
    float m_snap_rotation = 15.0f;
    float m_snap_scale = 0.5f;

    bool m_terrain_brush_enabled = false;
    bool m_select_terrain_tab = false;
    bool m_terrain_stroke = false;
    bool m_terrain_has_previous_hit = false;
    float m_terrain_previous_hit[2] = {};
    bool m_terrain_albedo_dirty = false;
    float m_terrain_albedo_bounds[4] = {};
    float m_terrain_albedo_elapsed = 0.0f;
    TerrainBrushMode m_terrain_brush_mode = TerrainBrushMode::Raise;
    int m_terrain_paint_layer = 0; // 0..3 = Layer 0..3, -1 = Erase (Auto-Biome)
    float m_terrain_brush_radius = 8.0f;
    float m_terrain_brush_strength = 4.0f;
    float m_terrain_flatten_height = 0.0f;
    float m_grass_brush_density = 15.0f;
    bool m_grass_erase_mode = false;
    float m_grass_brush_tint[3] = { 0.22f, 0.38f, 0.15f };
    int m_terrain_grid_input = 128;
    int m_terrain_seed_input = 1337;
    float m_terrain_height_input = 32.0f;
    float m_terrain_world_input = 240.0f;

    struct AssetEntry {
        std::string path; uint16_t id; bool is_model;
        std::filesystem::file_time_type modified{};
        bool timestamp_known = false;
    };
    std::vector<AssetEntry> m_assets;
    ProjectData m_project;
    float m_asset_poll_timer = 0.0f;
    std::vector<std::wstring> m_dropped_files;
    std::string m_asset_message;
    bool m_asset_message_pending = false;
    int m_asset_filter_category = 0; // 0 = All, 1 = Models, 2 = Textures, 3 = Missing

    // Editor Camera
    float m_cam_pos[3] = { 0.0f, 12.0f, -22.0f };
    float m_cam_yaw = 0.0f;     // in degrees
    float m_cam_pitch = 24.0f;  // in degrees
    float m_cam_speed = 18.0f;
    float m_cam_target[3] = { 0.0f, 1.4f, 0.0f };
    float m_cam_orbit_dist = 22.0f;
    float m_cam_velocity[3] = { 0.0f, 0.0f, 0.0f };
    float m_cam_zoom_velocity = 0.0f;

    // Window Panels Visibility
    bool m_show_viewport = true;
    bool m_show_hierarchy = true;
    bool m_show_inspector = true;
    bool m_show_environment = true;
    bool m_show_status_bar = true;
    bool m_show_materials = false;
    uint32_t m_selected_material_id = 0;
    std::string m_material_diag_title;
    std::string m_material_diag_report;
    bool m_show_material_diag = false;
    float m_delta_time = 0.016f;

    // Shared Memory buffer backing EngineContext
    std::vector<uint8_t> m_shared_buffer;
};

} // namespace GM3D::Studio
