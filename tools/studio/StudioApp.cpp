#include "StudioApp.h"
#include "SceneSerializer.h"
#include "Heightmap.h"
#include "ViewportMath.h"
#include "core/EngineContext.h"
#include "render/RenderWorld.h"
#include "physics/PhysicsWorld.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ImGuizmo.h"

#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <cmath>
#include <cctype>
#include <cwctype>
#include <iostream>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GM3D::Studio {

namespace {

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string path_utf8(const std::filesystem::path& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

void HelpTooltip(const char* description) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Euler (Degrees) to Quaternion (XYZW)
void EulerToQuat(float pitch, float yaw, float roll, float& qx, float& qy, float& qz, float& qw) {
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

// Quaternion (XYZW) to Euler (Degrees)
void QuatToEuler(float qx, float qy, float qz, float qw, float& pitch, float& yaw, float& roll) {
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
void ComputeLookAtLH(float* out, float ex, float ey, float ez, float tx, float ty, float tz, float upx, float upy, float upz) {
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
void ComputePerspectiveLH(float* out, float fovYDeg, float aspect, float nearZ, float farZ) {
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
void DrawVec3Control(const char* label, float* values, float resetValue = 0.0f, float speed = 0.1f) {
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

static LRESULT CALLBACK StudioWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    auto* app = reinterpret_cast<StudioApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (app) {
        return app->handle_message(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

StudioApp::StudioApp() = default;

StudioApp::~StudioApp() {
    shutdown();
}

bool StudioApp::initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hinstance = hInstance;

    // 1. Register Win32 Window Class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StudioWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GM3DStudioWindowClass";

    if (!RegisterClassExW(&wc)) return false;

    // 2. Create Window (1600x900 default)
    RECT rc = { 0, 0, static_cast<LONG>(m_window_width), static_cast<LONG>(m_window_height) };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"GM3D Studio - 3D World Editor",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // 3. Allocate Shared Memory Buffer (10000 instances capacity)
    const size_t total_buffer_size = sizeof(SharedBufferHeader) + 10000 * sizeof(SharedEntityData);
    m_shared_buffer.resize(total_buffer_size, 0);

    auto* header = reinterpret_cast<SharedBufferHeader*>(m_shared_buffer.data());
    header->magic = 0x474D3344; // 'GM3D'
    header->version = 1;
    header->entity_count = 0;
    header->max_entities = 10000;

    // 4. Initialize EngineContext
    double init_res = EngineContext::instance().initialize(
        m_shared_buffer.data(),
        static_cast<double>(total_buffer_size),
        m_hwnd,
        m_window_width,
        m_window_height
    );

    if (init_res != GMResult::OK) {
        std::cerr << "EngineContext failed to initialize!" << std::endl;
        return false;
    }

    // 5. Pre-allocate initial Viewport Render Texture so it's ready from frame 1
    resize_viewport_texture(1280, 720);

    // 6. Initialize Dear ImGui
    init_imgui();

    // 7. Show Window
    ShowWindow(m_hwnd, (nCmdShow == 0 || nCmdShow == SW_HIDE) ? SW_SHOWNORMAL : nCmdShow);
    UpdateWindow(m_hwnd);
    DragAcceptFiles(m_hwnd, TRUE);

    // 8. Initialize Default 3D Editor World
    new_scene();

    m_running = true;
    return true;
}

void StudioApp::init_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Load High-Quality Windows Segoe UI font with Cyrillic support
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 3;
    font_cfg.OversampleV = 2;
    font_cfg.RasterizerMultiply = 1.15f;
    const char* font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(font_path) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(font_path, 17.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
    } else {
        io.Fonts->AddFontDefault();
    }

    setup_style();

    auto* rw = EngineContext::instance().get_render_world();
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(rw->get_device(), rw->get_context());
    m_imgui_initialized = true;
}

void StudioApp::setup_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Modern Professional Dark Theme (Blender / Unreal Inspired)
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);

    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.48f, 0.52f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.13f, 0.14f, 0.17f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.22f, 0.26f, 0.85f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.09f, 0.10f, 0.12f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.22f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.34f, 0.38f, 0.46f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.26f, 0.64f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.26f, 0.64f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.38f, 0.74f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.25f, 0.36f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.52f, 0.90f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.17f, 0.21f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.22f, 0.30f, 0.40f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.25f, 0.36f, 0.50f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.24f, 0.28f, 0.36f, 1.00f);
    colors[ImGuiCol_TabSelected]           = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TabDimmed]             = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.26f, 0.64f, 1.00f, 0.45f);
}

int StudioApp::run() {
    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    MSG msg{};
    while (m_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!m_running) break;

        LARGE_INTEGER curTime;
        QueryPerformanceCounter(&curTime);
        float delta_time = static_cast<float>(curTime.QuadPart - lastTime.QuadPart) / static_cast<float>(freq.QuadPart);
        lastTime = curTime;
        delta_time = std::min(delta_time, 0.05f);
        m_delta_time = delta_time;

        update(delta_time);
        render();
    }

    return static_cast<int>(msg.wParam);
}

void StudioApp::update(float delta_time) {
    process_dropped_files();
    update_assets(delta_time);
    update_camera(delta_time);

    if (m_simulating) {
        auto* entities = reinterpret_cast<SharedEntityData*>(m_shared_buffer.data() + sizeof(SharedBufferHeader));
        for (size_t i = 0; i < m_scene.entities.size() && i < (m_scene.has_terrain ? 9999u : 10000u); ++i) {
            auto& e = m_scene.entities[i];
            if (e.physics_type == 2 && e.physics_body_id != 0) {
                const SharedEntityData& sed = entities[i];
                e.position[0] = sed.px;
                e.position[1] = sed.py;
                e.position[2] = sed.pz;
                QuatToEuler(sed.qx, sed.qy, sed.qz, sed.qw, e.rotation[0], e.rotation[1], e.rotation[2]);
            }
        }
    }

    sync_scene_to_engine();
}

void StudioApp::update_camera(float delta_time) {
    ImGuiIO& io = ImGui::GetIO();

    if (m_viewport_hovered || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        // Free-Fly Camera Mode: Hold Right Mouse Button
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                float direction[3];
                if (viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction)) {
                    m_cam_yaw = std::atan2(direction[0], direction[2]) * 57.2957795f;
                    m_cam_pitch = -std::asin(std::clamp(direction[1], -1.0f, 1.0f)) * 57.2957795f;
                }
            }
            float mouse_dx = io.MouseDelta.x;
            float mouse_dy = io.MouseDelta.y;

            m_cam_yaw += mouse_dx * 0.25f;
            m_cam_pitch += mouse_dy * 0.25f;
            m_cam_pitch = std::clamp(m_cam_pitch, -88.0f, 88.0f);

            float yawRad = m_cam_yaw * 0.0174532925f;
            float pitchRad = m_cam_pitch * 0.0174532925f;

            float fx = sinf(yawRad) * cosf(pitchRad);
            float fy = -sinf(pitchRad);
            float fz = cosf(yawRad) * cosf(pitchRad);

            float rx = cosf(yawRad);
            float ry = 0.0f;
            float rz = -sinf(yawRad);

            float speed = m_cam_speed * delta_time;
            if (io.KeyShift) speed *= 2.5f;

            if (ImGui::IsKeyDown(ImGuiKey_W)) { m_cam_pos[0] += fx * speed; m_cam_pos[1] += fy * speed; m_cam_pos[2] += fz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_S)) { m_cam_pos[0] -= fx * speed; m_cam_pos[1] -= fy * speed; m_cam_pos[2] -= fz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_D)) { m_cam_pos[0] += rx * speed; m_cam_pos[1] += ry * speed; m_cam_pos[2] += rz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_A)) { m_cam_pos[0] -= rx * speed; m_cam_pos[1] -= ry * speed; m_cam_pos[2] -= rz * speed; }
            if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) { m_cam_pos[1] += speed; }
            if (ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_C)) { m_cam_pos[1] -= speed; }

            m_cam_target[0] = m_cam_pos[0] + fx * 10.0f;
            m_cam_target[1] = m_cam_pos[1] + fy * 10.0f;
            m_cam_target[2] = m_cam_pos[2] + fz * 10.0f;
        }

        // Move along the actual look direction, preserving orientation.
        if (m_viewport_hovered && io.MouseWheel != 0.0f) {
            float direction[3];
            if (viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction)) {
                const float zoom = io.MouseWheel * 3.5f;
                for (int i = 0; i < 3; ++i) {
                    const float delta = direction[i] * zoom;
                    m_cam_pos[i] += delta;
                    m_cam_target[i] += delta;
                }
            }
        }


    }

    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput && !io.KeyCtrl && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && m_selected_entity_id >= 0) {
        for (const auto& e : m_scene.entities) {
            if (static_cast<int32_t>(e.id) != m_selected_entity_id) continue;
            float direction[3];
            if (!viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction)) break;
            float wpos[3], wrot[3], wscale[3];
            ComputeWorldTransform(m_scene, e, wpos, wrot, wscale);
            const float radius = e.is_point_light ? 1.0f : std::max({std::abs(wscale[0]), std::abs(wscale[1]), std::abs(wscale[2]), 0.5f});
            const float distance = std::max(3.0f, radius * 2.5f);
            for (int i = 0; i < 3; ++i) {
                m_cam_target[i] = wpos[i];
                m_cam_pos[i] = wpos[i] - direction[i] * distance;
            }
            m_cam_yaw = std::atan2(direction[0], direction[2]) * 57.2957795f;
            m_cam_pitch = -std::asin(std::clamp(direction[1], -1.0f, 1.0f)) * 57.2957795f;
            break;
        }
    }
}

void StudioApp::sync_scene_to_engine() {
    auto* header = reinterpret_cast<SharedBufferHeader*>(m_shared_buffer.data());
    auto* entities = reinterpret_cast<SharedEntityData*>(m_shared_buffer.data() + sizeof(SharedBufferHeader));

    header->magic = 0x474D3344; // 'GM3D'
    header->version = 1;
    const size_t entity_limit = m_scene.has_terrain ? 9999 : 10000;
    const size_t entity_count = std::min(m_scene.entities.size(), entity_limit);
    header->entity_count = static_cast<uint32_t>(entity_count + (m_scene.has_terrain ? 1 : 0));
    header->max_entities = 10000;

    header->camera_pos[0] = m_cam_pos[0];
    header->camera_pos[1] = m_cam_pos[1];
    header->camera_pos[2] = m_cam_pos[2];

    header->camera_target[0] = m_cam_target[0];
    header->camera_target[1] = m_cam_target[1];
    header->camera_target[2] = m_cam_target[2];

    header->camera_fov = 60.0f;
    header->camera_near = 0.1f;
    header->camera_far = 800.0f;

    for (size_t i = 0; i < entity_count; ++i) {
        const auto& e = m_scene.entities[i];
        SharedEntityData& out = entities[i];

        out.entity_id = e.id;

        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, e, wpos, wrot, wscale);

        out.px = wpos[0];
        out.py = wpos[1];
        out.pz = wpos[2];

        float qx, qy, qz, qw;
        EulerToQuat(wrot[0], wrot[1], wrot[2], qx, qy, qz, qw);
        out.qx = qx; out.qy = qy; out.qz = qz; out.qw = qw;

        out.sx = wscale[0];
        out.sy = wscale[1];
        out.sz = wscale[2];

        out.mesh_id = e.mesh_id;
        out.material_id = e.material_id;
        out.flags = 0;
        if (e.visible && !e.is_point_light && !e.is_group && !(m_scene.has_terrain && e.editor_grid)) out.flags |= 1;      // ACTIVE
        if (e.cast_shadow && !e.is_group) out.flags |= 2; // CAST_SHADOW
        if (m_simulating && e.physics_type == 2 && e.physics_body_id != 0) {
            out.flags |= 4; // IS_RIGIDBODY
        }

        out.physics_id = e.physics_body_id;
        out.custom_param1 = static_cast<float>(e.material_id);
        out.custom_param2 = static_cast<float>(e.texture_id);
    }

    if (m_scene.has_terrain) {
        auto& terrain = entities[entity_count];
        terrain = {};
        terrain.entity_id = UINT32_MAX;
        terrain.qw = 1.0f;
        terrain.sx = terrain.sy = terrain.sz = 1.0f;
        terrain.mesh_id = 5;
        terrain.flags = EntityFlags::ACTIVE | EntityFlags::CAST_SHADOW;
        terrain.custom_param2 = 5.0f;
    }

    // Sync Lighting & Sky
    auto& ctx = EngineContext::instance();
    float radPitch = m_scene.sun_pitch * 0.0174532925f;
    float radYaw = m_scene.sun_yaw * 0.0174532925f;
    float lx = -sinf(radYaw) * cosf(radPitch);
    float ly = -sinf(radPitch);
    float lz = cosf(radYaw) * cosf(radPitch);

    ctx.clear_point_lights();
    uint32_t light_index = 0;
    for (const auto& entity : m_scene.entities) {
        if (!entity.is_point_light || !entity.visible) continue;
        if (light_index >= 16) break;
        ctx.set_point_light(light_index++, entity.position[0], entity.position[1], entity.position[2],
                            std::max(0.1f, entity.light_radius), entity.light_color[0], entity.light_color[1],
                            entity.light_color[2], std::max(0.0f, entity.light_intensity));
    }
    if (auto* rw = ctx.get_render_world()) {
        rw->set_shadow_settings(m_scene.shadows_enabled, m_scene.shadow_strength, m_scene.shadow_softness, m_scene.shadow_coverage);
        for (const auto& mat : m_scene.materials) {
            MaterialGPU gpuMat{};
            gpuMat.base_color[0] = mat.base_color[0];
            gpuMat.base_color[1] = mat.base_color[1];
            gpuMat.base_color[2] = mat.base_color[2];
            gpuMat.base_color[3] = mat.base_color[3];
            gpuMat.roughness = mat.roughness;
            gpuMat.metallic = mat.metallic;
            gpuMat.emissive = mat.emissive;
            gpuMat.alpha_cutoff = mat.alpha_cutoff;
            gpuMat.uv_tiling[0] = mat.uv_tiling[0];
            gpuMat.uv_tiling[1] = mat.uv_tiling[1];
            gpuMat.uv_offset[0] = mat.uv_offset[0];
            gpuMat.uv_offset[1] = mat.uv_offset[1];
            gpuMat.alpha_mode = mat.alpha_mode;
            gpuMat.preset_id = mat.is_builtin_preset ? static_cast<int32_t>(mat.preset_index) : 0;
            rw->set_material_properties(static_cast<uint16_t>(mat.id), gpuMat, mat.albedo_tex_id, mat.normal_tex_id);
        }
    }
    ctx.set_sun_light(lx, ly, lz, m_scene.sun_color[0], m_scene.sun_color[1], m_scene.sun_color[2], m_scene.sun_intensity);
    ctx.set_ambient_light(m_scene.ambient_color[0], m_scene.ambient_color[1], m_scene.ambient_color[2], m_scene.ambient_intensity);
    ctx.set_sky_color(m_scene.sky_color[0], m_scene.sky_color[1], m_scene.sky_color[2]);
    ctx.set_fog(m_scene.fog_start, m_scene.fog_end, m_scene.sky_color[0], m_scene.sky_color[1], m_scene.sky_color[2]);
}

void StudioApp::resize_viewport_texture(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == m_viewport_width && height == m_viewport_height && m_viewport_tex)) {
        return;
    }

    m_viewport_width = std::max(64u, width);
    m_viewport_height = std::max(64u, height);

    m_viewport_srv.Reset();
    m_viewport_rtv.Reset();
    m_viewport_tex.Reset();

    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_viewport_width;
    td.Height = m_viewport_height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = rw->get_device()->CreateTexture2D(&td, nullptr, m_viewport_tex.GetAddressOf());
    if (FAILED(hr)) return;

    hr = rw->get_device()->CreateRenderTargetView(m_viewport_tex.Get(), nullptr, m_viewport_rtv.GetAddressOf());
    if (FAILED(hr)) return;

    hr = rw->get_device()->CreateShaderResourceView(m_viewport_tex.Get(), nullptr, m_viewport_srv.GetAddressOf());
    if (FAILED(hr)) return;
}

