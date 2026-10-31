#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include "TerrainSystem.h"

using Microsoft::WRL::ComPtr;

struct SceneConstantBuffer;

namespace GM3D {

struct GrassConfig {
    bool enabled = false;           // Default: disabled/absent until painted
    float density = 1.0f;           // Density multiplier (0.1 to 3.0)
    float base_height = 0.85f;      // Clump height in meters
    float base_width = 0.22f;       // Clump width in meters
    float max_distance = 95.0f;     // Max visibility distance before fading
    float fade_range = 25.0f;       // Transition range for smooth fade to 0
    float wind_speed = 1.8f;        // Wind oscillation frequency
    float wind_strength = 0.35f;    // Maximum sway displacement
    float wind_dir[2] = { 0.707f, 0.707f }; // Wind direction vector (X, Z)
    // Two ends of the per-instance colour blend. The default pair is the alpine green the
    // generator used to hardcode; a scene can repaint the whole carpet (violet meadow, dry
    // savanna, autumn field) without touching the shader.
    float color_a[3] = { 0.20f, 0.38f, 0.13f }; // shaded biome end
    float color_b[3] = { 0.30f, 0.46f, 0.16f }; // sunlit biome end
    uint32_t seed = 4242;           // Procedural distribution seed
};

struct GrassGPUInstance {
    float px, py, pz;               // 12 bytes - Root world position
    float rotation;                 // 4 bytes  - Yaw rotation in radians
    float scale_x, scale_y;         // 8 bytes  - Width and height scale
    float wind_phase;               // 4 bytes  - Individual wind phase offset
    float r, g, b;                  // 12 bytes - Blade tint color
    float pad[2];                   // 8 bytes  - 16-byte alignment (total 48 bytes)
};

struct GrassVertex {
    float x, y, z;                  // 12 bytes - Local position
    float nx, ny, nz;               // 12 bytes - Smoothed surface normal
    float u, v;                     // 8 bytes  - Texture / gradient UVs
    float wind_weight;              // 4 bytes  - Quadratic deflection weight (0 root, 1 tip)
    float ao;                       // 4 bytes  - Ambient occlusion factor (0.25 root, 1.0 tip)
};

struct alignas(16) GrassConstantBuffer {
    float view_proj[16];            // 64 bytes
    float camera_pos[3];            // 12 bytes
    float time;                     // 4 bytes
    float wind_params[4];           // 16 bytes: dir_x, dir_z, speed, strength
    float grass_params[4];          // 16 bytes: max_dist, fade_range, base_h, base_w
    float interaction_collider[4];  // 16 bytes: pos_x, pos_y, pos_z, radius
    float sun_dir[3];               // 12 bytes
    float sun_intensity;            // 4 bytes
    float sun_color[3];             // 12 bytes
    float ambient_intensity;        // 4 bytes
    float ambient_color[3];         // 12 bytes
    float pad0;                     // 4 bytes
    float fog_params[4];            // 16 bytes: start, end, unused, unused
    float fog_color[3];             // 12 bytes
    float pad1;                     // 4 bytes
};

class GrassSystem {
public:
    GrassSystem() = default;
    ~GrassSystem();

    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;

    bool initialize(ID3D11Device* device);
    void shutdown();

    void build_from_terrain(ID3D11Device* device, const TerrainSystem& terrain, const GrassConfig& config);
    void build_from_terrain(ID3D11Device* device, const TerrainSystem& terrain) {
        build_from_terrain(device, terrain, m_config);
    }

    void render(ID3D11DeviceContext* context, const SceneConstantBuffer& scene_cb,
                const float camera_pos[3], float total_time, const float interaction_pos[4] = nullptr);

    void set_enabled(bool enabled) noexcept { m_config.enabled = enabled; }
    [[nodiscard]] bool is_enabled() const noexcept { return m_config.enabled && m_instance_count > 0; }

    void clear();
    void set_instances(const std::vector<GrassGPUInstance>& instances, ID3D11Device* device);
    [[nodiscard]] const std::vector<GrassGPUInstance>& get_instances() const noexcept { return m_instances; }
    [[nodiscard]] std::vector<GrassGPUInstance>& get_instances_mut() noexcept { return m_instances; }

    uint32_t paint_brush(float hit_x, float hit_z, float radius, float density, float dt,
                         const TerrainSystem& terrain, float base_w, float base_h,
                         const float tint[3], ID3D11Device* device);

    uint32_t erase_brush(float hit_x, float hit_z, float radius, ID3D11Device* device);

    void snap_to_terrain(const TerrainSystem& terrain, ID3D11Device* device);

    bool save_to_file(const std::string& filepath) const;
    bool load_from_file(const std::string& filepath, ID3D11Device* device);

    void set_wind(float dir_x, float dir_z, float speed, float strength) noexcept {
        m_config.wind_dir[0] = dir_x;
        m_config.wind_dir[1] = dir_z;
        m_config.wind_speed = speed;
        m_config.wind_strength = strength;
    }

    void set_config(const GrassConfig& config) noexcept { m_config = config; }
    [[nodiscard]] const GrassConfig& get_config() const noexcept { return m_config; }
    [[nodiscard]] GrassConfig& get_config_mut() noexcept { return m_config; }
    [[nodiscard]] uint32_t get_instance_count() const noexcept { return m_instance_count; }

private:
    GrassConfig m_config;
    uint32_t m_instance_count = 0;
    std::vector<GrassGPUInstance> m_instances;
    float m_brush_accum = 0.0f;
    uint32_t m_brush_seed = 12345u;

    // Direct3D 11 Resources
    ComPtr<ID3D11Buffer>             m_vertex_buffer;
    ComPtr<ID3D11Buffer>             m_index_buffer;
    ComPtr<ID3D11Buffer>             m_instance_buffer;
    ComPtr<ID3D11Buffer>             m_constant_buffer;
    ComPtr<ID3D11VertexShader>       m_vertex_shader;
    ComPtr<ID3D11PixelShader>        m_pixel_shader;
    ComPtr<ID3D11InputLayout>        m_input_layout;
    ComPtr<ID3D11RasterizerState>    m_rasterizer_state;
    ComPtr<ID3D11DepthStencilState>  m_depth_state;

    bool init_geometry(ID3D11Device* device);
    bool init_shaders(ID3D11Device* device);
    bool update_instance_buffer(ID3D11Device* device);
};

} // namespace GM3D
