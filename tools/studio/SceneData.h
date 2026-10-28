#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace GM3D::Studio {

struct Material {
    bool operator==(const Material&) const = default;
    uint32_t id = 0;
    std::string name = "Default Material";

    // PBR Properties
    float base_color[4] = { 0.85f, 0.88f, 0.92f, 1.0f }; // RGBA tint + opacity
    float roughness = 0.45f;
    float metallic = 0.05f;
    float emissive = 0.0f;
    float emissive_color[3] = { 1.0f, 1.0f, 1.0f };

    // Texture Maps & Paths
    std::string albedo_path;
    std::string normal_path;
    uint16_t albedo_tex_id = 0;
    uint16_t normal_tex_id = 0;

    // UV Coordinates
    float uv_tiling[2] = { 1.0f, 1.0f };
    float uv_offset[2] = { 0.0f, 0.0f };

    // Transparency & Blend
    int alpha_mode = 0; // 0 = Opaque, 1 = Mask, 2 = Blend
    float alpha_cutoff = 0.5f;

    // Presets
    bool is_builtin_preset = false;
    uint32_t preset_index = 0; // 0..8 for built-in shader presets
};

struct Entity {
    bool operator==(const Entity&) const = default;
    uint32_t id = 0;
    std::string name = "Entity";
    std::string tag = "Default";
    uint32_t parent_id = 0;     // 0 = Root (no parent)
    bool is_group = false;       // True if this entity is a group / folder container
    bool is_point_light = false;
    float light_color[3] = { 1.0f, 0.82f, 0.62f };
    float light_intensity = 35.0f;
    float light_radius = 12.0f;
    bool editor_grid = false; // Legacy editor ground plane, suppressed when terrain is visible.

    // Transform
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float rotation[3] = { 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
    float scale[3] = { 1.0f, 1.0f, 1.0f };

    // Rendering Properties
    uint16_t mesh_id = 1;       // 1 = Cube, 2 = Sphere, 3 = Plane, 4 = Cylinder, 5 = Capsule
    uint16_t material_id = 0;   // 0 = Default, 1 = Coral, 2 = Gold, 3 = Emerald, 4 = Water, 5 = Amethyst, 6 = Pine, 7 = Cobalt, 8 = Granite
    std::string model_path;       // UTF-8 source path; IDs are session-local.
    std::string texture_path;
    uint16_t texture_id = 0;    // 0 = Default (White), 1 = Checker, 2 = Wood, 3 = Stone, 4 = Bark, 5 = Terrain
    bool cast_shadow = true;
    bool visible = true;
    float custom_param1 = 0.0f;
    float custom_param2 = 0.0f;

    // Physics Properties (Jolt Physics)
    int physics_type = 0;       // 0 = None, 1 = Static, 2 = Dynamic, 3 = Kinematic
    int collider_shape = 0;     // 0 = Box, 1 = Sphere, 2 = Capsule, 3 = Cylinder, 4 = Mesh
    float mass = 1.0f;
    float friction = 0.6f;
    float restitution = 0.1f;
    uint32_t physics_body_id = 0;
};

struct Scene {
    bool operator==(const Scene&) const = default;
    std::string name = "Untitled Scene";
    std::string filepath = "";
    std::vector<Entity> entities;
    std::vector<Material> materials;
    uint32_t next_entity_id = 1;
    uint32_t next_material_id = 9;

    Scene() {
        init_default_materials();
    }