void StudioApp::render() {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;

    // 1. Ensure Viewport Render Texture exists and is ready
    if (!m_viewport_tex || !m_viewport_rtv || !m_viewport_srv) {
        resize_viewport_texture(m_viewport_width, m_viewport_height);
    }

    // 2. Begin ImGui Frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // 3. Draw Dockspace & UI Panels (processes gizmo manipulation, inputs, inspector)
    draw_dockspace();
    draw_main_menu();
    if (m_show_viewport)    draw_viewport_panel();
    if (m_show_hierarchy)   draw_outliner_panel();
    if (m_show_inspector)   draw_inspector_panel();
    if (m_show_materials)   draw_material_editor_panel();
    if (m_show_environment) draw_environment_panel();
    if (m_show_status_bar)  draw_status_bar();
    if (m_asset_message_pending) { ImGui::OpenPopup("Asset import"); m_asset_message_pending = false; }
    if (ImGui::BeginPopupModal("Asset import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(m_asset_message.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (m_show_material_diag) { ImGui::OpenPopup("Material Import Diagnostics"); m_show_material_diag = false; }
    if (ImGui::BeginPopupModal("Material Import Diagnostics", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%s", m_material_diag_title.c_str());
        ImGui::Separator();
        ImGui::BeginChild("DiagScroll", ImVec2(560, 260), true);
        ImGui::TextUnformatted(m_material_diag_report.c_str());
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!m_simulating) {
        m_history.observe(m_scene, ImGui::IsAnyItemActive() || ImGuizmo::IsUsing() || m_terrain_stroke);
        const std::wstring title = m_history.dirty(m_scene)
            ? L"GM3D Studio - 3D World Editor *" : L"GM3D Studio - 3D World Editor";
        SetWindowTextW(m_hwnd, title.c_str());
    }

    // 4. Sync latest transformed entity data to shared buffer
    sync_scene_to_engine();

    // 5. Render 3D Scene into Viewport Render Target with real delta_time
    if (m_viewport_rtv) {
        rw->set_custom_render_target(m_viewport_rtv.Get());
        EngineContext::instance().update(m_delta_time);
        rw->set_custom_render_target(nullptr);
    }

    // 6. Render ImGui onto Main Window Swap Chain Backbuffer
    ImGui::Render();

    ID3D11RenderTargetView* back_rtv = rw->get_back_buffer_rtv();
    rw->get_context()->OMSetRenderTargets(1, &back_rtv, nullptr);
    const float clear_color[4] = { 0.07f, 0.08f, 0.10f, 1.0f };
    rw->get_context()->ClearRenderTargetView(back_rtv, clear_color);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    rw->get_swap_chain()->Present(1, 0);
}

void StudioApp::draw_dockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float status_bar_h = m_show_status_bar ? 28.0f : 0.0f;
    ImVec2 dockspace_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - status_bar_h);
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(dockspace_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("GM3D_DockSpace_Host", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("GM3D_DockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Initial Professional 3D Layout Setup (Blender / Unreal Style)
    static bool s_first_time = true;
    bool need_build = s_first_time || m_request_reset_layout;
    if (need_build) {
        s_first_time = false;
        bool force_reset = m_request_reset_layout;
        m_request_reset_layout = false;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
        if (force_reset || node == nullptr || (node->ChildNodes[0] == nullptr && node->ChildNodes[1] == nullptr)) {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);

            ImGuiID dock_main = dockspace_id;
            ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.26f, nullptr, &dock_main);
            ImGuiID dock_right_down = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.55f, nullptr, &dock_right);
            ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.22f, nullptr, &dock_main);

            ImGui::DockBuilderDockWindow("Viewport", dock_main);
            ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_right);
            ImGui::DockBuilderDockWindow("Inspector", dock_right_down);
            ImGui::DockBuilderDockWindow("Materials", dock_right_down);
            ImGui::DockBuilderDockWindow("Environment & Lighting", dock_left);

            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    ImGui::End();
}

void StudioApp::draw_main_menu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) new_project_dialog();
            if (ImGui::MenuItem("Open Project...")) open_project_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("New Scene", "Ctrl+N") && confirm_scene_replacement()) new_scene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O") && confirm_scene_replacement()) open_scene_dialog();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) save_scene();
            if (ImGui::MenuItem("Save Scene As...")) save_scene_as_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Import 3D Model (.glb, .obj)...", "Ctrl+I")) import_model_dialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) request_close();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_simulating && m_history.can_undo())) apply_history(false);
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_simulating && m_history.can_redo())) apply_history(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) duplicate_selected_entity();
            if (ImGui::MenuItem("Delete", "Del", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) delete_selected_entity();
            ImGui::Separator();
            if (ImGui::MenuItem("Group Selected", "Ctrl+G", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) group_selected_entities();
            if (ImGui::MenuItem("Unparent Selected", "Ctrl+Shift+G", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) unparent_selected_entities();
            if (ImGui::MenuItem("Snap to Surface", "G", false, !m_selected_entity_ids.empty() || m_selected_entity_id >= 0)) snap_selected_to_surface();
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                m_selected_entity_ids.clear();
                for (const auto& e : m_scene.entities) m_selected_entity_ids.push_back(e.id);
                if (!m_selected_entity_ids.empty()) m_selected_entity_id = static_cast<int32_t>(m_selected_entity_ids.back());
            }
            if (ImGui::MenuItem("Deselect All", "Escape", false, !m_selected_entity_ids.empty())) deselect_all();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Add Cube")) add_entity(0, "Cube");
            if (ImGui::MenuItem("Add Sphere")) add_entity(1, "Sphere");
            if (ImGui::MenuItem("Add Cylinder")) add_entity(2, "Cylinder");
            if (ImGui::MenuItem("Add Plane")) add_entity(3, "Plane");
            if (ImGui::MenuItem("Add Capsule")) add_entity(4, "Capsule");
            if (ImGui::MenuItem("Add Pine Tree")) add_entity(6, "PineTree");
            if (ImGui::MenuItem("Add Point Light")) add_point_light();
            ImGui::Separator();
            if (ImGui::MenuItem("Import 3D Model (.glb, .obj)...")) import_model_dialog();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Gizmo")) {
            if (ImGui::MenuItem("Translate", "W", m_gizmo_operation == GizmoOperation::Translate)) m_gizmo_operation = GizmoOperation::Translate;
            if (ImGui::MenuItem("Rotate", "E", m_gizmo_operation == GizmoOperation::Rotate)) m_gizmo_operation = GizmoOperation::Rotate;
            if (ImGui::MenuItem("Scale", "R", m_gizmo_operation == GizmoOperation::Scale)) m_gizmo_operation = GizmoOperation::Scale;
            ImGui::Separator();
            if (ImGui::MenuItem("Local Space", nullptr, m_gizmo_mode == GizmoMode::Local)) m_gizmo_mode = GizmoMode::Local;
            if (ImGui::MenuItem("World Space", nullptr, m_gizmo_mode == GizmoMode::World)) m_gizmo_mode = GizmoMode::World;
            ImGui::Separator();
            ImGui::MenuItem("Enable Snapping", nullptr, &m_gizmo_snap);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("3D Viewport", nullptr, &m_show_viewport);
            ImGui::MenuItem("Scene Hierarchy", nullptr, &m_show_hierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_show_inspector);
            ImGui::MenuItem("Materials", nullptr, &m_show_materials);
            ImGui::MenuItem("Environment & Lighting", nullptr, &m_show_environment);
            ImGui::MenuItem("Status Bar", nullptr, &m_show_status_bar);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout to Default")) {
                m_show_viewport = true;
                m_show_hierarchy = true;
                m_show_inspector = true;
                m_show_materials = true;
                m_show_environment = true;
                m_show_status_bar = true;
                m_request_reset_layout = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void StudioApp::draw_viewport_panel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport", &m_show_viewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    m_viewport_focused = ImGui::IsWindowFocused();
    m_viewport_hovered = ImGui::IsWindowHovered();

    // Compact toolbar keeps the viewport usable at narrow widths.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.085f, 0.105f, 1.0f));
    ImGui::BeginChild("ViewportToolbar", ImVec2(0, 43), false, ImGuiWindowFlags_NoScrollbar);
    if (m_simulating) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.20f, 0.19f, 1));
    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.29f, 1));
    if (ImGui::Button(m_simulating ? "Stop" : "Play")) {
        if (m_simulating) stop_simulation(); else start_simulation();
    }
    ImGui::PopStyleColor();
    HelpTooltip("Start or stop the physics preview. Press P. Stopping restores the scene.");
    ImGui::SameLine();
    if (ImGui::Button("+ Add")) ImGui::OpenPopup("ViewportAdd");
    HelpTooltip("Add a primitive, point light, or imported model to the scene.");
    if (ImGui::BeginPopup("ViewportAdd")) {
        if (ImGui::MenuItem("Cube")) add_entity(0, "Cube");
        if (ImGui::MenuItem("Sphere")) add_entity(1, "Sphere");
        if (ImGui::MenuItem("Cylinder")) add_entity(2, "Cylinder");
        if (ImGui::MenuItem("Plane")) add_entity(3, "Plane");
        if (ImGui::MenuItem("Capsule")) add_entity(4, "Capsule");
        if (ImGui::MenuItem("Pine tree")) add_entity(6, "PineTree");
        if (ImGui::MenuItem("Point light")) add_point_light();
        ImGui::Separator();
        if (ImGui::MenuItem("Import model...")) import_model_dialog();
        ImGui::EndPopup();
    }
    auto tool_button = [&](const char* text, bool selected) {
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.68f, 1.0f));
        bool pressed = ImGui::Button(text);
        if (selected) ImGui::PopStyleColor();
        return pressed;
    };
    ImGui::SameLine();
    if (tool_button("Move", !m_terrain_brush_enabled && m_gizmo_operation == GizmoOperation::Translate)) { m_terrain_brush_enabled = false; m_gizmo_operation = GizmoOperation::Translate; }
    HelpTooltip("Move the selected object with the gizmo. Shortcut: W.");
    ImGui::SameLine();
    if (tool_button("Rotate", !m_terrain_brush_enabled && m_gizmo_operation == GizmoOperation::Rotate)) { m_terrain_brush_enabled = false; m_gizmo_operation = GizmoOperation::Rotate; }
    HelpTooltip("Rotate the selected object with the gizmo. Shortcut: E.");
    ImGui::SameLine();
    if (tool_button("Scale", !m_terrain_brush_enabled && m_gizmo_operation == GizmoOperation::Scale)) { m_terrain_brush_enabled = false; m_gizmo_operation = GizmoOperation::Scale; }
    HelpTooltip("Scale the selected object with the gizmo. Shortcut: R.");
    ImGui::SameLine();
    if (tool_button("Sculpt", m_terrain_brush_enabled)) {
        m_terrain_brush_enabled = !m_terrain_brush_enabled;
        m_show_environment = true;
        m_select_terrain_tab = true;
    }
    HelpTooltip("Toggle terrain sculpting and open the brush settings.");
    ImGui::SameLine();
    if (ImGui::Button(m_gizmo_mode == GizmoMode::Local ? "Local" : "World"))
        m_gizmo_mode = (m_gizmo_mode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
    HelpTooltip("Switch gizmo axes between local object space and world space.");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_gizmo_snap);
    HelpTooltip("Snap gizmo transformations to fixed increments.");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // 2. 3D Render Image Viewport
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(1.0f, avail.x);
    avail.y = std::max(1.0f, avail.y);
    uint32_t w = static_cast<uint32_t>(avail.x);
    uint32_t h = static_cast<uint32_t>(avail.y);

    if (w > 64 && h > 64 && (w != m_viewport_width || h != m_viewport_height)) {
        resize_viewport_texture(w, h);
    }

    ImVec2 viewportMin = ImGui::GetCursorScreenPos();

    if (m_viewport_srv) {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_viewport_srv.Get()), avail);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ASSET")) {
                if (payload->DataSize == sizeof(size_t)) {
                    const size_t index = *static_cast<const size_t*>(payload->Data);
                    if (index < m_assets.size()) {
                        const auto asset = m_assets[index];
                        if (!(asset.is_model ? add_model_asset(asset.path, false) : apply_texture_asset(asset.path))) {
                            m_asset_message = "Could not use asset. Select an object for textures and stop simulation.";
                            m_asset_message_pending = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    update_terrain_brush(viewportMin.x, viewportMin.y, avail.x, avail.y);
    const bool light_clicked = draw_light_markers(viewportMin.x, viewportMin.y, avail.x, avail.y);

    // 3. 3D Mouse Picking (Click to select entity directly in viewport)
    if (!light_clicked && !m_terrain_brush_enabled && m_viewport_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            ImVec2 mpos = ImGui::GetMousePos();
            pick_entity_at_mouse(mpos.x - viewportMin.x, mpos.y - viewportMin.y, avail.x, avail.y);
        }
    }

    // 4. ImGuizmo 3D Transform Gizmo & Selection Bounds Overlay
    if (!m_terrain_brush_enabled && m_selected_entity_id >= 0 && m_viewport_srv) {
        Entity* sel = nullptr;
        for (auto& e : m_scene.entities) {
            if (static_cast<int32_t>(e.id) == m_selected_entity_id) {
                sel = &e;
                break;
            }
        }

        if (sel) {
            float view[16], proj[16];
            ComputeLookAtLH(view, m_cam_pos[0], m_cam_pos[1], m_cam_pos[2],
                                  m_cam_target[0], m_cam_target[1], m_cam_target[2],
                                  0.0f, 1.0f, 0.0f);

            float aspect = (avail.y > 0.0f) ? (avail.x / avail.y) : 1.777f;
            ComputePerspectiveLH(proj, 60.0f, aspect, 0.1f, 800.0f);

            // Draw glowing 3D selection bounding boxes for all selected objects
            for (uint32_t sel_id : m_selected_entity_ids) {
                Entity* ent = m_scene.find_entity(sel_id);
                if (ent && !ent->is_point_light && !ent->is_group) {
                    draw_selection_bounds(*ent, view, proj, viewportMin.x, viewportMin.y, avail.x, avail.y);
                }
            }
            if (m_selected_entity_ids.empty() && !sel->is_point_light && !sel->is_group) {
                draw_selection_bounds(*sel, view, proj, viewportMin.x, viewportMin.y, avail.x, avail.y);
            }

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(viewportMin.x, viewportMin.y, avail.x, avail.y);

            float modelMatrix[16];
            ComputeWorldMatrix(m_scene, *sel, modelMatrix);

            float prev_pos[3], prev_rot[3], prev_scale[3];
            ImGuizmo::DecomposeMatrixToComponents(modelMatrix, prev_pos, prev_rot, prev_scale);

            ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
            if (!sel->is_point_light && m_gizmo_operation == GizmoOperation::Rotate) op = ImGuizmo::ROTATE;
            if (!sel->is_point_light && m_gizmo_operation == GizmoOperation::Scale)  op = ImGuizmo::SCALE;

            ImGuizmo::MODE mode = (m_gizmo_mode == GizmoMode::Local) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            float* snap = nullptr;
            if (m_gizmo_snap) {
                if (op == ImGuizmo::TRANSLATE) snap = m_snap_translation;
                else if (op == ImGuizmo::ROTATE) snap = &m_snap_rotation;
                else if (op == ImGuizmo::SCALE) snap = &m_snap_scale;
            }

            if (ImGuizmo::Manipulate(view, proj, op, mode, modelMatrix, nullptr, snap)) {
                float new_pos[3], new_rot[3], new_scale[3];
                ImGuizmo::DecomposeMatrixToComponents(modelMatrix, new_pos, new_rot, new_scale);

                float delta_pos[3] = { new_pos[0] - prev_pos[0], new_pos[1] - prev_pos[1], new_pos[2] - prev_pos[2] };
                float delta_rot[3] = { new_rot[0] - prev_rot[0], new_rot[1] - prev_rot[1], new_rot[2] - prev_rot[2] };
                float delta_scale[3] = {
                    (prev_scale[0] > 1e-4f) ? (new_scale[0] / prev_scale[0]) : 1.0f,
                    (prev_scale[1] > 1e-4f) ? (new_scale[1] / prev_scale[1]) : 1.0f,
                    (prev_scale[2] > 1e-4f) ? (new_scale[2] / prev_scale[2]) : 1.0f
                };

                ComputeLocalTransform(m_scene, sel->parent_id, modelMatrix, sel->position, sel->rotation, sel->scale);

                // Apply delta to all other selected entities in world space
                for (uint32_t other_id : m_selected_entity_ids) {
                    if (other_id == sel->id) continue;
                    Entity* other = m_scene.find_entity(other_id);
                    if (!other) continue;
                    if (m_scene.is_descendant_of(other->id, sel->id)) continue;

                    float ow_pos[3], ow_rot[3], ow_scale[3], ow_mat[16];
                    ComputeWorldTransform(m_scene, *other, ow_pos, ow_rot, ow_scale, ow_mat);

                    if (m_gizmo_operation == GizmoOperation::Translate) {
                        ow_mat[12] += delta_pos[0];
                        ow_mat[13] += delta_pos[1];
                        ow_mat[14] += delta_pos[2];
                    } else if (m_gizmo_operation == GizmoOperation::Rotate) {
                        ow_rot[0] += delta_rot[0];
                        ow_rot[1] += delta_rot[1];
                        ow_rot[2] += delta_rot[2];
                        BuildMatrix(ow_pos, ow_rot, ow_scale, ow_mat);
                    } else if (m_gizmo_operation == GizmoOperation::Scale) {
                        ow_scale[0] *= delta_scale[0];
                        ow_scale[1] *= delta_scale[1];
                        ow_scale[2] *= delta_scale[2];
                        BuildMatrix(ow_pos, ow_rot, ow_scale, ow_mat);
                    }
                    ComputeLocalTransform(m_scene, other->parent_id, ow_mat, other->position, other->rotation, other->scale);
                }
            }
        }
    }

    ImGui::End();
}

void StudioApp::draw_outliner_entity_node(Entity& e, const std::string& filter, int& pending_action) {
    std::vector<uint32_t> children = m_scene.get_children(e.id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (is_entity_selected(e.id)) flags |= ImGuiTreeNodeFlags_Selected;

    std::string icon = e.is_group ? "[Group] " : (e.is_point_light ? "[Light] " : "[Mesh] ");
    std::string label = icon + e.name + " [" + std::to_string(e.id) + "]";

    bool node_open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(e.id)), flags, "%s", label.c_str());

    if (ImGui::IsItemClicked()) {
        bool ctrl = ImGui::GetIO().KeyCtrl;
        bool shift = ImGui::GetIO().KeyShift;
        select_entity(e.id, ctrl, shift);
    }

    // Drag source: dragging this entity to reparent
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("GM3D_ENTITY_HIERARCHY", &e.id, sizeof(e.id));
        ImGui::Text("%s", label.c_str());
        ImGui::EndDragDropSource();
    }

    // Drag target: drop another entity onto this entity to make it a child
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ENTITY_HIERARCHY")) {
            if (payload->DataSize == sizeof(uint32_t)) {
                uint32_t dropped_id = *static_cast<const uint32_t*>(payload->Data);
                set_entity_parent(dropped_id, e.id);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ASSET")) {
            if (payload->DataSize == sizeof(size_t)) {
                size_t idx = *static_cast<const size_t*>(payload->Data);
                if (idx < m_assets.size() && !m_assets[idx].is_model) {
                    apply_texture_asset(m_assets[idx].path);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!is_entity_selected(e.id)) select_entity(e.id, false, false);
        if (ImGui::MenuItem("Create Child Group")) {
            Entity group;
            group.id = m_scene.next_entity_id++;
            group.name = "Group_" + std::to_string(group.id);
            group.is_group = true;
            group.mesh_id = 0;
            group.parent_id = e.id;
            m_scene.entities.push_back(group);
            select_entity(group.id, false, false);
        }
        if (ImGui::MenuItem("Group Selected", "Ctrl+G")) group_selected_entities();
        if (e.parent_id != 0 && ImGui::MenuItem("Unparent", "Ctrl+Shift+G")) set_entity_parent(e.id, 0);
        if (ImGui::MenuItem("Snap to Surface", "G")) snap_selected_to_surface();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pending_action = 1;
        if (ImGui::MenuItem("Delete", "Del")) pending_action = 2;
        ImGui::EndPopup();
    }

    if (node_open) {
        for (uint32_t child_id : children) {
            Entity* child = m_scene.find_entity(child_id);
            if (child) draw_outliner_entity_node(*child, filter, pending_action);
        }
        ImGui::TreePop();
    }
}

