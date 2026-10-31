// Asset browser and the import / relink plumbing behind it.
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

bool StudioApp::save_project_registry() {
    if (m_project.filepath.empty()) return true;
    ProjectData data;
    data.filepath = m_project.filepath;
    data.last_scene = m_project.last_scene;
    for (const auto& asset : m_assets) data.assets.push_back({asset.path, asset.is_model});
    std::string error;
    if (!ProjectSerializer::save(data, error)) {
        m_asset_message = "Could not save the asset library: " + error;
        m_asset_message_pending = true;
        return false;
    }
    m_project = std::move(data);
    return true;
}

uint16_t StudioApp::load_asset(const std::string& path, bool is_model) {
    if (path.empty()) return 0;
    try {
        const auto absolute = path_utf8(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());
        auto it = std::find_if(m_assets.begin(), m_assets.end(), [&](const AssetEntry& item) {
            return item.is_model == is_model && _stricmp(item.path.c_str(), absolute.c_str()) == 0;
        });
        if (it != m_assets.end() && it->id) return it->id;
        const auto id = static_cast<uint16_t>(is_model ? EngineContext::instance().load_mesh_model(absolute.c_str())
                                                       : EngineContext::instance().load_texture(absolute.c_str()));
        if (!id) return 0;
        if (it != m_assets.end()) it->id = id;
        else {
            m_assets.push_back({absolute, id, is_model});
            it = m_assets.end() - 1;
            save_project_registry();
        }
        std::error_code ec;
        it->modified = std::filesystem::last_write_time(std::filesystem::u8path(absolute), ec);
        it->timestamp_known = !ec;
        return id;
    } catch (const std::exception&) { return 0; }
}

bool StudioApp::reload_asset(size_t index) {
    if (index >= m_assets.size()) return false;
    auto& asset = m_assets[index];
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return false;
    const uint16_t incoming = static_cast<uint16_t>(asset.is_model
        ? EngineContext::instance().load_mesh_model(asset.path.c_str())
        : EngineContext::instance().load_texture(asset.path.c_str()));
    if (!incoming) return false;
    if (!asset.id) asset.id = incoming;
    else if (!(asset.is_model ? rw->get_mesh_manager().replace_mesh(asset.id, incoming)
                               : rw->get_texture_manager().replace_texture(asset.id, incoming))) return false;
    std::error_code ec;
    asset.modified = std::filesystem::last_write_time(std::filesystem::u8path(asset.path), ec);
    asset.timestamp_known = !ec;
    return true;
}

void StudioApp::relink_asset(size_t index) {
    if (index >= m_assets.size() || m_simulating) return;
    const AssetEntry old = m_assets[index];
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = old.is_model
        ? L"3D Models (*.glb;*.gltf;*.obj)\0*.glb;*.gltf;*.obj\0\0"
        : L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    const std::string replacement = path_utf8(std::filesystem::absolute(std::filesystem::path(filename)).lexically_normal());
    const uint16_t id = load_asset(replacement, old.is_model);
    if (!id) { m_asset_message = "Could not load replacement: " + replacement; m_asset_message_pending = true; return; }
    for (auto& entity : m_scene.entities) {
        std::string& path = old.is_model ? entity.model_path : entity.texture_path;
        if (_stricmp(path.c_str(), old.path.c_str()) == 0) {
            path = replacement;
            if (old.is_model) entity.mesh_id = id; else entity.texture_id = id;
        }
    }
    if (!old.is_model) for (auto& material : m_scene.materials) {
        if (_stricmp(material.albedo_path.c_str(), old.path.c_str()) == 0) {
            material.albedo_path = replacement; material.albedo_tex_id = id;
        }
        if (_stricmp(material.normal_path.c_str(), old.path.c_str()) == 0) {
            material.normal_path = replacement; material.normal_tex_id = id;
        }
    }
    if (_stricmp(old.path.c_str(), replacement.c_str()) != 0) {
        const auto it = std::find_if(m_assets.begin(), m_assets.end(), [&](const AssetEntry& entry) {
            return entry.is_model == old.is_model && _stricmp(entry.path.c_str(), old.path.c_str()) == 0;
        });
        if (it != m_assets.end()) m_assets.erase(it);
        save_project_registry();
    }
}

