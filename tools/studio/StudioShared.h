#pragma once

// Small helpers shared by the Studio translation units.
//
// They used to live in an anonymous namespace inside StudioApp.cpp, which worked only while
// the editor was a single file. Every panel now needs them (HelpTooltip alone is called from
// most of the UI), so they are inline functions in the Studio namespace instead - the call
// sites did not change.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace GM3D::Studio {

inline std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

inline std::string path_utf8(const std::filesystem::path& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

inline void HelpTooltip(const char* description) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Quaternion (XYZW) to Euler (Degrees)
inline void QuatToEuler(float qx, float qy, float qz, float qw, float& pitch, float& yaw, float& roll) {
    float sinr_cosp = 2.0f * (qw * qx + qy * qz);
    float cosr_cosp = 1.0f - 2.0f * (qx * qx + qy * qy);
    pitch = atan2f(sinr_cosp, cosr_cosp) * 57.2957795f;

    float sinp = 2.0f * (qw * qy - qz * qx);
    if (fabsf(sinp) >= 1.0f)
        yaw = copysignf(90.0f, sinp);
    else
        yaw = asinf(sinp) * 57.2957795f;

    float siny_cosp = 2.0f * (qw * qz + qx * qy);
    float cosy_cosp = 1.0f - 2.0f * (qy * qy + qz * qz);
    roll = atan2f(siny_cosp, cosy_cosp) * 57.2957795f;
}

// Compute LookAt View Matrix
inline void ComputeLookAtLH(float* out, float ex, float ey, float ez, float tx, float ty, float tz, float upx, float upy, float upz) {
    float zx = tx - ex, zy = ty - ey, zz = tz - ez;
    float lenZ = sqrtf(zx * zx + zy * zy + zz * zz);
    if (lenZ > 0.0001f) { zx /= lenZ; zy /= lenZ; zz /= lenZ; } else { zz = 1.0f; }

    float xx = upy * zz - upz * zy;
    float xy = upz * zx - upx * zz;
    float xz = upx * zy - upy * zx;
    float lenX = sqrtf(xx * xx + xy * xy + xz * xz);
    if (lenX > 0.0001f) { xx /= lenX; xy /= lenX; xz /= lenX; } else { xx = 1.0f; }

    float yx = zy * xz - zz * xy;
    float yy = zz * xx - zx * xz;
    float yz = zx * xy - zy * xx;

    out[0] = xx; out[1] = yx; out[2] = zx; out[3] = 0.0f;
    out[4] = xy; out[5] = yy; out[6] = zy; out[7] = 0.0f;
    out[8] = xz; out[9] = yz; out[10] = zz; out[11] = 0.0f;
    out[12] = -(xx * ex + xy * ey + xz * ez);
    out[13] = -(yx * ex + yy * ey + yz * ez);
    out[14] = -(zx * ex + zy * ey + zz * ez);
    out[15] = 1.0f;
}

// Compute Perspective Projection Matrix
inline void ComputePerspectiveLH(float* out, float fovYDeg, float aspect, float nearZ, float farZ) {
    float fovRad = fovYDeg * 0.0174532925f;
    float tanHalfFov = tanf(fovRad * 0.5f);
    float yScale = 1.0f / tanHalfFov;
    float xScale = yScale / aspect;

    memset(out, 0, sizeof(float) * 16);
    out[0] = xScale;
    out[5] = yScale;
    out[10] = farZ / (farZ - nearZ);
    out[11] = 1.0f;
    out[14] = (-nearZ * farZ) / (farZ - nearZ);
}

// Blender / Unreal-style colored 3D vector input control
inline void DrawVec3Control(const char* label, float* values, float resetValue = 0.0f, float speed = 0.1f) {
    ImGui::PushID(label);

    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 75.0f);
    ImGui::Text("%s", label);
    ImGui::NextColumn();

    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));

    float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    ImVec2 buttonSize = ImVec2(lineHeight, lineHeight);

    // X (Red)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("X", buttonSize)) values[0] = resetValue;
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##X", &values[0], speed, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Y (Green)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.80f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.65f, 0.15f, 1.0f));
    if (ImGui::Button("Y", buttonSize)) values[1] = resetValue;
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Y", &values[1], speed, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Z (Blue)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.35f, 0.85f, 1.0f));
    if (ImGui::Button("Z", buttonSize)) values[2] = resetValue;
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Z", &values[2], speed, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();
}

} // namespace GM3D::Studio