void StudioApp::draw_outliner_panel() {
    ImGui::Begin("Scene Hierarchy", &m_show_hierarchy);
    ImGui::TextUnformatted("SCENE HIERARCHY");
    ImGui::TextDisabled("%zu objects (%zu selected)", m_scene.entities.size(), m_selected_entity_ids.size());
    ImGui::Spacing();

    // Top action bar
    if (ImGui::Button("+ Add Entity", ImVec2(100, 26))) {
        ImGui::OpenPopup("AddEntityPopup");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_selected_entity_ids.empty() && m_selected_entity_id < 0);
    if (ImGui::Button("Group (Ctrl+G)", ImVec2(105, 26))) {
        group_selected_entities();
    }
    HelpTooltip("Group all selected objects under a new parent transform.");
    ImGui::SameLine();
    if (ImGui::Button("Unparent", ImVec2(75, 26))) {
        unparent_selected_entities();
    }
    HelpTooltip("Detach selected objects from their parents back to root level.");
    ImGui::EndDisabled();

    if (ImGui::BeginPopup("AddEntityPopup")) {
        if (ImGui::MenuItem("Cube")) add_entity(0, "Cube");
        if (ImGui::MenuItem("Sphere")) add_entity(1, "Sphere");
        if (ImGui::MenuItem("Plane (Ground)")) add_entity(3, "Plane");
        if (ImGui::MenuItem("Cylinder")) add_entity(2, "Cylinder");
        if (ImGui::MenuItem("Capsule")) add_entity(4, "Capsule");
        if (ImGui::MenuItem("Pine Tree")) add_entity(6, "PineTree");
        if (ImGui::MenuItem("Point Light")) add_point_light();
        ImGui::Separator();
        if (ImGui::MenuItem("Empty Group Node")) {
            Entity group;
            group.id = m_scene.next_entity_id++;
            group.name = "Group_" + std::to_string(group.id);
            group.is_group = true;
            group.mesh_id = 0;
            m_scene.entities.push_back(group);
            select_entity(group.id, false, false);
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    static char search_buf[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search objects...", search_buf, sizeof(search_buf));
    HelpTooltip("Filter objects by name. Drag objects onto each other to parent them.");
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string filter = lower(search_buf);
    int pending_action = 0;

    ImGui::BeginChild("EntityList", ImVec2(0, 0), true);

    if (filter.empty()) {
        for (size_t i = 0; i < m_scene.entities.size(); ++i) {
            auto& e = m_scene.entities[i];
            if (e.parent_id == 0) {
                draw_outliner_entity_node(e, filter, pending_action);
            }
        }
    } else {
        for (size_t i = 0; i < m_scene.entities.size(); ++i) {
            auto& e = m_scene.entities[i];
            if (lower(e.name).find(filter) == std::string::npos) continue;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (is_entity_selected(e.id)) flags |= ImGuiTreeNodeFlags_Selected;

            std::string icon = e.is_group ? "[Group] " : (e.is_point_light ? "[Light] " : "[Mesh] ");
            std::string label = icon + e.name + " [" + std::to_string(e.id) + "]";
            bool node_open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(e.id)), flags, "%s", label.c_str());

            if (ImGui::IsItemClicked()) {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;
                select_entity(e.id, ctrl, shift);
            }
            if (ImGui::BeginPopupContextItem()) {
                if (!is_entity_selected(e.id)) select_entity(e.id, false, false);
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pending_action = 1;
                if (ImGui::MenuItem("Delete", "Del")) pending_action = 2;
                ImGui::EndPopup();
            }
            if (node_open) ImGui::TreePop();
        }
    }

    // Drag-and-drop to empty area at bottom: unparent to root level!
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID("EntityList_UnparentZone"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GM3D_ENTITY_HIERARCHY")) {
            if (payload->DataSize == sizeof(uint32_t)) {
                uint32_t dropped_id = *static_cast<const uint32_t*>(payload->Data);
                set_entity_parent(dropped_id, 0);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        if (!ImGui::GetIO().KeyCtrl) deselect_all();
    }

    ImGui::EndChild();
    if (pending_action == 1) duplicate_selected_entity();
    if (pending_action == 2) delete_selected_entity();

    ImGui::End();
}

void StudioApp::draw_inspector_panel() {
    ImGui::Begin("Inspector", &m_show_inspector);

    if (m_selected_entity_ids.size() > 1) {
        ImGui::TextUnformatted("MULTI-SELECTION INSPECTOR");
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%zu objects selected", m_selected_entity_ids.size());
        ImGui::Spacing();

        if (ImGui::Button("Group Selected (Ctrl+G)", ImVec2(-1, 28))) group_selected_entities();
        if (ImGui::Button("Unparent All (Ctrl+Shift+G)", ImVec2(-1, 26))) unparent_selected_entities();
        if (ImGui::Button("Snap All to Surface (G)", ImVec2(-1, 26))) snap_selected_to_surface();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete All Selected (Del)", ImVec2(-1, 26))) delete_selected_entity();
        ImGui::PopStyleColor();

        ImGui::SeparatorText("Alignment & Distribution");
        ImGui::TextUnformatted("Align X:");
        ImGui::SameLine();
        if (ImGui::Button("Min##AX", ImVec2(50, 0))) align_selected_entities(0, 0);
        ImGui::SameLine();
        if (ImGui::Button("Center##AX", ImVec2(60, 0))) align_selected_entities(0, 1);
        ImGui::SameLine();
        if (ImGui::Button("Max##AX", ImVec2(50, 0))) align_selected_entities(0, 2);

        ImGui::TextUnformatted("Align Y:");
        ImGui::SameLine();
        if (ImGui::Button("Bottom##AY", ImVec2(55, 0))) align_selected_entities(1, 0);
        ImGui::SameLine();
        if (ImGui::Button("Center##AY", ImVec2(60, 0))) align_selected_entities(1, 1);
        ImGui::SameLine();
        if (ImGui::Button("Top##AY", ImVec2(50, 0))) align_selected_entities(1, 2);

        ImGui::TextUnformatted("Align Z:");
        ImGui::SameLine();
        if (ImGui::Button("Min##AZ", ImVec2(50, 0))) align_selected_entities(2, 0);
        ImGui::SameLine();
        if (ImGui::Button("Center##AZ", ImVec2(60, 0))) align_selected_entities(2, 1);
        ImGui::SameLine();
        if (ImGui::Button("Max##AZ", ImVec2(50, 0))) align_selected_entities(2, 2);

        ImGui::Spacing();
        ImGui::TextUnformatted("Distribute Evenly:");
        if (ImGui::Button("Along X", ImVec2(75, 0))) align_selected_entities(0, 3);
        ImGui::SameLine();
        if (ImGui::Button("Along Y", ImVec2(75, 0))) align_selected_entities(1, 3);
        ImGui::SameLine();
        if (ImGui::Button("Along Z", ImVec2(75, 0))) align_selected_entities(2, 3);

        ImGui::End();
        return;
    }

    if (m_selected_entity_id < 0) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No entity selected.");
        ImGui::End();
        return;
    }

    Entity* sel = nullptr;
    for (auto& e : m_scene.entities) {
        if (static_cast<int32_t>(e.id) == m_selected_entity_id) {
            sel = &e;
            break;
        }
    }

    if (!sel) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Selected entity not found.");
        ImGui::End();
        return;
    }

    if (sel->editor_grid && m_scene.has_terrain)
        ImGui::TextWrapped("Editor grid is hidden while terrain is enabled to avoid overlapping surfaces.");

    ImGui::TextUnformatted("OBJECT PROPERTIES");
    ImGui::TextDisabled("ID %u%s", sel->id, sel->is_group ? " (Group Container)" : "");
    ImGui::Spacing();

    // Hierarchy parent information
    if (sel->parent_id != 0) {
        Entity* parent = m_scene.find_entity(sel->parent_id);
        std::string pname = parent ? (parent->name + " [ID " + std::to_string(parent->id) + "]") : ("[ID " + std::to_string(sel->parent_id) + "]");
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.95f, 1.0f), "Parent: %s", pname.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Unparent")) set_entity_parent(sel->id, 0);
    } else {
        ImGui::TextDisabled("Parent: None (Root Entity)");
    }

    // Name & Tag
    char name_buf[64];
    strncpy_s(name_buf, sel->name.c_str(), sizeof(name_buf));
    ImGui::TextUnformatted("Name"); ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Name", name_buf, sizeof(name_buf))) {
        sel->name = name_buf;
    }

    char tag_buf[64];
    strncpy_s(tag_buf, sel->tag.c_str(), sizeof(tag_buf));
    ImGui::TextUnformatted("Tag"); ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##Tag", tag_buf, sizeof(tag_buf))) {
        sel->tag = tag_buf;
    }

    ImGui::Separator();

    if (sel->is_point_light) {
        ImGui::SeparatorText("Point light");
        ImGui::Checkbox("Enabled", &sel->visible);
        DrawVec3Control("Position", sel->position, 0.0f, 0.1f);
        ImGui::TextUnformatted("Color"); ImGui::SetNextItemWidth(-1);
        ImGui::ColorEdit3("##LightColor", sel->light_color);
        ImGui::TextUnformatted("Intensity"); ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##LightIntensity", &sel->light_intensity, 0.5f, 0.0f, 500.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        HelpTooltip("Brightness of this point light. Point lights currently do not cast shadows.");
        ImGui::TextUnformatted("Radius"); ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##LightRadius", &sel->light_radius, 0.1f, 0.1f, 100.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
        HelpTooltip("Maximum distance affected by this point light.");
        ImGui::TextWrapped("Point lights illuminate objects but do not cast shadows. The sun casts dynamic shadows.");
        ImGui::End();
        return;
    }

    // Transform with Blender/Unreal style colored axes
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawVec3Control("Position", sel->position, 0.0f, 0.1f);
        DrawVec3Control("Rotation", sel->rotation, 0.0f, 0.5f);
        DrawVec3Control("Scale", sel->scale, 1.0f, 0.05f);

        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *sel, wpos, wrot, wscale);
        ImGui::TextDisabled("World: (%.2f, %.2f, %.2f)", wpos[0], wpos[1], wpos[2]);

        if (ImGui::Button("Snap to Surface (G)", ImVec2(-1, 24))) {
            snap_selected_to_surface();
        }
        HelpTooltip("Snap this object onto the terrain or ground plane.");
    }

    // Rendering
    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Model");
        if (!sel->model_path.empty()) {
            const auto name = path_utf8(std::filesystem::u8path(sel->model_path).filename());
            ImGui::TextWrapped("%s%s", sel->mesh_id ? "" : "Missing: ", name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sel->model_path.c_str());
        } else if (sel->mesh_id > 6) {
            ImGui::TextWrapped("Legacy model ID %u (source path unavailable)", sel->mesh_id);
        }
        const char* meshes[] = { "Cube", "Sphere", "Cylinder", "Plane", "Capsule", "Pine Tree" };
        const uint16_t mesh_ids[] = { 0, 1, 2, 3, 4, 6 };
        int current_mesh = -1;
        for (int i = 0; i < 6; ++i) if (sel->model_path.empty() && sel->mesh_id == mesh_ids[i]) current_mesh = i;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Mesh", current_mesh < 0 ? "Custom model" : meshes[current_mesh])) {
            for (int i = 0; i < 6; ++i) if (ImGui::Selectable(meshes[i], current_mesh == i)) {
                sel->mesh_id = mesh_ids[i];
                sel->model_path.clear();
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(m_simulating);
        if (ImGui::Button("Replace model...", ImVec2(-1, 0))) import_model_dialog(true);
        HelpTooltip("Replace this object's mesh while retaining its transform and other settings.");
        ImGui::EndDisabled();

        ImGui::TextUnformatted("Material");
        if (m_scene.materials.empty()) m_scene.init_default_materials();

        Material* cur_mat = m_scene.find_material(sel->material_id);
        std::string cur_mat_label = cur_mat ? cur_mat->name : ("Material ID: " + std::to_string(sel->material_id));

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##PBRMaterial", cur_mat_label.c_str())) {
            for (const auto& m : m_scene.materials) {
                ImGui::PushID(static_cast<int>(m.id));
                ImVec4 swatch(m.base_color[0], m.base_color[1], m.base_color[2], m.base_color[3]);
                ImGui::ColorButton("##sw", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                ImGui::SameLine();
                bool is_sel = (sel->material_id == m.id);
                if (ImGui::Selectable(m.name.c_str(), is_sel)) {
                    sel->material_id = static_cast<uint16_t>(m.id);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Edit Material...", ImVec2(130, 24))) {
            m_selected_material_id = sel->material_id;
            m_show_materials = true;
            ImGui::SetWindowFocus("Materials");
        }
        HelpTooltip("Open the Materials editor to tweak roughness, metallic, colors, and textures.");
        ImGui::SameLine();

        if (ImGui::Button("+ New##FromSel", ImVec2(70, 24))) {
            Material new_m{};
            if (cur_mat) new_m = *cur_mat;
            new_m.id = m_scene.next_material_id++;
            new_m.name = "Material " + std::to_string(new_m.id);
            new_m.is_builtin_preset = false;
            m_scene.materials.push_back(new_m);
            sel->material_id = static_cast<uint16_t>(new_m.id);
            m_selected_material_id = new_m.id;
            m_show_materials = true;
            ImGui::SetWindowFocus("Materials");
        }
        HelpTooltip("Create a new custom material based on this one.");

        if (m_selected_entity_ids.size() > 1) {
            char apply_txt[64];
            snprintf(apply_txt, sizeof(apply_txt), "Apply to All Selected (%zu)", m_selected_entity_ids.size());
            if (ImGui::Button(apply_txt, ImVec2(-1, 24))) {
                for (uint32_t eid : m_selected_entity_ids) {
                    for (auto& e : m_scene.entities) {
                        if (e.id == eid) e.material_id = sel->material_id;
                    }
                }
            }
        }

        ImGui::TextUnformatted("Texture");
        const char* builtin_textures[] = { "White", "Checker", "Tech grid", "Wood", "Metal" };
        const bool builtin = sel->texture_path.empty() && sel->texture_id < 5;
        if (!sel->texture_path.empty()) {
            const auto name = path_utf8(std::filesystem::u8path(sel->texture_path).filename());
            ImGui::TextWrapped("%s%s", sel->texture_id ? "" : "Missing: ", name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sel->texture_path.c_str());
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Texture", builtin ? builtin_textures[sel->texture_id] : "Custom texture")) {
            for (uint16_t i = 0; i < 5; ++i) if (ImGui::Selectable(builtin_textures[i], builtin && sel->texture_id == i)) {
                sel->texture_id = i;
                sel->texture_path.clear();
            }
            ImGui::EndCombo();
        }
        if (auto* rw = EngineContext::instance().get_render_world()) {
            auto* preview = rw->get_texture_manager().get_srv(sel->texture_id);
            if (preview) ImGui::Image(reinterpret_cast<ImTextureID>(preview), ImVec2(72, 72));
        }
        ImGui::BeginDisabled(m_simulating || sel->editor_grid);
        if (ImGui::Button("Choose texture...", ImVec2(-1, 0))) import_texture_dialog();
        HelpTooltip("Assign an image to the selected object. You can also drop an image onto the window.");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Images may also be dropped onto the window.");

        ImGui::Checkbox("Cast Shadow", &sel->cast_shadow);
        HelpTooltip("Include this object in the sun's shadow map.");
        ImGui::SameLine();
        ImGui::Checkbox("Visible", &sel->visible);
    }

    // Physics
    if (ImGui::CollapsingHeader("Physics (Jolt)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* p_types[] = { "None", "Static", "Dynamic", "Kinematic" };
        ImGui::TextUnformatted("Body type"); ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##BodyType", &sel->physics_type, p_types, IM_ARRAYSIZE(p_types));

        const char* shapes[] = { "Box", "Sphere", "Capsule", "Cylinder", "Convex Mesh" };
        ImGui::TextUnformatted("Collider"); ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##Collider", &sel->collider_shape, shapes, IM_ARRAYSIZE(shapes));

        if (sel->physics_type != 0) {
            ImGui::DragFloat("Mass", &sel->mass, 0.1f, 0.01f, 1000.0f);
            ImGui::SliderFloat("Friction", &sel->friction, 0.0f, 1.0f);
            ImGui::SliderFloat("Restitution", &sel->restitution, 0.0f, 1.0f);
        }
    }

    ImGui::End();
}

static void draw_material_preview_sphere(ImDrawList* draw_list, const ImVec2& center, float radius, const Material& mat) {
    if (!draw_list || radius < 2.0f) return;
    float r = mat.base_color[0];
    float g = mat.base_color[1];
    float b = mat.base_color[2];
    ImU32 baseCol = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
    ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(ImVec4(r * 0.35f, g * 0.35f, b * 0.35f, 1.0f));
    ImU32 specCol = mat.metallic > 0.5f 
        ? ImGui::ColorConvertFloat4ToU32(ImVec4(r * 0.8f + 0.2f, g * 0.8f + 0.2f, b * 0.8f + 0.2f, 1.0f))
        : IM_COL32(255, 255, 255, 220);

    draw_list->AddCircleFilled(center, radius, shadowCol, 32);
    ImVec2 highlightPos(center.x - radius * 0.28f, center.y - radius * 0.28f);
    draw_list->AddCircleFilled(highlightPos, radius * 0.72f, baseCol, 32);

    float specRadius = radius * (0.15f + (1.0f - std::clamp(mat.roughness, 0.0f, 1.0f)) * 0.22f);
    draw_list->AddCircleFilled(highlightPos, specRadius, specCol, 16);
}

void StudioApp::update_material_preview_texture(const Material& mat) {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    ID3D11Device* device = rw->get_device();
    ID3D11DeviceContext* context = rw->get_context();
    if (!device || !context) return;

    uint32_t hash = 17;
    auto hash_combine = [&hash](float v) {
        union { float f; uint32_t u; } u;
        u.f = v;
        hash = hash * 31 + u.u;
    };
    hash_combine(mat.base_color[0]);
    hash_combine(mat.base_color[1]);
    hash_combine(mat.base_color[2]);
    hash_combine(mat.base_color[3]);
    hash_combine(mat.roughness);
    hash_combine(mat.metallic);
    hash_combine(mat.emissive);
    hash = hash * 31 + mat.alpha_mode;
    hash = hash * 31 + mat.preset_index;
    hash = hash * 31 + (mat.is_builtin_preset ? 1 : 0);

    if (m_mat_preview_tex && m_mat_preview_srv && m_mat_preview_last_id == mat.id && m_mat_preview_hash == hash) {
        return;
    }
    m_mat_preview_last_id = mat.id;
    m_mat_preview_hash = hash;

    constexpr UINT SIZE = 128;
    if (!m_mat_preview_tex) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = SIZE;
        desc.Height = SIZE;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, m_mat_preview_tex.GetAddressOf()))) return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(m_mat_preview_tex.Get(), &srvDesc, m_mat_preview_srv.GetAddressOf()))) return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_mat_preview_tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

    float baseR = mat.base_color[0];
    float baseG = mat.base_color[1];
    float baseB = mat.base_color[2];
    float baseA = mat.base_color[3];
    float roughness = std::clamp(mat.roughness, 0.04f, 1.0f);
    float metallic = std::clamp(mat.metallic, 0.0f, 1.0f);
    float emissive = mat.emissive;
    float emR = mat.emissive_color[0];
    float emG = mat.emissive_color[1];
    float emB = mat.emissive_color[2];

    if (mat.is_builtin_preset && mat.preset_index > 0) {
        static const float preset_colors[9][3] = {
            { 0.9f, 0.9f, 0.9f },
            { 0.95f, 0.35f, 0.30f },
            { 0.95f, 0.78f, 0.35f },
            { 0.20f, 0.85f, 0.65f },
            { 0.20f, 0.55f, 0.95f },
            { 0.75f, 0.30f, 0.95f },
            { 0.35f, 0.55f, 0.25f },
            { 0.80f, 0.85f, 0.90f },
            { 0.50f, 0.50f, 0.52f },
        };
        uint32_t pIdx = std::clamp(mat.preset_index, 0u, 8u);
        baseR = preset_colors[pIdx][0];
        baseG = preset_colors[pIdx][1];
        baseB = preset_colors[pIdx][2];
    }

    float lx = 0.577f, ly = 0.707f, lz = 0.408f;
    float invL = 1.0f / sqrtf(lx*lx + ly*ly + lz*lz);
    lx *= invL; ly *= invL; lz *= invL;

    auto* pixels = static_cast<uint8_t*>(mapped.pData);
    const float radius = 56.0f;
    const float cx = 64.0f, cy = 64.0f;

    for (UINT y = 0; y < SIZE; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(pixels + y * mapped.RowPitch);
        for (UINT x = 0; x < SIZE; ++x) {
            float dx = (static_cast<float>(x) - cx) / radius;
            float dy = (static_cast<float>(y) - cy) / radius;
            float dist2 = dx * dx + dy * dy;

            if (dist2 > 1.0f) {
                bool checker = ((x / 8) ^ (y / 8)) & 1;
                uint8_t bg = checker ? 38 : 30;
                row[x] = (255 << 24) | (bg << 16) | (bg << 8) | bg;
                continue;
            }

            float dz = sqrtf(std::max(0.0f, 1.0f - dist2));
            float nx = dx, ny = -dy, nz = dz;

            float NdotL = std::max(0.0f, nx * lx + ny * ly + nz * lz);
            float NdotV = std::max(0.0f, nz);

            float hx = lx, hy = ly, hz = lz + 1.0f;
            float invH = 1.0f / sqrtf(hx*hx + hy*hy + hz*hz);
            hx *= invH; hy *= invH; hz *= invH;
            float NdotH = std::max(0.0f, nx * hx + ny * hy + nz * hz);
            float VdotH = std::max(0.0f, hz);

            float f0_r = (1.0f - metallic) * 0.04f + metallic * baseR;
            float f0_g = (1.0f - metallic) * 0.04f + metallic * baseG;
            float f0_b = (1.0f - metallic) * 0.04f + metallic * baseB;

            float fFactor = powf(1.0f - VdotH, 5.0f);
            float Fr = f0_r + (1.0f - f0_r) * fFactor;
            float Fg = f0_g + (1.0f - f0_g) * fFactor;
            float Fb = f0_b + (1.0f - f0_b) * fFactor;

            float alpha = roughness * roughness;
            float alpha2 = alpha * alpha;
            float denom = NdotH * NdotH * (alpha2 - 1.0f) + 1.0f;
            float D = alpha2 / (3.14159265f * denom * denom + 0.0001f);

            float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
            float gV = NdotV / (NdotV * (1.0f - k) + k + 0.0001f);
            float gL = NdotL / (NdotL * (1.0f - k) + k + 0.0001f);
            float G = gV * gL;

            float specFactor = (D * G) / (4.0f * NdotV * NdotL + 0.001f);
            float specR = Fr * specFactor;
            float specG = Fg * specFactor;
            float specB = Fb * specFactor;

            float diffR = (1.0f - metallic) * baseR;
            float diffG = (1.0f - metallic) * baseG;
            float diffB = (1.0f - metallic) * baseB;

            float rim = powf(1.0f - NdotV, 3.0f) * 0.15f;
            float ambR = baseR * 0.18f;
            float ambG = baseG * 0.18f;
            float ambB = baseB * 0.18f;

            float emisR = emR * emissive;
            float emisG = emG * emissive;
            float emisB = emB * emissive;

            float colR = ambR + (diffR + specR) * NdotL * 1.8f + rim + emisR;
            float colG = ambG + (diffG + specG) * NdotL * 1.8f + rim + emisG;
            float colB = ambB + (diffB + specB) * NdotL * 1.8f + rim + emisB;

            auto tonemap = [](float c) {
                c = std::max(0.0f, c);
                return (c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f);
            };
            uint8_t rByte = static_cast<uint8_t>(std::clamp(tonemap(colR) * 255.0f, 0.0f, 255.0f));
            uint8_t gByte = static_cast<uint8_t>(std::clamp(tonemap(colG) * 255.0f, 0.0f, 255.0f));
            uint8_t bByte = static_cast<uint8_t>(std::clamp(tonemap(colB) * 255.0f, 0.0f, 255.0f));
            uint8_t aByte = static_cast<uint8_t>(std::clamp(baseA * 255.0f, 0.0f, 255.0f));

            row[x] = (aByte << 24) | (bByte << 16) | (gByte << 8) | rByte;
        }
    }
    context->Unmap(m_mat_preview_tex.Get(), 0);
}

