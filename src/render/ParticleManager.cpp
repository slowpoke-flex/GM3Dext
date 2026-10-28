#include "ParticleManager.h"
#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char* g_particle_shader_hlsl = R"(
cbuffer SceneConstants : register(b0) {
    row_major float4x4 ViewProj;
    row_major float4x4 LightViewProj;
    float3 CameraPos;
    float Time;
};

struct VS_INPUT {
    float3 Pos       : POSITION;
    float2 UV        : TEXCOORD0;
    float3 InstPos   : INST_POS;
    float  InstSize  : INST_SIZE;
    float4 InstColor : INST_COLOR;
    float  InstRot   : INST_ROT;
    uint   InstType  : INST_TYPE;
};

struct PS_INPUT {
    float4 Pos   : SV_POSITION;
    float2 UV    : TEXCOORD0;
    float4 Color : COLOR;
    uint   Type  : TYPE;
};

PS_INPUT VS_Particle(VS_INPUT input) {
    PS_INPUT output;

    float s = sin(input.InstRot);
    float c = cos(input.InstRot);
    float2 rotQuad = float2(
        input.Pos.x * c - input.Pos.y * s,
        input.Pos.x * s + input.Pos.y * c
    );

    float3 worldPos;
    if (input.InstType == 4) {
        // Shockwave Ring lies flat on ground plane
        worldPos = input.InstPos + float3(rotQuad.x * input.InstSize, 0.04, rotQuad.y * input.InstSize);
    } else {
        // 3D Camera-facing billboard
        float3 toCam = normalize(CameraPos - input.InstPos);
        float3 right = normalize(cross(float3(0, 1, 0), toCam));
        if (length(right) < 0.001) right = float3(1, 0, 0);
        float3 up = cross(toCam, right);

        worldPos = input.InstPos + right * (rotQuad.x * input.InstSize) + up * (rotQuad.y * input.InstSize);
    }

    output.Pos = mul(float4(worldPos, 1.0), ViewProj);
    output.UV = input.UV;
    output.Color = input.InstColor;
    output.Type = input.InstType;
    return output;
}

float4 PS_Particle(PS_INPUT input) : SV_TARGET {
    float2 centerOffset = (input.UV - 0.5) * 2.0;
    float dist = length(centerOffset);

    if (input.Type == 4) {
        // Shockwave Ring
        float ringDist = abs(dist - 0.82);
        float ringAlpha = smoothstep(0.18, 0.0, ringDist);
        if (ringAlpha < 0.01) discard;
        return float4(input.Color.rgb * ringAlpha * 2.8, input.Color.a * ringAlpha);
    } else {
        // Soft Glowing Magical Orb / Spark
        if (dist > 1.0) discard;
        float core = saturate(1.0 - dist * dist);
        float glow = pow(core, 1.6) * 2.2;
        return float4(input.Color.rgb * glow * input.Color.a, input.Color.a * core);
    }
}
)";

ParticleManager::ParticleManager() {
    m_particles.resize(MAX_PARTICLES);
    m_instance_data.reserve(MAX_PARTICLES);
}

ParticleManager::~ParticleManager() {
    shutdown();
}

bool ParticleManager::initialize(ID3D11Device* device) {
    if (!device) return false;
    return init_shaders_and_geometry(device);
}

void ParticleManager::shutdown() {
    clear();
    m_quad_vb.Reset();
    m_quad_ib.Reset();
    m_instance_buffer.Reset();
    m_vertex_shader.Reset();
    m_pixel_shader.Reset();
    m_input_layout.Reset();
    m_blend_state.Reset();
    m_depth_state.Reset();
    m_rasterizer_state.Reset();
}