void StudioApp::update_assets(float delta_time) {
    m_asset_poll_timer += delta_time;
    if (m_asset_poll_timer < 1.0f) return;
    m_asset_poll_timer = 0.0f;
    for (size_t i = 0; i < m_assets.size(); ++i) {
        auto& asset = m_assets[i];
        if (!asset.id || !asset.timestamp_known) continue;
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(std::filesystem::u8path(asset.path), ec);
        if (!ec && modified != asset.modified && !reload_asset(i)) {
            // Do not retry a partially written or invalid file every frame.
            asset.modified = modified;
            m_asset_message = "Could not reload modified asset; the previous version remains active: " + asset.path;
            m_asset_message_pending = true;
        }
    }
}

bool StudioApp::add_model_asset(const std::string& path, bool replace_selected) {
    if (m_simulating) return false;
    const uint16_t id = load_asset(path, true);
    if (!id) return false;
    Entity* target = replace_selected ? selected_entity() : nullptr;
    if (replace_selected && !target) return false;
    if (!target) {
        add_entity(id, path_utf8(std::filesystem::u8path(path).stem()));
        target = selected_entity();
    }
    if (!target) return false;
    target->mesh_id = id;
    target->model_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());

    // Extract glTF / GLB materials and diagnostic limitations report
    auto ext = std::filesystem::u8path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".glb" || ext == ".gltf") {
        std::vector<GLTFMaterialInfo> extracted_materials;
        std::string report;
        if (MeshManager::extract_gltf_materials(path, extracted_materials, report)) {
            std::filesystem::path model_dir = std::filesystem::u8path(path).parent_path();
            uint32_t first_assigned_mat_id = 0;

            for (const auto& gltfMat : extracted_materials) {
                Material newMat{};
                newMat.id = m_scene.allocate_material_id();
                if (!newMat.id) { report += "\nSome materials could not be imported: no material slots remain.\n"; break; }
                newMat.name = gltfMat.name.empty() ? ("Material_" + std::to_string(newMat.id)) : gltfMat.name;
                newMat.is_builtin_preset = false;
                newMat.base_color[0] = gltfMat.base_color[0];
                newMat.base_color[1] = gltfMat.base_color[1];
                newMat.base_color[2] = gltfMat.base_color[2];
                newMat.base_color[3] = gltfMat.base_color[3];
                newMat.roughness = gltfMat.roughness;
                newMat.metallic = gltfMat.metallic;
                newMat.emissive = gltfMat.emissive;
                newMat.emissive_color[0] = gltfMat.emissive_color[0];
                newMat.emissive_color[1] = gltfMat.emissive_color[1];
                newMat.emissive_color[2] = gltfMat.emissive_color[2];
                newMat.alpha_mode = gltfMat.alpha_mode;
                newMat.alpha_cutoff = gltfMat.alpha_cutoff;

                if (!gltfMat.albedo_uri.empty()) {
                    std::filesystem::path tex_path = model_dir / std::filesystem::u8path(gltfMat.albedo_uri);
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(tex_path, ec)) {
                        std::string tex_path_str = path_utf8(tex_path);
                        uint16_t tid = load_asset(tex_path_str, false);
                        if (tid) {
                            newMat.albedo_tex_id = tid;
                            newMat.albedo_path = path_utf8(std::filesystem::absolute(tex_path).lexically_normal());
                        }
                    }
                }

                if (!gltfMat.normal_uri.empty()) {
                    std::filesystem::path nrm_path = model_dir / std::filesystem::u8path(gltfMat.normal_uri);
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(nrm_path, ec)) {
                        std::string nrm_path_str = path_utf8(nrm_path);
                        uint16_t tid = load_asset(nrm_path_str, false);
                        if (tid) {
                            newMat.normal_tex_id = tid;
                            newMat.normal_path = path_utf8(std::filesystem::absolute(nrm_path).lexically_normal());
                        }
                    }
                }

                m_scene.materials.push_back(newMat);
                if (first_assigned_mat_id == 0) first_assigned_mat_id = newMat.id;
            }

            if (first_assigned_mat_id != 0) {
                target->material_id = static_cast<uint16_t>(first_assigned_mat_id);
                m_selected_material_id = first_assigned_mat_id;
            }

            if (!report.empty()) {
                m_material_diag_title = "glTF Material Import Report: " + path_utf8(std::filesystem::u8path(path).filename());
                m_material_diag_report = report;
                m_show_material_diag = true;
            }
        }
    }

    return true;
}