void StudioApp::draw_material_editor_panel() {
    if (!m_show_materials) return;
    ImGui::SetNextWindowSize(ImVec2(580, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Materials", &m_show_materials)) {
        ImGui::End();
        return;
    }

    if (m_scene.materials.empty()) {
        m_scene.init_default_materials();
    }

    // Top Toolbar
    if (ImGui::Button("+ New Material", ImVec2(120, 26))) {
        Material new_mat;
        new_mat.id = m_scene.next_material_id++;
        new_mat.name = "Custom Material " + std::to_string(new_mat.id);
        new_mat.is_builtin_preset = false;
        m_scene.materials.push_back(new_mat);
        m_selected_material_id = new_mat.id;
    }
    HelpTooltip("Create a new customizable PBR material.");
    ImGui::SameLine();

    Material* active_mat = m_scene.find_material(m_selected_material_id);
    if (!active_mat && !m_scene.materials.empty()) {
        m_selected_material_id = m_scene.materials[0].id;
        active_mat = &m_scene.materials[0];
    }

    ImGui::BeginDisabled(!active_mat);
    if (ImGui::Button("Duplicate", ImVec2(90, 26))) {
        if (active_mat) {
            Material dup = *active_mat;
            dup.id = m_scene.next_material_id++;
            dup.name = active_mat->name + " (Copy)";
            dup.is_builtin_preset = false;
            m_scene.materials.push_back(dup);
            m_selected_material_id = dup.id;
            active_mat = m_scene.find_material(m_selected_material_id);
        }
    }
    HelpTooltip("Duplicate the selected material.");
    ImGui::SameLine();

    if (ImGui::Button("Delete", ImVec2(80, 26))) {
        if (active_mat && m_scene.materials.size() > 1) {
            uint32_t del_id = active_mat->id;
            m_scene.materials.erase(
                std::remove_if(m_scene.materials.begin(), m_scene.materials.end(),
                               [del_id](const Material& m) { return m.id == del_id; }),
                m_scene.materials.end()
            );
            m_selected_material_id = m_scene.materials.empty() ? 0 : m_scene.materials[0].id;
            active_mat = m_scene.find_material(m_selected_material_id);
        }
    }
    HelpTooltip("Delete this material from the scene.");
    ImGui::EndDisabled();

    ImGui::Separator();

    // Split Layout: Left List, Right Inspector
    float left_width = 170.0f;
    ImGui::BeginChild("MaterialListPane", ImVec2(left_width, 0), true);
    static char filter_buf[64] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##MatFilter", "Search...", filter_buf, sizeof(filter_buf));

    std::string filter = filter_buf;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    for (const auto& mat : m_scene.materials) {
        std::string lower_name = mat.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (!filter.empty() && lower_name.find(filter) == std::string::npos) continue;

        ImGui::PushID(static_cast<int>(mat.id));
        ImVec4 swatch(mat.base_color[0], mat.base_color[1], mat.base_color[2], mat.base_color[3]);
        ImGui::ColorButton("##sw", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
        ImGui::SameLine();

        bool is_selected = (mat.id == m_selected_material_id);
        if (ImGui::Selectable(mat.name.c_str(), is_selected)) {
            m_selected_material_id = mat.id;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Inspector Pane
    ImGui::BeginChild("MaterialPropertiesPane", ImVec2(0, 0), true);
    if (active_mat) {
        update_material_preview_texture(*active_mat);

        // Header with Name & Preview
        ImGui::Columns(2, "MatHeaderCols", false);
        ImGui::SetColumnWidth(0, 140.0f);

        // 3D Preview Image
        if (m_mat_preview_srv) {
            ImGui::Image(reinterpret_cast<ImTextureID>(m_mat_preview_srv.Get()), ImVec2(128, 128));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Cook-Torrance GGX PBR preview with roughness, metallic and Fresnel reflections");
            }
        }

        ImGui::NextColumn();

        char name_buf[128];
        strncpy_s(name_buf, active_mat->name.c_str(), sizeof(name_buf) - 1);
        ImGui::TextUnformatted("Material Name");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##MatName", name_buf, sizeof(name_buf))) {
            active_mat->name = name_buf;
        }

        ImGui::TextDisabled("ID: %u | Type: %s", active_mat->id,
            active_mat->is_builtin_preset ? "Builtin Preset" : "Custom PBR");

        if (active_mat->is_builtin_preset) {
            if (ImGui::Button("Convert to Custom PBR", ImVec2(-1, 24))) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("Convert this built-in preset to an editable custom PBR material.");
        }

        Entity* sel_e = selected_entity();
        if (sel_e) {
            if (ImGui::Button("Assign to Selected Object", ImVec2(-1, 26))) {
                sel_e->material_id = static_cast<uint16_t>(active_mat->id);
            }
        }
        if (m_selected_entity_ids.size() > 1) {
            char apply_txt[64];
            snprintf(apply_txt, sizeof(apply_txt), "Assign to All %zu Selected", m_selected_entity_ids.size());
            if (ImGui::Button(apply_txt, ImVec2(-1, 24))) {
                for (uint32_t eid : m_selected_entity_ids) {
                    for (auto& e : m_scene.entities) {
                        if (e.id == eid) {
                            e.material_id = static_cast<uint16_t>(active_mat->id);
                        }
                    }
                }
            }
        }

        ImGui::Columns(1);
        ImGui::Separator();

        // PBR Properties
        if (ImGui::CollapsingHeader("Surface Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::ColorEdit4("Base Color", active_mat->base_color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar)) {
                active_mat->is_builtin_preset = false;
            }

            if (ImGui::SliderFloat("Roughness", &active_mat->roughness, 0.0f, 1.0f, "%.2f")) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("0.0 = mirror glossy reflections, 1.0 = completely matte diffuse scattering.");

            if (ImGui::SliderFloat("Metallic", &active_mat->metallic, 0.0f, 1.0f, "%.2f")) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("0.0 = non-metal dielectric (plastic, wood, stone), 1.0 = full metallic conductor (iron, gold, brass).");

            if (ImGui::SliderFloat("Emissive Intensity", &active_mat->emissive, 0.0f, 10.0f, "%.2f")) {
                active_mat->is_builtin_preset = false;
            }
            HelpTooltip("Self-illuminating surface glow, amplified by HDR bloom.");

            if (active_mat->emissive > 0.001f) {
                if (ImGui::ColorEdit3("Emissive Color", active_mat->emissive_color)) {
                    active_mat->is_builtin_preset = false;
                }
            }
        }

        // Texture Slots
        if (ImGui::CollapsingHeader("Texture Maps", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextUnformatted("Albedo Map");
            if (!active_mat->albedo_path.empty()) {
                auto fname = path_utf8(std::filesystem::u8path(active_mat->albedo_path).filename());
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Loaded: %s", fname.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", active_mat->albedo_path.c_str());
            } else {
                ImGui::TextDisabled("None (Solid Color)");
            }
            if (ImGui::Button("Browse Albedo...", ImVec2(130, 24))) {
                wchar_t filename[32768] = L"";
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    std::string u8path = to_utf8(filename);
                    uint16_t tid = load_asset(u8path, false);
                    if (tid) {
                        active_mat->albedo_tex_id = tid;
                        active_mat->albedo_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(u8path)).lexically_normal());
                        active_mat->is_builtin_preset = false;
                    }
                }
            }
            if (!active_mat->albedo_path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##Albedo", ImVec2(60, 24))) {
                    active_mat->albedo_path.clear();
                    active_mat->albedo_tex_id = 0;
                }
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Normal Map");
            if (!active_mat->normal_path.empty()) {
                auto fname = path_utf8(std::filesystem::u8path(active_mat->normal_path).filename());
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Loaded: %s", fname.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", active_mat->normal_path.c_str());
            } else {
                ImGui::TextDisabled("None (Geometric Normals)");
            }
            if (ImGui::Button("Browse Normal...", ImVec2(130, 24))) {
                wchar_t filename[32768] = L"";
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    std::string u8path = to_utf8(filename);
                    uint16_t tid = load_asset(u8path, false);
                    if (tid) {
                        active_mat->normal_tex_id = tid;
                        active_mat->normal_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(u8path)).lexically_normal());
                        active_mat->is_builtin_preset = false;
                    }
                }
            }
            if (!active_mat->normal_path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##Normal", ImVec2(60, 24))) {
                    active_mat->normal_path.clear();
                    active_mat->normal_tex_id = 0;
                }
            }
        }

        // UV Coordinates
        if (ImGui::CollapsingHeader("UV Coordinates", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Tiling", active_mat->uv_tiling, 0.05f, 0.01f, 100.0f, "%.2f");
            ImGui::DragFloat2("Offset", active_mat->uv_offset, 0.02f, -100.0f, 100.0f, "%.2f");
        }

        // Alpha & Blending
        if (ImGui::CollapsingHeader("Alpha & Transparency", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* alpha_modes[] = { "Opaque", "Mask (Cutoff)", "Blend (Transparent)" };
            int current_mode = std::clamp(active_mat->alpha_mode, 0, 2);
            if (ImGui::Combo("Alpha Mode", &current_mode, alpha_modes, IM_ARRAYSIZE(alpha_modes))) {
                active_mat->alpha_mode = current_mode;
                active_mat->is_builtin_preset = false;
            }
            if (active_mat->alpha_mode == 1) {
                ImGui::SliderFloat("Alpha Cutoff", &active_mat->alpha_cutoff, 0.0f, 1.0f, "%.2f");
                HelpTooltip("Pixels with opacity below this threshold are discarded.");
            }
        }
    } else {
        ImGui::TextDisabled("Select or create a material to view its properties.");
    }
    ImGui::EndChild();

    ImGui::End();
}

