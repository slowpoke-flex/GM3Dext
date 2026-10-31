// Settings panel and the small persistent preferences file behind it.
//
// The Studio keeps user-level preferences (not scene data) in
// %APPDATA%\GM3DStudio\settings.ini: which DXGI adapter to render on, vsync, a frame-rate
// limit, shadow/grass/bloom overrides for the viewport, and the fly-camera speed.
// Scene-level render settings stay in the scene file where the exporter can see them.
#include "StudioApp.h"
#include "StudioShared.h"
#include "core/EngineContext.h"
#include "render/RenderWorld.h"

#include "imgui.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace GM3D::Studio {

namespace {

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool parse_bool(const std::string& value, bool fallback) {
    if (value == "1" || value == "true" || value == "yes") return true;
    if (value == "0" || value == "false" || value == "no") return false;
    return fallback;
}

} // namespace

void StudioApp::load_settings() {
    if (m_settings_path.empty()) {
        std::string base;
        if (const char* appdata = std::getenv("APPDATA")) base = appdata;
        if (base.empty()) base = std::filesystem::current_path().string();
        m_settings_path = (std::filesystem::path(base) / "GM3DStudio" / "settings.ini").string();
    }

    std::ifstream input(m_settings_path);
    if (!input) return;

    GraphicsSettings loaded;   // defaults, so a partial file is still valid
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        try {
            if (key == "adapter_index") loaded.adapter_index = std::stoi(value);
            else if (key == "vsync") loaded.vsync = parse_bool(value, loaded.vsync);
            else if (key == "fps_limit") loaded.fps_limit = std::clamp(std::stoi(value), 0, 480);
            else if (key == "shadow_override") loaded.shadow_override = std::clamp(std::stoi(value), -1, 3);
            else if (key == "show_grass") loaded.show_grass = parse_bool(value, loaded.show_grass);
            else if (key == "bloom") loaded.bloom = parse_bool(value, loaded.bloom);
            else if (key == "camera_speed") loaded.camera_speed = std::clamp(std::stof(value), 1.0f, 120.0f);
        } catch (const std::exception&) {
            // A malformed line only costs that one setting; the rest still load.
        }
    }
    m_graphics = loaded;
}

void StudioApp::save_settings() {
    if (m_settings_path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_settings_path).parent_path(), ec);

    std::ofstream output(m_settings_path, std::ios::trunc);
    if (!output) {
        std::cout << "[Settings] Could not write " << m_settings_path << "\n";
        return;
    }
    output << "# GM3D Studio preferences. Scene render settings live in the scene file.\n";
    output << "[graphics]\n";
    output << "adapter_index=" << m_graphics.adapter_index << "\n";
    output << "vsync=" << (m_graphics.vsync ? 1 : 0) << "\n";
    output << "fps_limit=" << m_graphics.fps_limit << "\n";
    output << "shadow_override=" << m_graphics.shadow_override << "\n";
    output << "show_grass=" << (m_graphics.show_grass ? 1 : 0) << "\n";
    output << "bloom=" << (m_graphics.bloom ? 1 : 0) << "\n";
    output << "camera_speed=" << m_graphics.camera_speed << "\n";
}

void StudioApp::apply_graphics_settings() {
    auto* rw = EngineContext::instance().get_render_world();
    if (!rw) return;
    rw->set_vsync(m_graphics.vsync);
    rw->set_bloom(m_graphics.bloom, 0.85f, 1.0f);
    rw->set_grass_enabled(m_graphics.show_grass && m_scene.grass_enabled);
    m_cam_speed = m_graphics.camera_speed;
}

void StudioApp::reset_graphics_settings() {
    m_graphics = GraphicsSettings{};
    m_cam_speed = m_graphics.camera_speed;
    apply_graphics_settings();
    save_settings();
}

