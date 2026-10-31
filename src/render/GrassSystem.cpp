#include "GrassSystem.h"
#include "RenderWorld.h"
#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include <fstream>

namespace GM3D {

namespace {

inline uint32_t hashU32(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    h *= 0x27D4EB2Du;
    h ^= h >> 16;
    return h;
}

inline float hash01(int32_t x, int32_t y, uint32_t seed) {
    return static_cast<float>(hashU32(x, y, seed) & 0xFFFFFFu) / 16777215.0f;
}

const char* s_grass_hlsl = R"(
cbuffer GrassCB : register(b0) {
    row_major float4x4 g_view_proj;
    float4   g_camera_pos_time;
    float4   g_wind_params;
    float4   g_grass_params;
    float4   g_interaction_collider;
    float4   g_sun_dir_intensity;
    float4   g_sun_color;
    float4   g_ambient_color;
    float4   g_fog_params;
    float4   g_fog_color;
};

struct VSInput {
    // Stream 0: Per-vertex geometry
    float3 pos          : POSITION;
    float3 normal       : NORMAL;
    float2 uv           : TEXCOORD0;
    float2 wind_ao      : TEXCOORD1;

    // Stream 1: Per-instance data
    float3 instPos      : INST_POS;
    float  instRotation : INST_ROT;
    float2 instScale    : INST_SCALE;
    float  instWindPhase: INST_PHASE;
    float3 instColor    : INST_COLOR;
    float2 instPad      : INST_PAD;
};

struct PSInput {
    float4 pos          : SV_POSITION;
    float3 worldPos     : POSITION;
    float3 normal       : NORMAL;
    float2 uv           : TEXCOORD0;
    float3 color        : COLOR0;
    float  ao           : TEXCOORD1;
};

PSInput VS_Grass(VSInput input) {
    PSInput output;

    float3 worldRoot = input.instPos;
    float distToCam = distance(worldRoot, g_camera_pos_time.xyz);

    float maxDist = g_grass_params.x;
    float fadeRange = g_grass_params.y;
    float distFade = saturate((maxDist - distToCam) / max(fadeRange, 0.001));
    float nearFade = saturate((distToCam - 1.5) / 1.0);
    distFade *= nearFade;

    if (distFade <= 0.0) {
        output.pos = float4(0.0, 0.0, 2.0, 1.0);
        output.worldPos = worldRoot;
        output.normal = float3(0, 1, 0);
        output.uv = float2(0, 0);
        output.color = float3(0, 0, 0);
        output.ao = 0.0;
        return output;
    }

    // 1. Rotate blade by instance yaw
    float s, c;
    sincos(input.instRotation, s, c);
    float3 localPos;
    localPos.x = input.pos.x * c - input.pos.z * s;
    localPos.z = input.pos.x * s + input.pos.z * c;
    localPos.y = input.pos.y;

    float3 localNorm;
    localNorm.x = input.normal.x * c - input.normal.z * s;
    localNorm.z = input.normal.x * s + input.normal.z * c;
    localNorm.y = input.normal.y;

    // Apply instance scaling. Distant blades are thinned instead of shrunk, so the
    // field does not appear to sink into the ground at range.
    float widthFade = 0.55 + 0.45 * distFade;
    localPos.xz *= input.instScale.x * g_grass_params.w * widthFade;
    localPos.y  *= input.instScale.y * g_grass_params.z * (0.80 + 0.20 * distFade);

    // 2. Dynamic Multi-Frequency Wind Simulation
    float windWeight = input.wind_ao.x;
    float time = g_camera_pos_time.w;
    float windSpeed = g_wind_params.z;
    float windStrength = g_wind_params.w;
    float2 windDir = normalize(g_wind_params.xy + float2(0.0001, 0.0001));

    float wave1 = sin(time * windSpeed + dot(worldRoot.xz, float2(0.35, 0.28)) + input.instWindPhase);
    float wave2 = sin(time * windSpeed * 2.3 + dot(worldRoot.xz, float2(-0.42, 0.51)) + input.instWindPhase * 1.6) * 0.4;
    float gust  = sin(time * windSpeed * 0.35 + dot(worldRoot.xz, float2(0.09, 0.07))) * 0.5 + 0.5;

    float windDisplacement = (wave1 + wave2) * (0.65 + 0.35 * gust) * windStrength * windWeight * distFade;
    localPos.xz += windDir * windDisplacement;
    localPos.y  -= abs(windDisplacement) * 0.22 * windWeight;

    // 3. Dynamic Player / Entity Trampling Interaction
    float3 finalWorld = worldRoot + localPos;
    if (g_interaction_collider.w > 0.001) {
        float3 colPos = g_interaction_collider.xyz;
        float pushRadius = g_interaction_collider.w;
        float3 toCollider = finalWorld - colPos;
        toCollider.y *= 1.4;
        float d = length(toCollider);
        if (d < pushRadius && d > 0.001) {
            float pushAmount = (1.0 - (d / pushRadius)) * windWeight;
            float3 pushDir = normalize(toCollider);
            localPos.xz += pushDir.xz * pushAmount * 1.35;
            localPos.y  -= pushAmount * 0.45;
            finalWorld = worldRoot + localPos;
        }
    }

    output.pos = mul(float4(finalWorld, 1.0), g_view_proj);
    output.worldPos = finalWorld;
    output.normal = normalize(localNorm);
    output.uv = input.uv;
    output.color = input.instColor;
    output.ao = input.wind_ao.y;
    return output;
}

float4 PS_Grass(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    // 1. Two-Sided Shading
    float3 N = normalize(input.normal);
    if (!isFrontFace) {
        // Foliage is two-sided but not mirror-symmetric: flip the horizontal part only and
        // keep the upward bias, otherwise every back face points away from the sun and the
        // blade renders as a black spike.
        N = float3(-N.x, N.y, -N.z);
    }

    // 2. Base Color Gradient (dark root to sunlit tip).
    // Instance tints are authored as sRGB-ish values, so square them into linear space
    // before lighting; otherwise the blades glow white against the linearised terrain.
    float tipFactor = saturate(input.uv.y);
    float3 rootColor = input.color * 0.62;
    float3 tipColor  = input.color * 1.02 + float3(0.01, 0.03, 0.00);
    float3 baseColor = lerp(rootColor, tipColor, tipFactor * tipFactor);
    baseColor *= sqrt(baseColor);

    float edgeDarkening = 0.88 + 0.12 * (1.0 - abs(input.uv.x - 0.5) * 2.0);
    baseColor *= edgeDarkening;

    // 3. Direct Sunlight Lighting & Subsurface Scattering (SSS)
    float3 L = normalize(-g_sun_dir_intensity.xyz);
    float3 V = normalize(g_camera_pos_time.xyz - input.worldPos);

    float NdotL = dot(N, L);
    float diffuse = saturate(NdotL * 0.75 + 0.25);

    float3 SSS_dir = normalize(L + N * 0.35);
    float VdotL = saturate(dot(-V, SSS_dir));
    float sss = pow(VdotL, 3.0) * 0.55 * tipFactor;

    float sunIntensity = g_sun_dir_intensity.w;
    float3 directLight = (diffuse + sss) * g_sun_color.xyz * sunIntensity;

    // 4. Ambient Sky Light with AO contact shadowing
    float3 ambient = g_ambient_color.xyz * g_sun_color.w * input.ao;

    // 5. Specular Sunlight Glint
    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specular = pow(NdotH, 24.0) * 0.10 * tipFactor * sunIntensity;

    float3 finalRGB = baseColor * (directLight + ambient) + g_sun_color.xyz * specular;

    // 6. Distance Atmospheric Fog Blend
    float distToCam = distance(input.worldPos, g_camera_pos_time.xyz);
    float fogStart = g_fog_params.x;
    float fogEnd   = g_fog_params.y;
    float fogFactor = saturate((distToCam - fogStart) / max(fogEnd - fogStart, 0.001));
    finalRGB = lerp(finalRGB, g_fog_color.xyz, fogFactor);

    return float4(finalRGB, 1.0);
}
)";

} // namespace

