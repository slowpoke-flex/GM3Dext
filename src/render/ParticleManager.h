#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <random>

using Microsoft::WRL::ComPtr;

// Particle Types for Magic Effects
enum ParticleType : uint32_t {
    PARTICLE_FIRE      = 0, // Fiery orange/yellow sparks & rising smoke embers
    PARTICLE_ARCANE    = 1, // Swirling celestial cyan & magenta magical vortex
    PARTICLE_HOLY      = 2, // Sparkling golden divine light dust & ascending rays
    PARTICLE_FROST     = 3, // Glacial cyan & sharp ice crystal sparkles
    PARTICLE_SHOCKWAVE = 4  // Expanding horizontal ground energy shockwave ring
};

struct ParticleGPUInstance {
    float px, py, pz;    // 12 bytes - Center Position
    float size;          // 4 bytes  - Current Size
    float r, g, b, a;    // 16 bytes - Color & Alpha
    float rotation;      // 4 bytes  - Rotation angle (radians)
    uint32_t type;       // 4 bytes  - Particle Type
    float pad[2];        // 8 bytes  - 16-byte alignment (total 48 bytes)
};

struct ParticleCPU {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b, a;
    float start_size;
    float end_size;
    float life;
    float max_life;
    float rotation;
    float rot_speed;
    float gravity;
    float drag;
    uint32_t type;
    bool active = false;
};

class ParticleManager {
public:
    ParticleManager();
    ~ParticleManager();

    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    bool initialize(ID3D11Device* device);
    void shutdown();

    void update(float dt);
    void render(ID3D11DeviceContext* context, ID3D11Buffer* scene_cb, const float camera_pos[3]);

    void spawn_burst(uint32_t type, float x, float y, float z, uint32_t count, float speed, float size, float r = -1.0f, float g = -1.0f, float b = -1.0f);
    void spawn_ring(uint32_t type, float x, float y, float z, float radius, uint32_t count, float r = -1.0f, float g = -1.0f, float b = -1.0f);
    void clear();

    [[nodiscard]] uint32_t get_active_count() const noexcept { return m_active_count; }

private:
    static constexpr uint32_t MAX_PARTICLES = 25000;

    std::vector<ParticleCPU> m_particles;
    std::vector<ParticleGPUInstance> m_instance_data;
    uint32_t m_active_count = 0;

    // D3D11 Resources
    ComPtr<ID3D11Buffer>            m_quad_vb;
    ComPtr<ID3D11Buffer>            m_quad_ib;
    ComPtr<ID3D11Buffer>            m_instance_buffer;
    ComPtr<ID3D11VertexShader>      m_vertex_shader;
    ComPtr<ID3D11PixelShader>       m_pixel_shader;
    ComPtr<ID3D11InputLayout>       m_input_layout;
    ComPtr<ID3D11BlendState>        m_blend_state;
    ComPtr<ID3D11DepthStencilState> m_depth_state;
    ComPtr<ID3D11RasterizerState>   m_rasterizer_state;

    std::mt19937 m_rng{ 1337 };

    bool init_shaders_and_geometry(ID3D11Device* device);
};