bool StudioApp::apply_texture_asset(const std::string& path) {
    if (m_simulating) return false;
    Entity* target = selected_entity();
    if (!target || target->editor_grid || target->is_point_light) return false;
    const uint16_t id = load_asset(path, false);
    if (!id) return false;
    target->texture_id = id;
    target->texture_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());
    return true;
}

void StudioApp::process_dropped_files() {
    if (m_dropped_files.empty()) return;
    auto paths = std::move(m_dropped_files);
    m_dropped_files.clear();
    std::string failures;
    for (const auto& wide_path : paths) {
        const std::string path = to_utf8(wide_path);
        auto ext = std::filesystem::path(wide_path).extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        bool ok = false;
        if (!m_simulating && (ext == L".glb" || ext == L".gltf" || ext == L".obj")) ok = add_model_asset(path, false);
        else if (!m_simulating && (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".tif" || ext == L".tiff" || ext == L".gif")) ok = apply_texture_asset(path);
        if (!ok) failures += path + "\n";
    }
    if (!failures.empty()) {
        m_asset_message = "Could not import these files. Stop simulation and select an object before dropping a texture. Supported models: OBJ, GLB, glTF; images: PNG, JPEG, BMP, TIFF, GIF.\n\n" + failures;
        m_asset_message_pending = true;
    }
}

void StudioApp::import_model_dialog(bool replace_selected) {
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"3D Models (*.glb;*.gltf;*.obj)\0*.glb;*.gltf;*.obj\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn) && !add_model_asset(to_utf8(filename), replace_selected)) {
        m_asset_message = "Could not load model: " + to_utf8(filename);
        m_asset_message_pending = true;
    }
}

void StudioApp::import_texture_dialog() {
    if (!selected_entity()) {
        m_asset_message = "Select an object before assigning a texture.";
        m_asset_message_pending = true;
        return;
    }
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn) && !apply_texture_asset(to_utf8(filename))) {
        m_asset_message = "Could not apply texture: " + to_utf8(filename);
        m_asset_message_pending = true;
    }
}

