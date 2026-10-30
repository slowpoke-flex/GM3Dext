#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace GM3D::Studio {
enum class TerrainBrushMode { Raise, Lower, Smooth, Flatten, Paint, Grass };

inline bool apply_terrain_brush(std::vector<float>& heights, uint32_t n, float world_size,
                                float x, float z, float radius, float strength, float dt,
                                TerrainBrushMode mode, float target_height) {
    if (n < 2 || heights.size() != static_cast<size_t>(n) * n || world_size <= 0 || radius <= 0 || strength <= 0 || dt <= 0) return false;
    const float cell = world_size / (n - 1), half = world_size * 0.5f;
    const int min_x = std::max(0, static_cast<int>(std::floor((x - radius + half) / cell)));
    const int max_x = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((x + radius + half) / cell)));
    const int min_z = std::max(0, static_cast<int>(std::floor((z - radius + half) / cell)));
    const int max_z = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((z + radius + half) / cell)));
    if (min_x > max_x || min_z > max_z) return false;
    // Smoothing needs the original neighbours, but only around this dab.
    const int copy_x = std::max(0, min_x - 1), copy_z = std::max(0, min_z - 1);
    const int copy_w = std::min(static_cast<int>(n) - 1, max_x + 1) - copy_x + 1;
    const int copy_h = std::min(static_cast<int>(n) - 1, max_z + 1) - copy_z + 1;
    std::vector<float> snapshot;
    if (mode == TerrainBrushMode::Smooth) {
        snapshot.resize(static_cast<size_t>(copy_w) * copy_h);
        for (int row = 0; row < copy_h; ++row)
            std::copy_n(heights.begin() + static_cast<size_t>(copy_z + row) * n + copy_x, copy_w,
                        snapshot.begin() + static_cast<size_t>(row) * copy_w);
    }
    bool changed = false;
    for (int iz = min_z; iz <= max_z; ++iz) for (int ix = min_x; ix <= max_x; ++ix) {
        const float dx = ix * cell - half - x, dz = iz * cell - half - z;
        const float t = std::sqrt(dx * dx + dz * dz) / radius;
        if (t >= 1.0f) continue;
        const float weight = (1.0f - t) * (1.0f - t) * (1.0f + 2.0f * t);
        const size_t i = static_cast<size_t>(iz) * n + ix;
        const float previous = heights[i];
        float value = previous;
        if (mode == TerrainBrushMode::Raise || mode == TerrainBrushMode::Lower) {
            value += (mode == TerrainBrushMode::Raise ? 1.0f : -1.0f) * strength * dt * weight;
        } else {
            float target = target_height;
            if (mode == TerrainBrushMode::Smooth) {
                float sum = 0; int count = 0;
                for (int yy = std::max(0, iz - 1); yy <= std::min(static_cast<int>(n) - 1, iz + 1); ++yy)
                    for (int xx = std::max(0, ix - 1); xx <= std::min(static_cast<int>(n) - 1, ix + 1); ++xx) {
                        sum += snapshot[static_cast<size_t>(yy - copy_z) * copy_w + (xx - copy_x)]; ++count;
                    }
                target = sum / count;
            }
            value += (target - value) * std::min(1.0f, strength * dt * weight);
        }
        changed |= value != previous;
        heights[i] = value;
    }
    return changed;
}

// Sample the segment between cursor hits so quick pointer movement does not leave gaps.
// The frame's elapsed time is distributed across samples, preserving stationary strength.
inline bool apply_terrain_brush_segment(std::vector<float>& heights, uint32_t n, float world_size,
                                        float from_x, float from_z, float to_x, float to_z,
                                        float radius, float strength, float dt,
                                        TerrainBrushMode mode, float target_height) {
    if (n < 2 || world_size <= 0 || radius <= 0 || strength <= 0 || dt <= 0) return false;
    const float dx = to_x - from_x, dz = to_z - from_z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float spacing = radius * 0.5f;
    const int steps = static_cast<int>(std::clamp(std::ceil(distance / spacing), 1.0f, 2048.0f));
    bool changed = false;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        changed |= apply_terrain_brush(heights, n, world_size, from_x + dx * t, from_z + dz * t,
                                       radius, strength, dt / steps, mode, target_height);
    }
    return changed;
}

