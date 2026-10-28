#pragma once
#include <cmath>

namespace GM3D::Studio {
// Matches the viewport's left-handed look-at camera and 60-degree perspective.
inline bool viewport_ray(const float (&origin)[3], const float (&target)[3],
                         float x, float y, float width, float height, float (&ray)[3]) {
    if (width <= 0.0f || height <= 0.0f) return false;
    float fx = target[0] - origin[0], fy = target[1] - origin[1], fz = target[2] - origin[2];
    const float forward_length = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (forward_length < 0.0001f) return false;
    fx /= forward_length; fy /= forward_length; fz /= forward_length;
    const float horizontal = std::sqrt(fx*fx + fz*fz);
    if (horizontal < 0.0001f) return false;
    const float rx = fz / horizontal, rz = -fx / horizontal;
    const float ux = rz * fy, uy = rx * fz - rz * fx, uz = -rx * fy;
    const float cx = (2.0f * x / width - 1.0f) * std::tan(0.5235987756f) * width / height;
    const float cy = (1.0f - 2.0f * y / height) * std::tan(0.5235987756f);
    ray[0] = rx * cx + ux * cy + fx;
    ray[1] = uy * cy + fy;
    ray[2] = rz * cx + uz * cy + fz;
    const float length = std::sqrt(ray[0]*ray[0] + ray[1]*ray[1] + ray[2]*ray[2]);
    if (length < 0.0001f) return false;
    for (float& component : ray) component /= length;
    return true;
}
}
