#pragma once

#include "core/SharedMemory.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "ParticleManager.h"
#include "TerrainSystem.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

struct RenderConfig {
    void* native_window_handle = nullptr;
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
    bool debug_validation = false;
};

// Shader Constant Buffer matching HLSL SceneConstants register(b0)
struct alignas(16) SceneConstantBuffer {
    float view_proj[16];          // 64 bytes
    float light_view_proj[16];    // 64 bytes (Cascade 0 Near Sun ViewProj)
    float camera_pos[3];          // 12 bytes
    float time;                   // 4 bytes (simulation time in seconds)
    float sun_dir[3];             // 12 bytes
    float pad1;                   // 4 bytes (ShadowStrength)
    float sun_color[3];           // 12 bytes
    float pad2;                   // 4 bytes (ShadowSoftness)
    float ambient_color[3];       // 12 bytes
    float pad3;                   // 4 bytes (ShadowAtlasSize)
    float fog_color[3];           // 12 bytes
    float fog_start;              // 4 bytes
    float fog_end;                // 4 bytes
    float pad_fog[3];             // 12 bytes (pad_fog[0] = CSM enabled flag)
    float inv_view_proj[16];      // 64 bytes
    float light_view_proj_c1[16]; // 64 bytes (Cascade 1 Far Sun ViewProj)
};

// Shader Constant Buffer matching HLSL LightConstants register(b2)
struct alignas(16) GPULightConstants {
    struct GPUPointLight {
        float pos[3];
        float radius;
        float color[3];
        float intensity;
    } lights[16];
    int32_t point_light_count = 0;
    int32_t max_shadow_lights = 4;
    float pad[2] = {0, 0};
    int32_t shadow_slots[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
};

// Shader Constant Buffer matching HLSL MaterialConstants register(b3)
struct alignas(16) MaterialGPU {
    float base_color[4] = { 0.85f, 0.88f, 0.92f, 1.0f }; // RGBA: albedo tint and opacity
    float roughness = 0.45f;
    float metallic = 0.05f;
    float emissive = 0.0f;
    float alpha_cutoff = 0.5f;
    float uv_tiling[2] = { 1.0f, 1.0f };
    float uv_offset[2] = { 0.0f, 0.0f };
    int32_t alpha_mode = 0;   // 0: Opaque, 1: Mask (cutoff), 2: Blend (transparency)
    int32_t preset_id = 0;    // 0: Custom PBR, 1..8: Builtin Presets, 100+: Unlit Emissive
    float pad[2] = { 0.0f, 0.0f };
};

class RenderWorld {
public:
    RenderWorld();
    ~RenderWorld();

    RenderWorld(const RenderWorld&) = delete;
    RenderWorld& operator=(const RenderWorld&) = delete;

    bool initialize(const RenderConfig& config);
    void shutdown();
    void resize(uint32_t width, uint32_t height);

    void render_frame(const SharedBufferHeader& header, const SharedEntityData* entities, uint32_t count, float delta_time);
    void set_viewport_rect(int32_t x, int32_t y, int32_t w, int32_t h);

    [[nodiscard]] bool is_initialized() const noexcept { return m_initialized; }
    [[nodiscard]] void* get_window_handle() const noexcept { return m_config.native_window_handle; }
    [[nodiscard]] MeshManager& get_mesh_manager() { return m_mesh_manager; }
    [[nodiscard]] GM3D::TextureManager& get_texture_manager() { return m_texture_manager; }
    uint16_t load_obj_mesh(const std::string& filepath);
    uint16_t load_model(const std::string& filepath);
    uint16_t load_texture(const std::string& filepath);
    bool set_normal_map(uint16_t target_tex_id, uint16_t normal_tex_id);

    // Dynamic PBR Materials
    void set_material_properties(uint16_t id, const MaterialGPU& props);
    void set_material_properties(uint16_t id, const MaterialGPU& props, uint16_t albedo_tex, uint16_t normal_tex);
    void clear_materials();
    const MaterialGPU* get_material_properties(uint16_t id) const;

    // Lighting & Atmosphere Controls
    void set_sun_light(float dx, float dy, float dz, float r, float g, float b, float intensity);
    void set_ambient_light(float r, float g, float b, float intensity);
    void set_point_light(uint32_t index, float x, float y, float z, float radius, float r, float g, float b, float intensity, bool cast_shadow = true);
    void clear_point_lights();
    void set_shadow_settings(bool enabled, float strength, float softness, float coverage);
    void set_shadow_quality(int quality, bool csm_enabled = true, int max_shadow_lights = 4);
    void set_sky_color(float r, float g, float b);
    void set_fog(float start, float end, float r = -1.0f, float g = -1.0f, float b = -1.0f);

