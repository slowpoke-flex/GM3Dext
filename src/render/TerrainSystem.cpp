#include "TerrainSystem.h"
#include <cmath>
#include <algorithm>
#include <iostream>

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

struct NoiseResult {
    float v, dx, dy;
};

// Value noise with quintic Hermite curve and analytic derivatives (from NewEra3D)
NoiseResult valueNoise(float x, float y, uint32_t seed) {
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    int32_t ix = static_cast<int32_t>(fx0), iy = static_cast<int32_t>(fy0);
    const float fx = x - fx0, fy = y - fy0;
    int32_t ix1 = ix + 1, iy1 = iy + 1;

    const float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float v = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    const float du = 30.0f * fx * fx * (fx * (fx - 2.0f) + 1.0f);
    const float dv = 30.0f * fy * fy * (fy * (fy - 2.0f) + 1.0f);

    const float a = hash01(ix, iy, seed), b = hash01(ix1, iy, seed);
    const float c = hash01(ix, iy1, seed), d = hash01(ix1, iy1, seed);
    const float k = a - b - c + d;

    return {
        2.0f * (a + (b - a) * u + (c - a) * v + k * u * v) - 1.0f,
        2.0f * du * ((b - a) + k * v),
        2.0f * dv * ((c - a) + k * u)
    };
}

float fbm(float x, float y, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * valueNoise(x, y, seed + static_cast<uint32_t>(o) * 101u).v;
        x *= 2.0f;
        y *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

inline float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

uint32_t TerrainSystem::generate(ID3D11Device* device, ID3D11DeviceContext* context,
                                MeshManager& mesh_mgr, TextureManager& tex_mgr, PhysicsWorld& physics_world,
                                const TerrainConfig& config, const std::vector<float>* heights) {
    if (!device || !context) return 0;

    m_config = config;
    const uint32_t N = std::clamp(config.grid_size, 32u, 512u);
    m_config.grid_size = N;
    const float W = config.world_size;
    const float H = config.height_scale;
    const uint32_t S = config.seed;

    m_heights.resize(N * N);

    // 1. Generate Heightfield with Derivative-damped fBm Erosion + Ridged Mountains (from NewEra3D)
    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(N - 1);
            const float v = static_cast<float>(y) / static_cast<float>(N - 1);

            // 1.1 Derivative-damped fBm (erosion simulation damping slope high-frequencies)
            float px = u * 3.6f + 11.3f, py = v * 3.6f + 4.7f;
            float h = 0.0f, amp = 0.5f, dx = 0.0f, dy = 0.0f;
            for (int o = 0; o < 10; ++o) {
                const NoiseResult n = valueNoise(px, py, S + static_cast<uint32_t>(o) * 31u);
                dx += n.dx;
                dy += n.dy;
                h += amp * n.v / (1.0f + dx * dx + dy * dy);
                const float rx = 0.8f * px - 0.6f * py;
                const float ry = 0.6f * px + 0.8f * py;
                px = rx * 2.0f;
                py = ry * 2.0f;
                amp *= 0.5f;
            }

            // 1.2 Ridged multifractal mountains rising away from the central plaza
            float ridge = 0.0f, ra = 0.5f;
            float fx = u * 2.4f + 3.1f, fy = v * 2.4f + 8.9f;
            for (int o = 0; o < 7; ++o) {
                const float n = 1.0f - std::fabs(valueNoise(fx, fy, (S ^ 0xABCDu) + static_cast<uint32_t>(o)).v);
                ridge += ra * n * n;
                fx *= 2.03f;
                fy *= 2.03f;
                ra *= 0.5f;
            }

            // 1.3 Distance from center of the island
            const float du = u - 0.5f;
            const float dv = v - 0.5f;
            const float dist = std::sqrt(du * du + dv * dv);

            // Central plaza: expansive sunlit valley
            const float plaza_mask = smoothstep(0.14f, 0.06f, dist);
            // Surrounding mountain ring gently rising toward the perimeter (no oppressive valley walls)
            const float mountain_mask = smoothstep(0.12f, 0.28f, dist) * smoothstep(0.48f, 0.38f, dist);
            // Island perimeter falloff (tapers smoothly into ocean water at edges)
            const float coast_falloff = smoothstep(0.49f, 0.38f, dist);

            float raw_h = (0.10f + 0.14f * h + mountain_mask * (0.65f * ridge));
            raw_h = mix(raw_h, 0.055f + 0.003f * fbm(u * 40.0f, v * 40.0f, 3, S + 99u), plaza_mask);
            raw_h = std::max(0.0f, raw_h * coast_falloff);

            m_heights[y * N + x] = raw_h * H;
        }
    }

    if (heights && heights->size() == m_heights.size()) m_heights = *heights;
    if (!m_splatmap.empty() && m_splatmap.size() != static_cast<size_t>(N) * N * 4) {
        m_splatmap.clear();
    }
    update_mesh(device, mesh_mgr);

    update_albedo(device, context, tex_mgr, -W * 0.5f, -W * 0.5f, W * 0.5f, W * 0.5f, true);

    // 4. Create Jolt Physics HeightField Collider
    update_collider(physics_world);

    m_generated = true;
    return m_physics_body_id;
}