GrassSystem::~GrassSystem() {
    shutdown();
}

void GrassSystem::shutdown() {
    m_instances.clear();
    m_instance_count = 0;
    m_vertex_buffer.Reset();
    m_index_buffer.Reset();
    m_instance_buffer.Reset();
    m_constant_buffer.Reset();
    m_vertex_shader.Reset();
    m_pixel_shader.Reset();
    m_input_layout.Reset();
    m_rasterizer_state.Reset();
    m_depth_state.Reset();
}

bool GrassSystem::initialize(ID3D11Device* device) {
    if (!device) return false;

    if (!init_geometry(device)) return false;
    if (!init_shaders(device)) return false;

    // 1. Constant Buffer
    D3D11_BUFFER_DESC cbd{};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(GrassConstantBuffer);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&cbd, nullptr, m_constant_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // 2. Two-Sided Rasterizer State
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE; // Two-sided grass blades
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&rd, m_rasterizer_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // 3. Depth-Stencil State (solid depth testing and writing)
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dsd.StencilEnable = FALSE;
    hr = device->CreateDepthStencilState(&dsd, m_depth_state.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

bool GrassSystem::init_geometry(ID3D11Device* device) {
    // A clump is a tuft: several thin, tapered, forward-curved blades scattered
    // across the tuft footprint. Blade width stays a fraction of the tuft so the
    // silhouette reads as grass instead of oversized cards.
    std::vector<GrassVertex> vertices;
    std::vector<uint16_t> indices;

    const float h = 1.0f;
    const float blade_half_width = 0.048f;   // fraction of the tuft footprint
    const float spread = 0.46f;              // root scatter radius, same units
    const int rows = 3;                      // 2 segments per blade
    const int blade_count = 12;

    for (int i = 0; i < blade_count; ++i) {
        const float fi = static_cast<float>(i);
        const float hash = std::fmod(fi * 0.6180339887f, 1.0f);
        const float yaw = fi * 2.39996323f;                  // golden angle
        const float offset_yaw = yaw * 1.7f;
        const float height = 0.62f + 0.38f * hash;
        const float width = 0.85f + 0.30f * hash;
        const float bend = 0.45f + 0.95f * hash;
        const float radius = (0.18f + 0.82f * hash) * spread;

        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const float wx = c, wz = s;      // width axis
        const float fx = -s, fz = c;     // bend axis

        const float ox = std::cos(offset_yaw) * radius;
        const float oz = std::sin(offset_yaw) * radius;

        // Face normal: mostly up so the tuft shades softly, tilted toward the bend.
        float nx = fx * 0.45f;
        float ny = 0.88f;
        float nz = fz * 0.45f;
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= nlen; ny /= nlen; nz /= nlen;

        const uint16_t base_idx = static_cast<uint16_t>(vertices.size());

        for (int r = 0; r < rows; ++r) {
            const float t = static_cast<float>(r) / static_cast<float>(rows - 1);
            const float taper = std::pow(1.0f - t, 0.65f);
            const float half = (r == rows - 1 ? blade_half_width * 0.15f : blade_half_width) * width * taper;
            const float lean = bend * t * t;      // quadratic forward bend
            const float y = h * height * t;

            const float cx = ox + fx * lean * 0.35f;
            const float cz = oz + fz * lean * 0.35f;

            GrassVertex left{};
            left.x = cx - wx * half;
            left.y = y;
            left.z = cz - wz * half;
            left.nx = nx; left.ny = ny; left.nz = nz;
            left.u = 0.0f; left.v = t;
            left.wind_weight = t * t;
            left.ao = 0.45f + 0.55f * t;

            GrassVertex right{};
            right.x = cx + wx * half;
            right.y = y;
            right.z = cz + wz * half;
            right.nx = nx; right.ny = ny; right.nz = nz;
            right.u = 1.0f; right.v = t;
            right.wind_weight = t * t;
            right.ao = 0.45f + 0.55f * t;

            vertices.push_back(left);
            vertices.push_back(right);
        }

        for (int r = 0; r < rows - 1; ++r) {
            const uint16_t v0 = static_cast<uint16_t>(base_idx + r * 2);
            const uint16_t v1 = static_cast<uint16_t>(v0 + 1);
            const uint16_t v2 = static_cast<uint16_t>(v0 + 2);
            const uint16_t v3 = static_cast<uint16_t>(v0 + 3);
            indices.push_back(v0); indices.push_back(v1); indices.push_back(v2);
            indices.push_back(v1); indices.push_back(v3); indices.push_back(v2);
        }
    }

    D3D11_BUFFER_DESC vbd{};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GrassVertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{ vertices.data(), 0, 0 };
    if (FAILED(device->CreateBuffer(&vbd, &vsd, m_vertex_buffer.GetAddressOf()))) return false;

    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd{ indices.data(), 0, 0 };
    if (FAILED(device->CreateBuffer(&ibd, &isd, m_index_buffer.GetAddressOf()))) return false;

    return true;
}

bool GrassSystem::init_shaders(ID3D11Device* device) {
    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3DBlob> ps_blob;
    ComPtr<ID3DBlob> error_blob;

    HRESULT hr = D3DCompile(s_grass_hlsl, strlen(s_grass_hlsl), "GrassHLSL", nullptr, nullptr,
                            "VS_Grass", "vs_5_0", 0, 0, vs_blob.GetAddressOf(), error_blob.GetAddressOf());
    if (FAILED(hr)) {
        if (error_blob) std::cerr << "[GrassSystem] VS error: " << (char*)error_blob->GetBufferPointer() << "\n";
        return false;
    }

    hr = D3DCompile(s_grass_hlsl, strlen(s_grass_hlsl), "GrassHLSL", nullptr, nullptr,
                    "PS_Grass", "ps_5_0", 0, 0, ps_blob.GetAddressOf(), error_blob.GetAddressOf());
    if (FAILED(hr)) {
        if (error_blob) std::cerr << "[GrassSystem] PS error: " << (char*)error_blob->GetBufferPointer() << "\n";
        return false;
    }

    if (FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, m_vertex_shader.GetAddressOf())))
        return false;

    if (FAILED(device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, m_pixel_shader.GetAddressOf())))
        return false;

    // Dual-stream Input Layout: Stream 0 (Vertex), Stream 1 (Instance)
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        // Stream 0: Vertex
        { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "TEXCOORD",   1, DXGI_FORMAT_R32G32_FLOAT,       0, 32, D3D11_INPUT_PER_VERTEX_DATA,   0 },

        // Stream 1: Instance
        { "INST_POS",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_ROT",   0, DXGI_FORMAT_R32_FLOAT,          1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_SCALE", 0, DXGI_FORMAT_R32G32_FLOAT,       1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_PHASE", 0, DXGI_FORMAT_R32_FLOAT,          1, 24, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_PAD",   0, DXGI_FORMAT_R32G32_FLOAT,       1, 40, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };

    hr = device->CreateInputLayout(layout, ARRAYSIZE(layout), vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), m_input_layout.GetAddressOf());
    return SUCCEEDED(hr);
}

