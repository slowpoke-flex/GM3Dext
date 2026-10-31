// Scene and project file handling: open, save, new, export and the file dialogs.
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

bool StudioApp::confirm_scene_replacement() {
    if (m_simulating) stop_simulation();
    m_history.observe(m_scene);
    if (!m_history.dirty(m_scene)) return true;
    const int choice = MessageBoxW(m_hwnd, L"Save changes to the current scene?", L"Unsaved changes",
                                   MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDYES) return save_scene();
    return choice == IDNO;
}

void StudioApp::new_scene() {
    if (m_simulating) {
        stop_simulation();
    }
    m_simulation_backup_valid = false;
    m_simulation_bodies.clear();

    // Destroy all existing physics bodies in Jolt
    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) {
        for (auto& e : m_scene.entities) {
            if (e.physics_body_id != 0) {
                pw->destroy_body(e.physics_body_id);
                e.physics_body_id = 0;
            }
        }
    }

    m_scene = Scene{};
    m_scene.name = "Untitled Scene";
    m_scene.filepath = "";
    deselect_all();
    m_selected_material_id = 0;
    m_cam_velocity[0] = m_cam_velocity[1] = m_cam_velocity[2] = 0.0f;
    m_cam_zoom_velocity = 0.0f;
    m_terrain_stroke = false;
    m_terrain_has_previous_hit = false;
    m_terrain_brush_enabled = false;
    m_terrain_paint_layer = 0;

    // 1. 3D Editor Infinite Ground Grid Field (Plane 240x240m with procedural grid shader)
    Entity grid_field;
    grid_field.id = m_scene.next_entity_id++;
    grid_field.name = "Grid Floor";
    grid_field.editor_grid = true;
    grid_field.mesh_id = 3; // Plane
    grid_field.material_id = 0; // Default (procedural grid)
    grid_field.texture_id = 0;
    grid_field.scale[0] = 240.0f; grid_field.scale[1] = 1.0f; grid_field.scale[2] = 240.0f;
    grid_field.position[0] = 0.0f; grid_field.position[1] = 0.0f; grid_field.position[2] = 0.0f;
    grid_field.cast_shadow = false;
    grid_field.physics_type = 1; // Static
    m_scene.entities.push_back(grid_field);

    // The default terrain flattens the central plaza to ~0.055 * height_scale metres, so
    // props have to sit on that height or they end up buried in the ground.
    const float plaza_ground = m_scene.has_terrain ? m_scene.terrain_height_scale * 0.055f : 0.0f;

    // 2. Central Reference Cube
    Entity cube;
    cube.id = m_scene.next_entity_id++;
    cube.name = "Central Cube";
    cube.mesh_id = 0; // Cube (ID = 0)
    cube.material_id = 2; // Gold Brass
    cube.position[0] = 0.0f; cube.position[1] = plaza_ground + 1.05f; cube.position[2] = 0.0f;
    cube.scale[0] = 2.0f; cube.scale[1] = 2.0f; cube.scale[2] = 2.0f;
    cube.cast_shadow = true;
    cube.physics_type = 2; // Dynamic
    m_scene.entities.push_back(cube);

    // 3. Decorative Sphere
    Entity sphere;
    sphere.id = m_scene.next_entity_id++;
    sphere.name = "Granite Boulder";
    sphere.mesh_id = 1; // Sphere (ID = 1)
    sphere.material_id = 8; // Weathered Granite
    sphere.position[0] = 4.0f; sphere.position[1] = plaza_ground + 1.2f; sphere.position[2] = 2.0f;
    sphere.scale[0] = 2.4f; sphere.scale[1] = 2.4f; sphere.scale[2] = 2.4f;
    sphere.cast_shadow = true;
    sphere.physics_type = 2; // Dynamic
    m_scene.entities.push_back(sphere);

    // Select the central cube by default
    select_entity(cube.id, false, false);

    // Reset camera position to view the grid and cube
    m_cam_pos[0] = 0.0f; m_cam_pos[1] = 6.0f; m_cam_pos[2] = -14.0f;
    m_cam_yaw = 0.0f;
    m_cam_pitch = 20.0f;
    m_cam_target[0] = 0.0f; m_cam_target[1] = 1.0f; m_cam_target[2] = 0.0f;

    rebuild_terrain();
    sync_scene_to_engine();
    m_history.reset(m_scene);
}

void StudioApp::import_heightmap_dialog() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Grayscale PFM heightmap (*.pfm)\0*.pfm\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    uint32_t size = 0; std::vector<float> heights; std::string error;
    if (!load_heightmap(filename, size, heights, error)) {
        MessageBoxA(m_hwnd, error.c_str(), "Heightmap import failed", MB_OK | MB_ICONERROR); return;
    }
    m_history.observe(m_scene);
    m_scene.terrain_grid_size = size;
    m_scene.terrain_heights = std::move(heights);
    rebuild_terrain();
    m_history.observe(m_scene);
}