void StudioApp::draw_environment_panel() {
    ImGui::Begin("Environment & Lighting", &m_show_environment);
    if (ImGui::BeginTabBar("WorldTools")) {
        const auto terrain_flags = m_select_terrain_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Terrain", nullptr, terrain_flags)) {
            m_select_terrain_tab = false;
            ImGui::TextUnformatted("TERRAIN SCULPT");
            ImGui::TextDisabled("Shape your landscape in the viewport");
            ImGui::Spacing();
            ImGui::BeginDisabled(m_simulating);
            if (ImGui::Checkbox("Enable terrain", &m_scene.has_terrain)) rebuild_terrain();
            HelpTooltip("Show or hide terrain and its physics collider. Sculpting is disabled during simulation.");
            ImGui::EndDisabled();
            if (m_simulating) ImGui::TextWrapped("Stop simulation to edit terrain.");
            if (m_scene.has_terrain) {
                ImGui::Spacing();
                ImGui::BeginDisabled(m_simulating);
                const bool brush_was_enabled = m_terrain_brush_enabled;
                if (brush_was_enabled) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.68f, 1));
                if (ImGui::Button(brush_was_enabled ? "Sculpting enabled" : "Enable sculpting", ImVec2(-1, 34)))
                    m_terrain_brush_enabled = !m_terrain_brush_enabled;
                if (brush_was_enabled) ImGui::PopStyleColor();
                HelpTooltip("Use the left mouse button in the viewport to sculpt. Turn this off to select objects.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Brush mode");
                const char* names[] = { "Raise", "Lower", "Smooth", "Flatten" };
                const float half_width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
                for (int i = 0; i < 4; ++i) {
                    if (i % 2) ImGui::SameLine();
                    const bool selected = static_cast<int>(m_terrain_brush_mode) == i;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.68f, 1));
                    if (ImGui::Button(names[i], ImVec2(half_width, 32))) {
                        m_terrain_brush_mode = static_cast<TerrainBrushMode>(i);
                        m_terrain_brush_enabled = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                    const char* help[] = { "Raise terrain under the brush.", "Lower terrain under the brush.",
                        "Smooth nearby height differences.", "Move heights toward the height sampled at the start of the stroke." };
                    HelpTooltip(help[i]);
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("Radius");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##BrushRadius", &m_terrain_brush_radius, 1.0f, 50.0f, "%.1f m");
                HelpTooltip("Radius of the brush in world units.");
                ImGui::TextUnformatted("Strength");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##BrushStrength", &m_terrain_brush_strength, 0.1f, 20.0f, "%.1f");
                HelpTooltip("Controls how quickly the brush changes terrain.");
                ImGui::TextWrapped("Drag on the terrain to sculpt. Flatten uses the height at the start of the stroke.");
                ImGui::BeginDisabled(!m_history.can_undo() || m_terrain_stroke);
                if (ImGui::Button("Undo   Ctrl+Z", ImVec2(-1, 30))) apply_history(false);
                HelpTooltip("Undo the most recent scene change. Redo with Ctrl+Y or Ctrl+Shift+Z.");
                ImGui::EndDisabled();
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Heightmap")) {
                    ImGui::TextWrapped("Import or export grayscale PFM files (32-bit heights in world units). Square maps: 32 to 512 pixels.");
                    if (ImGui::Button("Import heightmap...", ImVec2(-1, 30))) import_heightmap_dialog();
                    HelpTooltip("Replace the terrain heights and resolution. Undo with Ctrl+Z.");
                    if (ImGui::Button("Export heightmap...", ImVec2(-1, 30))) export_heightmap_dialog();
                    HelpTooltip("Save the current terrain heights, including the generated terrain, to a grayscale PFM file.");
                }
                if (ImGui::CollapsingHeader("Terrain generation")) {
                    ImGui::TextDisabled("Changes apply when regenerated.");
                    ImGui::TextUnformatted("Seed"); ImGui::SetNextItemWidth(-1);
                    ImGui::InputInt("##TerrainSeed", &m_terrain_seed_input);
                    ImGui::TextUnformatted("Height scale"); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##TerrainHeight", &m_terrain_height_input, 0.5f, 4.0f, 80.0f, "%.1f m");
                    ImGui::TextUnformatted("World size"); ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##TerrainSize", &m_terrain_world_input, 2.0f, 60.0f, 600.0f, "%.1f m");
                    if (ImGui::Button("Regenerate terrain...", ImVec2(-1, 30))) ImGui::OpenPopup("Regenerate terrain?");
                }
                if (ImGui::BeginPopupModal("Regenerate terrain?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Regeneration replaces the current sculpted terrain.");
                    ImGui::TextUnformatted("Save the scene first if you want to keep it.");
                    ImGui::Spacing();
                    if (ImGui::Button("Regenerate", ImVec2(130, 0))) {
                        m_scene.terrain_seed = static_cast<uint32_t>(m_terrain_seed_input);
                        m_scene.terrain_height_scale = std::clamp(m_terrain_height_input, 4.0f, 80.0f);
                        m_scene.terrain_world_size = std::clamp(m_terrain_world_input, 60.0f, 600.0f);
                        m_scene.terrain_heights.clear();
                        rebuild_terrain();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::EndDisabled();
            } else {
                ImGui::Spacing();
                ImGui::TextWrapped("Enable terrain to use sculpting tools.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lighting")) {
            ImGui::TextUnformatted("ENVIRONMENT");
            ImGui::TextDisabled("Light, atmosphere and fog");
            ImGui::Spacing();
            auto slider = [](const char* label, float* value, float min, float max) {
                ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
                std::string id = std::string("##") + label;
                ImGui::SliderFloat(id.c_str(), value, min, max);
            };
            auto color = [](const char* label, float* value) {
                ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
                std::string id = std::string("##") + label;
                ImGui::ColorEdit3(id.c_str(), value);
            };
            if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
                slider("Elevation", &m_scene.sun_pitch, 5, 85);
                slider("Azimuth", &m_scene.sun_yaw, 0, 360);
                color("Sun color", m_scene.sun_color);
                slider("Sun intensity", &m_scene.sun_intensity, 0.1f, 5);
            }
            if (ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                size_t count = 0;
                for (const auto& entity : m_scene.entities) if (entity.is_point_light) ++count;
                ImGui::TextDisabled("%zu / 16 lights", count);
                if (count > 16) ImGui::TextWrapped("Only the first 16 enabled lights are rendered.");
                if (ImGui::Button("+ Add point light", ImVec2(-1, 30))) add_point_light();
                ImGui::TextWrapped("Select a light marker or a light in the hierarchy to edit it.");
            }
            if (ImGui::CollapsingHeader("Sun shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Enable shadows", &m_scene.shadows_enabled);
                ImGui::BeginDisabled(!m_scene.shadows_enabled);
                slider("Shadow strength", &m_scene.shadow_strength, 0.0f, 1.0f);
                HelpTooltip("Adjust how dark sun shadows appear.");
                slider("Shadow softness", &m_scene.shadow_softness, 0.5f, 6.0f);
                HelpTooltip("Control the filtering radius of sun shadow edges.");
                slider("Shadow coverage", &m_scene.shadow_coverage, 20.0f, 240.0f);
                HelpTooltip("Set the world area covered by the sun shadow map. Smaller areas provide more detail.");
                ImGui::EndDisabled();
                ImGui::TextWrapped("Smaller coverage gives sharper nearby shadows.");
            }
            if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
                color("Ambient color", m_scene.ambient_color);
                slider("Ambient intensity", &m_scene.ambient_intensity, 0.1f, 3);
                color("Sky color", m_scene.sky_color);
                slider("Fog start", &m_scene.fog_start, 0, 500);
                slider("Fog end", &m_scene.fog_end, 10, 1000);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Assets")) {
            draw_assets_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void StudioApp::apply_history(bool redo) {
    if (m_simulating || m_terrain_stroke) return;
    refresh_terrain_albedo();
    m_history.observe(m_scene);
    const Scene previous = m_scene;
    if (!(redo ? m_history.redo(m_scene) : m_history.undo(m_scene))) return;
    if (previous.has_terrain != m_scene.has_terrain || previous.terrain_grid_size != m_scene.terrain_grid_size ||
        previous.terrain_world_size != m_scene.terrain_world_size || previous.terrain_height_scale != m_scene.terrain_height_scale ||
        previous.terrain_seed != m_scene.terrain_seed || previous.terrain_heights != m_scene.terrain_heights) {
        auto* rw = EngineContext::instance().get_render_world();
        auto* pw = EngineContext::instance().get_physics_world();
        const bool same_config = previous.has_terrain && m_scene.has_terrain &&
            previous.terrain_grid_size == m_scene.terrain_grid_size && previous.terrain_world_size == m_scene.terrain_world_size &&
            previous.terrain_height_scale == m_scene.terrain_height_scale && previous.terrain_seed == m_scene.terrain_seed;
        if (same_config && rw && pw && !m_scene.terrain_heights.empty() &&
            rw->get_terrain_system().replace_heights(m_scene.terrain_heights)) {
            rw->update_terrain_mesh(); rw->get_terrain_system().update_collider(*pw);
            const auto n = m_scene.terrain_grid_size;
            const float cell = m_scene.terrain_world_size / (n - 1), half = m_scene.terrain_world_size * 0.5f;
            uint32_t min_x = n, min_z = n, max_x = 0, max_z = 0;
            if (previous.terrain_heights.size() == m_scene.terrain_heights.size()) {
                for (uint32_t z = 0; z < n; ++z) for (uint32_t x = 0; x < n; ++x) {
                    const size_t i = static_cast<size_t>(z) * n + x;
                    if (previous.terrain_heights[i] != m_scene.terrain_heights[i]) {
                        min_x = std::min(min_x, x); min_z = std::min(min_z, z);
                        max_x = std::max(max_x, x); max_z = std::max(max_z, z);
                    }
                }
            }
            if (min_x < n) rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(),
                min_x * cell - half, min_z * cell - half, max_x * cell - half, max_z * cell - half);
            else rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(), -half, -half, half, half);
            m_terrain_albedo_dirty = false;
        } else rebuild_terrain();
    }
    if (m_selected_entity_id >= 0 && !selected_entity()) m_selected_entity_id = -1;
    sync_scene_to_engine();
}

bool StudioApp::confirm_scene_replacement() {
    if (m_simulating) stop_simulation();
    m_history.observe(m_scene);
    if (!m_history.dirty(m_scene)) return true;
    const int choice = MessageBoxW(m_hwnd, L"Save changes to the current scene?", L"Unsaved changes",
                                   MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDYES) return save_scene();
    return choice == IDNO;
}

void StudioApp::request_close() {
    if (confirm_scene_replacement()) m_running = false;
}

void StudioApp::draw_status_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float bar_height = 28.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - bar_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, bar_height));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 1.0f));

    if (ImGui::Begin("StatusBar", nullptr, flags)) {
        ImGuiIO& io = ImGui::GetIO();

        if (m_simulating) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.25f, 1.0f), "[SIMULATING JOLT PHYSICS]");
        } else {
            ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.45f, 1.0f), "[READY]");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::Text("Entities: %zu", m_scene.entities.size());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (m_selected_entity_id >= 0) {
            std::string selName = "None";
            for (const auto& e : m_scene.entities) {
                if (static_cast<int32_t>(e.id) == m_selected_entity_id) {
                    selName = e.name;
                    break;
                }
            }
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Selected: %s (ID: %d)", selName.c_str(), m_selected_entity_id);
        } else {
            ImGui::TextDisabled("No Selection (Click in Viewport to select)");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Cam: (%.1f, %.1f, %.1f)", m_cam_pos[0], m_cam_pos[1], m_cam_pos[2]);

        // Right aligned stats
        char right_text[128];
        snprintf(right_text, sizeof(right_text), "FPS: %.1f (%.2f ms) | DirectX 11 PBR | Jolt Physics", io.Framerate, 1000.0f / (io.Framerate > 0 ? io.Framerate : 1.0f));
        float right_width = ImGui::CalcTextSize(right_text).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > right_width + 10.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - right_width - 15.0f);
            ImGui::TextColored(ImVec4(0.68f, 0.74f, 0.82f, 1.0f), "%s", right_text);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void StudioApp::refresh_terrain_albedo() {
    if (!m_terrain_albedo_dirty) return;
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    if (rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(),
        m_terrain_albedo_bounds[0], m_terrain_albedo_bounds[1], m_terrain_albedo_bounds[2], m_terrain_albedo_bounds[3])) {
        m_terrain_albedo_dirty = false;
        m_terrain_albedo_elapsed = 0.0f;
    }
}

void StudioApp::update_terrain_brush(float min_x, float min_y, float width, float height) {
    auto* rw = EngineContext::instance().get_render_world();
    auto* pw = EngineContext::instance().get_physics_world();
    if (!rw || !pw) return;
    auto& terrain = rw->get_terrain_system();
    const bool active = m_terrain_brush_enabled && m_scene.has_terrain && !m_simulating && terrain.is_generated();
    if (m_terrain_albedo_dirty) {
        m_terrain_albedo_elapsed += m_delta_time;
        if (m_terrain_albedo_elapsed >= 0.2f) refresh_terrain_albedo();
    }
    if (m_terrain_stroke && (!active || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        refresh_terrain_albedo();
        terrain.update_collider(*pw);
        m_terrain_stroke = false;
        m_terrain_has_previous_hit = false;
    }
    if (!active || width <= 0 || height <= 0 || !m_viewport_srv) {
        m_terrain_has_previous_hit = false; return;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    if (!m_viewport_hovered || mouse.x < min_x || mouse.x >= min_x + width || mouse.y < min_y || mouse.y >= min_y + height || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_terrain_has_previous_hit = false; return;
    }

    float ray[3];
    if (!viewport_ray(m_cam_pos, m_cam_target, mouse.x - min_x, mouse.y - min_y, width, height, ray)) {
        m_terrain_has_previous_hit = false; return;
    }
    const float dx = ray[0], dy = ray[1], dz = ray[2];
    const auto& cfg = terrain.get_config();
    const float half = cfg.world_size * 0.5f;
    const float step = std::max(0.25f, std::min(2.0f, cfg.world_size / (cfg.grid_size - 1) * 0.5f));
    bool previous_valid = false, hit = false;
    float previous_t = 0, previous_d = 0, hit_x = 0, hit_z = 0;
    for (float t = 0.1f; t <= 800.0f; t += step) {
        float x = m_cam_pos[0] + dx * t, z = m_cam_pos[2] + dz * t;
        if (x < -half || x > half || z < -half || z > half) { previous_valid = false; continue; }
        float d = m_cam_pos[1] + dy * t - terrain.get_height(x, z);
        if (previous_valid && previous_d >= 0 && d <= 0) {
            float lo = previous_t, hi = t;
            for (int i = 0; i < 10; ++i) {
                float mid = (lo + hi) * 0.5f;
                float mx = m_cam_pos[0] + dx * mid, mz = m_cam_pos[2] + dz * mid;
                if (m_cam_pos[1] + dy * mid > terrain.get_height(mx, mz)) lo = mid; else hi = mid;
            }
            hit_x = m_cam_pos[0] + dx * hi; hit_z = m_cam_pos[2] + dz * hi; hit = true; break;
        }
        previous_valid = true; previous_t = t; previous_d = d;
    }
    if (!hit) { m_terrain_has_previous_hit = false; return; }

    float view[16], proj[16];
    ComputeLookAtLH(view, m_cam_pos[0], m_cam_pos[1], m_cam_pos[2], m_cam_target[0], m_cam_target[1], m_cam_target[2], 0, 1, 0);
    ComputePerspectiveLH(proj, 60.0f, width / height, 0.1f, 800.0f);
    auto project = [&](float x, float y, float z, ImVec2& out) {
        float vx = x * view[0] + y * view[4] + z * view[8] + view[12];
        float vy = x * view[1] + y * view[5] + z * view[9] + view[13];
        float vz = x * view[2] + y * view[6] + z * view[10] + view[14];
        float w = vz * proj[11];
        if (w <= 0.1f) return false;
        out = ImVec2(min_x + (vx * proj[0] / w * 0.5f + 0.5f) * width,
                     min_y + (0.5f - vy * proj[5] / w * 0.5f) * height);
        return true;
    };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(min_x, min_y), ImVec2(min_x + width, min_y + height), true);
    ImVec2 last; bool have_last = false;
    for (int i = 0; i <= 64; ++i) {
        const float angle = 6.283185307f * i / 64.0f;
        float x = hit_x + cosf(angle) * m_terrain_brush_radius, z = hit_z + sinf(angle) * m_terrain_brush_radius;
        ImVec2 point;
        bool valid = x >= -half && x <= half && z >= -half && z <= half && project(x, terrain.get_height(x, z) + 0.12f, z, point);
        if (valid && have_last) draw->AddLine(last, point, IM_COL32(255, 210, 60, 255), 2.0f);
        last = point; have_last = valid;
    }
    draw->PopClipRect();

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!m_terrain_stroke) {
            if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
            m_terrain_flatten_height = terrain.get_height(hit_x, hit_z);
            m_terrain_stroke = true;
        }
        const float from_x = m_terrain_has_previous_hit ? m_terrain_previous_hit[0] : hit_x;
        const float from_z = m_terrain_has_previous_hit ? m_terrain_previous_hit[1] : hit_z;
        m_terrain_previous_hit[0] = hit_x; m_terrain_previous_hit[1] = hit_z;
        m_terrain_has_previous_hit = true;
        auto heights = terrain.heights();
        if (apply_terrain_brush_segment(heights, cfg.grid_size, cfg.world_size, from_x, from_z, hit_x, hit_z,
                                        m_terrain_brush_radius, m_terrain_brush_strength,
                                        std::min(m_delta_time, 0.05f), m_terrain_brush_mode, m_terrain_flatten_height)) {
            terrain.replace_heights(heights);
            rw->update_terrain_mesh();
            const float left = std::min(from_x, hit_x) - m_terrain_brush_radius;
            const float top = std::min(from_z, hit_z) - m_terrain_brush_radius;
            const float right = std::max(from_x, hit_x) + m_terrain_brush_radius;
            const float bottom = std::max(from_z, hit_z) + m_terrain_brush_radius;
            if (!m_terrain_albedo_dirty) {
                m_terrain_albedo_bounds[0] = left; m_terrain_albedo_bounds[1] = top;
                m_terrain_albedo_bounds[2] = right; m_terrain_albedo_bounds[3] = bottom;
                m_terrain_albedo_dirty = true;
            } else {
                m_terrain_albedo_bounds[0] = std::min(m_terrain_albedo_bounds[0], left);
                m_terrain_albedo_bounds[1] = std::min(m_terrain_albedo_bounds[1], top);
                m_terrain_albedo_bounds[2] = std::max(m_terrain_albedo_bounds[2], right);
                m_terrain_albedo_bounds[3] = std::max(m_terrain_albedo_bounds[3], bottom);
            }
            m_scene.terrain_heights = std::move(heights);
        }
    }
}