void GrassSystem::build_from_terrain(ID3D11Device* device, const TerrainSystem& terrain, const GrassConfig& config) {
    m_config = config;
    m_instances.clear();

    if (!terrain.is_generated()) {
        m_instance_count = 0;
        return;
    }

    const float world_size = terrain.get_config().world_size;
    const uint32_t N = terrain.get_config().grid_size;
    const auto& splatmap = terrain.splatmap();
    const bool has_splat = (splatmap.size() == static_cast<size_t>(N) * N * 4);

    // Spawning parameters
    const float base_spacing = 0.30f / std::sqrt(std::max(0.1f, config.density));
    const float half_world = world_size * 0.46f; // Avoid outer ocean boundary
    const uint32_t seed = config.seed;

    int32_t steps = static_cast<int32_t>(world_size / base_spacing);
    steps = std::clamp(steps, 20, 500);

    for (int32_t iz = -steps / 2; iz <= steps / 2; ++iz) {
        for (int32_t ix = -steps / 2; ix <= steps / 2; ++ix) {
            float cx = ix * base_spacing;
            float cz = iz * base_spacing;

            // Jitter position within cell
            float jx = (hash01(ix, iz, seed) - 0.5f) * base_spacing * 0.85f;
            float jz = (hash01(ix, iz, seed + 101u) - 0.5f) * base_spacing * 0.85f;
            float wx = cx + jx;
            float wz = cz + jz;

            if (std::abs(wx) > half_world || std::abs(wz) > half_world) continue;

            float wy = terrain.get_height(wx, wz);

            // Water elevation check: grass only grows above coastal shoreline (> 1.4m) and below snow line (< 23.5m)
            if (wy < 1.4f || wy > 23.5f) continue;

            // Slope angle check: surface normal steepness
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            terrain.get_normal(wx, wz, nx, ny, nz);
            if (ny < 0.72f) continue; // Suppress on sheer cliff faces

            // Splatmap check: sample Layer 0 weight (Alpine Grass)
            if (has_splat) {
                float u = std::clamp((wx + world_size * 0.5f) / world_size, 0.0f, 1.0f);
                float v = std::clamp((wz + world_size * 0.5f) / world_size, 0.0f, 1.0f);
                uint32_t gx = std::min(static_cast<uint32_t>(u * (N - 1)), N - 1);
                uint32_t gy = std::min(static_cast<uint32_t>(v * (N - 1)), N - 1);
                size_t idx = (static_cast<size_t>(gy) * N + gx) * 4;
                float grass_weight = static_cast<float>(splatmap[idx + 0]) / 255.0f;
                float stone_weight = static_cast<float>(splatmap[idx + 3]) / 255.0f;
                if (grass_weight < 0.10f || stone_weight > 0.40f) continue;
            }

            GrassGPUInstance inst{};
            inst.px = wx;
            inst.py = wy;
            inst.pz = wz;

            // Random rotation [0, 2*pi)
            inst.rotation = hash01(ix, iz, seed + 202u) * 6.2831853f;

            // Natural scale variation
            float sx = 0.7f + 0.35f * hash01(ix, iz, seed + 303u);
            float sy = 0.55f + 0.35f * hash01(ix, iz, seed + 404u);
            inst.scale_x = sx;
            inst.scale_y = sy;

            // Individual wind phase
            inst.wind_phase = hash01(ix, iz, seed + 505u) * 6.2831853f;

            // Natural biome color variation between the two ends of the configured range.
            float color_blend = hash01(ix, iz, seed + 606u);
            inst.r = config.color_a[0] + (config.color_b[0] - config.color_a[0]) * color_blend;
            inst.g = config.color_a[1] + (config.color_b[1] - config.color_a[1]) * color_blend;
            inst.b = config.color_a[2] + (config.color_b[2] - config.color_a[2]) * color_blend;
            inst.pad[0] = 0.0f;
            inst.pad[1] = 0.0f;

            m_instances.push_back(inst);
            if (m_instances.size() >= 260000) break; // Hard safety ceiling
        }
        if (m_instances.size() >= 260000) break;
    }

    m_instance_count = static_cast<uint32_t>(m_instances.size());
    std::cout << "[GrassSystem] Generated " << m_instance_count << " procedural grass instances.\n";

    if (device) {
        update_instance_buffer(device);
    }
}