    // 3D Transform Gizmo
    struct GizmoData {
        bool enabled = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float sx = 1.0f;
        float sy = 1.0f;
        float sz = 1.0f;
        float axis_len = 2.8f;
    };
    void set_gizmo(bool enabled, float x, float y, float z, float sx, float sy, float sz, float axis_len = 2.8f);

    // Magic Particle Effects
    void particle_burst(uint32_t type, float x, float y, float z, uint32_t count, float speed, float size, float r = -1.0f, float g = -1.0f, float b = -1.0f);
    void particle_ring(uint32_t type, float x, float y, float z, float radius, uint32_t count, float r = -1.0f, float g = -1.0f, float b = -1.0f);
    void particle_clear();

    // HDR Bloom & Post-Processing Controls
    void set_bloom(bool enabled, float intensity = 0.85f, float threshold = 1.0f);

    // Animation Controls
    bool play_animation(uint32_t entity_id, int32_t anim_index, float speed = 1.0f, bool loop = true);
    bool stop_animation(uint32_t entity_id);
    bool set_animation_time(uint32_t entity_id, float time);
    uint32_t get_animation_count(uint16_t mesh_id) const;
    std::string get_animation_name(uint16_t mesh_id, uint32_t anim_index) const;
    float get_animation_duration(uint16_t mesh_id, uint32_t anim_index) const;

    // Procedural Terrain System
    uint32_t generate_terrain(uint32_t grid_size, float world_size, float height_scale, uint32_t seed, PhysicsWorld& physics_world, const std::vector<float>* heights = nullptr);
    bool update_terrain_mesh() { return m_terrain_system.update_mesh(m_device.Get(), m_mesh_manager); }
    bool update_terrain_mesh_region(float min_x, float min_z, float max_x, float max_z) {
        return m_terrain_system.update_mesh_region(m_device.Get(), m_context.Get(), m_mesh_manager, min_x, min_z, max_x, max_z);
    }
    float get_terrain_height(float world_x, float world_z) const;

    // Studio & Editor Accessors
    ID3D11Device* get_device() const { return m_device.Get(); }
    ID3D11DeviceContext* get_context() const { return m_context.Get(); }
    IDXGISwapChain* get_swap_chain() const { return m_swap_chain.Get(); }
    ID3D11RenderTargetView* get_back_buffer_rtv() const { return m_render_target_view.Get(); }
    GM3D::TerrainSystem& get_terrain_system() { return m_terrain_system; }
    void set_custom_render_target(ID3D11RenderTargetView* rtv) { m_custom_render_target = rtv; }

private:
    bool m_initialized = false;
    RenderConfig m_config;
    ComPtr<ID3D11RenderTargetView> m_custom_render_target;

    HWND m_parent_hwnd = nullptr;
    HWND m_child_hwnd = nullptr;
    int32_t m_vp_x = 0;
    int32_t m_vp_y = 0;
    int32_t m_vp_w = 0;
    int32_t m_vp_h = 0;

    // D3D11 Core Interfaces
    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain>         m_swap_chain;
    ComPtr<ID3D11RenderTargetView> m_render_target_view;
    ComPtr<ID3D11Texture2D>        m_depth_stencil_buffer;
    ComPtr<ID3D11DepthStencilView> m_depth_stencil_view;
    ComPtr<ID3D11DepthStencilState> m_depth_stencil_state;
    ComPtr<ID3D11DepthStencilState> m_gizmo_depth_stencil_state;
    ComPtr<ID3D11RasterizerState>   m_rasterizer_state;

    // Real-Time Cascaded Sun & Omnidirectional Point Light Shadow Atlas
    static constexpr UINT SHADOW_MAP_SIZE = 4096;
    UINT m_shadow_atlas_size = 4096;
    int m_shadow_quality = 2; // 0 = Low (1024), 1 = Medium (2048), 2 = High (4096)
    bool m_csm_enabled = true;
    int m_max_shadow_lights = 4;
    bool m_point_light_cast_shadow[16] = {};
    SceneConstantBuffer m_cached_scene_cb{};
    ComPtr<ID3D11Texture2D>          m_shadow_texture;
    ComPtr<ID3D11DepthStencilView>   m_shadow_dsv;
    ComPtr<ID3D11ShaderResourceView> m_shadow_srv;
    ComPtr<ID3D11SamplerState>       m_shadow_sampler;
    ComPtr<ID3D11RasterizerState>    m_shadow_rasterizer_state;
    ComPtr<ID3D11DepthStencilState>  m_shadow_depth_stencil_state;
    ComPtr<ID3D11VertexShader>       m_shadow_vertex_shader;
    bool create_shadow_atlas(UINT atlas_size);