bool StudioApp::draw_light_markers(float min_x, float min_y, float width, float height) {
    if (width <= 0 || height <= 0 || !m_viewport_srv) return false;
    float view[16], proj[16];
    ComputeLookAtLH(view, m_cam_pos[0], m_cam_pos[1], m_cam_pos[2],
        m_cam_target[0], m_cam_target[1], m_cam_target[2], 0, 1, 0);
    ComputePerspectiveLH(proj, 60.0f, width / height, 0.1f, 800.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(min_x, min_y), ImVec2(min_x + width, min_y + height), true);
    bool clicked = false;
    for (const auto& light : m_scene.entities) {
        if (!light.is_point_light) continue;
        const float x = light.position[0], y = light.position[1], z = light.position[2];
        const float vx = x * view[0] + y * view[4] + z * view[8] + view[12];
        const float vy = x * view[1] + y * view[5] + z * view[9] + view[13];
        const float vz = x * view[2] + y * view[6] + z * view[10] + view[14];
        if (vz <= 0.1f || vz > 800.0f) continue;
        const ImVec2 point(min_x + (0.5f + vx * proj[0] / vz * 0.5f) * width,
                           min_y + (0.5f - vy * proj[5] / vz * 0.5f) * height);
        if (point.x < min_x || point.x > min_x + width || point.y < min_y || point.y > min_y + height) continue;
        const bool selected = static_cast<int32_t>(light.id) == m_selected_entity_id;
        const ImU32 color = light.visible ? IM_COL32(255, 207, 92, 245) : IM_COL32(140, 145, 150, 190);
        draw->AddCircleFilled(point, 7.0f, IM_COL32(20, 25, 31, 220));
        draw->AddCircle(point, selected ? 11.0f : 8.0f, color, 20, selected ? 2.5f : 1.5f);
        draw->AddCircleFilled(point, 3.0f, color);
        const ImVec2 mouse = ImGui::GetMousePos();
        const float dx = mouse.x - point.x, dy = mouse.y - point.y;
        if (!m_terrain_brush_enabled && m_viewport_hovered && dx * dx + dy * dy <= 144.0f &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            m_selected_entity_id = static_cast<int32_t>(light.id);
            m_gizmo_operation = GizmoOperation::Translate;
            clicked = true;
        }
    }
    draw->PopClipRect();
    return clicked;
}

void StudioApp::pick_entity_at_mouse(float mouseX, float mouseY, float viewportW, float viewportH) {
    if (viewportW <= 0.0f || viewportH <= 0.0f) return;

    float ray[3];
    if (!viewport_ray(m_cam_pos, m_cam_target, mouseX, mouseY, viewportW, viewportH, ray)) return;
    const float dirX = ray[0], dirY = ray[1], dirZ = ray[2];

    float closestT = 1e9f;
    int32_t hitEntityId = -1;

    for (const auto& e : m_scene.entities) {
        if (!e.visible || e.editor_grid || e.is_point_light || e.is_group) continue;

        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, e, wpos, wrot, wscale);

        float maxScale = std::max({ std::abs(wscale[0]), std::abs(wscale[1]), std::abs(wscale[2]) });
        float radius = maxScale * 0.75f;

        float ocX = m_cam_pos[0] - wpos[0];
        float ocY = m_cam_pos[1] - wpos[1];
        float ocZ = m_cam_pos[2] - wpos[2];

        float b = ocX * dirX + ocY * dirY + ocZ * dirZ;
        float c = (ocX * ocX + ocY * ocY + ocZ * ocZ) - radius * radius;
        float discr = b * b - c;

        if (discr >= 0.0f) {
            float t = -b - sqrtf(discr);
            if (t <= 0.1f) t = -b + sqrtf(discr);
            if (t > 0.1f && t < closestT) {
                closestT = t;
                hitEntityId = static_cast<int32_t>(e.id);
            }
        }
    }

    bool ctrl = ImGui::GetIO().KeyCtrl;
    if (hitEntityId != -1) {
        select_entity(static_cast<uint32_t>(hitEntityId), ctrl, false);
    } else if (!ctrl) {
        deselect_all();
    }
}

void StudioApp::draw_selection_bounds(const Entity& entity, const float* view, const float* proj, float minX, float minY, float sizeX, float sizeY) {
    if (entity.mesh_id == 3 && entity.scale[0] > 50.0f) return;
    if (entity.is_group) return;

    float hx = 0.5f;
    float hy = 0.5f;
    float hz = 0.5f;

    float corners[8][3] = {
        { -hx, -hy, -hz },
        {  hx, -hy, -hz },
        {  hx,  hy, -hz },
        { -hx,  hy, -hz },
        { -hx, -hy,  hz },
        {  hx, -hy,  hz },
        {  hx,  hy,  hz },
        { -hx,  hy,  hz }
    };

    float world_matrix[16];
    ComputeWorldMatrix(m_scene, entity, world_matrix);

    auto Project = [&](float lx, float ly, float lz, ImVec2& outPt) -> bool {
        float wx = lx * world_matrix[0] + ly * world_matrix[4] + lz * world_matrix[8]  + world_matrix[12];
        float wy = lx * world_matrix[1] + ly * world_matrix[5] + lz * world_matrix[9]  + world_matrix[13];
        float wz = lx * world_matrix[2] + ly * world_matrix[6] + lz * world_matrix[10] + world_matrix[14];

        float vx = wx * view[0] + wy * view[4] + wz * view[8]  + view[12];
        float vy = wx * view[1] + wy * view[5] + wz * view[9]  + view[13];
        float vz = wx * view[2] + wy * view[6] + wz * view[10] + view[14];
        float vw = wx * view[3] + wy * view[7] + wz * view[11] + view[15];

        if (vw <= 0.1f) return false;

        float cx = vx * proj[0] + vy * proj[4] + vz * proj[8]  + vw * proj[12];
        float cy = vx * proj[1] + vy * proj[5] + vz * proj[9]  + vw * proj[13];
        float cz = vx * proj[2] + vy * proj[6] + vz * proj[10] + vw * proj[14];
        float cw = vx * proj[3] + vy * proj[7] + vz * proj[11] + vw * proj[15];

        if (cw <= 0.1f) return false;

        float ndcX = cx / cw;
        float ndcY = cy / cw;
        if (ndcX < -3.0f || ndcX > 3.0f || ndcY < -3.0f || ndcY > 3.0f) return false;

        outPt.x = minX + (ndcX * 0.5f + 0.5f) * sizeX;
        outPt.y = minY + (-ndcY * 0.5f + 0.5f) * sizeY;
        return true;
    };

    ImVec2 screenPts[8];
    bool valid[8];
    for (int i = 0; i < 8; ++i) {
        valid[i] = Project(corners[i][0], corners[i][1], corners[i][2], screenPts[i]);
    }

    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 boxCol = IM_COL32(255, 175, 45, 220);

    for (int i = 0; i < 12; ++i) {
        int a = edges[i][0];
        int b = edges[i][1];
        if (valid[a] && valid[b]) {
            drawList->AddLine(screenPts[a], screenPts[b], boxCol, 1.5f);
        }
    }
}

void StudioApp::start_simulation() {
    if (m_simulating) return;
    auto* pw = EngineContext::instance().get_physics_world();
    if (!pw) return;
    m_history.observe(m_scene);
    m_simulation_backup = m_scene;
    m_simulation_backup_valid = true;
    m_simulation_bodies.clear();
    m_simulating = true;

    for (auto& e : m_scene.entities) {
        if (e.is_point_light || e.physics_type == 0 || (m_scene.has_terrain && e.editor_grid)) continue;

        RigidBodyDesc desc{};
        desc.shape_type = static_cast<BodyShapeType>(std::clamp(e.collider_shape, 0, 3));
        desc.motion_type = (e.physics_type == 1) ? MotionType::Static :
                           (e.physics_type == 2) ? MotionType::Dynamic : MotionType::Kinematic;

        desc.position[0] = e.position[0];
        desc.position[1] = e.position[1];
        desc.position[2] = e.position[2];

        float qx, qy, qz, qw;
        EulerToQuat(e.rotation[0], e.rotation[1], e.rotation[2], qx, qy, qz, qw);
        desc.rotation[0] = qx;
        desc.rotation[1] = qy;
        desc.rotation[2] = qz;
        desc.rotation[3] = qw;

        if (e.mesh_id == 3 && e.scale[0] > 50.0f) {
            desc.shape_type = BodyShapeType::Box;
            desc.motion_type = MotionType::Static;
            desc.half_extents[0] = e.scale[0] * 0.5f;
            desc.half_extents[1] = 0.5f;
            desc.half_extents[2] = e.scale[2] * 0.5f;
            desc.position[1] = -0.5f;
        } else {
            desc.half_extents[0] = std::max(0.05f, e.scale[0] * 0.5f);
            desc.half_extents[1] = std::max(0.05f, e.scale[1] * 0.5f);
            desc.half_extents[2] = std::max(0.05f, e.scale[2] * 0.5f);
            desc.radius = std::max(0.05f, e.scale[0] * 0.5f);
            desc.half_height = std::max(0.05f, e.scale[1] * 0.5f);
        }

        desc.mass = e.mass;
        desc.friction = e.friction;
        desc.restitution = e.restitution;

        e.physics_body_id = pw->create_body(desc);
        if (e.physics_body_id) m_simulation_bodies.push_back(e.physics_body_id);
    }
}

void StudioApp::stop_simulation() {
    if (!m_simulating) return;
    m_simulating = false;
    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) for (uint32_t body : m_simulation_bodies) pw->destroy_body(body);
    m_simulation_bodies.clear();

    if (m_simulation_backup_valid) {
        const bool terrain_changed = m_scene.has_terrain != m_simulation_backup.has_terrain ||
            m_scene.terrain_grid_size != m_simulation_backup.terrain_grid_size ||
            m_scene.terrain_world_size != m_simulation_backup.terrain_world_size ||
            m_scene.terrain_height_scale != m_simulation_backup.terrain_height_scale ||
            m_scene.terrain_seed != m_simulation_backup.terrain_seed ||
            m_scene.terrain_heights != m_simulation_backup.terrain_heights;
        m_scene = std::move(m_simulation_backup);
        m_simulation_backup_valid = false;
        if (terrain_changed) rebuild_terrain();
        if (m_selected_entity_id >= 0 && !selected_entity()) m_selected_entity_id = -1;
        sync_scene_to_engine();
    }
}

void StudioApp::new_scene() {
    if (m_simulating) {
        stop_simulation();
    }
    m_simulation_backup_valid = false;
    m_simulation_bodies.clear();

    // Destroy all existing physics bodies in Jolt
    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) {
        for (auto& e : m_scene.entities) {
            if (e.physics_body_id != 0) {
                pw->destroy_body(e.physics_body_id);
                e.physics_body_id = 0;
            }
        }
    }

    m_scene = Scene{};
    m_scene.name = "Untitled Scene";
    m_scene.filepath = "";
    deselect_all();

    // 1. 3D Editor Infinite Ground Grid Field (Plane 240x240m with procedural grid shader)
    Entity grid_field;
    grid_field.id = m_scene.next_entity_id++;
    grid_field.name = "Grid Floor";
    grid_field.editor_grid = true;
    grid_field.mesh_id = 3; // Plane
    grid_field.material_id = 0; // Default (procedural grid)
    grid_field.texture_id = 0;
    grid_field.scale[0] = 240.0f; grid_field.scale[1] = 1.0f; grid_field.scale[2] = 240.0f;
    grid_field.position[0] = 0.0f; grid_field.position[1] = 0.0f; grid_field.position[2] = 0.0f;
    grid_field.cast_shadow = false;
    grid_field.physics_type = 1; // Static
    m_scene.entities.push_back(grid_field);

    // 2. Central Reference Cube
    Entity cube;
    cube.id = m_scene.next_entity_id++;
    cube.name = "Central Cube";
    cube.mesh_id = 0; // Cube (ID = 0)
    cube.material_id = 2; // Gold Brass
    cube.position[0] = 0.0f; cube.position[1] = 1.0f; cube.position[2] = 0.0f;
    cube.scale[0] = 2.0f; cube.scale[1] = 2.0f; cube.scale[2] = 2.0f;
    cube.cast_shadow = true;
    cube.physics_type = 2; // Dynamic
    m_scene.entities.push_back(cube);

    // 3. Decorative Sphere
    Entity sphere;
    sphere.id = m_scene.next_entity_id++;
    sphere.name = "Granite Boulder";
    sphere.mesh_id = 1; // Sphere (ID = 1)
    sphere.material_id = 8; // Weathered Granite
    sphere.position[0] = 4.0f; sphere.position[1] = 1.2f; sphere.position[2] = 2.0f;
    sphere.scale[0] = 2.4f; sphere.scale[1] = 2.4f; sphere.scale[2] = 2.4f;
    sphere.cast_shadow = true;
    sphere.physics_type = 2; // Dynamic
    m_scene.entities.push_back(sphere);

    // Select the central cube by default
    select_entity(cube.id, false, false);

    // Reset camera position to view the grid and cube
    m_cam_pos[0] = 0.0f; m_cam_pos[1] = 6.0f; m_cam_pos[2] = -14.0f;
    m_cam_yaw = 0.0f;
    m_cam_pitch = 20.0f;
    m_cam_target[0] = 0.0f; m_cam_target[1] = 1.0f; m_cam_target[2] = 0.0f;

    rebuild_terrain();
    sync_scene_to_engine();
    m_history.reset(m_scene);
}

void StudioApp::rebuild_terrain() {
    m_terrain_albedo_dirty = false;
    m_terrain_albedo_elapsed = 0.0f;
    auto& ctx = EngineContext::instance();
    auto* rw = ctx.get_render_world();
    auto* pw = ctx.get_physics_world();
    if (!rw || !pw) return;
    m_terrain_stroke = false;
    m_terrain_has_previous_hit = false;
    m_terrain_seed_input = static_cast<int>(m_scene.terrain_seed);
    m_terrain_height_input = m_scene.terrain_height_scale;
    m_terrain_world_input = m_scene.terrain_world_size;
    if (m_scene.has_terrain) {
        rw->generate_terrain(m_scene.terrain_grid_size, m_scene.terrain_world_size,
            m_scene.terrain_height_scale, m_scene.terrain_seed, *pw,
            m_scene.terrain_heights.empty() ? nullptr : &m_scene.terrain_heights);
    } else {
        rw->get_terrain_system().clear(*pw);
    }
}

void StudioApp::import_heightmap_dialog() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Grayscale PFM heightmap (*.pfm)\0*.pfm\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    uint32_t size = 0; std::vector<float> heights; std::string error;
    if (!load_heightmap(filename, size, heights, error)) {
        MessageBoxA(m_hwnd, error.c_str(), "Heightmap import failed", MB_OK | MB_ICONERROR); return;
    }
    m_history.observe(m_scene);
    m_scene.terrain_grid_size = size;
    m_scene.terrain_heights = std::move(heights);
    rebuild_terrain();
    m_history.observe(m_scene);
}

void StudioApp::export_heightmap_dialog() {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw || !rw->get_terrain_system().is_generated()) {
        MessageBoxW(m_hwnd, L"Terrain is not available.", L"Heightmap export failed", MB_OK | MB_ICONERROR); return;
    }
    wchar_t filename[MAX_PATH] = L"terrain.pfm";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Grayscale PFM heightmap (*.pfm)\0*.pfm\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = L"pfm";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string error;
    const auto& terrain = rw->get_terrain_system();
    if (!save_heightmap(filename, terrain.get_config().grid_size, terrain.heights(), error))
        MessageBoxA(m_hwnd, error.c_str(), "Heightmap export failed", MB_OK | MB_ICONERROR);
}

void StudioApp::open_scene_dialog() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) load_scene(to_utf8(filename));
}

bool StudioApp::load_scene(const std::string& path) {
    Scene loaded_scene;
    if (!SceneSerializer::load_from_file(loaded_scene, path)) {
        MessageBoxW(m_hwnd, L"Could not load the scene file.", L"Load Failed", MB_OK | MB_ICONERROR);
        return false;
    }

    std::string missing;
    for (auto& entity : loaded_scene.entities) {
        if (!entity.model_path.empty()) {
            entity.mesh_id = load_asset(entity.model_path, true);
            if (!entity.mesh_id) missing += "Model: " + entity.model_path + "\n";
        }
        if (!entity.texture_path.empty()) {
            entity.texture_id = load_asset(entity.texture_path, false);
            if (!entity.texture_id) missing += "Texture: " + entity.texture_path + "\n";
        }
    }
    if (!missing.empty()) {
        m_asset_message = "Some assets could not be loaded. Their source paths were kept; use Replace in the inspector to relink them.\n\n" + missing;
        m_asset_message_pending = true;
    }
    if (m_simulating) stop_simulation();
    m_simulation_backup_valid = false;
    m_simulation_bodies.clear();
    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) {
        for (auto& e : m_scene.entities) {
            if (e.physics_body_id != 0) {
                pw->destroy_body(e.physics_body_id);
                e.physics_body_id = 0;
            }
        }
    }

    m_scene = std::move(loaded_scene);
    rebuild_terrain();
    deselect_all();
    sync_scene_to_engine();
    m_history.reset(m_scene);
    std::cout << "Scene loaded successfully from: " << path << std::endl;
    if (!m_project.filepath.empty()) { m_project.last_scene = path; save_project_registry(); }
    return true;
}

Entity* StudioApp::selected_entity() {
    for (auto& entity : m_scene.entities)
        if (static_cast<int32_t>(entity.id) == m_selected_entity_id) return &entity;
    return nullptr;
}

