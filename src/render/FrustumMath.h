#pragma once

// Frustum culling helpers shared by the renderer and its tests.
//
// Engine convention: row-major matrices, row-vector arithmetic inside HLSL
// (D3DCOMPILE_PACK_MATRIX_ROW_MAJOR), left-handed projection with depth in [0, 1].
// A clip-space point is therefore `p * view_proj`, and the six clip planes are
// combinations of the matrix columns (not rows).
//
// This header intentionally has no Direct3D or DirectXMath dependency so it can be
// unit tested without a device.

#include <cmath>

namespace GM3D {

struct FrustumPlanes {
    // Plane equation: a*x + b*y + c*z + d >= 0 means "inside".
    float planes[6][4];
};

inline FrustumPlanes extract_frustum_planes(const float view_proj[16]) {
    float c0[4], c1[4], c2[4], c3[4];
    for (int row = 0; row < 4; ++row) {
        c0[row] = view_proj[row * 4 + 0];
        c1[row] = view_proj[row * 4 + 1];
        c2[row] = view_proj[row * 4 + 2];
        c3[row] = view_proj[row * 4 + 3];
    }

    FrustumPlanes frustum{};
    const float* columns[3] = { c0, c1, c2 };
    for (int axis = 0; axis < 3; ++axis) {
        for (int k = 0; k < 4; ++k) {
            frustum.planes[axis * 2 + 0][k] = columns[axis][k] + c3[k];
            frustum.planes[axis * 2 + 1][k] = c3[k] - columns[axis][k];
        }
    }
    // Depth range [0, 1] instead of the OpenGL [-1, 1]: near is z >= 0, far is z <= w.
    for (int k = 0; k < 4; ++k) {
        frustum.planes[4][k] = c2[k];
        frustum.planes[5][k] = c3[k] - c2[k];
    }
    return frustum;
}

inline bool aabb_inside_frustum(const FrustumPlanes& frustum, const float bounds_min[3], const float bounds_max[3]) {
    for (int i = 0; i < 6; ++i) {
        const float* plane = frustum.planes[i];
        // Test only the corner that is furthest along the plane normal: if even that
        // one is behind the plane, the whole box is outside.
        const float px = (plane[0] >= 0.0f) ? bounds_max[0] : bounds_min[0];
        const float py = (plane[1] >= 0.0f) ? bounds_max[1] : bounds_min[1];
        const float pz = (plane[2] >= 0.0f) ? bounds_max[2] : bounds_min[2];
        if (plane[0] * px + plane[1] * py + plane[2] * pz + plane[3] < 0.0f) return false;
    }
    return true;
}

// Transforms a local-space AABB by translation + rotation + scale and returns the
// world-space AABB. Eight corners keep mirrored (negative scale) transforms correct.
inline void transform_aabb(const float position[3], const float rotation[4], const float scale[3],
                           const float local_min[3], const float local_max[3],
                           float out_min[3], float out_max[3]) {
    const float qx = rotation[0], qy = rotation[1], qz = rotation[2], qw = rotation[3];
    const float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
    const float r01 = 2.0f * (qx * qy - qz * qw);
    const float r02 = 2.0f * (qx * qz + qy * qw);
    const float r10 = 2.0f * (qx * qy + qz * qw);
    const float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
    const float r12 = 2.0f * (qy * qz - qx * qw);
    const float r20 = 2.0f * (qx * qz - qy * qw);
    const float r21 = 2.0f * (qy * qz + qx * qw);
    const float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);

    for (int corner = 0; corner < 8; ++corner) {
        const float lx = (corner & 1) ? local_max[0] : local_min[0];
        const float ly = (corner & 2) ? local_max[1] : local_min[1];
        const float lz = (corner & 4) ? local_max[2] : local_min[2];

        const float sx = lx * scale[0];
        const float sy = ly * scale[1];
        const float sz = lz * scale[2];

        const float wx = r00 * sx + r01 * sy + r02 * sz + position[0];
        const float wy = r10 * sx + r11 * sy + r12 * sz + position[1];
        const float wz = r20 * sx + r21 * sy + r22 * sz + position[2];

        if (corner == 0) {
            out_min[0] = out_max[0] = wx;
            out_min[1] = out_max[1] = wy;
            out_min[2] = out_max[2] = wz;
        } else {
            out_min[0] = (wx < out_min[0]) ? wx : out_min[0];
            out_min[1] = (wy < out_min[1]) ? wy : out_min[1];
            out_min[2] = (wz < out_min[2]) ? wz : out_min[2];
            out_max[0] = (wx > out_max[0]) ? wx : out_max[0];
            out_max[1] = (wy > out_max[1]) ? wy : out_max[1];
            out_max[2] = (wz > out_max[2]) ? wz : out_max[2];
        }
    }
}

} // namespace GM3D
