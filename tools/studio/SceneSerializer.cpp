#include "SceneSerializer.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <utility>
#include <unordered_set>
#include "nlohmann/json.hpp"

using json = nlohmann::ordered_json;

namespace GM3D::Studio {
namespace {

std::string path_utf8(const std::filesystem::path& path) {
    auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string store_asset_path(const std::string& asset, const std::string& scene_file) {
    if (asset.empty()) return {};
    const auto absolute = std::filesystem::absolute(std::filesystem::u8path(asset)).lexically_normal();
    const auto base = std::filesystem::absolute(std::filesystem::u8path(scene_file)).parent_path();
    const auto relative = absolute.lexically_relative(base);
    return path_utf8(relative.empty() ? absolute : relative);
}

std::string resolve_asset_path(const std::string& asset, const std::string& scene_file) {
    if (asset.empty()) return {};
    auto path = std::filesystem::u8path(asset);
    if (path.is_relative()) path = std::filesystem::absolute(std::filesystem::u8path(scene_file)).parent_path() / path;
    return path_utf8(path.lexically_normal());
}

void read_vec3(const json& object, const char* key, float (&value)[3]) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() < 3) return;
    for (size_t i = 0; i < 3; ++i) value[i] = (*it)[i].get<float>();
}

} // namespace

bool SceneSerializer::save_to_file(const Scene& scene, const std::string& filepath) {
    try {
        json j;
        j["name"] = scene.name;
        j["version"] = 1;

        // Environment
        j["environment"]["sun"] = {
            {"pitch", scene.sun_pitch},
            {"yaw", scene.sun_yaw},
            {"color", {scene.sun_color[0], scene.sun_color[1], scene.sun_color[2]}},
            {"intensity", scene.sun_intensity}
        };

        j["environment"]["shadows"] = {
            {"enabled", scene.shadows_enabled}, {"strength", scene.shadow_strength},
            {"softness", scene.shadow_softness}, {"coverage", scene.shadow_coverage}
        };

        j["environment"]["ambient"] = {
            {"color", {scene.ambient_color[0], scene.ambient_color[1], scene.ambient_color[2]}},
            {"intensity", scene.ambient_intensity}
        };

        j["environment"]["sky_color"] = {scene.sky_color[0], scene.sky_color[1], scene.sky_color[2]};
        j["environment"]["fog"] = {
            {"start", scene.fog_start},
            {"end", scene.fog_end}
        };

        // Terrain
        j["terrain"] = {
            {"enabled", scene.has_terrain},
            {"grid_size", scene.terrain_grid_size},
            {"world_size", scene.terrain_world_size},
            {"height_scale", scene.terrain_height_scale},
            {"seed", scene.terrain_seed}
        };

        if (!scene.terrain_heights.empty()) j["terrain"]["heights"] = scene.terrain_heights;

        // Materials
        json materials_array = json::array();
        for (const auto& m : scene.materials) {
            json mat;
            mat["id"] = m.id;
            mat["name"] = m.name;
            mat["base_color"] = { m.base_color[0], m.base_color[1], m.base_color[2], m.base_color[3] };
            mat["roughness"] = m.roughness;
            mat["metallic"] = m.metallic;
            mat["emissive"] = m.emissive;
            mat["emissive_color"] = { m.emissive_color[0], m.emissive_color[1], m.emissive_color[2] };
            if (!m.albedo_path.empty()) mat["albedo_path"] = store_asset_path(m.albedo_path, filepath);
            if (!m.normal_path.empty()) mat["normal_path"] = store_asset_path(m.normal_path, filepath);
            mat["uv_tiling"] = { m.uv_tiling[0], m.uv_tiling[1] };
            mat["uv_offset"] = { m.uv_offset[0], m.uv_offset[1] };
            mat["alpha_mode"] = m.alpha_mode;
            mat["alpha_cutoff"] = m.alpha_cutoff;
            if (m.is_builtin_preset) {
                mat["is_builtin_preset"] = true;
                mat["preset_index"] = m.preset_index;
            }
            materials_array.push_back(mat);
        }
        j["materials"] = materials_array;

        // Entities
        json entities_array = json::array();
        for (const auto& e : scene.entities) {
            json ent;
            ent["id"] = e.id;
            ent["name"] = e.name;
            ent["tag"] = e.tag;
            if (e.parent_id != 0) ent["parent_id"] = e.parent_id;
            if (e.is_group) ent["is_group"] = true;
            if (e.editor_grid) ent["editor_grid"] = true;

            ent["transform"] = {
                {"position", {e.position[0], e.position[1], e.position[2]}},
                {"rotation", {e.rotation[0], e.rotation[1], e.rotation[2]}},
                {"scale", {e.scale[0], e.scale[1], e.scale[2]}}
            };

            ent["render"] = {
                {"mesh_id", e.mesh_id},
                {"material_id", e.material_id},
                {"texture_id", e.texture_id},
                {"cast_shadow", e.cast_shadow},
                {"visible", e.visible},
                {"param1", e.custom_param1},
                {"param2", e.custom_param2}
            };

            if (!e.model_path.empty()) ent["render"]["model_path"] = store_asset_path(e.model_path, filepath);
            if (!e.texture_path.empty()) ent["render"]["texture_path"] = store_asset_path(e.texture_path, filepath);

            if (e.is_point_light) {
                ent["light"] = {{"type", "point"}, {"color", {e.light_color[0], e.light_color[1], e.light_color[2]}},
                    {"intensity", e.light_intensity}, {"radius", e.light_radius}};
            }

            ent["physics"] = {
                {"type", e.physics_type},
                {"shape", e.collider_shape},
                {"mass", e.mass},
                {"friction", e.friction},
                {"restitution", e.restitution}
            };

            entities_array.push_back(ent);
        }
        j["entities"] = entities_array;

        const std::string contents = j.dump(4);
        const auto destination = std::filesystem::absolute(std::filesystem::u8path(filepath));
        wchar_t temporary[MAX_PATH]{};
        if (!GetTempFileNameW(destination.parent_path().c_str(), L"g3d", 0, temporary)) {
            std::cerr << "Failed to create temporary scene file: " << filepath << std::endl;
            return false;
        }
        const std::filesystem::path temp_path(temporary);
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (file) file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.close();
        if (!file || !MoveFileExW(temp_path.c_str(), destination.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored;
            std::filesystem::remove(temp_path, ignored);
            std::cerr << "Failed to write or replace scene file: " << filepath << std::endl;
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Exception during scene saving: " << ex.what() << std::endl;
        return false;
    }
}

bool SceneSerializer::load_from_file(Scene& scene, const std::string& filepath) {
    try {
        std::ifstream file(std::filesystem::u8path(filepath));
        if (!file.is_open()) {
            std::cerr << "Failed to open file for loading: " << filepath << std::endl;
            return false;
        }

        json j;
        file >> j;

        if (!j.is_object()) {
            throw std::runtime_error("Scene root must be an object");
        }
        Scene loaded;
        loaded.name = j.value("name", "Loaded Scene");
        loaded.filepath = filepath;

        // Environment
        if (j.contains("environment")) {
            const auto& env = j["environment"];
            if (env.contains("sun")) {
                loaded.sun_pitch = env["sun"].value("pitch", 64.2f);
                loaded.sun_yaw = env["sun"].value("yaw", 45.0f);
                read_vec3(env["sun"], "color", loaded.sun_color);
                loaded.sun_intensity = env["sun"].value("intensity", 1.85f);
            }
            if (env.contains("shadows")) {
                const auto& shadow = env["shadows"];
                loaded.shadows_enabled = shadow.value("enabled", loaded.shadows_enabled);
                loaded.shadow_strength = shadow.value("strength", loaded.shadow_strength);
                loaded.shadow_softness = shadow.value("softness", loaded.shadow_softness);
                loaded.shadow_coverage = shadow.value("coverage", loaded.shadow_coverage);
            }
            if (env.contains("ambient")) {
                read_vec3(env["ambient"], "color", loaded.ambient_color);
                loaded.ambient_intensity = env["ambient"].value("intensity", 1.15f);
            }
            read_vec3(env, "sky_color", loaded.sky_color);
            if (env.contains("fog")) {
                loaded.fog_start = env["fog"].value("start", 160.0f);
                loaded.fog_end = env["fog"].value("end", 600.0f);
            }
        }

        // Terrain
        if (j.contains("terrain")) {
            const auto& terr = j["terrain"];
            loaded.has_terrain = terr.value("enabled", true);
            loaded.terrain_grid_size = terr.value("grid_size", 128u);
            loaded.terrain_world_size = terr.value("world_size", 240.0f);
            loaded.terrain_height_scale = terr.value("height_scale", 32.0f);
            loaded.terrain_seed = terr.value("seed", 1337u);
            if (terr.contains("heights")) {
                if (loaded.terrain_grid_size < 32 || loaded.terrain_grid_size > 512 ||
                    !terr["heights"].is_array() || terr["heights"].size() != static_cast<size_t>(loaded.terrain_grid_size) * loaded.terrain_grid_size)
                    throw std::runtime_error("Invalid terrain heightfield size");
                loaded.terrain_heights = terr["heights"].get<std::vector<float>>();
                for (float height : loaded.terrain_heights)
                    if (!std::isfinite(height)) throw std::runtime_error("Invalid terrain height");
            }
        }

        // Materials
        if (j.contains("materials") && j["materials"].is_array()) {
            loaded.materials.clear();
            uint32_t max_mat_id = 8;
            for (const auto& mat_json : j["materials"]) {
                Material m;
                m.id = mat_json.value("id", 0u);
                m.name = mat_json.value("name", "Material");
                if (mat_json.contains("base_color") && mat_json["base_color"].is_array() && mat_json["base_color"].size() >= 4) {
                    for (size_t i = 0; i < 4; ++i) m.base_color[i] = mat_json["base_color"][i].get<float>();
                }
                m.roughness = mat_json.value("roughness", 0.45f);
                m.metallic = mat_json.value("metallic", 0.05f);
                m.emissive = mat_json.value("emissive", 0.0f);
                if (mat_json.contains("emissive_color") && mat_json["emissive_color"].is_array() && mat_json["emissive_color"].size() >= 3) {
                    for (size_t i = 0; i < 3; ++i) m.emissive_color[i] = mat_json["emissive_color"][i].get<float>();
                }
                m.albedo_path = resolve_asset_path(mat_json.value("albedo_path", std::string{}), filepath);
                m.normal_path = resolve_asset_path(mat_json.value("normal_path", std::string{}), filepath);
                if (mat_json.contains("uv_tiling") && mat_json["uv_tiling"].is_array() && mat_json["uv_tiling"].size() >= 2) {
                    m.uv_tiling[0] = mat_json["uv_tiling"][0].get<float>();
                    m.uv_tiling[1] = mat_json["uv_tiling"][1].get<float>();
                }
                if (mat_json.contains("uv_offset") && mat_json["uv_offset"].is_array() && mat_json["uv_offset"].size() >= 2) {
                    m.uv_offset[0] = mat_json["uv_offset"][0].get<float>();
                    m.uv_offset[1] = mat_json["uv_offset"][1].get<float>();
                }
                m.alpha_mode = mat_json.value("alpha_mode", 0);
                m.alpha_cutoff = mat_json.value("alpha_cutoff", 0.5f);
                m.is_builtin_preset = mat_json.value("is_builtin_preset", false);
                m.preset_index = mat_json.value("preset_index", 0u);

                if (m.id > max_mat_id) max_mat_id = m.id;
                loaded.materials.push_back(m);
            }
            loaded.next_material_id = max_mat_id + 1;
        } else {
            loaded.init_default_materials();
        }

        // Entities
        uint32_t max_id = 0;
        if (j.contains("entities") && j["entities"].is_array()) {
            for (const auto& ent_json : j["entities"]) {
                Entity e;
                e.id = ent_json.value("id", 0u);
                e.name = ent_json.value("name", "Entity");
                e.tag = ent_json.value("tag", "Default");
                e.parent_id = ent_json.value("parent_id", 0u);
                e.is_group = ent_json.value("is_group", false);

                if (e.id > max_id) max_id = e.id;

                if (ent_json.contains("transform")) {
                    const auto& t = ent_json["transform"];
                    read_vec3(t, "position", e.position);
                    read_vec3(t, "rotation", e.rotation);
                    read_vec3(t, "scale", e.scale);
                }

                if (ent_json.contains("render")) {
                    const auto& r = ent_json["render"];
                    e.mesh_id = r.value("mesh_id", (uint16_t)1);
                    e.material_id = r.value("material_id", (uint16_t)0);
                    e.texture_id = r.value("texture_id", (uint16_t)0);
                    e.model_path = resolve_asset_path(r.value("model_path", std::string{}), filepath);
                    e.texture_path = resolve_asset_path(r.value("texture_path", std::string{}), filepath);
                    e.cast_shadow = r.value("cast_shadow", true);
                    e.visible = r.value("visible", true);
                    e.custom_param1 = r.value("param1", 0.0f);
                    e.custom_param2 = r.value("param2", 0.0f);
                }

                e.editor_grid = ent_json.value("editor_grid", e.name == "Grid Floor" && e.mesh_id == 3 &&
                    e.scale[0] >= 50.0f && e.scale[2] >= 50.0f);

                if (ent_json.contains("light")) {
                    const auto& light = ent_json["light"];
                    e.is_point_light = light.value("type", std::string{}) == "point";
                    if (e.is_point_light) {
                        read_vec3(light, "color", e.light_color);
                        e.light_intensity = light.value("intensity", e.light_intensity);
                        e.light_radius = light.value("radius", e.light_radius);
                    }
                }

                if (ent_json.contains("physics")) {
                    const auto& p = ent_json["physics"];
                    e.physics_type = p.value("type", 0);
                    e.collider_shape = p.value("shape", 0);
                    e.mass = p.value("mass", 1.0f);
                    e.friction = p.value("friction", 0.6f);
                    e.restitution = p.value("restitution", 0.1f);
                }

                loaded.entities.push_back(e);
            }
        }
        if (max_id == std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Scene entity ID exceeds the supported range");
        }
        loaded.next_entity_id = max_id + 1;

        // Sanitize hierarchy: ensure valid parent IDs and prevent circular parent-child cycles
        std::unordered_set<uint32_t> valid_ids;
        for (const auto& ent : loaded.entities) valid_ids.insert(ent.id);
        for (auto& ent : loaded.entities) {
            if (ent.parent_id != 0) {
                if (ent.parent_id == ent.id || valid_ids.find(ent.parent_id) == valid_ids.end()) {
                    ent.parent_id = 0;
                }
            }
        }
        for (auto& ent : loaded.entities) {
            if (ent.parent_id != 0 && loaded.is_descendant_of(ent.parent_id, ent.id)) {
                ent.parent_id = 0;
            }
        }

        scene = std::move(loaded);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Exception during scene loading: " << ex.what() << std::endl;
        return false;
    }
}

} // namespace GM3D::Studio