void StudioApp::draw_assets_tab() {
    ImGui::TextUnformatted("ASSET LIBRARY");
    if (m_project.filepath.empty()) ImGui::TextDisabled("No project open: imports are session-only");
    else ImGui::TextWrapped("Project: %s", path_utf8(std::filesystem::u8path(m_project.filepath).filename()).c_str());
    ImGui::TextWrapped("Drop OBJ, GLB or glTF models anywhere in the window. Drop an image to texture the selected object.");
    ImGui::BeginDisabled(m_simulating);
    if (ImGui::Button("Import model...", ImVec2(-1, 30))) import_model_dialog();
    HelpTooltip("Import an OBJ, GLB, or glTF file and add it to the scene.");
    if (ImGui::Button("Assign texture...", ImVec2(-1, 30))) import_texture_dialog();
    HelpTooltip("Select an image and apply it to the selected object.");
    ImGui::EndDisabled();

    // Check missing count and totals
    size_t missing_count = 0;
    size_t model_count = 0;
    size_t texture_count = 0;
    for (const auto& a : m_assets) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(a.path), ec)) ++missing_count;
        if (a.is_model) ++model_count; else ++texture_count;
    }

    if (missing_count > 0) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        ImGui::BeginChild("MissingAlert", ImVec2(0, 58), true);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Warning: %zu missing asset(s) detected!", missing_count);
        if (ImGui::Button("Auto-Relink All Missing Assets", ImVec2(-1, 24))) {
            auto_relink_missing_assets();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Category filter buttons
    auto cat_btn = [this](const char* label, int cat) {
        if (m_asset_filter_category == cat) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.7f, 1.0f));
        if (ImGui::Button(label)) m_asset_filter_category = cat;
        if (m_asset_filter_category == cat) ImGui::PopStyleColor();
    };
    std::string all_label = "All (" + std::to_string(m_assets.size()) + ")";
    std::string mod_label = "Models (" + std::to_string(model_count) + ")";
    std::string tex_label = "Textures (" + std::to_string(texture_count) + ")";
    std::string mis_label = "Missing (" + std::to_string(missing_count) + ")";

    cat_btn(all_label.c_str(), 0); ImGui::SameLine();
    cat_btn(mod_label.c_str(), 1); ImGui::SameLine();
    cat_btn(tex_label.c_str(), 2); ImGui::SameLine();
    if (missing_count > 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
    cat_btn(mis_label.c_str(), 3);
    if (missing_count > 0) ImGui::PopStyleColor();

    static char search[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##AssetSearch", "Search assets...", search, sizeof(search));
    HelpTooltip("Filter project resources by their path or filename.");
    std::string filter = search;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (m_assets.empty()) ImGui::TextDisabled("Imported files appear here.");
    size_t relink_index = static_cast<size_t>(-1);

    for (size_t i = 0; i < m_assets.size(); ++i) {
        const auto& asset = m_assets[i];
        std::error_code asset_error;
        const bool missing = !std::filesystem::is_regular_file(std::filesystem::u8path(asset.path), asset_error);

        if (m_asset_filter_category == 1 && !asset.is_model) continue;
        if (m_asset_filter_category == 2 && asset.is_model) continue;
        if (m_asset_filter_category == 3 && !missing) continue;

        std::string searchable = asset.path;
        std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!filter.empty() && searchable.find(filter) == std::string::npos) continue;

        ImGui::PushID(static_cast<int>(i));
        const std::string name = path_utf8(std::filesystem::u8path(asset.path).filename());

        if (missing) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[MISSING %s]", asset.is_model ? "MODEL" : "TEXTURE");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.75f, 0.95f, 1.0f), "[%s]", asset.is_model ? "3D MODEL" : "TEXTURE");
        }

        // Thumbnail / Visual representation
        if (!asset.is_model && asset.id && !missing) {
            auto* rw = EngineContext::instance().get_render_world();
            if (rw) {
                ImTextureID srv = reinterpret_cast<ImTextureID>(rw->get_texture_manager().get_srv(asset.id));
                ImGui::Image(srv, ImVec2(52, 52), ImVec2(0,0), ImVec2(1,1), ImVec4(1,1,1,1), ImVec4(0.3f, 0.35f, 0.4f, 0.8f));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Image(srv, ImVec2(140, 140));
                    ImGui::Text("%s", name.c_str());
                    ImGui::EndTooltip();
                }
            }
        } else if (asset.is_model && !missing) {
            std::filesystem::path p = std::filesystem::u8path(asset.path);
            std::string ext = path_utf8(p.extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.24f, 0.32f, 1.0f));
            ImGui::Button((std::string("[3D] ") + ext).c_str(), ImVec2(52, 52));
            ImGui::PopStyleColor();
        }

        ImGui::TextWrapped("%s", name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.path.c_str());

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GM3D_ASSET", &i, sizeof(i));
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::BeginDisabled(m_simulating || missing || (!asset.is_model && !selected_entity()));
        if (ImGui::Button(asset.is_model ? "Add to scene" : "Apply to selected", ImVec2(-1, 0))) {
            const std::string path = asset.path;
            if (!(asset.is_model ? add_model_asset(path, false) : apply_texture_asset(path))) {
                m_asset_message = "Could not use asset: " + path;
                m_asset_message_pending = true;
            }
        }
        ImGui::EndDisabled();

        if (!missing && ImGui::Button(asset.id ? "Reload" : (asset.is_model ? "Load asset" : "Load preview"), ImVec2(-1, 0))) {
            if (!reload_asset(i)) { m_asset_message = "Could not load asset: " + asset.path; m_asset_message_pending = true; }
        }

        ImGui::BeginDisabled(m_simulating);
        if (ImGui::Button("Relink...", ImVec2(-1, 0))) relink_index = i;
        HelpTooltip("Choose a replacement file and update matching objects in the current scene.");
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::PopID();
    }
    if (relink_index != static_cast<size_t>(-1)) relink_asset(relink_index);
}