bool StudioApp::save_project_registry() {
    if (m_project.filepath.empty()) return true;
    ProjectData data;
    data.filepath = m_project.filepath;
    data.last_scene = m_project.last_scene;
    for (const auto& asset : m_assets) data.assets.push_back({asset.path, asset.is_model});
    std::string error;
    if (!ProjectSerializer::save(data, error)) {
        m_asset_message = "Could not save the asset library: " + error;
        m_asset_message_pending = true;
        return false;
    }
    m_project = std::move(data);
    return true;
}

uint16_t StudioApp::load_asset(const std::string& path, bool is_model) {
    if (path.empty()) return 0;
    try {
        const auto absolute = path_utf8(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());
        auto it = std::find_if(m_assets.begin(), m_assets.end(), [&](const AssetEntry& item) {
            return item.is_model == is_model && _stricmp(item.path.c_str(), absolute.c_str()) == 0;
        });
        if (it != m_assets.end() && it->id) return it->id;
        const auto id = static_cast<uint16_t>(is_model ? EngineContext::instance().load_mesh_model(absolute.c_str())
                                                       : EngineContext::instance().load_texture(absolute.c_str()));
        if (!id) return 0;
        if (it != m_assets.end()) it->id = id;
        else {
            m_assets.push_back({absolute, id, is_model});
            it = m_assets.end() - 1;
            save_project_registry();
        }
        std::error_code ec;
        it->modified = std::filesystem::last_write_time(std::filesystem::u8path(absolute), ec);
        it->timestamp_known = !ec;
        return id;
    } catch (const std::exception&) { return 0; }
}

bool StudioApp::reload_asset(size_t index) {
    if (index >= m_assets.size()) return false;
    auto& asset = m_assets[index];
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return false;
    const uint16_t incoming = static_cast<uint16_t>(asset.is_model
        ? EngineContext::instance().load_mesh_model(asset.path.c_str())
        : EngineContext::instance().load_texture(asset.path.c_str()));
    if (!incoming) return false;
    if (!asset.id) asset.id = incoming;
    else if (!(asset.is_model ? rw->get_mesh_manager().replace_mesh(asset.id, incoming)
                               : rw->get_texture_manager().replace_texture(asset.id, incoming))) return false;
    std::error_code ec;
    asset.modified = std::filesystem::last_write_time(std::filesystem::u8path(asset.path), ec);
    asset.timestamp_known = !ec;
    return true;
}

void StudioApp::relink_asset(size_t index) {
    if (index >= m_assets.size() || m_simulating) return;
    const AssetEntry old = m_assets[index];
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = old.is_model
        ? L"3D Models (*.glb;*.gltf;*.obj)\0*.glb;*.gltf;*.obj\0\0"
        : L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    const std::string replacement = path_utf8(std::filesystem::absolute(std::filesystem::path(filename)).lexically_normal());
    const uint16_t id = load_asset(replacement, old.is_model);
    if (!id) { m_asset_message = "Could not load replacement: " + replacement; m_asset_message_pending = true; return; }
    for (auto& entity : m_scene.entities) {
        std::string& path = old.is_model ? entity.model_path : entity.texture_path;
        if (_stricmp(path.c_str(), old.path.c_str()) == 0) {
            path = replacement;
            if (old.is_model) entity.mesh_id = id; else entity.texture_id = id;
        }
    }
    if (_stricmp(old.path.c_str(), replacement.c_str()) != 0) {
        const auto it = std::find_if(m_assets.begin(), m_assets.end(), [&](const AssetEntry& entry) {
            return entry.is_model == old.is_model && _stricmp(entry.path.c_str(), old.path.c_str()) == 0;
        });
        if (it != m_assets.end()) m_assets.erase(it);
        save_project_registry();
    }
}

void StudioApp::update_assets(float delta_time) {
    m_asset_poll_timer += delta_time;
    if (m_asset_poll_timer < 1.0f) return;
    m_asset_poll_timer = 0.0f;
    for (size_t i = 0; i < m_assets.size(); ++i) {
        auto& asset = m_assets[i];
        if (!asset.id || !asset.timestamp_known) continue;
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(std::filesystem::u8path(asset.path), ec);
        if (!ec && modified != asset.modified && !reload_asset(i)) {
            // Do not retry a partially written or invalid file every frame.
            asset.modified = modified;
            m_asset_message = "Could not reload modified asset; the previous version remains active: " + asset.path;
            m_asset_message_pending = true;
        }
    }
}

void StudioApp::new_project_dialog() {
    if (!confirm_scene_replacement()) return;
    wchar_t filename[32768] = L"Project.gm3dproject";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Project (*.gm3dproject)\0*.gm3dproject\0\0";
    ofn.lpstrDefExt = L"gm3dproject"; ofn.lpstrFile = filename;
    ofn.nMaxFile = static_cast<DWORD>(std::size(filename)); ofn.Flags = OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    const std::filesystem::path target(filename);
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        m_asset_message = "A project file already exists at this location. Choose another name or open the existing project.";
        m_asset_message_pending = true; return;
    }
    ProjectData project; project.filepath = to_utf8(filename);
    std::string error;
    if (!ProjectSerializer::save(project, error)) {
        m_asset_message = "Could not create project: " + error; m_asset_message_pending = true; return;
    }
    std::filesystem::create_directories(target.parent_path() / L"Assets", ec);
    if (!ec) std::filesystem::create_directories(target.parent_path() / L"Scenes", ec);
    if (ec) { m_asset_message = "Project created, but Assets or Scenes folder could not be created: " + ec.message(); m_asset_message_pending = true; }
    m_project = std::move(project); m_assets.clear(); new_scene();
}

void StudioApp::open_project_dialog() {
    if (!confirm_scene_replacement()) return;
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Project (*.gm3dproject)\0*.gm3dproject\0\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    ProjectData project; std::string error;
    if (!ProjectSerializer::load(to_utf8(filename), project, error)) {
        m_asset_message = "Could not open project: " + error; m_asset_message_pending = true; return;
    }
    m_assets.clear();
    for (const auto& asset : project.assets) m_assets.push_back({asset.path, 0, asset.is_model});
    m_project = std::move(project);
    new_scene();
    if (!m_project.last_scene.empty()) {
        const std::string last_scene = m_project.last_scene;
        load_scene(last_scene);
    }
}

bool StudioApp::add_model_asset(const std::string& path, bool replace_selected) {
    if (m_simulating) return false;
    const uint16_t id = load_asset(path, true);
    if (!id) return false;
    Entity* target = replace_selected ? selected_entity() : nullptr;
    if (replace_selected && !target) return false;
    if (!target) {
        add_entity(id, path_utf8(std::filesystem::u8path(path).stem()));
        target = selected_entity();
    }
    if (!target) return false;
    target->mesh_id = id;
    target->model_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());

    // Extract glTF / GLB materials and diagnostic limitations report
    auto ext = std::filesystem::u8path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".glb" || ext == ".gltf") {
        std::vector<GLTFMaterialInfo> extracted_materials;
        std::string report;
        if (MeshManager::extract_gltf_materials(path, extracted_materials, report)) {
            std::filesystem::path model_dir = std::filesystem::u8path(path).parent_path();
            uint32_t first_assigned_mat_id = 0;

            for (const auto& gltfMat : extracted_materials) {
                Material newMat{};
                newMat.id = m_scene.next_material_id++;
                newMat.name = gltfMat.name.empty() ? ("Material_" + std::to_string(newMat.id)) : gltfMat.name;
                newMat.is_builtin_preset = false;
                newMat.base_color[0] = gltfMat.base_color[0];
                newMat.base_color[1] = gltfMat.base_color[1];
                newMat.base_color[2] = gltfMat.base_color[2];
                newMat.base_color[3] = gltfMat.base_color[3];
                newMat.roughness = gltfMat.roughness;
                newMat.metallic = gltfMat.metallic;
                newMat.emissive = gltfMat.emissive;
                newMat.emissive_color[0] = gltfMat.emissive_color[0];
                newMat.emissive_color[1] = gltfMat.emissive_color[1];
                newMat.emissive_color[2] = gltfMat.emissive_color[2];
                newMat.alpha_mode = gltfMat.alpha_mode;
                newMat.alpha_cutoff = gltfMat.alpha_cutoff;

                if (!gltfMat.albedo_uri.empty()) {
                    std::filesystem::path tex_path = model_dir / std::filesystem::u8path(gltfMat.albedo_uri);
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(tex_path, ec)) {
                        std::string tex_path_str = path_utf8(tex_path);
                        uint16_t tid = load_asset(tex_path_str, false);
                        if (tid) {
                            newMat.albedo_tex_id = tid;
                            newMat.albedo_path = path_utf8(std::filesystem::absolute(tex_path).lexically_normal());
                        }
                    }
                }

                if (!gltfMat.normal_uri.empty()) {
                    std::filesystem::path nrm_path = model_dir / std::filesystem::u8path(gltfMat.normal_uri);
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(nrm_path, ec)) {
                        std::string nrm_path_str = path_utf8(nrm_path);
                        uint16_t tid = load_asset(nrm_path_str, false);
                        if (tid) {
                            newMat.normal_tex_id = tid;
                            newMat.normal_path = path_utf8(std::filesystem::absolute(nrm_path).lexically_normal());
                        }
                    }
                }

                m_scene.materials.push_back(newMat);
                if (first_assigned_mat_id == 0) first_assigned_mat_id = newMat.id;
            }

            if (first_assigned_mat_id != 0) {
                target->material_id = static_cast<uint16_t>(first_assigned_mat_id);
                m_selected_material_id = first_assigned_mat_id;
            }

            if (!report.empty()) {
                m_material_diag_title = "glTF Material Import Report: " + path_utf8(std::filesystem::u8path(path).filename());
                m_material_diag_report = report;
                m_show_material_diag = true;
            }
        }
    }

    return true;
}

bool StudioApp::apply_texture_asset(const std::string& path) {
    if (m_simulating) return false;
    Entity* target = selected_entity();
    if (!target || target->editor_grid || target->is_point_light) return false;
    const uint16_t id = load_asset(path, false);
    if (!id) return false;
    target->texture_id = id;
    target->texture_path = path_utf8(std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal());
    return true;
}

void StudioApp::process_dropped_files() {
    if (m_dropped_files.empty()) return;
    auto paths = std::move(m_dropped_files);
    m_dropped_files.clear();
    std::string failures;
    for (const auto& wide_path : paths) {
        const std::string path = to_utf8(wide_path);
        auto ext = std::filesystem::path(wide_path).extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        bool ok = false;
        if (!m_simulating && (ext == L".glb" || ext == L".gltf" || ext == L".obj")) ok = add_model_asset(path, false);
        else if (!m_simulating && (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".tif" || ext == L".tiff" || ext == L".gif")) ok = apply_texture_asset(path);
        if (!ok) failures += path + "\n";
    }
    if (!failures.empty()) {
        m_asset_message = "Could not import these files. Stop simulation and select an object before dropping a texture. Supported models: OBJ, GLB, glTF; images: PNG, JPEG, BMP, TIFF, GIF.\n\n" + failures;
        m_asset_message_pending = true;
    }
}

void StudioApp::import_model_dialog(bool replace_selected) {
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"3D Models (*.glb;*.gltf;*.obj)\0*.glb;*.gltf;*.obj\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn) && !add_model_asset(to_utf8(filename), replace_selected)) {
        m_asset_message = "Could not load model: " + to_utf8(filename);
        m_asset_message_pending = true;
    }
}

void StudioApp::import_texture_dialog() {
    if (!selected_entity()) {
        m_asset_message = "Select an object before assigning a texture.";
        m_asset_message_pending = true;
        return;
    }
    wchar_t filename[32768] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.gif\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn) && !apply_texture_asset(to_utf8(filename))) {
        m_asset_message = "Could not apply texture: " + to_utf8(filename);
        m_asset_message_pending = true;
    }
}

void StudioApp::draw_assets_tab() {
    ImGui::TextUnformatted("ASSET LIBRARY");
    if (m_project.filepath.empty()) ImGui::TextDisabled("No project open: imports are session-only");
    else ImGui::TextWrapped("Project: %s", path_utf8(std::filesystem::u8path(m_project.filepath).filename()).c_str());
    ImGui::TextWrapped("Drop OBJ, GLB or glTF models anywhere in the window. Drop an image to texture the selected object.");
    ImGui::BeginDisabled(m_simulating);
    if (ImGui::Button("Import model...", ImVec2(-1, 30))) import_model_dialog();
    HelpTooltip("Import an OBJ, GLB, or glTF file and add it to the scene.");
    if (ImGui::Button("Assign texture...", ImVec2(-1, 30))) import_texture_dialog();
    HelpTooltip("Select an image and apply it to the selected object.");
    ImGui::EndDisabled();

    // Check missing count and totals
    size_t missing_count = 0;
    size_t model_count = 0;
    size_t texture_count = 0;
    for (const auto& a : m_assets) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(a.path), ec)) ++missing_count;
        if (a.is_model) ++model_count; else ++texture_count;
    }

    if (missing_count > 0) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        ImGui::BeginChild("MissingAlert", ImVec2(0, 58), true);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Warning: %zu missing asset(s) detected!", missing_count);
        if (ImGui::Button("Auto-Relink All Missing Assets", ImVec2(-1, 24))) {
            auto_relink_missing_assets();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Category filter buttons
    auto cat_btn = [this](const char* label, int cat) {
        if (m_asset_filter_category == cat) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.7f, 1.0f));
        if (ImGui::Button(label)) m_asset_filter_category = cat;
        if (m_asset_filter_category == cat) ImGui::PopStyleColor();
    };
    std::string all_label = "All (" + std::to_string(m_assets.size()) + ")";
    std::string mod_label = "Models (" + std::to_string(model_count) + ")";
    std::string tex_label = "Textures (" + std::to_string(texture_count) + ")";
    std::string mis_label = "Missing (" + std::to_string(missing_count) + ")";

    cat_btn(all_label.c_str(), 0); ImGui::SameLine();
    cat_btn(mod_label.c_str(), 1); ImGui::SameLine();
    cat_btn(tex_label.c_str(), 2); ImGui::SameLine();
    if (missing_count > 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
    cat_btn(mis_label.c_str(), 3);
    if (missing_count > 0) ImGui::PopStyleColor();

    static char search[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##AssetSearch", "Search assets...", search, sizeof(search));
    HelpTooltip("Filter project resources by their path or filename.");
    std::string filter = search;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (m_assets.empty()) ImGui::TextDisabled("Imported files appear here.");
    size_t relink_index = static_cast<size_t>(-1);

    for (size_t i = 0; i < m_assets.size(); ++i) {
        const auto& asset = m_assets[i];
        std::error_code asset_error;
        const bool missing = !std::filesystem::is_regular_file(std::filesystem::u8path(asset.path), asset_error);

        if (m_asset_filter_category == 1 && !asset.is_model) continue;
        if (m_asset_filter_category == 2 && asset.is_model) continue;
        if (m_asset_filter_category == 3 && !missing) continue;

        std::string searchable = asset.path;
        std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!filter.empty() && searchable.find(filter) == std::string::npos) continue;

        ImGui::PushID(static_cast<int>(i));
        const std::string name = path_utf8(std::filesystem::u8path(asset.path).filename());

        if (missing) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[MISSING %s]", asset.is_model ? "MODEL" : "TEXTURE");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.75f, 0.95f, 1.0f), "[%s]", asset.is_model ? "3D MODEL" : "TEXTURE");
        }

        // Thumbnail / Visual representation
        if (!asset.is_model && asset.id && !missing) {
            auto* rw = EngineContext::instance().get_render_world();
            if (rw) {
                ImTextureID srv = reinterpret_cast<ImTextureID>(rw->get_texture_manager().get_srv(asset.id));
                ImGui::Image(srv, ImVec2(52, 52), ImVec2(0,0), ImVec2(1,1), ImVec4(1,1,1,1), ImVec4(0.3f, 0.35f, 0.4f, 0.8f));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Image(srv, ImVec2(140, 140));
                    ImGui::Text("%s", name.c_str());
                    ImGui::EndTooltip();
                }
            }
        } else if (asset.is_model && !missing) {
            std::filesystem::path p = std::filesystem::u8path(asset.path);
            std::string ext = path_utf8(p.extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.24f, 0.32f, 1.0f));
            ImGui::Button((std::string("[3D] ") + ext).c_str(), ImVec2(52, 52));
            ImGui::PopStyleColor();
        }

        ImGui::TextWrapped("%s", name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.path.c_str());

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("GM3D_ASSET", &i, sizeof(i));
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::BeginDisabled(m_simulating || missing || (!asset.is_model && !selected_entity()));
        if (ImGui::Button(asset.is_model ? "Add to scene" : "Apply to selected", ImVec2(-1, 0))) {
            const std::string path = asset.path;
            if (!(asset.is_model ? add_model_asset(path, false) : apply_texture_asset(path))) {
                m_asset_message = "Could not use asset: " + path;
                m_asset_message_pending = true;
            }
        }
        ImGui::EndDisabled();

        if (!missing && ImGui::Button(asset.id ? "Reload" : (asset.is_model ? "Load asset" : "Load preview"), ImVec2(-1, 0))) {
            if (!reload_asset(i)) { m_asset_message = "Could not load asset: " + asset.path; m_asset_message_pending = true; }
        }

        ImGui::BeginDisabled(m_simulating);
        if (ImGui::Button("Relink...", ImVec2(-1, 0))) relink_index = i;
        HelpTooltip("Choose a replacement file and update matching objects in the current scene.");
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::PopID();
    }
    if (relink_index != static_cast<size_t>(-1)) relink_asset(relink_index);
}