    // Shaders & Input Layout
    ComPtr<ID3D11VertexShader>     m_vertex_shader;
    ComPtr<ID3D11PixelShader>      m_pixel_shader;
    ComPtr<ID3D11VertexShader>     m_sky_vertex_shader;
    ComPtr<ID3D11PixelShader>      m_sky_pixel_shader;
    ComPtr<ID3D11DepthStencilState> m_sky_depth_stencil_state;
    ComPtr<ID3D11RasterizerState>   m_sky_rasterizer_state;
    ComPtr<ID3D11InputLayout>      m_input_layout;
    ComPtr<ID3D11Buffer>           m_constant_buffer;
    ComPtr<ID3D11Buffer>           m_instance_buffer;
    ComPtr<ID3D11Buffer>           m_bone_constant_buffer;
    ComPtr<ID3D11Buffer>           m_light_constant_buffer;
    ComPtr<ID3D11Buffer>           m_material_constant_buffer;
    ComPtr<ID3D11BlendState>       m_alpha_blend_state;

    std::unordered_map<uint16_t, MaterialGPU> m_materials;
    std::unordered_map<uint16_t, std::pair<uint16_t, uint16_t>> m_material_textures;

    GPULightConstants m_light_constants{};
    float m_sun_dir[3] = { -0.577f, -0.707f, 0.408f };
    float m_sun_color[3] = { 1.0f, 0.95f, 0.90f };
    float m_sun_intensity = 2.8f;
    float m_ambient_color[3] = { 0.22f, 0.28f, 0.38f };
    float m_ambient_intensity = 1.6f;
    float m_sky_color[4] = { 0.38f, 0.52f, 0.68f, 1.0f };
    float m_fog_start = 35.0f;
    float m_fog_end = 110.0f;
    float m_total_time = 0.0f;
    bool m_shadows_enabled = true;
    float m_shadow_strength = 0.85f;
    float m_shadow_softness = 2.0f;
    float m_shadow_coverage = 110.0f;

    struct EntityAnimState {
        int32_t anim_index = -1;
        float current_time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool is_playing = false;
    };
    std::unordered_map<uint32_t, EntityAnimState> m_entity_anims;

    // Subsystems
    MeshManager m_mesh_manager;
    GM3D::TextureManager m_texture_manager;
    ParticleManager m_particle_manager;
    GM3D::TerrainSystem m_terrain_system;
    uint32_t m_max_instances = 10000;
    GizmoData m_gizmo{};

    // HDR & Post-Processing Pipeline
    bool m_bloom_enabled = true;
    float m_bloom_threshold = 1.0f;
    float m_bloom_intensity = 0.85f;
    float m_exposure = 1.15f;

    ComPtr<ID3D11Texture2D>          m_hdr_texture;
    ComPtr<ID3D11RenderTargetView>   m_hdr_rtv;
    ComPtr<ID3D11ShaderResourceView> m_hdr_srv;

    ComPtr<ID3D11Texture2D>          m_bloom_tex[2];
    ComPtr<ID3D11RenderTargetView>   m_bloom_rtv[2];
    ComPtr<ID3D11ShaderResourceView> m_bloom_srv[2];
    uint32_t m_bloom_w = 0;
    uint32_t m_bloom_h = 0;

    ComPtr<ID3D11VertexShader>       m_post_vs;
    ComPtr<ID3D11PixelShader>        m_bloom_extract_ps;
    ComPtr<ID3D11PixelShader>        m_blur_h_ps;
    ComPtr<ID3D11PixelShader>        m_blur_v_ps;
    ComPtr<ID3D11PixelShader>        m_composite_ps;
    ComPtr<ID3D11SamplerState>       m_linear_clamp_sampler;
    ComPtr<ID3D11Buffer>             m_post_cb;

    bool init_device_and_swapchain();
    bool init_shaders();
    bool init_shadow_resources();
    bool init_instance_buffer();
    bool init_post_processing_resources();
    bool create_post_textures(uint32_t width, uint32_t height);
    void update_view_proj(const SharedBufferHeader& header);
    void render_sky();
    void draw_gizmo(const SharedBufferHeader& header);
    void render_post_processing();
};
