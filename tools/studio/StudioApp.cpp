#include "StudioApp.h"
#include "StudioShared.h"
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
#include <shlobj.h>
#include <wincodec.h>
#include <filesystem>
#include <cmath>
#include <cctype>
#include <cwctype>
#include <iostream>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GM3D::Studio {

namespace {

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

    // 2. Create Window (adjust to desktop work area if screen is <= 1640x940)
    RECT work_area = { 0, 0, 1600, 900 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    LONG screen_w = work_area.right - work_area.left;
    LONG screen_h = work_area.bottom - work_area.top;

    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;
    int target_w = static_cast<int>(m_window_width);
    int target_h = static_cast<int>(m_window_height);

    if (screen_w <= 1640 || screen_h <= 940) {
        target_w = static_cast<int>(std::max<LONG>(1024, screen_w - 40));
        target_h = static_cast<int>(std::max<LONG>(680, screen_h - 60));
        posX = work_area.left + (screen_w - target_w) / 2;
        posY = work_area.top + (screen_h - target_h) / 2;
        m_window_width = static_cast<uint32_t>(target_w);
        m_window_height = static_cast<uint32_t>(target_h);
    }

    RECT rc = { 0, 0, target_w, target_h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"GM3D Studio - 3D World Editor",
        WS_OVERLAPPEDWINDOW, posX, posY,
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

    if (!m_startup_scene.empty()) {
        if (load_scene(m_startup_scene)) {
            std::cout << "Loaded startup scene: " << m_startup_scene << std::endl;
        } else {
            std::cerr << "Failed to load startup scene: " << m_startup_scene << std::endl;
        }
    }

    if (m_grass_demo) build_grass_demo();

    if (m_startup_camera_valid) {
        m_cam_pos[0] = m_startup_camera[0];
        m_cam_pos[1] = m_startup_camera[1];
        m_cam_pos[2] = m_startup_camera[2];
        m_cam_target[0] = m_startup_camera[3];
        m_cam_target[1] = m_startup_camera[4];
        m_cam_target[2] = m_startup_camera[5];
        sync_scene_to_engine();
    }

    if (m_disable_shadows) {
        // Same trap as grass: the per-frame sync restores these from the scene.
        m_scene.shadows_enabled = false;
        if (auto* rw = EngineContext::instance().get_render_world()) rw->set_shadow_settings(false, 0.0f, 2.0f, 110.0f);
    }
    if (m_disable_grass) {
        // The per-frame sync re-applies m_scene.grass_enabled, so clear it at the source.
        m_scene.grass_enabled = false;
        if (auto* rw = EngineContext::instance().get_render_world()) rw->set_grass_enabled(false);
    }

    if (m_bench_stress) build_bench_stress_scene();

    if (m_simulate_frames > 0) {
        sync_scene_to_engine();
        start_simulation();
    }

    if (m_bench_frames > 0) {
        if (auto* rw_bench = EngineContext::instance().get_render_world()) rw_bench->set_vsync(false);
        m_bench_frame = 0;
        m_bench_cpu_ms.clear();
        m_bench_total_ms.clear();
    }

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

        if (m_bench_frames > 0) {
            ++m_bench_frame;
            if (static_cast<int>(m_bench_total_ms.size()) >= m_bench_frames) {
                finish_bench();
                break;
            }
        }
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
                float wpos[3] = {
                    sed.px - e.collider_offset[0],
                    sed.py - e.collider_offset[1],
                    sed.pz - e.collider_offset[2]
                };
                float wrot[3];
                QuatToEuler(sed.qx, sed.qy, sed.qz, sed.qw, wrot[0], wrot[1], wrot[2]);
                if (e.parent_id == 0) {
                    e.position[0] = wpos[0]; e.position[1] = wpos[1]; e.position[2] = wpos[2];
                    e.rotation[0] = wrot[0]; e.rotation[1] = wrot[1]; e.rotation[2] = wrot[2];
                } else {
                    float old_wpos[3], old_wrot[3], old_wscale[3], wmat[16];
                    ComputeWorldTransform(m_scene, e, old_wpos, old_wrot, old_wscale, wmat);
                    ImGuizmo::RecomposeMatrixFromComponents(wpos, wrot, old_wscale, wmat);
                    ComputeLocalTransform(m_scene, e.parent_id, wmat, e.position, e.rotation, e.scale);
                }
            }
        }
    }

    sync_scene_to_engine();
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
        float lpos[3], lrot[3], lscale[3];
        ComputeWorldTransform(m_scene, entity, lpos, lrot, lscale);
        ctx.set_point_light(light_index++, lpos[0], lpos[1], lpos[2],
                            std::max(0.1f, entity.light_radius), entity.light_color[0], entity.light_color[1],
                            entity.light_color[2], std::max(0.0f, entity.light_intensity),
                            entity.cast_shadow ? 1.0 : 0.0);
    }
    if (auto* rw = ctx.get_render_world()) {
        rw->set_shadow_settings(m_scene.shadows_enabled, m_scene.shadow_strength, m_scene.shadow_softness, m_scene.shadow_coverage);
        rw->set_shadow_quality(m_scene.shadow_quality, m_scene.csm_enabled, m_scene.max_shadow_lights);
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
        for (const auto& entity : m_scene.entities) {
            if (entity.has_animator) {
                if (entity.anim_playing) {
                    rw->play_animation(entity.id, entity.anim_clip_index, entity.anim_speed, entity.anim_loop);
                } else {
                    rw->play_animation(entity.id, entity.anim_clip_index, 0.0f, entity.anim_loop);
                    rw->set_animation_time(entity.id, entity.anim_time);
                }
            } else {
                rw->stop_animation(entity.id);
            }
        }
    }
    ctx.set_sun_light(lx, ly, lz, m_scene.sun_color[0], m_scene.sun_color[1], m_scene.sun_color[2], m_scene.sun_intensity);
    ctx.set_ambient_light(m_scene.ambient_color[0], m_scene.ambient_color[1], m_scene.ambient_color[2], m_scene.ambient_intensity);
    ctx.set_sky_color(m_scene.sky_color[0], m_scene.sky_color[1], m_scene.sky_color[2]);
    ctx.set_fog(m_scene.fog_start, m_scene.fog_end, m_scene.sky_color[0], m_scene.sky_color[1], m_scene.sky_color[2]);
}