bool StudioApp::save_scene() {
    if (m_simulating) stop_simulation();
    m_history.observe(m_scene);
    if (m_scene.filepath.empty()) return save_scene_as_dialog();
    if (!SceneSerializer::save_to_file(m_scene, m_scene.filepath)) {
        MessageBoxW(m_hwnd, L"Could not save the scene file.", L"Save Failed", MB_OK | MB_ICONERROR);
        return false;
    }
    m_history.mark_saved(m_scene);
    return true;
}

bool StudioApp::save_scene_as_dialog() {
    if (m_simulating) stop_simulation();
    m_history.observe(m_scene);
    wchar_t filename[MAX_PATH] = L"scene.json";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"GM3D Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    std::wstring scene_dir;
    if (!m_project.filepath.empty()) {
        auto folder = std::filesystem::u8path(m_project.filepath).parent_path() / L"Scenes";
        if (std::filesystem::is_directory(folder)) { scene_dir = folder.wstring(); ofn.lpstrInitialDir = scene_dir.c_str(); }
    }
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return false;
    const std::string path = to_utf8(filename);
    if (!SceneSerializer::save_to_file(m_scene, path)) {
        MessageBoxW(m_hwnd, L"Could not save the scene file.", L"Save Failed", MB_OK | MB_ICONERROR);
        return false;
    }
    m_scene.filepath = path;
    if (!m_project.filepath.empty()) { m_project.last_scene = path; save_project_registry(); }
    m_history.mark_saved(m_scene);
    return true;
}

void StudioApp::add_entity(uint16_t mesh_id, const std::string& name) {
    Entity e;
    e.id = m_scene.next_entity_id++;
    e.name = name + "_" + std::to_string(e.id);
    e.mesh_id = mesh_id;

    float direction[3] = {0.0f, 0.0f, 1.0f};
    viewport_ray(m_cam_pos, m_cam_target, 0.5f, 0.5f, 1.0f, 1.0f, direction);
    for (int i = 0; i < 3; ++i) e.position[i] = m_cam_pos[i] + direction[i] * 6.0f;

    m_scene.entities.push_back(e);
    select_entity(e.id, false, false);
}

void StudioApp::add_point_light() {
    size_t count = 0;
    for (const auto& entity : m_scene.entities) if (entity.is_point_light) ++count;
    if (count >= 16) {
        m_asset_message = "A scene can contain up to 16 point lights.";
        m_asset_message_pending = true;
        return;
    }
    add_entity(0, "Point Light");
    if (auto* entity = selected_entity()) {
        entity->is_point_light = true;
        entity->cast_shadow = false;
        entity->physics_type = 0;
        m_terrain_brush_enabled = false;
        m_gizmo_operation = GizmoOperation::Translate;
    }
}

void StudioApp::delete_selected_entity() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    std::unordered_set<uint32_t> to_delete;
    for (uint32_t id : m_selected_entity_ids) {
        to_delete.insert(id);
        std::vector<uint32_t> descendants;
        m_scene.get_all_descendants(id, descendants);
        for (uint32_t desc_id : descendants) to_delete.insert(desc_id);
    }

    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) {
        for (const auto& e : m_scene.entities) {
            if (to_delete.count(e.id) && e.physics_body_id != 0) {
                pw->destroy_body(e.physics_body_id);
            }
        }
    }

    m_scene.entities.erase(
        std::remove_if(m_scene.entities.begin(), m_scene.entities.end(),
            [&to_delete](const Entity& e) { return to_delete.count(e.id) > 0; }),
        m_scene.entities.end()
    );

    deselect_all();
}

void StudioApp::duplicate_selected_entity() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    std::vector<uint32_t> root_selected;
    for (uint32_t id : m_selected_entity_ids) {
        bool has_selected_ancestor = false;
        for (uint32_t other : m_selected_entity_ids) {
            if (id != other && m_scene.is_descendant_of(id, other)) {
                has_selected_ancestor = true;
                break;
            }
        }
        if (!has_selected_ancestor) root_selected.push_back(id);
    }

    std::unordered_map<uint32_t, uint32_t> old_to_new;
    std::vector<Entity> new_entities;
    std::vector<uint32_t> newly_created_roots;

    for (uint32_t root_id : root_selected) {
        std::vector<uint32_t> subtree;
        subtree.push_back(root_id);
        m_scene.get_all_descendants(root_id, subtree);

        for (uint32_t orig_id : subtree) {
            const Entity* orig = m_scene.find_entity(orig_id);
            if (!orig) continue;

            Entity copy = *orig;
            copy.id = m_scene.next_entity_id++;
            copy.physics_body_id = 0;
            old_to_new[orig_id] = copy.id;

            if (orig_id == root_id) {
                copy.name += "_Copy";
                copy.position[0] += 1.0f;
                copy.position[2] += 1.0f;
                newly_created_roots.push_back(copy.id);
            }
            new_entities.push_back(copy);
        }
    }

    for (auto& new_ent : new_entities) {
        if (new_ent.parent_id != 0) {
            auto it = old_to_new.find(new_ent.parent_id);
            if (it != old_to_new.end()) {
                new_ent.parent_id = it->second;
            }
        }
    }

    for (auto& new_ent : new_entities) {
        m_scene.entities.push_back(new_ent);
    }

    m_selected_entity_ids = newly_created_roots;
    m_selected_entity_id = newly_created_roots.empty() ? -1 : static_cast<int32_t>(newly_created_roots.front());
}

bool StudioApp::is_entity_selected(uint32_t id) const {
    return std::find(m_selected_entity_ids.begin(), m_selected_entity_ids.end(), id) != m_selected_entity_ids.end();
}

void StudioApp::select_entity(uint32_t id, bool multi, bool range) {
    if (id == 0) {
        deselect_all();
        return;
    }
    if (range && m_selected_entity_id > 0) {
        int idx1 = -1, idx2 = -1;
        for (int i = 0; i < static_cast<int>(m_scene.entities.size()); ++i) {
            if (static_cast<int32_t>(m_scene.entities[i].id) == m_selected_entity_id) idx1 = i;
            if (m_scene.entities[i].id == id) idx2 = i;
        }
        if (idx1 >= 0 && idx2 >= 0) {
            int start = std::min(idx1, idx2);
            int end = std::max(idx1, idx2);
            for (int i = start; i <= end; ++i) {
                uint32_t curr_id = m_scene.entities[i].id;
                if (!is_entity_selected(curr_id)) m_selected_entity_ids.push_back(curr_id);
            }
            m_selected_entity_id = static_cast<int32_t>(id);
            return;
        }
    }
    if (multi) {
        auto it = std::find(m_selected_entity_ids.begin(), m_selected_entity_ids.end(), id);
        if (it != m_selected_entity_ids.end()) {
            m_selected_entity_ids.erase(it);
            if (m_selected_entity_id == static_cast<int32_t>(id)) {
                m_selected_entity_id = m_selected_entity_ids.empty() ? -1 : static_cast<int32_t>(m_selected_entity_ids.back());
            }
        } else {
            m_selected_entity_ids.push_back(id);
            m_selected_entity_id = static_cast<int32_t>(id);
        }
    } else {
        m_selected_entity_ids = { id };
        m_selected_entity_id = static_cast<int32_t>(id);
    }
}

void StudioApp::deselect_all() {
    m_selected_entity_ids.clear();
    m_selected_entity_id = -1;
}

void StudioApp::set_entity_parent(uint32_t child_id, uint32_t new_parent_id) {
    if (child_id == 0 || child_id == new_parent_id) return;
    Entity* child = m_scene.find_entity(child_id);
    if (!child) return;

    if (new_parent_id != 0) {
        if (m_scene.is_descendant_of(new_parent_id, child_id)) {
            m_asset_message = "Cannot parent an object to its own descendant.";
            m_asset_message_pending = true;
            return;
        }
    }

    if (child->parent_id == new_parent_id) return;

    float world_matrix[16];
    ComputeWorldMatrix(m_scene, *child, world_matrix);

    child->parent_id = new_parent_id;

    ComputeLocalTransform(m_scene, new_parent_id, world_matrix, child->position, child->rotation, child->scale);
}

void StudioApp::group_selected_entities() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    std::vector<uint32_t> targets;
    for (uint32_t id : m_selected_entity_ids) {
        bool has_ancestor = false;
        for (uint32_t other : m_selected_entity_ids) {
            if (id != other && m_scene.is_descendant_of(id, other)) {
                has_ancestor = true;
                break;
            }
        }
        if (!has_ancestor) targets.push_back(id);
    }
    if (targets.empty()) return;

    float center[3] = { 0.0f, 0.0f, 0.0f };
    for (uint32_t id : targets) {
        Entity* e = m_scene.find_entity(id);
        if (!e) continue;
        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *e, wpos, wrot, wscale);
        center[0] += wpos[0];
        center[1] += wpos[1];
        center[2] += wpos[2];
    }
    center[0] /= targets.size();
    center[1] /= targets.size();
    center[2] /= targets.size();

    Entity group;
    group.id = m_scene.next_entity_id++;
    group.name = "Group_" + std::to_string(group.id);
    group.is_group = true;
    group.mesh_id = 0;
    group.visible = true;
    group.position[0] = center[0];
    group.position[1] = center[1];
    group.position[2] = center[2];

    m_scene.entities.push_back(group);

    for (uint32_t id : targets) {
        set_entity_parent(id, group.id);
    }

    m_selected_entity_ids = { group.id };
    m_selected_entity_id = static_cast<int32_t>(group.id);
}

void StudioApp::unparent_selected_entities() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    for (uint32_t id : m_selected_entity_ids) {
        set_entity_parent(id, 0);
    }
}

void StudioApp::snap_selected_to_surface() {
    if (m_selected_entity_ids.empty() && m_selected_entity_id >= 0) {
        m_selected_entity_ids.push_back(static_cast<uint32_t>(m_selected_entity_id));
    }
    if (m_selected_entity_ids.empty()) return;

    for (uint32_t id : m_selected_entity_ids) {
        Entity* ent = m_scene.find_entity(id);
        if (!ent || ent->is_group) continue;

        float wpos[3], wrot[3], wscale[3], wmat[16];
        ComputeWorldTransform(m_scene, *ent, wpos, wrot, wscale, wmat);

        float surface_y = 0.0f;
        if (m_scene.has_terrain) {
            surface_y = static_cast<float>(EngineContext::instance().terrain_get_height(wpos[0], wpos[2]));
        }

        float bottom_offset = ent->is_point_light ? 0.0f : (wscale[1] * 0.5f);
        wmat[13] = surface_y + bottom_offset;

        ComputeLocalTransform(m_scene, ent->parent_id, wmat, ent->position, ent->rotation, ent->scale);
    }
}

void StudioApp::align_selected_entities(int axis, int mode) {
    if (m_selected_entity_ids.size() < 2) return;
    if (axis < 0 || axis > 2) return;

    std::vector<std::pair<float, Entity*>> items;
    for (uint32_t id : m_selected_entity_ids) {
        Entity* e = m_scene.find_entity(id);
        if (!e) continue;
        float wpos[3], wrot[3], wscale[3];
        ComputeWorldTransform(m_scene, *e, wpos, wrot, wscale);
        items.push_back({ wpos[axis], e });
    }
    if (items.size() < 2) return;

    if (mode == 3) {
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        float min_val = items.front().first;
        float max_val = items.back().first;
        float step = (max_val - min_val) / static_cast<float>(items.size() - 1);
        for (size_t i = 1; i + 1 < items.size(); ++i) {
            Entity* ent = items[i].second;
            float target_val = min_val + step * static_cast<float>(i);
            float wpos[3], wrot[3], wscale[3], wmat[16];
            ComputeWorldTransform(m_scene, *ent, wpos, wrot, wscale, wmat);
            wmat[12 + axis] = target_val;
            ComputeLocalTransform(m_scene, ent->parent_id, wmat, ent->position, ent->rotation, ent->scale);
        }
        return;
    }

    float target_val = items.front().first;
    if (mode == 0) {
        for (const auto& item : items) target_val = std::min(target_val, item.first);
    } else if (mode == 1) {
        float sum = 0.0f;
        for (const auto& item : items) sum += item.first;
        target_val = sum / items.size();
    } else if (mode == 2) {
        for (const auto& item : items) target_val = std::max(target_val, item.first);
    }

    for (const auto& item : items) {
        Entity* ent = item.second;
        float wpos[3], wrot[3], wscale[3], wmat[16];
        ComputeWorldTransform(m_scene, *ent, wpos, wrot, wscale, wmat);
        wmat[12 + axis] = target_val;
        ComputeLocalTransform(m_scene, ent->parent_id, wmat, ent->position, ent->rotation, ent->scale);
    }
}

void StudioApp::auto_relink_missing_assets() {
    if (m_project.filepath.empty() && m_scene.filepath.empty()) {
        m_asset_message = "Save or open a project/scene first so the editor knows where to search for missing files.";
        m_asset_message_pending = true;
        return;
    }

    std::vector<std::filesystem::path> search_dirs;
    std::error_code ec;
    if (!m_project.filepath.empty()) {
        auto project_dir = std::filesystem::u8path(m_project.filepath).parent_path();
        if (std::filesystem::is_directory(project_dir, ec)) {
            search_dirs.push_back(project_dir);
            if (std::filesystem::is_directory(project_dir / "assets", ec)) search_dirs.push_back(project_dir / "assets");
            if (std::filesystem::is_directory(project_dir / "models", ec)) search_dirs.push_back(project_dir / "models");
            if (std::filesystem::is_directory(project_dir / "textures", ec)) search_dirs.push_back(project_dir / "textures");
        }
    }
    if (!m_scene.filepath.empty()) {
        auto scene_dir = std::filesystem::u8path(m_scene.filepath).parent_path();
        if (std::filesystem::is_directory(scene_dir, ec)) {
            search_dirs.push_back(scene_dir);
            if (std::filesystem::is_directory(scene_dir / "assets", ec)) search_dirs.push_back(scene_dir / "assets");
        }
    }

    size_t relinked_count = 0;
    for (auto& asset : m_assets) {
        if (std::filesystem::is_regular_file(std::filesystem::u8path(asset.path), ec)) continue;

        std::filesystem::path original_filename = std::filesystem::u8path(asset.path).filename();
        std::filesystem::path found_path;

        for (const auto& dir : search_dirs) {
            auto candidate = dir / original_filename;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                found_path = candidate;
                break;
            }
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file() && entry.path().filename() == original_filename) {
                        found_path = entry.path();
                        break;
                    }
                }
            } catch (...) {}
            if (!found_path.empty()) break;
        }

        if (!found_path.empty()) {
            std::string new_path = path_utf8(std::filesystem::absolute(found_path).lexically_normal());
            std::string old_path = asset.path;
            asset.path = new_path;

            uint16_t id = load_asset(new_path, asset.is_model);
            if (id) {
                asset.id = id;
                for (auto& e : m_scene.entities) {
                    if (asset.is_model && _stricmp(e.model_path.c_str(), old_path.c_str()) == 0) {
                        e.model_path = new_path;
                        e.mesh_id = id;
                    }
                    if (!asset.is_model && _stricmp(e.texture_path.c_str(), old_path.c_str()) == 0) {
                        e.texture_path = new_path;
                        e.texture_id = id;
                    }
                }
                ++relinked_count;
            }
        }
    }

    if (relinked_count > 0) {
        save_project_registry();
        m_asset_message = "Successfully relinked " + std::to_string(relinked_count) + " missing asset(s)!";
        m_asset_message_pending = true;
    } else {
        m_asset_message = "Could not find matching replacement files in the project or scene directory.";
        m_asset_message_pending = true;
    }
}

LRESULT StudioApp::handle_message(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                m_dropped_files.push_back(std::move(path));
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && LOWORD(lParam) > 64 && HIWORD(lParam) > 64) {
                m_window_width = LOWORD(lParam);
                m_window_height = HIWORD(lParam);
                auto* rw = EngineContext::instance().get_render_world();
                if (rw) rw->resize(m_window_width, m_window_height);
            }
            return 0;

        case WM_KEYDOWN:
            if (ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return 0;
            if ((GetKeyState(VK_RBUTTON) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000)) return 0;
            if (wParam == 'W') m_gizmo_operation = GizmoOperation::Translate;
            if (wParam == 'E') m_gizmo_operation = GizmoOperation::Rotate;
            if (wParam == 'R') m_gizmo_operation = GizmoOperation::Scale;
            if (wParam == 'P') {
                if (!m_simulating) start_simulation();
                else stop_simulation();
            }
            if (wParam == 'G' && !(GetKeyState(VK_CONTROL) & 0x8000)) snap_selected_to_surface();
            if (wParam == VK_DELETE) delete_selected_entity();
            if (wParam == VK_ESCAPE) deselect_all();
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wParam == 'S') save_scene();
                if (wParam == 'O' && confirm_scene_replacement()) open_scene_dialog();
                if (wParam == 'D') duplicate_selected_entity();
                if (wParam == 'N' && confirm_scene_replacement()) new_scene();
                if (wParam == 'I') import_model_dialog();
                if (wParam == 'Z') apply_history((GetKeyState(VK_SHIFT) & 0x8000) != 0);
                if (wParam == 'Y') apply_history(true);
                if (wParam == 'G') {
                    if (GetKeyState(VK_SHIFT) & 0x8000) unparent_selected_entities();
                    else group_selected_entities();
                }
                if (wParam == 'A') {
                    m_selected_entity_ids.clear();
                    for (const auto& e : m_scene.entities) m_selected_entity_ids.push_back(e.id);
                    if (!m_selected_entity_ids.empty()) m_selected_entity_id = static_cast<int32_t>(m_selected_entity_ids.back());
                }
            }
            return 0;

        case WM_CLOSE:
            request_close();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void StudioApp::shutdown() {
    if (m_simulating) {
        stop_simulation();
    }

    if (m_imgui_initialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imgui_initialized = false;
    }

    m_viewport_srv.Reset();
    m_viewport_rtv.Reset();
    m_viewport_tex.Reset();

    EngineContext::instance().shutdown();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

} // namespace GM3D::Studio