bool TerrainSystem::update_albedo(ID3D11Device* device, ID3D11DeviceContext* context, TextureManager& tex_mgr,
                                  float min_x, float min_z, float max_x, float max_z, bool create) {
    if (!device || !context || m_heights.empty()) return false;
    constexpr uint32_t TEX_SIZE = 2048;
    const uint32_t N = m_config.grid_size;
    const float W = m_config.world_size;
    const uint32_t S = m_config.seed;
    const float half_w = W * 0.5f;
    const float cell_size = W / static_cast<float>(N - 1);
    const float cav_radius = std::max(2.5f, cell_size * 2.2f);
    const float normal_delta = std::max(1.8f, cell_size * 1.2f);
    // A changed sample affects bilinear interpolation, normals and cavity samples around it.
    const float margin = cell_size + std::max(cav_radius, normal_delta);
    auto pixel = [W, half_w](float coord) {
        return (coord + half_w) / W * static_cast<float>(TEX_SIZE - 1);
    };
    const uint32_t left = create ? 0 : static_cast<uint32_t>(std::clamp(std::floor(pixel(min_x - margin)), 0.0f, float(TEX_SIZE - 1)));
    const uint32_t top = create ? 0 : static_cast<uint32_t>(std::clamp(std::floor(pixel(min_z - margin)), 0.0f, float(TEX_SIZE - 1)));
    const uint32_t right = create ? TEX_SIZE : static_cast<uint32_t>(std::clamp(std::ceil(pixel(max_x + margin)) + 1.0f, 1.0f, float(TEX_SIZE)));
    const uint32_t bottom = create ? TEX_SIZE : static_cast<uint32_t>(std::clamp(std::ceil(pixel(max_z + margin)) + 1.0f, 1.0f, float(TEX_SIZE)));
    if (right <= left || bottom <= top) return false;
    const uint32_t region_width = right - left;
    std::vector<uint8_t> albedo_rgba(static_cast<size_t>(region_width) * (bottom - top) * 4);

    const bool has_splat = (m_splatmap.size() == static_cast<size_t>(N) * N * 4);
    TerrainLayer default_layers[4] = {
        { { 0.22f, 0.36f, 0.15f }, 0.78f, 1.0f },
        { { 0.38f, 0.36f, 0.34f }, 0.85f, 1.0f },
        { { 0.80f, 0.73f, 0.52f }, 0.65f, 1.0f },
        { { 0.46f, 0.43f, 0.39f }, 0.70f, 1.5f }
    };
    for (size_t i = 0; i < 4 && i < m_layers.size(); ++i) {
        default_layers[i] = m_layers[i];
    }

    for (uint32_t ty = top; ty < bottom; ++ty) {
        for (uint32_t tx = left; tx < right; ++tx) {
            const float u = static_cast<float>(tx) / static_cast<float>(TEX_SIZE - 1);
            const float v = static_cast<float>(ty) / static_cast<float>(TEX_SIZE - 1);

            const float worldX = -half_w + u * W;
            const float worldZ = -half_w + v * W;
            const float h = get_height(worldX, worldZ);

            // 1. Central difference gradient for smooth surface normal
            const float hL = get_height(worldX - normal_delta, worldZ);
            const float hR = get_height(worldX + normal_delta, worldZ);
            const float hD = get_height(worldX, worldZ - normal_delta);
            const float hU = get_height(worldX, worldZ + normal_delta);

            float nx = -(hR - hL) / (2.0f * normal_delta);
            float ny = 1.0f;
            float nz = -(hU - hD) / (2.0f * normal_delta);
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 0.0001f) {
                nx /= len; ny /= len; nz /= len;
            } else {
                nx = 0.0f; ny = 1.0f; nz = 0.0f;
            }

            // Slope tangent: 0 = flat ground, 1.0 = 45 degree slope
            const float slope = std::sqrt(std::max(0.0f, 1.0f - ny * ny));

            // 2. NewEra3D-style Multi-Scale Cavity AO (Valley & Crevice Ambient Occlusion)
            const float hN = get_height(worldX, worldZ - cav_radius);
            const float hS = get_height(worldX, worldZ + cav_radius);
            const float hE = get_height(worldX + cav_radius, worldZ);
            const float hW = get_height(worldX - cav_radius, worldZ);
            const float rD = cav_radius * 0.7071f;
            const float hNE = get_height(worldX + rD, worldZ - rD);
            const float hNW = get_height(worldX - rD, worldZ - rD);
            const float hSE = get_height(worldX + rD, worldZ + rD);
            const float hSW = get_height(worldX - rD, worldZ + rD);
            const float meanH = (hN + hS + hE + hW + hNE + hNW + hSE + hSW) * 0.125f;
            const float depth = h - meanH;
            const float cavity = std::clamp(1.0f + 0.95f * (depth / cav_radius), 0.42f, 1.08f);

            // 3. Multi-octave organic procedural noise for subtle natural variegation (no sharp thresholds)
            const float n1 = fbm(u * 50.0f, v * 50.0f, 4, S + 111u);
            const float n2 = fbm(u * 180.0f, v * 180.0f, 3, S + 222u);
            const float n_grain = fbm(u * 500.0f, v * 500.0f, 2, S + 333u);

            auto ramp = [](float val, float a, float b) {
                const float t = std::clamp((val - a) / (b - a), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };

            // ------------------------------------------------------------------------------------
            // Natural Hypsometric Biome Palette (NewEra3D Reference Color Model)
            // ------------------------------------------------------------------------------------
            // Biome 1: Coastal Shoreline & Fine Sand (elevation 0.0m to 2.4m)
            float r = mix(0.76f, 0.84f, 0.5f + 0.5f * n1);
            float g = mix(0.70f, 0.76f, 0.5f + 0.5f * n1);
            float b = mix(0.50f, 0.56f, 0.5f + 0.5f * n1);

            // Biome 2: Lowland Valley Alpine Grassland & Moss (elevation 1.6m to 9.0m)
            const float grass_t = ramp(h, 1.6f, 3.2f);
            const float gr_r = mix(0.19f, 0.25f, 0.5f + 0.5f * n1);
            const float gr_g = mix(0.31f, 0.39f, 0.5f + 0.5f * n1) + 0.02f * n2;
            const float gr_b = mix(0.12f, 0.16f, 0.5f - 0.5f * n1);
            r = mix(r, gr_r, grass_t);
            g = mix(g, gr_g, grass_t);
            b = mix(b, gr_b, grass_t);

            // Biome 3: Subalpine Highland Meadow & Sedge (elevation 7.5m to 16.5m)
            const float meadow_t = ramp(h, 7.5f, 14.5f);
            const float md_r = 0.33f + 0.03f * n2;
            const float md_g = 0.34f + 0.03f * n2;
            const float md_b = 0.20f + 0.02f * n2;
            r = mix(r, md_r, meadow_t);
            g = mix(g, md_g, meadow_t);
            b = mix(b, md_b, meadow_t);

            // Biome 4: Mountain Scree & Weathered Granite (elevation 14.0m to 25.5m)
            const float scree_t = ramp(h, 14.0f, 22.0f);
            const float sc_r = 0.40f + 0.04f * n2;
            const float sc_g = 0.38f + 0.03f * n2;
            const float sc_b = 0.36f + 0.03f * n2;
            r = mix(r, sc_r, scree_t);
            g = mix(g, sc_g, scree_t);
            b = mix(b, sc_b, scree_t);

            // Biome 5: Sheer Cliff Bedrock & Geological Rock Strata (steep slopes at any elevation)
            const float cliff_t = ramp(slope, 0.36f, 0.72f);
            const float strata = std::sin(h * 3.8f + std::sin(worldX * 0.35f + worldZ * 0.35f) * 1.6f) * 0.5f + 0.5f;
            const float strata_factor = 0.86f + 0.26f * strata;
            const float cl_r = (0.26f + 0.03f * n2) * strata_factor;
            const float cl_g = (0.25f + 0.03f * n2) * strata_factor;
            const float cl_b = (0.24f + 0.03f * n2) * strata_factor;
            r = mix(r, cl_r, cliff_t);
            g = mix(g, cl_g, cliff_t);
            b = mix(b, cl_b, cliff_t);

            // Biome 6: Alpine Summit Snow (ONLY highest mountain peaks > 25.5m on gentle slopes)
            const float snow_t = ramp(h, 25.5f, 29.5f) * (1.0f - ramp(slope, 0.42f, 0.65f));
            r = mix(r, 0.92f + 0.03f * n2, snow_t);
            g = mix(g, 0.94f + 0.03f * n2, snow_t);
            b = mix(b, 0.97f + 0.02f * n2, snow_t);

            // Biome 7: Central Ancient Ruins Plaza (Radius < 21m)
            const float du = u - 0.5f;
            const float dv = v - 0.5f;
            const float dist = std::sqrt(du * du + dv * dv);
            const float plaza_t = smoothstep(0.12f, 0.05f, dist);
            if (plaza_t > 0.001f) {
                // Cobblestone / flagstone grid
                const float gx = std::abs(std::fmod(worldX * 0.65f, 1.0f) - 0.5f);
                const float gz = std::abs(std::fmod(worldZ * 0.65f, 1.0f) - 0.5f);
                const float mortar = 1.0f - smoothstep(0.015f, 0.05f, std::min(gx, gz));
                const float stone_tint = (1.0f - mortar * 0.22f) * (0.94f + 0.12f * n_grain);
                const float pl_r = 0.44f * stone_tint;
                const float pl_g = 0.42f * stone_tint;
                const float pl_b = 0.39f * stone_tint;
                r = mix(r, pl_r, plaza_t);
                g = mix(g, pl_g, plaza_t);
                b = mix(b, pl_b, plaza_t);
            }

            // 3.5 Blend User-Painted Terrain Material Layers (Splatmap)
            if (has_splat) {
                const float gx = u * static_cast<float>(N - 1);
                const float gy = v * static_cast<float>(N - 1);
                const uint32_t x0 = std::min(static_cast<uint32_t>(gx), N - 2);
                const uint32_t y0 = std::min(static_cast<uint32_t>(gy), N - 2);
                const uint32_t x1 = x0 + 1;
                const uint32_t y1 = y0 + 1;
                const float fx = gx - static_cast<float>(x0);
                const float fy = gy - static_cast<float>(y0);

                const size_t i00 = (static_cast<size_t>(y0) * N + x0) * 4;
                const size_t i10 = (static_cast<size_t>(y0) * N + x1) * 4;
                const size_t i01 = (static_cast<size_t>(y1) * N + x0) * 4;
                const size_t i11 = (static_cast<size_t>(y1) * N + x1) * 4;

                float weights[4] = {};
                float painted_sum = 0.0f;
                for (int c = 0; c < 4; ++c) {
                    const float w00 = static_cast<float>(m_splatmap[i00 + c]) / 255.0f;
                    const float w10 = static_cast<float>(m_splatmap[i10 + c]) / 255.0f;
                    const float w01 = static_cast<float>(m_splatmap[i01 + c]) / 255.0f;
                    const float w11 = static_cast<float>(m_splatmap[i11 + c]) / 255.0f;
                    const float w0 = w00 * (1.0f - fx) + w10 * fx;
                    const float w1 = w01 * (1.0f - fx) + w11 * fx;
                    weights[c] = w0 * (1.0f - fy) + w1 * fy;
                    painted_sum += weights[c];
                }

                if (painted_sum > 0.001f) {
                    float pr = 0.0f, pg = 0.0f, pb = 0.0f;
                    for (int c = 0; c < 4; ++c) {
                        if (weights[c] <= 0.0001f) continue;
                        const float nw = weights[c] / painted_sum;
                        float lr = default_layers[c].color[0];
                        float lg = default_layers[c].color[1];
                        float lb = default_layers[c].color[2];
                        const float lscale = std::max(0.1f, default_layers[c].scale);

                        if (c == 0) {
                            // Grass / foliage organic variation
                            const float var = 0.92f + 0.12f * n1 + 0.05f * n2;
                            lr *= var; lg *= var; lb *= var;
                        } else if (c == 1) {
                            // Cliff / rock strata detail
                            const float var = (0.90f + 0.10f * n2) * strata_factor;
                            lr *= var; lg *= var; lb *= var;
                        } else if (c == 2) {
                            // Sand / soil fine grain
                            const float var = 0.94f + 0.08f * n1 + 0.04f * n_grain;
                            lr *= var; lg *= var; lb *= var;
                        } else {
                            // Cobblestone / tiled path
                            const float cgx = std::abs(std::fmod(std::abs(worldX) * 0.65f * lscale, 1.0f) - 0.5f);
                            const float cgz = std::abs(std::fmod(std::abs(worldZ) * 0.65f * lscale, 1.0f) - 0.5f);
                            const float mortar = 1.0f - smoothstep(0.015f, 0.05f, std::min(cgx, cgz));
                            const float stone_tint = (1.0f - mortar * 0.22f) * (0.94f + 0.12f * n_grain);
                            lr *= stone_tint; lg *= stone_tint; lb *= stone_tint;
                        }
                        pr += lr * nw;
                        pg += lg * nw;
                        pb += lb * nw;
                    }
                    const float blend_t = std::clamp(painted_sum, 0.0f, 1.0f);
                    r = mix(r, pr, blend_t);
                    g = mix(g, pg, blend_t);
                    b = mix(b, pb, blend_t);
                }
            }

            // 4. Apply Multi-Scale Cavity Ambient Occlusion Shading
            r *= cavity;
            g *= cavity;
            b *= cavity;

            const size_t pIdx = ((ty - top) * region_width + (tx - left)) * 4;
            albedo_rgba[pIdx + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            albedo_rgba[pIdx + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
            albedo_rgba[pIdx + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
            albedo_rgba[pIdx + 3] = 255;
        }
    }
    if (create) return tex_mgr.set_texture_rgba(TERRAIN_TEXTURE_ID, device, context, TEX_SIZE, TEX_SIZE, albedo_rgba.data(), "Terrain_Albedo", true);
    return tex_mgr.update_texture_region(TERRAIN_TEXTURE_ID, context, left, top, region_width, bottom - top, albedo_rgba.data());
}

bool TerrainSystem::update_mesh(ID3D11Device* device, MeshManager& mesh_mgr) {
    if (!device || m_heights.empty()) return false;
    const uint32_t N = m_config.grid_size;
    const float W = m_config.world_size;
    const float half_w = W * 0.5f;
    const float cell_size = W / static_cast<float>(N - 1);
    m_vertices.resize(static_cast<size_t>(N) * N);

    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N; ++x) {
            const size_t idx = y * N + x;
            const float u = static_cast<float>(x) / static_cast<float>(N - 1);
            const float v = static_cast<float>(y) / static_cast<float>(N - 1);

            const float posX = -half_w + u * W;
            const float posY = m_heights[idx];
            const float posZ = -half_w + v * W;

            // Central difference gradient for smooth vertex normal
            const int32_t lx = std::max(0, static_cast<int32_t>(x) - 1);
            const int32_t rx = std::min(static_cast<int32_t>(N) - 1, static_cast<int32_t>(x) + 1);
            const int32_t dy_idx = std::max(0, static_cast<int32_t>(y) - 1);
            const int32_t uy_idx = std::min(static_cast<int32_t>(N) - 1, static_cast<int32_t>(y) + 1);

            const float hL = m_heights[y * N + lx];
            const float hR = m_heights[y * N + rx];
            const float hD = m_heights[dy_idx * N + x];
            const float hU = m_heights[uy_idx * N + x];

            const float dX = (rx - lx) * cell_size;
            const float dZ = (uy_idx - dy_idx) * cell_size;

            float normX = -(hR - hL) / dX;
            float normY = 1.0f;
            float normZ = -(hU - hD) / dZ;

            const float len = std::sqrt(normX * normX + normY * normY + normZ * normZ);
            if (len > 0.0001f) {
                normX /= len; normY /= len; normZ /= len;
            }

            // UV mapping: 1.0 UV maps across the terrain, with high-res texture repetition
            m_vertices[idx] = Vertex3D(posX, posY, posZ, normX, normY, normZ, u, v);
        }
    }

    const size_t expected_indices = static_cast<size_t>(N - 1) * (N - 1) * 6;
    if (m_indices.size() != expected_indices) {
        m_indices.clear();
        m_indices.reserve(expected_indices);
        for (uint32_t y = 0; y < N - 1; ++y) {
            for (uint32_t x = 0; x < N - 1; ++x) {
                uint32_t i0 = y * N + x;
                uint32_t i1 = y * N + (x + 1);
                uint32_t i2 = (y + 1) * N + x;
                uint32_t i3 = (y + 1) * N + (x + 1);

                m_indices.push_back(i0);
                m_indices.push_back(i2);
                m_indices.push_back(i1);

                m_indices.push_back(i1);
                m_indices.push_back(i2);
                m_indices.push_back(i3);
            }
        }
    }

    return mesh_mgr.set_mesh(device, TERRAIN_MESH_ID, m_vertices, m_indices);
}

bool TerrainSystem::update_mesh_region(ID3D11Device* device, ID3D11DeviceContext* context, MeshManager& mesh_mgr,
                                       float min_x, float min_z, float max_x, float max_z) {
    if (!device || m_heights.empty()) return false;
    const uint32_t N = m_config.grid_size;
    if (!context || m_vertices.size() != static_cast<size_t>(N) * N) {
        return update_mesh(device, mesh_mgr);
    }

    const float W = m_config.world_size;
    const float half_w = W * 0.5f;
    const float cell_size = W / static_cast<float>(N - 1);

    // Expand dirty bounds by 2 cells in each direction so central-difference normals of neighbors are always updated
    const int32_t gx0 = std::clamp(static_cast<int32_t>(std::floor((min_x + half_w) / cell_size)) - 2, 0, static_cast<int32_t>(N) - 1);
    const int32_t gx1 = std::clamp(static_cast<int32_t>(std::ceil((max_x + half_w) / cell_size)) + 2, 0, static_cast<int32_t>(N) - 1);
    const int32_t gz0 = std::clamp(static_cast<int32_t>(std::floor((min_z + half_w) / cell_size)) - 2, 0, static_cast<int32_t>(N) - 1);
    const int32_t gz1 = std::clamp(static_cast<int32_t>(std::ceil((max_z + half_w) / cell_size)) + 2, 0, static_cast<int32_t>(N) - 1);
    if (gx0 > gx1 || gz0 > gz1) return false;

    for (int32_t y = gz0; y <= gz1; ++y) {
        for (int32_t x = gx0; x <= gx1; ++x) {
            const size_t idx = static_cast<size_t>(y) * N + x;
            m_vertices[idx].y = m_heights[idx];

            const int32_t lx = std::max(0, x - 1);
            const int32_t rx = std::min(static_cast<int32_t>(N) - 1, x + 1);
            const int32_t dy_idx = std::max(0, y - 1);
            const int32_t uy_idx = std::min(static_cast<int32_t>(N) - 1, y + 1);

            const float hL = m_heights[static_cast<size_t>(y) * N + lx];
            const float hR = m_heights[static_cast<size_t>(y) * N + rx];
            const float hD = m_heights[static_cast<size_t>(dy_idx) * N + x];
            const float hU = m_heights[static_cast<size_t>(uy_idx) * N + x];

            const float dX = (rx - lx) * cell_size;
            const float dZ = (uy_idx - dy_idx) * cell_size;

            float normX = -(hR - hL) / dX;
            float normY = 1.0f;
            float normZ = -(hU - hD) / dZ;

            const float len = std::sqrt(normX * normX + normY * normY + normZ * normZ);
            if (len > 0.0001f) {
                normX /= len; normY /= len; normZ /= len;
            }

            m_vertices[idx].nx = normX;
            m_vertices[idx].ny = normY;
            m_vertices[idx].nz = normZ;
        }
    }

    const uint32_t start_vertex = static_cast<uint32_t>(gz0) * N;
    const uint32_t vertex_count = static_cast<uint32_t>(gz1 - gz0 + 1) * N;
    if (!mesh_mgr.update_mesh_vertices(context, TERRAIN_MESH_ID, m_vertices, start_vertex, vertex_count)) {
        return update_mesh(device, mesh_mgr);
    }
    return true;
}

bool TerrainSystem::replace_heights(const std::vector<float>& heights) {
    if (heights.size() != m_heights.size() || heights.empty()) return false;
    m_heights = heights;
    return true;
}

void TerrainSystem::set_layers(const std::vector<TerrainLayer>& layers) {
    m_layers = layers;
}

void TerrainSystem::set_splatmap(const std::vector<uint8_t>& splatmap) {
    m_splatmap = splatmap;
}

void TerrainSystem::update_collider(PhysicsWorld& physics_world) {
    if (m_physics_body_id) physics_world.destroy_body(m_physics_body_id);
    m_physics_body_id = physics_world.create_heightfield_body(0.0f, 0.0f, 0.0f,
        m_config.world_size, m_config.grid_size, m_heights.data());
}

void TerrainSystem::clear(PhysicsWorld& physics_world) {
    if (m_physics_body_id) physics_world.destroy_body(m_physics_body_id);
    m_physics_body_id = 0;
    m_heights.clear();
    m_vertices.clear();
    m_indices.clear();
    m_splatmap.clear();
    m_generated = false;
}

float TerrainSystem::get_height(float world_x, float world_z) const {
    if (m_heights.empty()) return 0.0f;

    const float half_w = m_config.world_size * 0.5f;
    const float u = (world_x + half_w) / m_config.world_size;
    const float v = (world_z + half_w) / m_config.world_size;

    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return 0.0f;
    }

    const uint32_t N = m_config.grid_size;
    const float gx = u * static_cast<float>(N - 1);
    const float gy = v * static_cast<float>(N - 1);

    const uint32_t x0 = std::min(static_cast<uint32_t>(gx), N - 2);
    const uint32_t y0 = std::min(static_cast<uint32_t>(gy), N - 2);
    const uint32_t x1 = x0 + 1;
    const uint32_t y1 = y0 + 1;

    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);

    const float h00 = m_heights[y0 * N + x0];
    const float h10 = m_heights[y0 * N + x1];
    const float h01 = m_heights[y1 * N + x0];
    const float h11 = m_heights[y1 * N + x1];

    const float h0 = h00 * (1.0f - tx) + h10 * tx;
    const float h1 = h01 * (1.0f - tx) + h11 * tx;

    return h0 * (1.0f - ty) + h1 * ty;
}

void TerrainSystem::get_normal(float world_x, float world_z, float& out_nx, float& out_ny, float& out_nz) const {
    const float cell_size = (m_config.grid_size > 1) ? (m_config.world_size / static_cast<float>(m_config.grid_size - 1)) : 1.8f;
    const float delta = cell_size * 1.0f;
    const float hL = get_height(world_x - delta, world_z);
    const float hR = get_height(world_x + delta, world_z);
    const float hD = get_height(world_x, world_z - delta);
    const float hU = get_height(world_x, world_z + delta);

    float nx = -(hR - hL) / (2.0f * delta);
    float ny = 1.0f;
    float nz = -(hU - hD) / (2.0f * delta);

    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.0001f) {
        out_nx = nx / len;
        out_ny = ny / len;
        out_nz = nz / len;
    } else {
        out_nx = 0.0f;
        out_ny = 1.0f;
        out_nz = 0.0f;
    }
}

} // namespace GM3D