void GrassSystem::clear() {
    m_instances.clear();
    m_instance_count = 0;
    m_instance_buffer.Reset();
}

void GrassSystem::set_instances(const std::vector<GrassGPUInstance>& instances, ID3D11Device* device) {
    m_instances = instances;
    m_instance_count = static_cast<uint32_t>(m_instances.size());
    if (device) {
        update_instance_buffer(device);
    }
}

uint32_t GrassSystem::paint_brush(float hit_x, float hit_z, float radius, float density, float dt,
                                  const TerrainSystem& terrain, float base_w, float base_h,
                                  const float tint[3], ID3D11Device* device) {
    if (!terrain.is_generated() || radius <= 0.0f || density <= 0.0f) return 0;
    if (m_instances.size() >= 260000) return 0;

    m_brush_accum += density * 30.0f * std::clamp(dt, 0.001f, 0.05f);
    int to_spawn = static_cast<int>(m_brush_accum);
    if (to_spawn <= 0) return 0;
    m_brush_accum -= static_cast<float>(to_spawn);

    const float world_size = terrain.get_config().world_size;
    const float half_world = world_size * 0.49f;
    const float min_dist_sq = 0.22f * 0.22f;

    uint32_t added = 0;
    for (int i = 0; i < to_spawn && m_instances.size() < 120000; ++i) {
        float r = radius * std::sqrt(hash01(i, static_cast<int>(m_instances.size()), 777u + m_brush_seed++));
        float theta = hash01(i, static_cast<int>(m_instances.size()), 999u + m_brush_seed++) * 6.2831853f;
        float x = hit_x + r * std::cos(theta);
        float z = hit_z + r * std::sin(theta);

        if (std::abs(x) > half_world || std::abs(z) > half_world) continue;

        float nx = 0.0f, ny = 1.0f, nz = 0.0f;
        terrain.get_normal(x, z, nx, ny, nz);
        if (ny < 0.60f) continue;

        bool too_close = false;
        for (const auto& existing : m_instances) {
            float edx = existing.px - x;
            float edz = existing.pz - z;
            if (edx * edx + edz * edz < min_dist_sq) {
                too_close = true;
                break;
            }
        }
        if (too_close) continue;

        float y = terrain.get_height(x, z);

        GrassGPUInstance inst{};
        inst.px = x;
        inst.py = y;
        inst.pz = z;
        inst.rotation = hash01(static_cast<int>(x * 10), static_cast<int>(z * 10), 123u) * 6.2831853f;
        float s_var = 0.85f + 0.30f * hash01(static_cast<int>(x * 10), static_cast<int>(z * 10), 456u);
        inst.scale_x = base_w * s_var;
        inst.scale_y = base_h * s_var;
        inst.wind_phase = hash01(static_cast<int>(x * 10), static_cast<int>(z * 10), 789u) * 6.2831853f;

        float c_var = 0.85f + 0.30f * hash01(static_cast<int>(x * 10), static_cast<int>(z * 10), 321u);
        inst.r = std::clamp(tint[0] * c_var, 0.05f, 1.0f);
        inst.g = std::clamp(tint[1] * c_var, 0.05f, 1.0f);
        inst.b = std::clamp(tint[2] * c_var, 0.05f, 1.0f);
        inst.pad[0] = 0.0f;
        inst.pad[1] = 0.0f;

        m_instances.push_back(inst);
        ++added;
    }

    if (added > 0) {
        m_instance_count = static_cast<uint32_t>(m_instances.size());
        m_config.enabled = true;
        if (device) update_instance_buffer(device);
    }
    return added;
}

