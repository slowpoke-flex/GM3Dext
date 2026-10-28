#pragma once

#include "SceneData.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

namespace GM3D::Studio {

// Build 4x4 matrix from Translation, Euler Rotation (deg: pitch-X, yaw-Y, roll-Z), Scale
inline void BuildMatrix(const float* pos, const float* rot, const float* scale, float* matrix) {
    float radX = rot[0] * 0.0174532925f;
    float radY = rot[1] * 0.0174532925f;
    float radZ = rot[2] * 0.0174532925f;

    float cx = cosf(radX), sx = sinf(radX);
    float cy = cosf(radY), sy = sinf(radY);
    float cz = cosf(radZ), sz = sinf(radZ);

    matrix[0]  = (cy * cz) * scale[0];
    matrix[1]  = (cy * sz) * scale[0];
    matrix[2]  = (-sy)     * scale[0];
    matrix[3]  = 0.0f;

    matrix[4]  = (sx * sy * cz - cx * sz) * scale[1];
    matrix[5]  = (sx * sy * sz + cx * cz) * scale[1];
    matrix[6]  = (sx * cy)                * scale[1];
    matrix[7]  = 0.0f;

    matrix[8]  = (cx * sy * cz + sx * sz) * scale[2];
    matrix[9]  = (cx * sy * sz - sx * cz) * scale[2];
    matrix[10] = (cx * cy)                * scale[2];
    matrix[11] = 0.0f;

    matrix[12] = pos[0];
    matrix[13] = pos[1];
    matrix[14] = pos[2];
    matrix[15] = 1.0f;
}

// 4x4 Row-major Matrix Multiplication: C = A * B
inline void MultiplyMatrices(const float* a, const float* b, float* out) {
    float temp[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            temp[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] +
                              a[i * 4 + 1] * b[1 * 4 + j] +
                              a[i * 4 + 2] * b[2 * 4 + j] +
                              a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    std::memcpy(out, temp, sizeof(float) * 16);
}

// Exact 4x4 Matrix Inversion
inline bool InvertMatrix(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5]  * m[10] * m[15] - m[5]  * m[11] * m[14] - m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] + m[13] * m[6]  * m[11] - m[13] * m[7]  * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4]  * m[11] * m[14] + m[8]  * m[6]  * m[15] - 
              m[8] * m[7]  * m[14] - m[12] * m[6]  * m[11] + m[12] * m[7]  * m[10];
    inv[8] = m[4]  * m[9]  * m[15] - m[4]  * m[11] * m[13] - m[8]  * m[5]  * m[15] + 
             m[8]  * m[7]  * m[13] + m[12] * m[5]  * m[11] - m[12] * m[7]  * m[9];
    inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8]  * m[5]  * m[14] - 
               m[8] * m[6]  * m[13] - m[12]* m[5]  * m[10] + m[12] * m[6]  * m[9];

    inv[1] = -m[1] * m[10] * m[15] + m[1]  * m[11] * m[14] + m[9]  * m[2]  * m[15] - 
              m[9] * m[3]  * m[14] - m[13] * m[2]  * m[11] + m[13] * m[3]  * m[10];
    inv[5] = m[0]  * m[10] * m[15] - m[0]  * m[11] * m[14] - m[8]  * m[2]  * m[15] + 
             m[8]  * m[3]  * m[14] + m[12] * m[2]  * m[11] - m[12] * m[3]  * m[10];
    inv[9] = -m[0] * m[9]  * m[15] + m[0]  * m[11] * m[13] + m[8]  * m[1]  * m[15] - 
              m[8] * m[3]  * m[13] - m[12] * m[1]  * m[11] + m[12] * m[3]  * m[9];
    inv[13] = m[0] * m[9]  * m[14] - m[0]  * m[10] * m[13] - m[8]  * m[1]  * m[14] + 
              m[8] * m[2]  * m[13] + m[12] * m[1]  * m[10] - m[12] * m[2]  * m[9];

    inv[2] = m[1]  * m[6] * m[15] - m[1]  * m[7] * m[14] - m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0]  * m[7] * m[14] + m[4]  * m[2] * m[15] - 
              m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0]  * m[7] * m[13] - m[4]  * m[1] * m[15] + 
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
    inv[14] = -m[0]* m[5] * m[14] + m[0]  * m[6] * m[13] + m[4]  * m[1] * m[14] - 
               m[4]* m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - m[9] * m[2] * m[7]  + m[9] * m[3] * m[6];
    inv[7] = m[0]  * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + 
             m[4]  * m[3] * m[10] + m[8] * m[2] * m[7]  - m[8] * m[3] * m[6];
    inv[11] = -m[0]* m[5] * m[11] + m[0] * m[7] * m[9]  + m[4] * m[1] * m[11] - 
               m[4]* m[3] * m[9]  - m[8] * m[1] * m[7]  + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9]  - m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9]  + m[8] * m[1] * m[6]  - m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(det) < 1e-7f) return false;

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * invDet;
    return true;
}