void StudioApp::render() {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;

    LARGE_INTEGER prof_freq{}, prof_mark{};
    const bool prof = m_frame_profile;
    if (prof) {
        QueryPerformanceFrequency(&prof_freq);
        QueryPerformanceCounter(&prof_mark);
    }
    auto mark = [&]() -> float {
        if (!prof) return 0.0f;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float ms = static_cast<float>((now.QuadPart - prof_mark.QuadPart) * 1000.0
                                            / static_cast<double>(prof_freq.QuadPart));
        prof_mark = now;
        return ms;
    };

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
        std::wstring scene_display_name;
        if (!m_scene.filepath.empty()) {
            scene_display_name = std::filesystem::u8path(m_scene.filepath).filename().wstring();
        } else {
            scene_display_name = L"Untitled Scene";
        }
        std::wstring new_title = L"GM3D Studio - [" + scene_display_name + (m_history.dirty(m_scene) ? L" *]" : L"]");
        static std::wstring s_last_title;
        if (new_title != s_last_title) {
            s_last_title = new_title;
            SetWindowTextW(m_hwnd, new_title.c_str());
        }
    }

    // 4. Sync latest transformed entity data to shared buffer
    FrameSample sample;
    if (prof) sample.ui = mark();
    sync_scene_to_engine();
    if (prof) sample.sync = mark();

    // 5. Render 3D Scene into Viewport Render Target with real delta_time
    if (m_viewport_rtv) {
        rw->set_custom_render_target(m_viewport_rtv.Get());
        LARGE_INTEGER bench_freq{}, bench_t0{}, bench_t1{}, bench_t2{};
        const bool bench = m_bench_frames > 0;
        if (bench) {
            QueryPerformanceFrequency(&bench_freq);
            QueryPerformanceCounter(&bench_t0);
        }
        EngineContext::instance().update(m_delta_time);
        if (bench) {
            QueryPerformanceCounter(&bench_t1);
            rw->get_context()->Flush();
            QueryPerformanceCounter(&bench_t2);
            if (m_bench_frame >= 30) {
                const double scale = 1000.0 / static_cast<double>(bench_freq.QuadPart);
                m_bench_cpu_ms.push_back(static_cast<float>((bench_t1.QuadPart - bench_t0.QuadPart) * scale));
                m_bench_total_ms.push_back(static_cast<float>((bench_t2.QuadPart - bench_t0.QuadPart) * scale));
            }
        }
        rw->set_custom_render_target(nullptr);
    }
    if (prof) sample.scene = mark();

    // 6. Render ImGui onto Main Window Swap Chain Backbuffer
    ImGui::Render();

    ID3D11RenderTargetView* back_rtv = rw->get_back_buffer_rtv();
    rw->get_context()->OMSetRenderTargets(1, &back_rtv, nullptr);
    const float clear_color[4] = { 0.07f, 0.08f, 0.10f, 1.0f };
    rw->get_context()->ClearRenderTargetView(back_rtv, clear_color);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (prof) sample.imgui = mark();

    if (!m_screenshot_path.empty() && ++m_capture_frame >= std::max(1, m_capture_frames)) {
        capture_backbuffer(m_screenshot_path);
        m_screenshot_path.clear();
        m_running = false;
    }

    // --simulate N: run the physics simulation headless for N frames, then print where the
    // bodies came to rest so a collider can be checked without looking at a picture.
    if (m_simulate_frames > 0 && ++m_simulate_frame >= m_simulate_frames) {
        report_simulation();
        m_running = false;
    }

    rw->get_swap_chain()->Present(m_bench_frames > 0 ? 0 : 1, 0);
    if (prof) {
        sample.present = mark();
        sample.total = sample.ui + sample.sync + sample.scene + sample.imgui + sample.present;
        m_prof_samples.push_back(sample);
        report_frame_profile();
    }
}

