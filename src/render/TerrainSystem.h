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

struct TerrainLayer {
    float color[3] = { 0.22f, 0.36f, 0.15f };
    float roughness = 0.78f;
    float scale = 1.0f;
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
    bool update_mesh_region(ID3D11Device* device, ID3D11DeviceContext* context, MeshManager& mesh_mgr,
                            float min_x, float min_z, float max_x, float max_z);
    bool update_albedo(ID3D11Device* device, ID3D11DeviceContext* context, TextureManager& tex_mgr,
                       float min_x, float min_z, float max_x, float max_z, bool create = false);
    bool replace_heights(const std::vector<float>& heights);
    void set_layers(const std::vector<TerrainLayer>& layers);
    void set_splatmap(const std::vector<uint8_t>& splatmap);
    void update_collider(PhysicsWorld& physics_world);
    void clear(PhysicsWorld& physics_world);

    // Bakes a compact RGBA8 weight map (grass/rock/sand/cobble) that the terrain
    // pixel shader uses to blend the tiled PBR detail textures. Rebuilt whenever the
    // heights or the painted splatmap change.
    bool update_detail_splat(ID3D11Device* device, ID3D11DeviceContext* context);
    ID3D11ShaderResourceView* get_detail_splat_srv() const noexcept { return m_detail_splat_srv.Get(); }
    bool has_detail_splat() const noexcept { return m_detail_splat_srv != nullptr; }

    const std::vector<float>& heights() const noexcept { return m_heights; }
    std::vector<float>& heights_mut() noexcept { return m_heights; }
    const std::vector<uint8_t>& splatmap() const noexcept { return m_splatmap; }
    std::vector<uint8_t>& splatmap_mut() noexcept { return m_splatmap; }
    const std::vector<TerrainLayer>& layers() const noexcept { return m_layers; }

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
    std::vector<float> m_heights;     // grid_size * grid_size samples
    std::vector<Vertex3D> m_vertices; // Cached vertex array for fast regional updates
    std::vector<uint32_t> m_indices;  // Cached index array
    std::vector<TerrainLayer> m_layers;
    std::vector<uint8_t> m_splatmap;  // grid_size * grid_size * 4 RGBA layer weights
    ComPtr<ID3D11Texture2D> m_detail_splat_texture;
    ComPtr<ID3D11ShaderResourceView> m_detail_splat_srv;
};

} // namespace GM3D