bool ParticleManager::init_shaders_and_geometry(ID3D11Device* device) {
    // 1. Compile Shaders
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompile(g_particle_shader_hlsl, strlen(g_particle_shader_hlsl), "ParticleShaders",
                            nullptr, nullptr, "VS_Particle", "vs_5_0", compileFlags, 0,
                            vsBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = D3DCompile(g_particle_shader_hlsl, strlen(g_particle_shader_hlsl), "ParticleShaders",
                            nullptr, nullptr, "PS_Particle", "ps_5_0", compileFlags, 0,
                            psBlob.GetAddressOf(), errBlob.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertex_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixel_shader.GetAddressOf());
    if (FAILED(hr)) return false;

    // 2. Input Layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        // Stream 0: Base Quad
        { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA,   0 },
        // Stream 1: Per-Instance Particle Data
        { "INST_POS",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_SIZE",  0, DXGI_FORMAT_R32_FLOAT,          1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_ROT",   0, DXGI_FORMAT_R32_FLOAT,          1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INST_TYPE",  0, DXGI_FORMAT_R32_UINT,           1, 36, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };

    hr = device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_input_layout.GetAddressOf());
    if (FAILED(hr)) return false;

    // 3. Quad Geometry
    struct QuadVertex {
        float x, y, z;
        float u, v;
    };
    QuadVertex quadVertices[] = {
        { -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f, 1.0f },
        {  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
        { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f },
    };
    uint32_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    D3D11_BUFFER_DESC vbd{};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = sizeof(quadVertices);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vData{ quadVertices, 0, 0 };
    hr = device->CreateBuffer(&vbd, &vData, m_quad_vb.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC ibd{};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = sizeof(quadIndices);
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iData{ quadIndices, 0, 0 };
    hr = device->CreateBuffer(&ibd, &iData, m_quad_ib.GetAddressOf());
    if (FAILED(hr)) return false;

    // 4. Dynamic Instance Buffer
    D3D11_BUFFER_DESC instBd{};
    instBd.Usage = D3D11_USAGE_DYNAMIC;
    instBd.ByteWidth = sizeof(ParticleGPUInstance) * MAX_PARTICLES;
    instBd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    instBd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&instBd, nullptr, m_instance_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // 5. Additive Blend State (radiant glowing magical overlap)
    D3D11_BLEND_DESC bsd{};
    bsd.RenderTarget[0].BlendEnable = TRUE;
    bsd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bsd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; // Additive
    bsd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bsd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bsd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&bsd, m_blend_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // 6. Depth Stencil State (Read-only depth test to prevent z-fighting while preserving occlusion)
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; // Do not write to depth buffer
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device->CreateDepthStencilState(&dsd, m_depth_state.GetAddressOf());
    if (FAILED(hr)) return false;

    // 7. Rasterizer State (No back-face culling)
    D3D11_RASTERIZER_DESC rd{};
    rd.CullMode = D3D11_CULL_NONE;
    rd.FillMode = D3D11_FILL_SOLID;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&rd, m_rasterizer_state.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

void ParticleManager::update(float dt) {
    m_active_count = 0;
    m_instance_data.clear();

    for (auto& p : m_particles) {
        if (!p.active) continue;

        p.life += dt;
        if (p.life >= p.max_life) {
            p.active = false;
            continue;
        }

        float progress = p.life / p.max_life;

        // Physics Integration
        p.vy += p.gravity * dt;
        p.vx *= (1.0f - p.drag * dt);
        p.vy *= (1.0f - p.drag * dt);
        p.vz *= (1.0f - p.drag * dt);

        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;

        p.rotation += p.rot_speed * dt;

        // Size & Alpha Curves
        float cur_size = p.start_size + (p.end_size - p.start_size) * progress;
        float cur_alpha = p.a * (1.0f - progress * progress); // Quadratic fade-out

        // Populate GPU instance
        ParticleGPUInstance inst{};
        inst.px = p.x;
        inst.py = p.y;
        inst.pz = p.z;
        inst.size = cur_size;
        inst.r = p.r;
        inst.g = p.g;
        inst.b = p.b;
        inst.a = cur_alpha;
        inst.rotation = p.rotation;
        inst.type = p.type;

        m_instance_data.push_back(inst);
        m_active_count++;
    }
}

void ParticleManager::render(ID3D11DeviceContext* context, ID3D11Buffer* scene_cb, const float camera_pos[3]) {
    (void)camera_pos;
    if (m_active_count == 0 || !context || !m_instance_buffer) return;

    // 1. Upload instances to dynamic GPU buffer
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(m_instance_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, m_instance_data.data(), sizeof(ParticleGPUInstance) * m_active_count);
        context->Unmap(m_instance_buffer.Get(), 0);
    }

    // 2. Set Pipeline State
    float blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(m_blend_state.Get(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_depth_state.Get(), 0);
    context->RSSetState(m_rasterizer_state.Get());

    context->IASetInputLayout(m_input_layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* vbs[] = { m_quad_vb.Get(), m_instance_buffer.Get() };
    UINT strides[] = { 20, sizeof(ParticleGPUInstance) };
    UINT offsets[] = { 0, 0 };
    context->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    context->IASetIndexBuffer(m_quad_ib.Get(), DXGI_FORMAT_R32_UINT, 0);

    context->VSSetShader(m_vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(m_pixel_shader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &scene_cb);

    // 3. Draw Instanced Billboards in a single GPU call
    context->DrawIndexedInstanced(6, m_active_count, 0, 0, 0);

    // 4. Restore Blend State
    context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

void ParticleManager::spawn_burst(uint32_t type, float x, float y, float z, uint32_t count, float speed, float size, float r, float g, float b) {
    std::uniform_real_distribution<float> dist_unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist_pos(0.0f, 1.0f);
    std::uniform_real_distribution<float> dist_rot(0.0f, static_cast<float>(2.0 * M_PI));

    for (uint32_t i = 0; i < count; ++i) {
        // Find inactive slot
        ParticleCPU* slot = nullptr;
        for (auto& p : m_particles) {
            if (!p.active) {
                slot = &p;
                break;
            }
        }
        if (!slot) break;

        slot->active = true;
        slot->type = type;
        slot->x = x;
        slot->y = y;
        slot->z = z;
        slot->life = 0.0f;
        slot->rotation = dist_rot(m_rng);
        slot->rot_speed = dist_unit(m_rng) * 4.0f;

        // Normalized random direction sphere
        float dx = dist_unit(m_rng);
        float dy = dist_unit(m_rng);
        float dz = dist_unit(m_rng);
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (len > 0.0001f) { dx /= len; dy /= len; dz /= len; }
        else { dy = 1.0f; }

        float spd = speed * (0.5f + dist_pos(m_rng) * 0.8f);

        if (type == PARTICLE_FIRE) {
            // Fire: Rising upward flames with embers
            slot->vx = dx * spd * 0.6f;
            slot->vy = std::abs(dy) * spd * 1.5f + 1.2f;
            slot->vz = dz * spd * 0.6f;
            slot->max_life = 0.6f + dist_pos(m_rng) * 0.6f;
            slot->start_size = size * (0.8f + dist_pos(m_rng) * 0.5f);
            slot->end_size = size * 0.1f;
            slot->gravity = 1.5f; // Fire rises
            slot->drag = 1.2f;
            slot->r = (r >= 0) ? r : (1.0f);
            slot->g = (g >= 0) ? g : (0.45f + dist_pos(m_rng) * 0.45f);
            slot->b = (b >= 0) ? b : (0.05f);
            slot->a = 1.0f;
        } else if (type == PARTICLE_ARCANE) {
            // Arcane: Swirling celestial vortex
            slot->vx = dx * spd * 1.2f;
            slot->vy = dy * spd * 1.2f + 0.5f;
            slot->vz = dz * spd * 1.2f;
            slot->max_life = 0.8f + dist_pos(m_rng) * 0.7f;
            slot->start_size = size * 0.9f;
            slot->end_size = size * 0.2f;
            slot->gravity = -0.5f;
            slot->drag = 1.6f;
            slot->r = (r >= 0) ? r : (dist_pos(m_rng) > 0.5f ? 0.25f : 0.85f);
            slot->g = (g >= 0) ? g : (0.55f);
            slot->b = (b >= 0) ? b : (1.0f);
            slot->a = 1.0f;
        } else if (type == PARTICLE_HOLY) {
            // Holy: Ascending golden divine sparkles
            slot->vx = dx * spd * 0.5f;
            slot->vy = std::abs(dy) * spd * 1.2f + 0.8f;
            slot->vz = dz * spd * 0.5f;
            slot->max_life = 1.2f + dist_pos(m_rng) * 0.8f;
            slot->start_size = size * 0.6f;
            slot->end_size = size * 0.05f;
            slot->gravity = 0.2f;
            slot->drag = 0.8f;
            slot->r = (r >= 0) ? r : 1.0f;
            slot->g = (g >= 0) ? g : (0.85f + dist_pos(m_rng) * 0.15f);
            slot->b = (b >= 0) ? b : (0.25f + dist_pos(m_rng) * 0.25f);
            slot->a = 1.0f;
        } else if (type == PARTICLE_FROST) {
            // Frost: Glacial sparkling shards & drift
            slot->vx = dx * spd * 1.4f;
            slot->vy = dy * spd * 0.8f;
            slot->vz = dz * spd * 1.4f;
            slot->max_life = 1.0f + dist_pos(m_rng) * 0.6f;
            slot->start_size = size * 0.7f;
            slot->end_size = size * 0.15f;
            slot->gravity = -1.8f; // Falls slowly like ice
            slot->drag = 2.0f;
            slot->r = (r >= 0) ? r : (0.35f + dist_pos(m_rng) * 0.35f);
            slot->g = (g >= 0) ? g : 0.85f;
            slot->b = (b >= 0) ? b : 1.0f;
            slot->a = 1.0f;
        }
    }
}

void ParticleManager::spawn_ring(uint32_t type, float x, float y, float z, float radius, uint32_t count, float r, float g, float b) {
    (void)count;
    // Spawn horizontal shockwave ring
    ParticleCPU* slot = nullptr;
    for (auto& p : m_particles) {
        if (!p.active) {
            slot = &p;
            break;
        }
    }
    if (!slot) return;

    slot->active = true;
    slot->type = (type == PARTICLE_SHOCKWAVE) ? PARTICLE_SHOCKWAVE : type;
    slot->x = x;
    slot->y = y;
    slot->z = z;
    slot->vx = 0.0f;
    slot->vy = 0.0f;
    slot->vz = 0.0f;
    slot->life = 0.0f;
    slot->max_life = 0.65f;
    slot->start_size = 0.5f;
    slot->end_size = radius * 2.2f;
    slot->rotation = 0.0f;
    slot->rot_speed = 0.0f;
    slot->gravity = 0.0f;
    slot->drag = 0.0f;
    slot->r = (r >= 0) ? r : 0.35f;
    slot->g = (g >= 0) ? g : 0.85f;
    slot->b = (b >= 0) ? b : 1.0f;
    slot->a = 1.0f;
}

void ParticleManager::clear() {
    for (auto& p : m_particles) {
        p.active = false;
    }
    m_active_count = 0;
    m_instance_data.clear();
}