void StudioApp::export_heightmap_dialog() {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw || !rw->get_terrain_system().is_generated()) {
        MessageBoxW(m_hwnd, L"Terrain is not available.", L"Heightmap export failed", MB_OK | MB_ICONERROR); return;
    }
    wchar_t filename[MAX_PATH] = L"terrain.pfm";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Grayscale PFM heightmap (*.pfm)\0*.pfm\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = L"pfm";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string error;
    const auto& terrain = rw->get_terrain_system();
    if (!save_heightmap(filename, terrain.get_config().grid_size, terrain.heights(), error))
        MessageBoxA(m_hwnd, error.c_str(), "Heightmap export failed", MB_OK | MB_ICONERROR);
}

bool StudioApp::export_gamemaker_package(const std::string& export_dir, std::string& out_summary) {
    if (auto* rw = EngineContext::instance().get_render_world()) {
        if (m_scene.has_terrain && rw->get_terrain_system().is_generated()) {
            m_scene.terrain_heights = rw->get_terrain_system().heights();
            m_scene.terrain_splatmap = rw->get_terrain_system().splatmap();
        }
    }
    return SceneSerializer::export_for_gamemaker(m_scene, export_dir, out_summary);
}

void StudioApp::export_gamemaker_dialog() {
    BROWSEINFOW browse{};
    browse.hwndOwner = m_hwnd;
    browse.lpszTitle = L"Choose a folder for the GameMaker scene, loader and assets.";
    browse.ulFlags = BIF_RETURNONLYFSDIRS;
    PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browse);
    if (!selected) return;
    wchar_t folder[MAX_PATH]{};
    const bool resolved = SHGetPathFromIDListW(selected, folder) != FALSE;
    CoTaskMemFree(selected);
    if (!resolved) {
        MessageBoxW(m_hwnd, L"Could not access the selected folder.", L"GameMaker Export", MB_OK | MB_ICONERROR);
        return;
    }
    const std::filesystem::path target_folder(folder);
    std::error_code ec;
    if ((std::filesystem::exists(target_folder / L"scene_gm3d.json", ec) ||
         std::filesystem::exists(target_folder / L"scr_gm3d_load_scene.gml", ec) ||
         (m_scene.has_terrain && std::filesystem::exists(target_folder / L"terrain.pfm", ec))) &&
        MessageBoxW(m_hwnd, L"This folder already contains exported scene, loader or terrain files. Replace them?",
                    L"GameMaker Export", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    std::string export_dir = path_utf8(target_folder);
    std::string summary;
    if (export_gamemaker_package(export_dir, summary)) {
        m_asset_message = summary;
        m_asset_message_pending = true;
    } else {
        MessageBoxA(m_hwnd, summary.c_str(), "GameMaker Export Failed", MB_OK | MB_ICONERROR);
    }
}

void StudioApp::open_scene_dialog() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) load_scene(to_utf8(filename));
}

bool StudioApp::load_scene(const std::string& path) {
    Scene loaded_scene;
    if (!SceneSerializer::load_from_file(loaded_scene, path)) {
        MessageBoxW(m_hwnd, L"Could not load the scene file.", L"Load Failed", MB_OK | MB_ICONERROR);
        return false;
    }

    std::string missing;
    for (auto& entity : loaded_scene.entities) {
        if (!entity.model_path.empty()) {
            entity.mesh_id = load_asset(entity.model_path, true);
            if (!entity.mesh_id) missing += "Model: " + entity.model_path + "\n";
        }
        if (!entity.texture_path.empty()) {
            entity.texture_id = load_asset(entity.texture_path, false);
            if (!entity.texture_id) missing += "Texture: " + entity.texture_path + "\n";
        }
    }
    for (auto& material : loaded_scene.materials) {
        if (!material.albedo_path.empty()) {
            material.albedo_tex_id = load_asset(material.albedo_path, false);
            if (!material.albedo_tex_id) missing += "Material albedo: " + material.albedo_path + "\n";
        }
        if (!material.normal_path.empty()) {
            material.normal_tex_id = load_asset(material.normal_path, false);
            if (!material.normal_tex_id) missing += "Material normal: " + material.normal_path + "\n";
        }
    }
    // Keep missing scene references visible in the asset browser so they can
    // be relinked even when they were never part of the project registry.
    bool registered_missing = false;
    auto register_missing = [&](const std::string& path, bool model, uint16_t id) {
        if (path.empty() || id != 0) return;
        const auto it = std::find_if(m_assets.begin(), m_assets.end(), [&](const AssetEntry& item) {
            return item.is_model == model && _stricmp(item.path.c_str(), path.c_str()) == 0;
        });
        if (it == m_assets.end()) { m_assets.push_back({path, 0, model}); registered_missing = true; }
    };
    for (const auto& entity : loaded_scene.entities) {
        register_missing(entity.model_path, true, entity.mesh_id);
        register_missing(entity.texture_path, false, entity.texture_id);
    }
    for (const auto& material : loaded_scene.materials) {
        register_missing(material.albedo_path, false, material.albedo_tex_id);
        register_missing(material.normal_path, false, material.normal_tex_id);
    }
    if (registered_missing) save_project_registry();
    if (!missing.empty()) {
        m_asset_message = "Some assets could not be loaded. Their source paths were kept; use the Assets panel to relink them, or replace maps in the Materials panel.\n\n" + missing;
        m_asset_message_pending = true;
    }
    if (m_simulating) stop_simulation();
    m_simulation_backup_valid = false;
    m_simulation_bodies.clear();
    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) {
        for (auto& e : m_scene.entities) {
            if (e.physics_body_id != 0) {
                pw->destroy_body(e.physics_body_id);
                e.physics_body_id = 0;
            }
        }
    }

    m_scene = std::move(loaded_scene);
    rebuild_terrain();
    deselect_all();
    m_selected_material_id = 0;
    m_cam_velocity[0] = m_cam_velocity[1] = m_cam_velocity[2] = 0.0f;
    m_cam_zoom_velocity = 0.0f;
    sync_scene_to_engine();
    m_history.reset(m_scene);

    for (const auto& e : m_scene.entities) {
        if (!e.editor_grid) {
            select_entity(e.id, false, false);
            focus_selected();
            break;
        }
    }
    std::cout << "Scene loaded successfully from: " << path << std::endl;
    if (!m_project.filepath.empty()) { m_project.last_scene = path; save_project_registry(); }
    return true;
}