uint32_t GrassSystem::erase_brush(float hit_x, float hit_z, float radius, ID3D11Device* device) {
    if (m_instances.empty() || radius <= 0.0f) return 0;
    const float r2 = radius * radius;
    const size_t prev_size = m_instances.size();

    auto it = std::remove_if(m_instances.begin(), m_instances.end(), [&](const GrassGPUInstance& inst) {
        float dx = inst.px - hit_x;
        float dz = inst.pz - hit_z;
        return (dx * dx + dz * dz) <= r2;
    });

    if (it != m_instances.end()) {
        m_instances.erase(it, m_instances.end());
        m_instance_count = static_cast<uint32_t>(m_instances.size());
        if (device) update_instance_buffer(device);
        return static_cast<uint32_t>(prev_size - m_instances.size());
    }
    return 0;
}

void GrassSystem::snap_to_terrain(const TerrainSystem& terrain, ID3D11Device* device) {
    if (!terrain.is_generated() || m_instances.empty()) return;
    for (auto& inst : m_instances) {
        inst.py = terrain.get_height(inst.px, inst.pz);
    }
    if (device) update_instance_buffer(device);
}

bool GrassSystem::save_to_file(const std::string& filepath) const {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    uint32_t magic = 0x53415247; // 'GRAS'
    uint32_t version = 1;
    uint32_t count = static_cast<uint32_t>(m_instances.size());
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&count), 4);
    if (count > 0) {
        out.write(reinterpret_cast<const char*>(m_instances.data()), count * sizeof(GrassGPUInstance));
    }
    return out.good();
}