void StudioApp::restart_editor() {
    // The adapter is chosen when the D3D11 device is created, so switching GPUs relaunches the
    // editor instead of tearing the render world down under ImGui and the asset registry.
    save_settings();

    wchar_t exe_path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return;

    std::wstring command_line = GetCommandLineW();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe_path, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process)) {
        MessageBoxW(m_hwnd, L"Could not start a new GM3D Studio process.", L"Restart failed",
                    MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    m_running = false;
    PostQuitMessage(0);
}

void StudioApp::draw_settings_panel() {
    if (!m_show_settings) return;

    if (m_settings_focus) {
        m_settings_focus = false;
        ImGui::SetNextWindowFocus();
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Settings", &m_show_settings)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    ImGui::SeparatorText("Graphics");

    // --- Video adapter ------------------------------------------------------------------
    std::vector<std::string> labels;
    labels.reserve(m_video_adapters.size() + 1);
    labels.emplace_back("Automatic (system default)");
    for (const auto& adapter : m_video_adapters) labels.push_back(adapter.label());

    int selected = m_graphics.adapter_index + 1;   // combo index 0 = automatic
    if (selected < 0 || selected >= static_cast<int>(labels.size())) selected = 0;

    ImGui::TextUnformatted("Video adapter");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##VideoAdapter", labels[selected].c_str())) {
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            if (ImGui::Selectable(labels[i].c_str(), selected == i)) {
                m_graphics.adapter_index = i - 1;
                changed = true;
            }
            if (selected == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    HelpTooltip("DXGI adapter used for the editor viewport. On hybrid laptops this is the "
                "difference between the discrete GPU and a cross-adapter copy inside Present.");

    ImGui::TextDisabled("In use: %s",
                        m_applied_adapter_name.empty() ? "unknown" : m_applied_adapter_name.c_str());

    if (m_graphics.adapter_index != m_applied_adapter_index) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
                           "The adapter is chosen when the device is created - restart to apply.");
        if (ImGui::Button("Restart editor", ImVec2(-1.0f, 28.0f))) {
            if (confirm_scene_replacement()) restart_editor();   // restart_editor saves the settings
        }
        HelpTooltip("Saves the scene if you ask for it, writes settings.ini and starts a new "
                    "GM3D Studio process with the same command line.");
    }

    ImGui::Spacing();

    // --- Frame pacing -------------------------------------------------------------------
    if (ImGui::Checkbox("V-Sync", &m_graphics.vsync)) changed = true;
    HelpTooltip("Presents the editor window with a sync interval of 1. Turning it off lets the "
                "frame-rate limit below (or the GPU) decide the pace.");

    ImGui::TextUnformatted("Frame rate limit");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##FpsLimit", &m_graphics.fps_limit, 0, 240, m_graphics.fps_limit == 0 ? "Off" : "%d FPS")) {
        changed = true;
    }
    HelpTooltip("Sleeps the main loop when a frame finished early. 0 disables the limiter; "
                "V-Sync can still cap the frame rate at the display refresh.");

    ImGui::Spacing();

    // --- Quality overrides --------------------------------------------------------------
    static const char* kShadowModes[] = { "Use scene setting", "Off", "Low (1024)", "Medium (2048)", "High (4096)" };
    int shadow_mode = std::clamp(m_graphics.shadow_override + 1, 0, 4);
    ImGui::TextUnformatted("Shadows");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##ShadowOverride", &shadow_mode, kShadowModes, IM_ARRAYSIZE(kShadowModes))) {
        m_graphics.shadow_override = shadow_mode - 1;
        changed = true;
    }
    HelpTooltip("Preview override for the sun and point-light shadows. 'Use scene setting' "
                "leaves the value authored in the scene (and exported to GameMaker) alone.");

    if (ImGui::Checkbox("Show grass", &m_graphics.show_grass)) {
        changed = true;
        apply_graphics_settings();
    }
    HelpTooltip("Hides the procedural/painted carpet without touching the scene. Rebuilds "
                "terrain or reloads the scene honour this toggle.");

    if (ImGui::Checkbox("Bloom", &m_graphics.bloom)) {
        changed = true;
        apply_graphics_settings();
    }
    HelpTooltip("HDR bloom pass (bright pass, two-pass blur, ACES tonemap keeps the result).");

    ImGui::Spacing();

    // --- Editor -------------------------------------------------------------------------
    ImGui::SeparatorText("Editor");
    ImGui::TextUnformatted("Fly camera speed");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##CameraSpeed", &m_graphics.camera_speed, 2.0f, 80.0f, "%.1f m/s")) {
        m_cam_speed = m_graphics.camera_speed;
        changed = true;
    }
    HelpTooltip("Metres per second while holding the right mouse button. Shift still multiplies it by 2.5.");

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset to defaults", ImVec2(160.0f, 28.0f))) {
        reset_graphics_settings();
        changed = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Settings: %s", m_settings_path.c_str());

    if (changed) save_settings();

    ImGui::End();
}

} // namespace GM3D::Studio