void StudioApp::new_project_dialog() {
    if (!confirm_scene_replacement()) return;
    wchar_t filename[32768] = L"Project.gm3dproject";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Project (*.gm3dproject)\0*.gm3dproject\0\0";
    ofn.lpstrDefExt = L"gm3dproject"; ofn.lpstrFile = filename;
    ofn.nMaxFile = static_cast<DWORD>(std::size(filename)); ofn.Flags = OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    const std::filesystem::path target(filename);
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        m_asset_message = "A project file already exists at this location. Choose another name or open the existing project.";
        m_asset_message_pending = true; return;
    }
    ProjectData project; project.filepath = to_utf8(filename);
    std::string error;
    if (!ProjectSerializer::save(project, error)) {
        m_asset_message = "Could not create project: " + error; m_asset_message_pending = true; return;
    }
    std::filesystem::create_directories(target.parent_path() / L"Assets", ec);
    if (!ec) std::filesystem::create_directories(target.parent_path() / L"Scenes", ec);
    if (ec) { m_asset_message = "Project created, but Assets or Scenes folder could not be created: " + ec.message(); m_asset_message_pending = true; }
    m_project = std::move(project); m_assets.clear(); new_scene();
}

void StudioApp::open_project_dialog() {
    if (!confirm_scene_replacement()) return;
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Project (*.gm3dproject)\0*.gm3dproject\0\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    ProjectData project; std::string error;
    if (!ProjectSerializer::load(to_utf8(filename), project, error)) {
        m_asset_message = "Could not open project: " + error; m_asset_message_pending = true; return;
    }
    m_assets.clear();
    for (const auto& asset : project.assets) m_assets.push_back({asset.path, 0, asset.is_model});
    m_project = std::move(project);
    new_scene();
    if (!m_project.last_scene.empty()) {
        const std::string last_scene = m_project.last_scene;
        load_scene(last_scene);
    }
}

bool StudioApp::save_scene() {
    if (m_simulating) stop_simulation();
    m_history.observe(m_scene);
    if (m_scene.filepath.empty()) return save_scene_as_dialog();
    if (!SceneSerializer::save_to_file(m_scene, m_scene.filepath)) {
        MessageBoxW(m_hwnd, L"Could not save the scene file.", L"Save Failed", MB_OK | MB_ICONERROR);
        return false;
    }
    m_history.mark_saved(m_scene);
    return true;
}

bool StudioApp::save_scene_as_dialog() {
    if (m_simulating) stop_simulation();
    m_history.observe(m_scene);
    wchar_t filename[MAX_PATH] = L"scene.json";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    std::wstring scene_dir;
    if (!m_project.filepath.empty()) {
        auto folder = std::filesystem::u8path(m_project.filepath).parent_path() / L"Scenes";
        if (std::filesystem::is_directory(folder)) { scene_dir = folder.wstring(); ofn.lpstrInitialDir = scene_dir.c_str(); }
    }
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return false;
    const std::string path = to_utf8(filename);
    if (!SceneSerializer::save_to_file(m_scene, path)) {
        MessageBoxW(m_hwnd, L"Could not save the scene file.", L"Save Failed", MB_OK | MB_ICONERROR);
        return false;
    }
    m_scene.filepath = path;
    if (!m_project.filepath.empty()) { m_project.last_scene = path; save_project_registry(); }
    m_history.mark_saved(m_scene);
    return true;
}

} // namespace GM3D::Studio
