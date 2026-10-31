#include "SceneSerializer.h"
#include "TransformMath.h"
#include "Heightmap.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cctype>
#include <array>
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

bool same_file_contents(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    const auto size_a = std::filesystem::file_size(a, ec);
    if (ec) return false;
    const auto size_b = std::filesystem::file_size(b, ec);
    if (ec || size_a != size_b) return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left || !right) return false;
    std::array<char, 65536> lhs{}, rhs{};
    while (left && right) {
        left.read(lhs.data(), lhs.size()); right.read(rhs.data(), rhs.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(lhs.begin(), lhs.begin() + left.gcount(), rhs.begin())) return false;
    }
    return left.eof() && right.eof() && !left.bad() && !right.bad();
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
            {"softness", scene.shadow_softness}, {"coverage", scene.shadow_coverage},
            {"csm_enabled", scene.csm_enabled}, {"quality", scene.shadow_quality},
            {"max_shadow_lights", scene.max_shadow_lights}
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

        // Terrain & Foliage
        j["terrain"] = {
            {"enabled", scene.has_terrain},
            {"grid_size", scene.terrain_grid_size},
            {"world_size", scene.terrain_world_size},
            {"height_scale", scene.terrain_height_scale},
            {"seed", scene.terrain_seed},
            {"detail_scale", scene.terrain_detail_scale},
            {"grass_enabled", scene.grass_enabled},
            {"grass_density", scene.grass_density},
            {"grass_height", scene.grass_height},
            {"grass_width", scene.grass_width},
            {"grass_max_distance", scene.grass_max_distance},
            {"grass_fade_range", scene.grass_fade_range},
            {"grass_wind_speed", scene.grass_wind_speed},
            {"grass_wind_strength", scene.grass_wind_strength},
            {"grass_color_a", {scene.grass_color_a[0], scene.grass_color_a[1], scene.grass_color_a[2]}},
            {"grass_color_b", {scene.grass_color_b[0], scene.grass_color_b[1], scene.grass_color_b[2]}}
        };

        if (!scene.grass_clumps.empty()) {
            json clumps_array = json::array();
            for (const auto& c : scene.grass_clumps) {
                clumps_array.push_back({
                    c.x, c.y, c.z, c.rot, c.sx, c.sy, c.r, c.g, c.b
                });
            }
            j["terrain"]["grass_clumps"] = clumps_array;
        }

        if (!scene.terrain_heights.empty()) j["terrain"]["heights"] = scene.terrain_heights;

        if (!scene.terrain_layers.empty()) {
            json layers_array = json::array();
            for (const auto& layer : scene.terrain_layers) {
                layers_array.push_back({
                    {"name", layer.name},
                    {"color", {layer.color[0], layer.color[1], layer.color[2]}},
                    {"roughness", layer.roughness},
                    {"scale", layer.scale}
                });
            }
            j["terrain"]["layers"] = layers_array;
        }
        if (!scene.terrain_splatmap.empty()) {
            j["terrain"]["splatmap"] = scene.terrain_splatmap;
        }

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

            float wpos[3], wrot[3], wscale[3];
            ComputeWorldTransform(scene, e, wpos, wrot, wscale);
            float wqx = 0.0f, wqy = 0.0f, wqz = 0.0f, wqw = 1.0f;
            EulerToQuat(wrot[0], wrot[1], wrot[2], wqx, wqy, wqz, wqw);

            ent["transform"] = {
                {"position", {e.position[0], e.position[1], e.position[2]}},
                {"rotation", {e.rotation[0], e.rotation[1], e.rotation[2]}},
                {"scale", {e.scale[0], e.scale[1], e.scale[2]}},
                {"world_position", {wpos[0], wpos[1], wpos[2]}},
                {"world_rotation", {wrot[0], wrot[1], wrot[2]}},
                {"world_scale", {wscale[0], wscale[1], wscale[2]}},
                {"world_quat", {wqx, wqy, wqz, wqw}}
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
                {"collider_size", {e.collider_size[0], e.collider_size[1], e.collider_size[2]}},
                {"collider_offset", {e.collider_offset[0], e.collider_offset[1], e.collider_offset[2]}},
                {"mass", e.mass},
                {"friction", e.friction},
                {"restitution", e.restitution}
            };

            if (e.has_animator || e.anim_playing || e.anim_clip_index != 0) {
                ent["animator"] = {
                    {"enabled", e.has_animator},
                    {"clip_index", e.anim_clip_index},
                    {"speed", e.anim_speed},
                    {"loop", e.anim_loop},
                    {"playing", e.anim_playing},
                    {"time", e.anim_time}
                };
            }

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
        // Files without a version predate the versioned format and retain the
        // legacy defaults below. Do not silently open future formats as v1.
        if (j.contains("version") && (!j["version"].is_number_integer() || j["version"].get<int64_t>() != 1))
            throw std::runtime_error("Unsupported scene format version");
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
                loaded.csm_enabled = shadow.value("csm_enabled", loaded.csm_enabled);
                loaded.shadow_quality = std::clamp(shadow.value("quality", loaded.shadow_quality), 1, 3);
                loaded.max_shadow_lights = std::clamp(shadow.value("max_shadow_lights", loaded.max_shadow_lights), 0, 4);
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
            loaded.terrain_detail_scale = terr.value("detail_scale", 1.0f);
            loaded.grass_enabled = terr.value("grass_enabled", false);
            loaded.grass_density = terr.value("grass_density", 1.0f);
            loaded.grass_height = terr.value("grass_height", 0.85f);
            loaded.grass_width = terr.value("grass_width", 0.22f);
            loaded.grass_max_distance = terr.value("grass_max_distance", 95.0f);
            loaded.grass_fade_range = terr.value("grass_fade_range", 25.0f);
            loaded.grass_wind_speed = terr.value("grass_wind_speed", 1.8f);
            loaded.grass_wind_strength = terr.value("grass_wind_strength", 0.35f);
            const json color_a = terr.value("grass_color_a", json::array({0.20f, 0.38f, 0.13f}));
            const json color_b = terr.value("grass_color_b", json::array({0.30f, 0.46f, 0.16f}));
            for (int c = 0; c < 3; ++c) {
                loaded.grass_color_a[c] = color_a.size() == 3 ? color_a[c].get<float>() : Scene{}.grass_color_a[c];
                loaded.grass_color_b[c] = color_b.size() == 3 ? color_b[c].get<float>() : Scene{}.grass_color_b[c];
            }
            loaded.grass_clumps.clear();
            if (terr.contains("grass_clumps") && terr["grass_clumps"].is_array()) {
                for (const auto& item : terr["grass_clumps"]) {
                    if (item.is_array() && item.size() >= 9) {
                        SceneGrassClump c;
                        c.x = item[0].get<float>();
                        c.y = item[1].get<float>();
                        c.z = item[2].get<float>();
                        c.rot = item[3].get<float>();
                        c.sx = item[4].get<float>();
                        c.sy = item[5].get<float>();
                        c.r = item[6].get<float>();
                        c.g = item[7].get<float>();
                        c.b = item[8].get<float>();
                        loaded.grass_clumps.push_back(c);
                    }
                }
            }
            if (terr.contains("heights")) {
                if (loaded.terrain_grid_size < 32 || loaded.terrain_grid_size > 512 ||
                    !terr["heights"].is_array() || terr["heights"].size() != static_cast<size_t>(loaded.terrain_grid_size) * loaded.terrain_grid_size)
                    throw std::runtime_error("Invalid terrain heightfield size");
                loaded.terrain_heights = terr["heights"].get<std::vector<float>>();
                for (float height : loaded.terrain_heights)
                    if (!std::isfinite(height)) throw std::runtime_error("Invalid terrain height");
            }
            if (terr.contains("layers") && terr["layers"].is_array() && !terr["layers"].empty()) {
                loaded.terrain_layers.clear();
                for (const auto& l_json : terr["layers"]) {
                    TerrainMaterialLayer layer;
                    layer.name = l_json.value("name", "Layer");
                    read_vec3(l_json, "color", layer.color);
                    layer.roughness = l_json.value("roughness", 0.75f);
                    layer.scale = l_json.value("scale", 1.0f);
                    loaded.terrain_layers.push_back(layer);
                }
            }
            if (terr.contains("splatmap")) {
                if (loaded.terrain_grid_size < 32 || loaded.terrain_grid_size > 512 ||
                    !terr["splatmap"].is_array() || terr["splatmap"].size() != static_cast<size_t>(loaded.terrain_grid_size) * loaded.terrain_grid_size * 4)
                    throw std::runtime_error("Invalid terrain splatmap size");
                loaded.terrain_splatmap = terr["splatmap"].get<std::vector<uint8_t>>();
            }
        }

        // Materials
        if (j.contains("materials") && j["materials"].is_array()) {
            loaded.materials.clear();
            uint32_t max_mat_id = 8;
            for (const auto& mat_json : j["materials"]) {
                Material m;
                m.id = mat_json.value("id", 0u);
                if (m.id > std::numeric_limits<uint16_t>::max())
                    throw std::runtime_error("Material ID exceeds the supported range");
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
                    read_vec3(p, "collider_size", e.collider_size);
                    read_vec3(p, "collider_offset", e.collider_offset);
                    e.mass = p.value("mass", 1.0f);
                    e.friction = p.value("friction", 0.6f);
                    e.restitution = p.value("restitution", 0.1f);
                }

                if (ent_json.contains("animator")) {
                    const auto& a = ent_json["animator"];
                    e.has_animator = a.value("enabled", true);
                    e.anim_clip_index = a.value("clip_index", 0);
                    e.anim_speed = a.value("speed", 1.0f);
                    e.anim_loop = a.value("loop", true);
                    e.anim_playing = a.value("playing", false);
                    e.anim_time = a.value("time", 0.0f);
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

bool SceneSerializer::export_for_gamemaker(const Scene& scene, const std::string& export_dir, std::string& out_summary) {
    try {
        // Validate all inputs before touching an existing export package.
        auto validate_asset = [&](const std::string& asset_path) {
            if (asset_path.empty()) return true;
            const auto path = std::filesystem::u8path(asset_path);
            std::error_code file_ec;
            if (!std::filesystem::is_regular_file(path, file_ec)) {
                out_summary = "Missing resource: " + asset_path;
                return false;
            }
            auto extension = path.extension().string();
            for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension == ".gltf") {
                std::ifstream input(path, std::ios::binary);
                json model;
                try { input >> model; } catch (const std::exception&) {
                    out_summary = "Cannot read glTF model: " + asset_path;
                    return false;
                }
                for (const char* section : {"buffers", "images"}) {
                    if (!model.contains(section) || !model[section].is_array()) continue;
                    for (const auto& item : model[section]) {
                        if (!item.is_object() || !item.contains("uri") || !item["uri"].is_string()) continue;
                        const auto uri = item["uri"].get<std::string>();
                        if (uri.rfind("data:", 0) != 0) {
                            out_summary = "glTF uses external resources that cannot yet be packaged: " + asset_path + " (" + uri + "). Use a self-contained GLB.";
                            return false;
                        }
                    }
                }
            }
            return true;
        };
        for (const auto& mat : scene.materials)
            if (!validate_asset(mat.albedo_path) || !validate_asset(mat.normal_path)) return false;
        for (const auto& ent : scene.entities)
            if (!validate_asset(ent.model_path) || !validate_asset(ent.texture_path)) return false;

        std::error_code ec;
        const auto root = std::filesystem::absolute(std::filesystem::u8path(export_dir));
        const auto assets_dir = root / "assets";
        std::filesystem::create_directories(assets_dir, ec);
        if (ec) {
            out_summary = "Failed to create export directory: " + export_dir;
            return false;
        }

        Scene exported = scene;
        // Rendering uses the first four layers and fills absent entries with the
        // default palette. Preserve that behavior for older scenes at runtime.
        const Scene defaults;
        if (exported.terrain_layers.size() > 4) exported.terrain_layers.resize(4);
        while (exported.terrain_layers.size() < 4)
            exported.terrain_layers.push_back(defaults.terrain_layers[exported.terrain_layers.size()]);
        size_t copied_assets = 0;
        std::unordered_map<std::string, std::string> staged_map;

        auto stage_asset = [&](std::string& asset_path) -> bool {
            if (asset_path.empty()) return true;
            auto it = staged_map.find(asset_path);
            if (it != staged_map.end()) {
                asset_path = it->second;
                return true;
            }
            const auto src = std::filesystem::u8path(asset_path);
            std::error_code file_ec;
            if (!std::filesystem::is_regular_file(src, file_ec)) {
                out_summary = "Resource is no longer available: " + asset_path;
                return false;
            }

            std::filesystem::path filename = src.filename();
            std::filesystem::path dst = assets_dir / filename;
            int suffix = 1;
            bool reuse = false;
            while (std::filesystem::exists(dst, file_ec)) {
                std::error_code eq_ec;
                if ((std::filesystem::equivalent(src, dst, eq_ec) && !eq_ec) || same_file_contents(src, dst)) {
                    reuse = true;
                    break;
                }
                dst = assets_dir / (src.stem().u8string() + u8"_" +
                    std::u8string(reinterpret_cast<const char8_t*>(std::to_string(suffix++).c_str())) +
                    src.extension().u8string());
            }
            if (!reuse) std::filesystem::copy_file(src, dst, std::filesystem::copy_options::none, file_ec);
            if (!file_ec) {
                std::string staged_full = path_utf8(dst.lexically_normal());
                staged_map[asset_path] = staged_full;
                asset_path = staged_full;
                if (!reuse) ++copied_assets;
                return true;
            }
            out_summary = "Failed to copy resource: " + asset_path + " (" + file_ec.message() + ")";
            return false;
        };

        for (auto& mat : exported.materials) {
            if (!stage_asset(mat.albedo_path) || !stage_asset(mat.normal_path)) return false;
        }
        for (auto& ent : exported.entities) {
            if (!stage_asset(ent.model_path) || !stage_asset(ent.texture_path)) return false;
        }

        const auto scene_json_path = root / "scene_gm3d.json";
        if (!save_to_file(exported, path_utf8(scene_json_path))) {
            out_summary = "Failed to write exported scene_gm3d.json";
            return false;
        }

        if (exported.has_terrain && !exported.terrain_heights.empty()) {
            std::string pfm_err;
            if (!save_heightmap(root / "terrain.pfm", exported.terrain_grid_size, exported.terrain_heights, pfm_err)) {
                out_summary = "Failed to export terrain heightmap: " + pfm_err;
                return false;
            }
        }

        if (exported.grass_enabled && !exported.grass_clumps.empty()) {
            const auto grass_bin_path = root / "grass.bin";
            std::ofstream grass_out(grass_bin_path, std::ios::binary | std::ios::trunc);
            if (grass_out) {
                uint32_t magic = 0x53415247; // 'GRAS'
                uint32_t version = 1;
                uint32_t count = static_cast<uint32_t>(exported.grass_clumps.size());
                grass_out.write(reinterpret_cast<const char*>(&magic), 4);
                grass_out.write(reinterpret_cast<const char*>(&version), 4);
                grass_out.write(reinterpret_cast<const char*>(&count), 4);
                for (const auto& c : exported.grass_clumps) {
                    SceneGrassBinaryInstance inst{};
                    inst.px = c.x; inst.py = c.y; inst.pz = c.z;
                    inst.rotation = c.rot;
                    inst.scale_x = c.sx; inst.scale_y = c.sy;
                    inst.wind_phase = (c.x * 0.13f + c.z * 0.17f);
                    inst.r = c.r; inst.g = c.g; inst.b = c.b;
                    inst.pad[0] = 0.0f; inst.pad[1] = 0.0f;
                    grass_out.write(reinterpret_cast<const char*>(&inst), sizeof(inst));
                }
            }
        }

        const auto gml_script_path = root / "scr_gm3d_load_scene.gml";
        std::ofstream gml(gml_script_path, std::ios::binary | std::ios::trunc);
        if (!gml) {
            out_summary = "Failed to write scr_gm3d_load_scene.gml";
            return false;
        }

        static const char* kGmlLoaderScript = R"GML(// ==============================================================================
// GM3D Studio -> GameMaker LTS Scene & Asset Loader
// Auto-generated by GM3D Studio Exporter
// ==============================================================================

/// @function gm3d_load_scene_json(scene_json_path, asset_base_dir)
/// @desc Loads a GM3D Studio exported scene (.json), configures environment/lighting/materials/terrain,
///       and spawns all 3D entities, Jolt colliders, and skeletal animations.
function gm3d_load_scene_json(_scene_json_path, _asset_base_dir = "") {
    if (!file_exists(_scene_json_path)) {
        show_debug_message("[GM3D Loader] Scene file not found: " + string(_scene_json_path));
        return undefined;
    }

    if (_asset_base_dir == "") {
        _asset_base_dir = filename_dir(_scene_json_path);
        if (_asset_base_dir != "" && string_char_at(_asset_base_dir, string_length(_asset_base_dir)) != "/" &&
            string_char_at(_asset_base_dir, string_length(_asset_base_dir)) != "\\") {
            _asset_base_dir += "/";
        }
    }

    var _buf = buffer_load(_scene_json_path);
    if (_buf < 0) return undefined;
    // JSON files are not NUL-terminated, while buffer_string expects a terminator.
    buffer_seek(_buf, buffer_seek_end, 0);
    buffer_write(_buf, buffer_u8, 0);
    buffer_seek(_buf, buffer_seek_start, 0);
    var _json_str = buffer_read(_buf, buffer_string);
    buffer_delete(_buf);

    var _data = json_parse(_json_str);
    var _result = {
        name: variable_struct_exists(_data, "name") ? _data.name : "Exported Scene",
        entities: [],
        physics_bodies: [],
        loaded_models: {},
        loaded_textures: {}
    };

    // 1. Environment, Sun, Cascaded Shadows & Atmosphere
    if (variable_struct_exists(_data, "environment")) {
        var _env = _data.environment;
        if (variable_struct_exists(_env, "sun")) {
            var _sun = _env.sun;
            var _pitch = degtorad(_sun.pitch);
            var _yaw   = degtorad(_sun.yaw);
            var _lx = -sin(_yaw) * cos(_pitch);
            var _ly = -sin(_pitch);
            var _lz =  cos(_yaw) * cos(_pitch);
            gm3d_light_set_sun(_lx, _ly, _lz, _sun.color[0], _sun.color[1], _sun.color[2], _sun.intensity);
        }
        if (variable_struct_exists(_env, "shadows")) {
            var _sh = _env.shadows;
            var _en = variable_struct_exists(_sh, "enabled") ? (_sh.enabled ? 1.0 : 0.0) : 1.0;
            var _st = variable_struct_exists(_sh, "strength") ? _sh.strength : 0.85;
            var _so = variable_struct_exists(_sh, "softness") ? _sh.softness : 2.0;
            var _cv = variable_struct_exists(_sh, "coverage") ? _sh.coverage : 110.0;
            var _csm = variable_struct_exists(_sh, "csm_enabled") ? (_sh.csm_enabled ? 1.0 : 0.0) : 1.0;
            var _qual = variable_struct_exists(_sh, "quality") ? _sh.quality : 2.0;
            var _max_pl = variable_struct_exists(_sh, "max_shadow_lights") ? _sh.max_shadow_lights : 4.0;
            render_set_shadow_settings(_en, _st, _so, _cv);
            render_set_shadow_quality(_qual, _csm, _max_pl);
        }
        if (variable_struct_exists(_env, "ambient")) {
            var _amb = _env.ambient;
            gm3d_light_set_ambient(_amb.color[0], _amb.color[1], _amb.color[2], _amb.intensity);
        }
        if (variable_struct_exists(_env, "sky_color")) {
            var _sky = _env.sky_color;
            gm3d_set_sky_color(_sky[0], _sky[1], _sky[2]);
            if (variable_struct_exists(_env, "fog")) {
                gm3d_set_fog(_env.fog.start, _env.fog.end, _sky[0], _sky[1], _sky[2]);
            }
        }
    }

    // 2. Terrain Generation
    if (variable_struct_exists(_data, "terrain")) {
        var _terr = _data.terrain;
        if (variable_struct_exists(_terr, "enabled") && _terr.enabled) {
            var _terrain_body = gm3d_terrain_generate(_terr.grid_size, _terr.world_size, _terr.height_scale, _terr.seed);
            if (_terrain_body <= 0) {
                show_debug_message("[GM3D Loader] Failed to generate terrain.");
                return undefined;
            }
            // The Studio's detail tiling knob: 0 leaves the flat macro colour, which is what a
            // stylised scene wants. The DLL defaults to 1.0 (one photograph tile per 2.5 m).
            if (variable_struct_exists(_terr, "detail_scale")) {
                terrain_set_detail_scale(_terr.detail_scale);
            }
            var _heights = variable_struct_exists(_terr, "heights") ? _terr.heights : [];
            var _weights = variable_struct_exists(_terr, "splatmap") ? _terr.splatmap : [];
            var _layers = variable_struct_exists(_terr, "layers") ? _terr.layers : [];
            var _hc = array_length(_heights), _wc = array_length(_weights), _lc = array_length(_layers);
            if (_lc != 0 && _lc != 4) {
                show_debug_message("[GM3D Loader] Terrain must contain four material layers.");
                return undefined;
            }
            var _terrain_bytes = 12 + _hc * 4 + _wc + _lc * 20;
            var _terrain_buf = buffer_create(_terrain_bytes, buffer_fixed, 1);
            buffer_write(_terrain_buf, buffer_u32, _hc);
            buffer_write(_terrain_buf, buffer_u32, _wc);
            buffer_write(_terrain_buf, buffer_u32, _lc);
            for (var _hi = 0; _hi < _hc; ++_hi) buffer_write(_terrain_buf, buffer_f32, _heights[_hi]);
            for (var _wi = 0; _wi < _wc; ++_wi) buffer_write(_terrain_buf, buffer_u8, _weights[_wi]);
            for (var _li = 0; _li < _lc; ++_li) {
                var _layer = _layers[_li];
                buffer_write(_terrain_buf, buffer_f32, _layer.color[0]);
                buffer_write(_terrain_buf, buffer_f32, _layer.color[1]);
                buffer_write(_terrain_buf, buffer_f32, _layer.color[2]);
                buffer_write(_terrain_buf, buffer_f32, _layer.roughness);
                buffer_write(_terrain_buf, buffer_f32, _layer.scale);
            }
            _terrain_body = terrain_apply_edit_data(buffer_get_address(_terrain_buf), _terrain_bytes);
            buffer_delete(_terrain_buf);
            if (_terrain_body <= 0) {
                show_debug_message("[GM3D Loader] Failed to restore terrain edits: " + string(_terrain_body));
                return undefined;
            }
            var _terrain_inst = instance_create_depth(0, 0, 0, obj_3d_entity);
            _terrain_inst.mesh_id = GM3D_MESH_TERRAIN;
            _terrain_inst.material_id = 0;
            _terrain_inst.texture_id = GM3D_TEX_TERRAIN;
            _terrain_inst.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
            _terrain_inst.physics_body_id = _terrain_body;
            array_push(_result.entities, _terrain_inst);
            if (variable_struct_exists(_terr, "grass_enabled") && _terr.grass_enabled) {
                // Blade size and palette before anything is generated: a procedural rebuild
                // keeps whatever the config holds, so a violet short carpet needs no baked
                // grass.bin - it comes out of the same terrain the hills came from.
                if (variable_struct_exists(_terr, "grass_height")) {
                    var _gh = _terr.grass_height;
                    var _gw = variable_struct_exists(_terr, "grass_width") ? _terr.grass_width : 0.22;
                    var _ca = variable_struct_exists(_terr, "grass_color_a") ? _terr.grass_color_a : [0.20, 0.38, 0.13];
                    var _cb = variable_struct_exists(_terr, "grass_color_b") ? _terr.grass_color_b : [0.30, 0.46, 0.16];
                    grass_set_shape(_gh, _gw, _ca[0], _ca[1], _ca[2], _cb[0], _cb[1], _cb[2]);
                }
                grass_set_enabled(1.0);
                var _wspd = variable_struct_exists(_terr, "grass_wind_speed") ? _terr.grass_wind_speed : 1.8;
                var _wstr = variable_struct_exists(_terr, "grass_wind_strength") ? _terr.grass_wind_strength : 0.35;
                grass_set_wind(0.707, 0.707, _wspd, _wstr);
                var _grass_bin = _asset_base_dir + "grass.bin";
                if (file_exists(_grass_bin)) {
                    grass_load_file(_grass_bin);
                } else {
                    grass_rebuild(variable_struct_exists(_terr, "grass_density") ? _terr.grass_density : 1.0,
                                  _terr.seed);
                }
            } else {
                grass_set_enabled(0.0);
                grass_clear();
            }
        }
    }

    // Helper to resolve & cache texture IDs
    var _resolve_tex = function(_rel_path, _base_dir, _cache) {
        if (_rel_path == "" || is_undefined(_rel_path)) return 0;
        if (variable_struct_exists(_cache, _rel_path)) return variable_struct_get(_cache, _rel_path);
        var _full = _base_dir + _rel_path;
        if (!file_exists(_full)) _full = _rel_path;
        var _tid = file_exists(_full) ? gm3d_texture_load(_full) : 0;
        variable_struct_set(_cache, _rel_path, _tid);
        return _tid;
    };

    // Helper to resolve & cache model IDs
    var _resolve_model = function(_rel_path, _base_dir, _cache) {
        if (_rel_path == "" || is_undefined(_rel_path)) return 0;
        if (variable_struct_exists(_cache, _rel_path)) return variable_struct_get(_cache, _rel_path);
        var _full = _base_dir + _rel_path;
        if (!file_exists(_full)) _full = _rel_path;
        var _mid = file_exists(_full) ? gm3d_mesh_load(_full) : 0;
        variable_struct_set(_cache, _rel_path, _mid);
        return _mid;
    };

    // 3. PBR Materials
    if (variable_struct_exists(_data, "materials") && is_array(_data.materials)) {
        var _mcount = array_length(_data.materials);
        for (var _mi = 0; _mi < _mcount; ++_mi) {
            var _m = _data.materials[_mi];
            var _preset = variable_struct_exists(_m, "is_builtin_preset") && _m.is_builtin_preset;
            if (!_preset) {
                material_set_pbr(_m.id, _m.base_color[0], _m.base_color[1], _m.base_color[2], _m.base_color[3],
                                 _m.roughness, _m.metallic, _m.emissive);
                var _alb = variable_struct_exists(_m, "albedo_path") ? _resolve_tex(_m.albedo_path, _asset_base_dir, _result.loaded_textures) : 0;
                var _nrm = variable_struct_exists(_m, "normal_path") ? _resolve_tex(_m.normal_path, _asset_base_dir, _result.loaded_textures) : 0;
                material_set_textures(_m.id, _alb, _nrm);
                if (variable_struct_exists(_m, "uv_tiling") && variable_struct_exists(_m, "uv_offset")) {
                    material_set_uv(_m.id, _m.uv_tiling[0], _m.uv_tiling[1], _m.uv_offset[0], _m.uv_offset[1]);
                }
                if (variable_struct_exists(_m, "alpha_mode")) {
                    material_set_alpha(_m.id, _m.alpha_mode, variable_struct_exists(_m, "alpha_cutoff") ? _m.alpha_cutoff : 0.5);
                }
            }
        }
    }

    // 4. Entities, Point Lights, Jolt Colliders & Skeletal Animations
    gm3d_light_point_clear();
    var _light_slot = 0;

    if (variable_struct_exists(_data, "entities") && is_array(_data.entities)) {
        var _ecount = array_length(_data.entities);
        for (var _ei = 0; _ei < _ecount; ++_ei) {
            var _e = _data.entities[_ei];
            if (variable_struct_exists(_e, "is_group") && _e.is_group) continue;
            if (variable_struct_exists(_e, "editor_grid") && _e.editor_grid) continue;

            var _tr = _e.transform;
            var _pos = variable_struct_exists(_tr, "world_position") ? _tr.world_position : _tr.position;
            var _scl = variable_struct_exists(_tr, "world_scale") ? _tr.world_scale : _tr.scale;
            var _qat = variable_struct_exists(_tr, "world_quat") ? _tr.world_quat : [0, 0, 0, 1];

            // Point Light Entity
            if (variable_struct_exists(_e, "light")) {
                var _vis = (!variable_struct_exists(_e, "render") || _e.render.visible);
                if (_vis && _light_slot < 16) {
                    var _lt = _e.light;
                    var _cast = (!variable_struct_exists(_e, "render") || _e.render.cast_shadow) ? 1.0 : 0.0;
                    light_point_set_ex(_light_slot, _pos[0], _pos[1], _pos[2], _lt.radius,
                                       _lt.color[0], _lt.color[1], _lt.color[2], _lt.intensity, _cast);
                    _light_slot++;
                }
                continue;
            }

            var _rn = _e.render;
            var _mesh_id = _rn.mesh_id;
            if (variable_struct_exists(_rn, "model_path") && _rn.model_path != "") {
                var _loaded_mid = _resolve_model(_rn.model_path, _asset_base_dir, _result.loaded_models);
                if (_loaded_mid > 0) _mesh_id = _loaded_mid;
            }

            var _tex_id = _rn.texture_id;
            if (variable_struct_exists(_rn, "texture_path") && _rn.texture_path != "") {
                var _loaded_tid = _resolve_tex(_rn.texture_path, _asset_base_dir, _result.loaded_textures);
                if (_loaded_tid > 0) _tex_id = _loaded_tid;
            }

            var _body_id = 0;
            var _flags = (_rn.visible ? GM3D_FLAG_ACTIVE : 0) | (_rn.cast_shadow ? GM3D_FLAG_CAST_SHADOW : 0);

            if (variable_struct_exists(_e, "physics") && _e.physics.type > 0) {
                var _ph = _e.physics;
                var _motion = (_ph.type == 1) ? GM3D_MOTION_STATIC : ((_ph.type == 2) ? GM3D_MOTION_DYNAMIC : GM3D_MOTION_KINEMATIC);
                var _csz = variable_struct_exists(_ph, "collider_size") ? _ph.collider_size : [1, 1, 1];
                var _cof = variable_struct_exists(_ph, "collider_offset") ? _ph.collider_offset : [0, 0, 0];
                var _bx = _pos[0] + _cof[0];
                var _by = _pos[1] + _cof[1];
                var _bz = _pos[2] + _cof[2];
                var _sx = max(0.1, abs(_scl[0] * _csz[0]));
                var _sy = max(0.1, abs(_scl[1] * _csz[1]));
                var _sz = max(0.1, abs(_scl[2] * _csz[2]));

                if (_ph.shape == 1) {
                    _body_id = gm3d_body_create_sphere(_bx, _by, _bz, _sx * 0.5, _ph.mass, _motion);
                } else if (_ph.shape == 2) {
                    _body_id = gm3d_body_create_capsule(_bx, _by, _bz, _sx * 0.5, _sy, _ph.mass, _motion);
                } else if (_ph.shape == 3) {
                    _body_id = gm3d_body_create_cylinder(_bx, _by, _bz, _sx * 0.5, _sy, _ph.mass, _motion);
                } else {
                    _body_id = gm3d_body_create_box(_bx, _by, _bz, _sx, _sy, _sz, _ph.mass, _motion);
                }

                if (_body_id > 0) {
                    gm3d_body_set_friction(_body_id, _ph.friction);
                    gm3d_body_set_restitution(_body_id, _ph.restitution);
                    gm3d_body_set_transform(_body_id, _bx, _by, _bz, _qat[0], _qat[1], _qat[2], _qat[3]);
                    if (_ph.type == 2) _flags |= GM3D_FLAG_RIGIDBODY;
                    array_push(_result.physics_bodies, _body_id);
                }
            }

            if (asset_get_index("obj_3d_entity") > -1) {
                var _inst = instance_create_depth(0, 0, 0, obj_3d_entity);
                _inst.x = _pos[0];
                _inst.y = _pos[1];
                _inst.z = _pos[2];
                _inst.qx = _qat[0];
                _inst.qy = _qat[1];
                _inst.qz = _qat[2];
                _inst.qw = _qat[3];
                _inst.image_xscale = _scl[0];
                _inst.image_yscale = _scl[1];
                _inst.image_zscale = _scl[2];
                _inst.mesh_id = _mesh_id;
                _inst.material_id = _rn.material_id;
                _inst.texture_id = _tex_id;
                _inst.flags = _flags;
                _inst.physics_body_id = _body_id;

                if (variable_struct_exists(_e, "animator") && _e.animator.enabled && _e.animator.playing) {
                    gm3d_anim_play(_inst.id, _e.animator.clip_index, _e.animator.speed, _e.animator.loop);
                }
                array_push(_result.entities, _inst);
            }
        }
    }

    show_debug_message("[GM3D Loader] Loaded scene '" + _result.name + "' (" + string(array_length(_result.entities)) + " entities).");
    return _result;
}
)GML";

        gml.write(kGmlLoaderScript, static_cast<std::streamsize>(std::strlen(kGmlLoaderScript)));
        gml.close();
        if (!gml) {
            out_summary = "Failed to write scr_gm3d_load_scene.gml";
            return false;
        }

        out_summary = "Exported scene_gm3d.json, scr_gm3d_load_scene.gml, and " +
                      std::to_string(copied_assets) + " asset file(s) to:\n" + path_utf8(root);
        out_summary += "\nThe GameMaker project must use the current GM3Dext DLL and extension definitions.";
        return true;
    } catch (const std::exception& ex) {
        out_summary = std::string("Exception during GameMaker export: ") + ex.what();
        return false;
    }
}

} // namespace GM3D::Studio