inline bool apply_terrain_paint_brush(std::vector<uint8_t>& splatmap, uint32_t n, float world_size,
                                      float x, float z, float radius, float strength, float dt,
                                      int layer_index) {
    if (n < 2 || world_size <= 0 || radius <= 0 || strength <= 0 || dt <= 0) return false;
    const size_t expected_size = static_cast<size_t>(n) * n * 4;
    if (splatmap.size() != expected_size) {
        splatmap.assign(expected_size, 0);
    }
    const float cell = world_size / (n - 1), half = world_size * 0.5f;
    const int min_x = std::max(0, static_cast<int>(std::floor((x - radius + half) / cell)));
    const int max_x = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((x + radius + half) / cell)));
    const int min_z = std::max(0, static_cast<int>(std::floor((z - radius + half) / cell)));
    const int max_z = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((z + radius + half) / cell)));
    if (min_x > max_x || min_z > max_z) return false;

    bool changed = false;
    for (int iz = min_z; iz <= max_z; ++iz) {
        for (int ix = min_x; ix <= max_x; ++ix) {
            const float dx = ix * cell - half - x, dz = iz * cell - half - z;
            const float t = std::sqrt(dx * dx + dz * dz) / radius;
            if (t >= 1.0f) continue;
            const float weight = (1.0f - t) * (1.0f - t) * (1.0f + 2.0f * t);
            const float delta = std::clamp(strength * dt * weight * 120.0f, 0.0f, 255.0f);
            if (delta < 0.5f) continue;

            const size_t base = (static_cast<size_t>(iz) * n + ix) * 4;
            float w[4] = {
                static_cast<float>(splatmap[base + 0]),
                static_cast<float>(splatmap[base + 1]),
                static_cast<float>(splatmap[base + 2]),
                static_cast<float>(splatmap[base + 3])
            };

            if (layer_index < 0 || layer_index >= 4) {
                for (int c = 0; c < 4; ++c) {
                    w[c] = std::max(0.0f, w[c] - delta);
                }
            } else {
                w[layer_index] = std::min(255.0f, w[layer_index] + delta);
                float other_sum = 0.0f;
                for (int c = 0; c < 4; ++c) {
                    if (c != layer_index) other_sum += w[c];
                }
                const float remaining = std::max(0.0f, 255.0f - w[layer_index]);
                if (other_sum > remaining && other_sum > 0.001f) {
                    const float scale = remaining / other_sum;
                    for (int c = 0; c < 4; ++c) {
                        if (c != layer_index) w[c] *= scale;
                    }
                }
            }

            for (int c = 0; c < 4; ++c) {
                uint8_t next_val = static_cast<uint8_t>(std::clamp(std::round(w[c]), 0.0f, 255.0f));
                if (splatmap[base + c] != next_val) {
                    splatmap[base + c] = next_val;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

inline bool apply_terrain_paint_segment(std::vector<uint8_t>& splatmap, uint32_t n, float world_size,
                                        float from_x, float from_z, float to_x, float to_z,
                                        float radius, float strength, float dt,
                                        int layer_index) {
    if (n < 2 || world_size <= 0 || radius <= 0 || strength <= 0 || dt <= 0) return false;
    const float dx = to_x - from_x, dz = to_z - from_z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float spacing = radius * 0.5f;
    const int steps = static_cast<int>(std::clamp(std::ceil(distance / spacing), 1.0f, 2048.0f));
    bool changed = false;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        changed |= apply_terrain_paint_brush(splatmap, n, world_size, from_x + dx * t, from_z + dz * t,
                                             radius, strength, dt / steps, layer_index);
    }
    return changed;
}
}