bool GrassSystem::load_from_file(const std::string& filepath, ID3D11Device* device) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return false;
    uint32_t magic = 0, version = 0, count = 0;
    in.read(reinterpret_cast<char*>(&magic), 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&count), 4);
    if (magic != 0x53415247 || version != 1) return false;
    m_instances.resize(count);
    if (count > 0) {
        in.read(reinterpret_cast<char*>(m_instances.data()), count * sizeof(GrassGPUInstance));
    }
    m_instance_count = static_cast<uint32_t>(m_instances.size());
    m_config.enabled = (m_instance_count > 0);
    if (device) update_instance_buffer(device);
    return true;
}

bool GrassSystem::update_instance_buffer(ID3D11Device* device) {
    if (!device) return false;

    m_instance_buffer.Reset();
    if (m_instances.empty()) return true;

    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(m_instances.size() * sizeof(GrassGPUInstance));
    ibd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ibd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA isd{ m_instances.data(), 0, 0 };
    HRESULT hr = device->CreateBuffer(&ibd, &isd, m_instance_buffer.GetAddressOf());
    return SUCCEEDED(hr);
}

void GrassSystem::render(ID3D11DeviceContext* context, const SceneConstantBuffer& scene_cb,
                         const float camera_pos[3], float total_time, const float interaction_pos[4]) {
    if (!context || m_instance_count == 0 || !m_config.enabled) return;
    if (!m_instance_buffer || !m_vertex_buffer || !m_index_buffer) return;

    // 1. Update Constant Buffer
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(m_constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto* cb = static_cast<GrassConstantBuffer*>(mapped.pData);
        memcpy(cb->view_proj, scene_cb.view_proj, sizeof(float) * 16);
        cb->camera_pos[0] = camera_pos[0];
        cb->camera_pos[1] = camera_pos[1];
        cb->camera_pos[2] = camera_pos[2];
        cb->time = total_time;

        cb->wind_params[0] = m_config.wind_dir[0];
        cb->wind_params[1] = m_config.wind_dir[1];
        cb->wind_params[2] = m_config.wind_speed;
        cb->wind_params[3] = m_config.wind_strength;

        cb->grass_params[0] = m_config.max_distance;
        cb->grass_params[1] = m_config.fade_range;
        cb->grass_params[2] = m_config.base_height;
        cb->grass_params[3] = m_config.base_width;

        if (interaction_pos) {
            cb->interaction_collider[0] = interaction_pos[0];
            cb->interaction_collider[1] = interaction_pos[1];
            cb->interaction_collider[2] = interaction_pos[2];
            cb->interaction_collider[3] = interaction_pos[3];
        } else {
            cb->interaction_collider[0] = 0.0f;
            cb->interaction_collider[1] = 0.0f;
            cb->interaction_collider[2] = 0.0f;
            cb->interaction_collider[3] = 0.0f;
        }

        cb->sun_dir[0] = scene_cb.sun_dir[0];
        cb->sun_dir[1] = scene_cb.sun_dir[1];
        cb->sun_dir[2] = scene_cb.sun_dir[2];
        cb->sun_intensity = 1.6f;

        cb->sun_color[0] = scene_cb.sun_color[0];
        cb->sun_color[1] = scene_cb.sun_color[1];
        cb->sun_color[2] = scene_cb.sun_color[2];
        cb->ambient_intensity = 1.1f;

        cb->ambient_color[0] = scene_cb.ambient_color[0];
        cb->ambient_color[1] = scene_cb.ambient_color[1];
        cb->ambient_color[2] = scene_cb.ambient_color[2];
        cb->pad0 = 0.0f;

        cb->fog_params[0] = scene_cb.fog_start;
        cb->fog_params[1] = scene_cb.fog_end;
        cb->fog_params[2] = 0.0f;
        cb->fog_params[3] = 0.0f;

        cb->fog_color[0] = scene_cb.fog_color[0];
        cb->fog_color[1] = scene_cb.fog_color[1];
        cb->fog_color[2] = scene_cb.fog_color[2];
        cb->pad1 = 0.0f;

        context->Unmap(m_constant_buffer.Get(), 0);
    }

    // 2. Set Render States (Two-Sided, Solid Depth)
    context->RSSetState(m_rasterizer_state.Get());
    context->OMSetDepthStencilState(m_depth_state.Get(), 0);
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    // 3. Set Shaders & Buffers
    context->IASetInputLayout(m_input_layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* vbs[2] = { m_vertex_buffer.Get(), m_instance_buffer.Get() };
    UINT strides[2] = { sizeof(GrassVertex), sizeof(GrassGPUInstance) };
    UINT offsets[2] = { 0, 0 };
    context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    context->IASetIndexBuffer(m_index_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);

    context->VSSetShader(m_vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(m_pixel_shader.Get(), nullptr, 0);

    context->VSSetConstantBuffers(0, 1, m_constant_buffer.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constant_buffer.GetAddressOf());

    // 4. Draw All Grass Instances in a Single Call
    // 3 quads * 6 indices = 18 indices per clump
    context->DrawIndexedInstanced(18, m_instance_count, 0, 0, 0);
}

} // namespace GM3D
