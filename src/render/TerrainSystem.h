#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <d3d11.h>
#include "MeshManager.h"
#include "TextureManager.h"
#include "physics/PhysicsWorld.h"

namespace GM3D {

struct TerrainConfig {
    uint32_t grid_size = 128;      // Grid resolution (128x128 vertices = 16,384 vertices, 32,258 triangles)
    float world_size = 240.0f;     // Total width & length in meters (-120m to +120m)
    float height_scale = 32.0f;    // Peak mountain height in meters
    uint32_t seed = 1337;          // Procedural seed
};

class TerrainSystem {
public:
    TerrainSystem() = default;
    ~TerrainSystem() = default;

    // Generates terrain mesh, multi-biome albedo + normal map, and Jolt heightfield body
    uint32_t generate(ID3D11Device* device, ID3D11DeviceContext* context, 
                      MeshManager& mesh_mgr, TextureManager& tex_mgr, PhysicsWorld& physics_world,
                      const TerrainConfig& config, const std::vector<float>* heights = nullptr);

    bool update_mesh(ID3D11Device* device, MeshManager& mesh_mgr);
    bool update_albedo(ID3D11Device* device, ID3D11DeviceContext* context, TextureManager& tex_mgr,
                       float min_x, float min_z, float max_x, float max_z, bool create = false);
    bool replace_heights(const std::vector<float>& heights);
    void update_collider(PhysicsWorld& physics_world);
    void clear(PhysicsWorld& physics_world);
    const std::vector<float>& heights() const noexcept { return m_heights; }

    // Instant O(1) bilinear height query at any world (X, Z) coordinate
    float get_height(float world_x, float world_z) const;

    // Surface normal query at any world (X, Z) coordinate
    void get_normal(float world_x, float world_z, float& out_nx, float& out_ny, float& out_nz) const;

    [[nodiscard]] bool is_generated() const noexcept { return m_generated; }
    [[nodiscard]] const TerrainConfig& get_config() const noexcept { return m_config; }
    [[nodiscard]] uint32_t get_physics_body_id() const noexcept { return m_physics_body_id; }

    static constexpr uint16_t TERRAIN_MESH_ID = 5;
    static constexpr uint16_t TERRAIN_TEXTURE_ID = 5;

private:
    TerrainConfig m_config;
    bool m_generated = false;
    uint32_t m_physics_body_id = 0;
    std::vector<float> m_heights; // grid_size * grid_size samples
};

} // namespace GM3D
