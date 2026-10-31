// Unit tests for the culling math used by RenderWorld. No Direct3D device required:
// the helpers in FrustumMath.h are pure math on the engine's matrix convention
// (row-major storage, row-vector arithmetic, left-handed projection, depth [0,1]).

#include "render/FrustumMath.h"

#include <cmath>
#include <iostream>

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool nearly(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

// Camera at the origin looking down +Z, 90 degrees vertical FOV, square aspect.
void build_identity_lh_projection(float out[16]) {
    const float near_z = 0.1f;
    const float far_z = 100.0f;
    const float h = 1.0f / std::tan(45.0f * 0.5f * 3.14159265358979323846f / 180.0f);
    const float w = h;
    const float q = far_z / (far_z - near_z);
    const float proj[16] = {
        w,    0.0f, 0.0f,        0.0f,
        0.0f, h,    0.0f,        0.0f,
        0.0f, 0.0f, q,           1.0f,
        0.0f, 0.0f, -q * near_z, 0.0f
    };
    for (int i = 0; i < 16; ++i) out[i] = proj[i];
}

void test_frustum_planes() {
    float view_proj[16];
    build_identity_lh_projection(view_proj);
    const GM3D::FrustumPlanes frustum = GM3D::extract_frustum_planes(view_proj);

    const float inside_min[3] = { -0.5f, -0.5f, 4.5f };
    const float inside_max[3] = {  0.5f,  0.5f, 5.5f };
    check(GM3D::aabb_inside_frustum(frustum, inside_min, inside_max),
          "box in front of the camera must be visible");

    const float behind_min[3] = { -0.5f, -0.5f, -5.5f };
    const float behind_max[3] = {  0.5f,  0.5f, -4.5f };
    check(!GM3D::aabb_inside_frustum(frustum, behind_min, behind_max),
          "box behind the camera must be culled");

    const float beyond_far_min[3] = { -0.5f, -0.5f, 120.0f };
    const float beyond_far_max[3] = {  0.5f,  0.5f, 121.0f };
    check(!GM3D::aabb_inside_frustum(frustum, beyond_far_min, beyond_far_max),
          "box past the far plane must be culled");

    const float beside_min[3] = { 40.0f, -0.5f, 4.5f };
    const float beside_max[3] = { 41.0f,  0.5f, 5.5f };
    check(!GM3D::aabb_inside_frustum(frustum, beside_min, beside_max),
          "box far to the side must be culled");

    const float above_min[3] = { -0.5f, 40.0f, 4.5f };
    const float above_max[3] = {  0.5f, 41.0f, 5.5f };
    check(!GM3D::aabb_inside_frustum(frustum, above_min, above_max),
          "box far above must be culled");

    const float straddle_min[3] = { -0.5f, -0.5f, -0.2f };
    const float straddle_max[3] = {  0.5f,  0.5f,  1.2f };
    check(GM3D::aabb_inside_frustum(frustum, straddle_min, straddle_max),
          "box straddling the near plane must not be culled");

    const float huge_min[3] = { -500.0f, -500.0f, -500.0f };
    const float huge_max[3] = {  500.0f,  500.0f,  500.0f };
    check(GM3D::aabb_inside_frustum(frustum, huge_min, huge_max),
          "a box enclosing the frustum must not be culled");
}

void test_transform_aabb() {
    const float local_min[3] = { -0.5f, -0.5f, -0.5f };
    const float local_max[3] = {  0.5f,  0.5f,  0.5f };
    float world_min[3], world_max[3];

    const float position[3] = { 0.0f, 1.0f, 0.0f };
    const float no_rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float scale[3] = { 2.0f, 2.0f, 2.0f };
    GM3D::transform_aabb(position, no_rotation, scale, local_min, local_max, world_min, world_max);
    check(nearly(world_min[0], -1.0f) && nearly(world_max[0], 1.0f) &&
          nearly(world_min[1], 0.0f) && nearly(world_max[1], 2.0f) &&
          nearly(world_min[2], -1.0f) && nearly(world_max[2], 1.0f),
          "translation and scale must be applied to the local bounds");

    // 90 degrees around Y: the long X axis of the box becomes the long Z axis.
    const float box_min[3] = { -1.0f, -0.5f, -0.5f };
    const float box_max[3] = {  1.0f,  0.5f,  0.5f };
    const float half_sqrt2 = 0.70710678f;
    const float quarter_turn[4] = { 0.0f, half_sqrt2, 0.0f, half_sqrt2 };
    const float unit_scale[3] = { 1.0f, 1.0f, 1.0f };
    const float origin[3] = { 0.0f, 0.0f, 0.0f };
    GM3D::transform_aabb(origin, quarter_turn, unit_scale, box_min, box_max, world_min, world_max);
    check(nearly(world_min[0], -0.5f) && nearly(world_max[0], 0.5f) &&
          nearly(world_min[1], -0.5f) && nearly(world_max[1], 0.5f) &&
          nearly(world_min[2], -1.0f) && nearly(world_max[2], 1.0f),
          "a quarter turn around Y must swap the X and Z extents");

    // Mirrored scale must not produce an inverted (empty) box.
    const float mirrored_scale[3] = { -1.0f, 1.0f, 1.0f };
    GM3D::transform_aabb(origin, no_rotation, mirrored_scale, box_min, box_max, world_min, world_max);
    check(nearly(world_min[0], -1.0f) && nearly(world_max[0], 1.0f) &&
          world_min[0] <= world_max[0] && world_min[1] <= world_max[1] && world_min[2] <= world_max[2],
          "negative scale must still produce an ordered bounds box");
}

} // namespace

int main() {
    test_frustum_planes();
    test_transform_aabb();
    if (g_failures == 0) {
        std::cout << "FrustumMathTests: all checks passed\n";
        return 0;
    }
    std::cerr << "FrustumMathTests: " << g_failures << " failure(s)\n";
    return 1;
}