// Standalone matrix decomposition into Translation, Euler Angles (deg), and Scale
inline void DecomposeMatrix(const float* m, float* pos, float* rot, float* scale) {
    pos[0] = m[12];
    pos[1] = m[13];
    pos[2] = m[14];

    float sx = std::sqrt(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    float sy = std::sqrt(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
    float sz = std::sqrt(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);

    scale[0] = sx;
    scale[1] = sy;
    scale[2] = sz;

    if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f) {
        rot[0] = rot[1] = rot[2] = 0.0f;
        return;
    }

    float r00 = m[0] / sx, r01 = m[1] / sx, r02 = m[2] / sx;
    float r12 = m[6] / sy, r22 = m[10] / sz;

    float sinY = -r02;
    if (sinY < -1.0f) sinY = -1.0f;
    if (sinY > 1.0f) sinY = 1.0f;
    float radY = std::asin(sinY);
    rot[1] = radY * 57.2957795f;

    float cosY = std::cos(radY);
    if (std::abs(cosY) > 1e-4f) {
        rot[0] = std::atan2(r12, r22) * 57.2957795f;
        rot[2] = std::atan2(r01, r00) * 57.2957795f;
    } else {
        float r11 = m[5] / sy, r21 = m[9] / sz;
        rot[0] = std::atan2(-r21, r11) * 57.2957795f;
        rot[2] = 0.0f;
    }
}

// Recursively compute the World Matrix of an entity in a Scene
inline void ComputeWorldMatrix(const Scene& scene, const Entity& entity, float* out_world_matrix) {
    float local_matrix[16];
    BuildMatrix(entity.position, entity.rotation, entity.scale, local_matrix);

    if (entity.parent_id == 0) {
        std::memcpy(out_world_matrix, local_matrix, sizeof(float) * 16);
        return;
    }

    // Traverse ancestors up to root
    std::vector<const Entity*> chain;
    uint32_t curr = entity.parent_id;
    std::unordered_set<uint32_t> visited{entity.id};
    while (curr != 0 && visited.insert(curr).second) {
        const Entity* p = scene.find_entity(curr);
        if (!p) break;
        chain.push_back(p);
        curr = p->parent_id;
    }

    if (chain.empty()) {
        std::memcpy(out_world_matrix, local_matrix, sizeof(float) * 16);
        return;
    }

    // Multiply ancestors from root downwards
    float parent_world[16];
    BuildMatrix(chain.back()->position, chain.back()->rotation, chain.back()->scale, parent_world);
    for (int i = static_cast<int>(chain.size()) - 2; i >= 0; --i) {
        float next_local[16];
        BuildMatrix(chain[i]->position, chain[i]->rotation, chain[i]->scale, next_local);
        MultiplyMatrices(next_local, parent_world, parent_world);
    }

    // Combine local with parent world: World = Local * ParentWorld
    MultiplyMatrices(local_matrix, parent_world, out_world_matrix);
}

// Compute World Position, Rotation, Scale, and optionally World Matrix
inline void ComputeWorldTransform(const Scene& scene, const Entity& entity, float* out_pos, float* out_rot, float* out_scale, float* out_world_matrix = nullptr) {
    if (entity.parent_id == 0) {
        out_pos[0] = entity.position[0]; out_pos[1] = entity.position[1]; out_pos[2] = entity.position[2];
        out_rot[0] = entity.rotation[0]; out_rot[1] = entity.rotation[1]; out_rot[2] = entity.rotation[2];
        out_scale[0] = entity.scale[0];  out_scale[1] = entity.scale[1];  out_scale[2] = entity.scale[2];
        if (out_world_matrix) BuildMatrix(out_pos, out_rot, out_scale, out_world_matrix);
        return;
    }

    float world_matrix[16];
    ComputeWorldMatrix(scene, entity, world_matrix);
    DecomposeMatrix(world_matrix, out_pos, out_rot, out_scale);
    if (out_world_matrix) std::memcpy(out_world_matrix, world_matrix, sizeof(float) * 16);
}

// Convert a desired World Matrix into Local Position, Rotation, Scale relative to a given parent_id
inline bool ComputeLocalTransform(const Scene& scene, uint32_t parent_id, const float* world_matrix, float* out_pos, float* out_rot, float* out_scale) {
    if (parent_id == 0) {
        DecomposeMatrix(world_matrix, out_pos, out_rot, out_scale);
        return true;
    }
    const Entity* parent = scene.find_entity(parent_id);
    if (!parent) {
        DecomposeMatrix(world_matrix, out_pos, out_rot, out_scale);
        return false;
    }
    float parent_world[16];
    ComputeWorldMatrix(scene, *parent, parent_world);
    float parent_inv[16];
    if (!InvertMatrix(parent_world, parent_inv)) {
        DecomposeMatrix(world_matrix, out_pos, out_rot, out_scale);
        return false;
    }
    float local_matrix[16];
    MultiplyMatrices(world_matrix, parent_inv, local_matrix);
    DecomposeMatrix(local_matrix, out_pos, out_rot, out_scale);
    return true;
}

// A TRS transform cannot represent every matrix (for example, shear caused by
// rotated non-uniform scales). Reject those cases instead of moving the object.
inline bool ReparentEntityPreservingWorld(Scene& scene, uint32_t child_id, uint32_t parent_id) {
    Entity* child = scene.find_entity(child_id);
    if (!child || child_id == parent_id || (parent_id && !scene.find_entity(parent_id)) ||
        (parent_id && scene.is_descendant_of(parent_id, child_id))) return false;
    if (child->parent_id == parent_id) return true;
    float world[16], position[3], rotation[3], scale[3];
    ComputeWorldMatrix(scene, *child, world);
    if (!ComputeLocalTransform(scene, parent_id, world, position, rotation, scale)) return false;
    float local[16], restored[16];
    BuildMatrix(position, rotation, scale, local);
    if (parent_id) {
        float parent_world[16];
        ComputeWorldMatrix(scene, *scene.find_entity(parent_id), parent_world);
        MultiplyMatrices(local, parent_world, restored);
    } else {
        std::memcpy(restored, local, sizeof(restored));
    }
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(restored[i]) || std::abs(restored[i] - world[i]) > 0.001f * std::max(1.0f, std::abs(world[i]))) return false;
    child->parent_id = parent_id;
    std::memcpy(child->position, position, sizeof(position));
    std::memcpy(child->rotation, rotation, sizeof(rotation));
    std::memcpy(child->scale, scale, sizeof(scale));
    return true;
}

// Convert Euler (Degrees) to Quaternion (XYZW)
inline void EulerToQuat(float pitch, float yaw, float roll, float& qx, float& qy, float& qz, float& qw) {
    float p = pitch * 0.5f * 0.0174532925f;
    float y = yaw   * 0.5f * 0.0174532925f;
    float r = roll  * 0.5f * 0.0174532925f;

    float sinP = sinf(p); float cosP = cosf(p);
    float sinY = sinf(y); float cosY = cosf(y);
    float sinR = sinf(r); float cosR = cosf(r);

    qx = sinR * cosY * cosP - cosR * sinY * sinP;
    qy = cosR * sinY * cosP + sinR * cosY * sinP;
    qz = cosR * cosY * sinP - sinR * sinY * cosP;
    qw = cosR * cosY * cosP + sinR * sinY * sinP;
}

} // namespace GM3D::Studio