    void init_default_materials() {
        if (!materials.empty()) return;
        materials.clear();
        Material m0; m0.id = 0; m0.name = "Default White"; m0.is_builtin_preset = true; m0.preset_index = 0;
        materials.push_back(m0);
        Material m1; m1.id = 1; m1.name = "Coral Red"; m1.is_builtin_preset = true; m1.preset_index = 1;
        m1.base_color[0] = 0.92f; m1.base_color[1] = 0.28f; m1.base_color[2] = 0.18f; m1.roughness = 0.35f; m1.metallic = 0.10f;
        materials.push_back(m1);
        Material m2; m2.id = 2; m2.name = "Gold Brass"; m2.is_builtin_preset = true; m2.preset_index = 2;
        m2.base_color[0] = 1.0f; m2.base_color[1] = 0.78f; m2.base_color[2] = 0.25f; m2.roughness = 0.25f; m2.metallic = 0.85f;
        materials.push_back(m2);
        Material m3; m3.id = 3; m3.name = "Emerald Teal"; m3.is_builtin_preset = true; m3.preset_index = 3;
        m3.base_color[0] = 0.18f; m3.base_color[1] = 0.82f; m3.base_color[2] = 0.60f; m3.roughness = 0.30f; m3.metallic = 0.20f;
        materials.push_back(m3);
        Material m4; m4.id = 4; m4.name = "Water Azure"; m4.is_builtin_preset = true; m4.preset_index = 4;
        m4.base_color[0] = 0.12f; m4.base_color[1] = 0.65f; m4.base_color[2] = 0.78f; m4.roughness = 0.06f; m4.metallic = 0.15f;
        materials.push_back(m4);
        Material m5; m5.id = 5; m5.name = "Amethyst Purple"; m5.is_builtin_preset = true; m5.preset_index = 5;
        m5.base_color[0] = 0.70f; m5.base_color[1] = 0.25f; m5.base_color[2] = 0.92f; m5.roughness = 0.35f; m5.metallic = 0.05f;
        materials.push_back(m5);
        Material m6; m6.id = 6; m6.name = "Pine Foliage"; m6.is_builtin_preset = true; m6.preset_index = 6;
        m6.base_color[0] = 0.10f; m6.base_color[1] = 0.28f; m6.base_color[2] = 0.13f; m6.roughness = 0.70f; m6.metallic = 0.02f;
        materials.push_back(m6);
        Material m7; m7.id = 7; m7.name = "Royal Steel"; m7.is_builtin_preset = true; m7.preset_index = 7;
        m7.base_color[0] = 0.12f; m7.base_color[1] = 0.25f; m7.base_color[2] = 0.52f; m7.roughness = 0.25f; m7.metallic = 0.85f;
        materials.push_back(m7);
        Material m8; m8.id = 8; m8.name = "Weathered Granite"; m8.is_builtin_preset = true; m8.preset_index = 8;
        m8.base_color[0] = 0.44f; m8.base_color[1] = 0.42f; m8.base_color[2] = 0.40f; m8.roughness = 0.82f; m8.metallic = 0.02f;
        materials.push_back(m8);
        next_material_id = 9;
    }

    Material* find_material(uint32_t id) {
        for (auto& m : materials) {
            if (m.id == id) return &m;
        }
        return nullptr;
    }

    const Material* find_material(uint32_t id) const {
        for (const auto& m : materials) {
            if (m.id == id) return &m;
        }
        return nullptr;
    }

    // Sun & Directional Light
    float sun_pitch = 64.2f;
    float sun_yaw = 45.0f;
    float sun_color[3] = { 1.0f, 0.96f, 0.88f };
    float sun_intensity = 1.85f;

    // Directional shadow settings
    bool shadows_enabled = true;
    float shadow_strength = 0.85f;
    float shadow_softness = 2.0f;
    float shadow_coverage = 110.0f;

    // Ambient Skylight & Atmosphere
    float ambient_color[3] = { 0.45f, 0.58f, 0.78f };
    float ambient_intensity = 1.15f;
    float sky_color[3] = { 0.12f, 0.38f, 0.85f };
    float fog_start = 160.0f;
    float fog_end = 600.0f;

    // Procedural Island Terrain
    bool has_terrain = true;
    uint32_t terrain_grid_size = 128;
    float terrain_world_size = 240.0f;
    float terrain_height_scale = 32.0f;
    uint32_t terrain_seed = 1337;
    uint32_t terrain_body_id = 0;
    std::vector<float> terrain_heights; // Empty means procedural terrain.

    Entity* find_entity(uint32_t id) {
        for (auto& e : entities) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    const Entity* find_entity(uint32_t id) const {
        for (const auto& e : entities) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    std::vector<uint32_t> get_children(uint32_t parent_id) const {
        std::vector<uint32_t> result;
        for (const auto& e : entities) {
            if (e.parent_id == parent_id) {
                result.push_back(e.id);
            }
        }
        return result;
    }

    bool is_descendant_of(uint32_t entity_id, uint32_t potential_ancestor_id) const {
        if (entity_id == 0 || potential_ancestor_id == 0 || entity_id == potential_ancestor_id) {
            return false;
        }
        uint32_t current_id = entity_id;
        int depth_guard = 0;
        while (current_id != 0 && depth_guard++ < 256) {
            const Entity* ent = find_entity(current_id);
            if (!ent || ent->parent_id == 0) break;
            if (ent->parent_id == potential_ancestor_id) return true;
            current_id = ent->parent_id;
        }
        return false;
    }

    void get_all_descendants(uint32_t parent_id, std::vector<uint32_t>& out) const {
        if (parent_id == 0) return;
        for (const auto& e : entities) {
            if (e.parent_id == parent_id) {
                out.push_back(e.id);
                get_all_descendants(e.id, out);
            }
        }
    }
};

} // namespace GM3D::Studio