void StudioApp::auto_relink_missing_assets() {
    if (m_project.filepath.empty() && m_scene.filepath.empty()) {
        m_asset_message = "Save or open a project/scene first so the editor knows where to search for missing files.";
        m_asset_message_pending = true;
        return;
    }

    std::vector<std::filesystem::path> search_dirs;
    std::error_code ec;
    if (!m_project.filepath.empty()) {
        auto project_dir = std::filesystem::u8path(m_project.filepath).parent_path();
        if (std::filesystem::is_directory(project_dir, ec)) {
            search_dirs.push_back(project_dir);
            if (std::filesystem::is_directory(project_dir / "assets", ec)) search_dirs.push_back(project_dir / "assets");
            if (std::filesystem::is_directory(project_dir / "models", ec)) search_dirs.push_back(project_dir / "models");
            if (std::filesystem::is_directory(project_dir / "textures", ec)) search_dirs.push_back(project_dir / "textures");
        }
    }
    if (!m_scene.filepath.empty()) {
        auto scene_dir = std::filesystem::u8path(m_scene.filepath).parent_path();
        if (std::filesystem::is_directory(scene_dir, ec)) {
            search_dirs.push_back(scene_dir);
            if (std::filesystem::is_directory(scene_dir / "assets", ec)) search_dirs.push_back(scene_dir / "assets");
        }
    }

    size_t relinked_count = 0;
    for (auto& asset : m_assets) {
        if (std::filesystem::is_regular_file(std::filesystem::u8path(asset.path), ec)) continue;

        std::filesystem::path original_filename = std::filesystem::u8path(asset.path).filename();
        std::filesystem::path found_path;

        for (const auto& dir : search_dirs) {
            auto candidate = dir / original_filename;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                found_path = candidate;
                break;
            }
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file() && entry.path().filename() == original_filename) {
                        found_path = entry.path();
                        break;
                    }
                }
            } catch (...) {}
            if (!found_path.empty()) break;
        }

        if (!found_path.empty()) {
            std::string new_path = path_utf8(std::filesystem::absolute(found_path).lexically_normal());
            std::string old_path = asset.path;
            asset.path = new_path;

            uint16_t id = load_asset(new_path, asset.is_model);
            if (id) {
                asset.id = id;
                for (auto& e : m_scene.entities) {
                    if (asset.is_model && _stricmp(e.model_path.c_str(), old_path.c_str()) == 0) {
                        e.model_path = new_path;
                        e.mesh_id = id;
                    }
                    if (!asset.is_model && _stricmp(e.texture_path.c_str(), old_path.c_str()) == 0) {
                        e.texture_path = new_path;
                        e.texture_id = id;
                    }
                }
                if (!asset.is_model) for (auto& material : m_scene.materials) {
                    if (_stricmp(material.albedo_path.c_str(), old_path.c_str()) == 0) {
                        material.albedo_path = new_path; material.albedo_tex_id = id;
                    }
                    if (_stricmp(material.normal_path.c_str(), old_path.c_str()) == 0) {
                        material.normal_path = new_path; material.normal_tex_id = id;
                    }
                }
                ++relinked_count;
            }
        }
    }

    if (relinked_count > 0) {
        save_project_registry();
        m_asset_message = "Successfully relinked " + std::to_string(relinked_count) + " missing asset(s)!";
        m_asset_message_pending = true;
    } else {
        m_asset_message = "Could not find matching replacement files in the project or scene directory.";
        m_asset_message_pending = true;
    }
}

} // namespace GM3D::Studio
