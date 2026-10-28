#include "Diagnostics.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>

namespace GM3D::Studio {
namespace {
bool exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
}
void asset_check(std::vector<Diagnostic>& out, const std::string& path, const std::string& label) {
    if (!path.empty() && !exists(path)) out.push_back({true, "missing-asset", label + ": " + path});
}
}
std::vector<Diagnostic> validate_scene(const Scene& scene) {
    std::vector<Diagnostic> out;
    std::unordered_set<uint32_t> ids;
    size_t active_lights = 0;
    if (scene.entities.size() > (scene.has_terrain ? 9999u : 10000u))
        out.push_back({true, "entity-limit", "Scene exceeds the render buffer capacity."});
    for (const auto& entity : scene.entities) {
        const auto label = entity.name + " (ID " + std::to_string(entity.id) + ")";
        if (!entity.id || !ids.insert(entity.id).second) out.push_back({true, "entity-id", "Missing or duplicate ID: " + label});
        if (entity.id > 2147483647u) out.push_back({true, "entity-id-range", "ID is outside the editor selection range: " + label});
        for (size_t i = 0; i < 3; ++i) {
            if (!std::isfinite(entity.position[i]) || !std::isfinite(entity.rotation[i]) || !std::isfinite(entity.scale[i])) {
                out.push_back({true, "transform", "Non-finite transform: " + label}); break;
            }
            if (std::abs(entity.scale[i]) < 0.0001f) {
                out.push_back({false, "scale", "Near-zero scale may cause rendering or physics problems: " + label}); break;
            }
        }
        asset_check(out, entity.model_path, label + " model");
        asset_check(out, entity.texture_path, label + " texture");
        if (entity.model_path.empty() && entity.mesh_id >= 100)
            out.push_back({false, "legacy-model", "Model has a transient ID without a source path: " + label});
        if (entity.texture_path.empty() && entity.texture_id >= 6)
            out.push_back({false, "legacy-texture", "Texture has a transient ID without a source path: " + label});
        if (entity.is_point_light && entity.visible) ++active_lights;
        if (entity.physics_type != 0 && entity.collider_shape == 4)
            out.push_back({false, "collider", "Mesh collider is not implemented; editor simulation uses a cylinder: " + label});
    }
    for (const auto& material : scene.materials) {
        const auto label = "Material " + material.name + " (ID " + std::to_string(material.id) + ")";
        asset_check(out, material.albedo_path, label + " albedo");
        asset_check(out, material.normal_path, label + " normal");
    }

    // Validate hierarchy parent references and cycle detection
    for (const auto& entity : scene.entities) {
        if (entity.parent_id != 0) {
            const auto label = entity.name + " (ID " + std::to_string(entity.id) + ")";
            if (entity.parent_id == entity.id) {
                out.push_back({true, "hierarchy-self-parent", "Entity cannot be its own parent: " + label});
            } else if (ids.find(entity.parent_id) == ids.end()) {
                out.push_back({true, "hierarchy-missing-parent", "Entity references non-existent parent ID " + std::to_string(entity.parent_id) + ": " + label});
            } else if (scene.is_descendant_of(entity.parent_id, entity.id)) {
                out.push_back({true, "hierarchy-cycle", "Circular parenting cycle detected for: " + label});
            }
        }
    }

    if (active_lights > 16) out.push_back({false, "light-limit", "Only the first 16 enabled point lights render."});
    if (scene.has_terrain && (scene.terrain_grid_size < 32 || scene.terrain_grid_size > 512 ||
        !std::isfinite(scene.terrain_world_size) || scene.terrain_world_size <= 0.0f ||
        !std::isfinite(scene.terrain_height_scale)))
        out.push_back({true, "terrain-config", "Invalid terrain size, scale, or resolution."});
    if (!scene.terrain_heights.empty() && scene.terrain_heights.size() != static_cast<size_t>(scene.terrain_grid_size) * scene.terrain_grid_size)
        out.push_back({true, "terrain-heightfield", "Terrain height count does not match grid size."});
    if (scene.fog_end <= scene.fog_start) out.push_back({false, "fog-range", "Fog end should be greater than fog start."});
    return out;
}
std::vector<Diagnostic> validate_project(const ProjectData& project) {
    std::vector<Diagnostic> out;
    std::unordered_set<std::string> paths;
    if (!project.last_scene.empty() && !exists(project.last_scene))
        out.push_back({true, "missing-scene", "Last scene is missing: " + project.last_scene});
    for (const auto& asset : project.assets) {
        asset_check(out, asset.path, asset.is_model ? "model" : "texture");
        if (!paths.insert(asset.path).second) out.push_back({false, "duplicate-asset", "Duplicate project entry: " + asset.path});
    }
    return out;
}
}