void StudioApp::apply_history(bool redo) {
    if (m_simulating || m_terrain_stroke) return;
    refresh_terrain_albedo();
    m_history.observe(m_scene);
    const Scene previous = m_scene;
    if (!(redo ? m_history.redo(m_scene) : m_history.undo(m_scene))) return;
    if (previous.has_terrain != m_scene.has_terrain || previous.terrain_grid_size != m_scene.terrain_grid_size ||
        previous.terrain_world_size != m_scene.terrain_world_size || previous.terrain_height_scale != m_scene.terrain_height_scale ||
        previous.terrain_seed != m_scene.terrain_seed || previous.terrain_heights != m_scene.terrain_heights ||
        previous.terrain_layers != m_scene.terrain_layers || previous.terrain_splatmap != m_scene.terrain_splatmap) {
        auto* rw = EngineContext::instance().get_render_world();
        auto* pw = EngineContext::instance().get_physics_world();
        const bool same_config = previous.has_terrain && m_scene.has_terrain &&
            previous.terrain_grid_size == m_scene.terrain_grid_size && previous.terrain_world_size == m_scene.terrain_world_size &&
            previous.terrain_height_scale == m_scene.terrain_height_scale && previous.terrain_seed == m_scene.terrain_seed;
        const bool heights_changed = (previous.terrain_heights != m_scene.terrain_heights);
        if (same_config && rw && pw && (!heights_changed || (!m_scene.terrain_heights.empty() &&
            rw->get_terrain_system().replace_heights(m_scene.terrain_heights)))) {
            sync_terrain_layers();
            rw->get_terrain_system().set_splatmap(m_scene.terrain_splatmap);
            const auto n = m_scene.terrain_grid_size;
            const float cell = m_scene.terrain_world_size / (n - 1), half = m_scene.terrain_world_size * 0.5f;
            uint32_t min_x = n, min_z = n, max_x = 0, max_z = 0;
            if (heights_changed && previous.terrain_heights.size() == m_scene.terrain_heights.size()) {
                for (uint32_t z = 0; z < n; ++z) for (uint32_t x = 0; x < n; ++x) {
                    const size_t i = static_cast<size_t>(z) * n + x;
                    if (previous.terrain_heights[i] != m_scene.terrain_heights[i]) {
                        min_x = std::min(min_x, x); min_z = std::min(min_z, z);
                        max_x = std::max(max_x, x); max_z = std::max(max_z, z);
                    }
                }
            }
            if (heights_changed) {
                if (min_x < n) {
                    rw->update_terrain_mesh_region(min_x * cell - half, min_z * cell - half, max_x * cell - half, max_z * cell - half);
                } else {
                    rw->update_terrain_mesh();
                }
                rw->get_terrain_system().update_collider(*pw);
            }
            if (previous.terrain_splatmap != m_scene.terrain_splatmap &&
                previous.terrain_splatmap.size() == m_scene.terrain_splatmap.size() &&
                m_scene.terrain_splatmap.size() == static_cast<size_t>(n) * n * 4) {
                for (uint32_t z = 0; z < n; ++z) for (uint32_t x = 0; x < n; ++x) {
                    const size_t base = (static_cast<size_t>(z) * n + x) * 4;
                    if (previous.terrain_splatmap[base + 0] != m_scene.terrain_splatmap[base + 0] ||
                        previous.terrain_splatmap[base + 1] != m_scene.terrain_splatmap[base + 1] ||
                        previous.terrain_splatmap[base + 2] != m_scene.terrain_splatmap[base + 2] ||
                        previous.terrain_splatmap[base + 3] != m_scene.terrain_splatmap[base + 3]) {
                        min_x = std::min(min_x, x); min_z = std::min(min_z, z);
                        max_x = std::max(max_x, x); max_z = std::max(max_z, z);
                    }
                }
            }
            if (min_x < n && previous.terrain_layers == m_scene.terrain_layers) {
                rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(),
                    min_x * cell - half, min_z * cell - half, max_x * cell - half, max_z * cell - half);
            } else {
                rw->get_terrain_system().update_albedo(rw->get_device(), rw->get_context(), rw->get_texture_manager(), -half, -half, half, half);
            }
            m_terrain_albedo_dirty = false;
        } else rebuild_terrain();
    }
    if (m_selected_entity_id >= 0 && !selected_entity()) m_selected_entity_id = -1;
    sync_scene_to_engine();
}

void StudioApp::request_close() {
    if (confirm_scene_replacement()) m_running = false;
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
