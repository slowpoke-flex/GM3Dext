#include "StudioApp.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <shellapi.h>

#ifdef _WIN32
// Hybrid laptops (Optimus / AMD switchable graphics) route the process to the integrated
// GPU unless the executable asks for the discrete one. With the bitblt swap chain that
// meant a cross-adapter copy inside Present - measured at ~32 ms per frame on this
// machine, which is where the editor's "30 FPS with two objects" came from.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static std::string to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), length, nullptr, nullptr);
    return out;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Enable Per-Monitor V2 DPI awareness for crystal-clear text and proper window sizing
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Optional console for debugging output
#ifdef _DEBUG
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
#endif

    // Explicit opt-in logging works in release builds without opening a console.
    std::ofstream log;
    auto* old_cout = std::cout.rdbuf();
    auto* old_cerr = std::cerr.rdbuf();
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    int bench_frames = 0;
    bool bench_stress = false;
    std::string startup_scene;
    std::string screenshot_path;
    int capture_frames = 30;
    bool grass_demo = false;
    bool frame_profile = false;
    bool camera_override = false;
    float camera_values[6] = {};
    bool disable_shadows = false;
    bool disable_grass = false;
    int simulate_frames = 0;
    if (arguments) {
        for (int i = 1; i < argument_count; ++i) {
            const std::wstring arg = arguments[i];
            if (arg == L"--log") {
                if (++i >= argument_count) {
                    LocalFree(arguments);
                    MessageBoxW(nullptr, L"--log requires a file path.", L"GM3D Studio", MB_OK | MB_ICONERROR);
                    return 2;
                }
                log.open(std::filesystem::path(arguments[i]), std::ios::out | std::ios::app);
                if (!log) {
                    LocalFree(arguments);
                    MessageBoxW(nullptr, L"Could not open the requested log file.", L"GM3D Studio", MB_OK | MB_ICONERROR);
                    return 2;
                }
                std::cout.rdbuf(log.rdbuf());
                std::cerr.rdbuf(log.rdbuf());
                std::cout << "GM3D Studio starting\n" << std::flush;
            } else if (arg == L"--bench") {
                bench_frames = 240;
                if (i + 1 < argument_count) {
                    const wchar_t* next = arguments[i + 1];
                    if (next[0] >= L'0' && next[0] <= L'9') {
                        bench_frames = _wtoi(next);
                        ++i;
                    }
                }
                if (bench_frames < 31) bench_frames = 31;
            } else if (arg == L"--bench-stress") {
                bench_stress = true;
            } else if (arg == L"--scene" && i + 1 < argument_count) {
                startup_scene = to_utf8(arguments[++i]);
            } else if (arg == L"--screenshot" && i + 1 < argument_count) {
                screenshot_path = to_utf8(arguments[++i]);
            } else if (arg == L"--frames" && i + 1 < argument_count) {
                capture_frames = _wtoi(arguments[++i]);
                if (capture_frames < 1) capture_frames = 1;
            } else if (arg == L"--grass-demo") {
                grass_demo = true;
            } else if (arg == L"--camera" && i + 1 < argument_count) {
                // px,py,pz,tx,ty,tz - lets a script reproduce an exact view for screenshots.
                std::wstring spec(arguments[++i]);
                float values[6] = {};
                int parsed = 0;
                size_t start = 0;
                while (parsed < 6 && start <= spec.size()) {
                    const size_t comma = spec.find(L',', start);
                    const std::wstring token = spec.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
                    values[parsed++] = static_cast<float>(_wtof(token.c_str()));
                    if (comma == std::wstring::npos) break;
                    start = comma + 1;
                }
                if (parsed == 6) {
                    camera_override = true;
                    for (int c = 0; c < 6; ++c) camera_values[c] = values[c];
                }
            } else if (arg == L"--frame-profile") {
                frame_profile = true;
            } else if (arg == L"--no-shadows") {
                disable_shadows = true;
            } else if (arg == L"--no-grass") {
                disable_grass = true;
            } else if (arg == L"--simulate" && i + 1 < argument_count) {
                // Runs the Jolt preview headless: start the simulation, step N frames, print
                // where every body came to rest, exit. Used to check colliders numerically.
                simulate_frames = _wtoi(arguments[++i]);
                if (simulate_frames < 1) simulate_frames = 1;
            }
        }
        LocalFree(arguments);
    }

    int result = 1;
    {
        GM3D::Studio::StudioApp app;
        app.set_bench_frames(bench_frames);
        app.set_bench_stress(bench_stress);
        app.set_startup_scene(startup_scene);
        app.set_screenshot_path(screenshot_path);
        app.set_capture_frames(capture_frames);
        app.set_grass_demo(grass_demo);
        app.set_frame_profile(frame_profile);
        if (camera_override) app.set_startup_camera(camera_values);
        if (disable_shadows) app.set_disable_shadows(true);
        if (disable_grass) app.set_disable_grass(true);
        if (simulate_frames > 0) app.set_simulate_frames(simulate_frames);
        if (!app.initialize(hInstance, nCmdShow)) {
            std::cerr << "Failed to initialize GM3D Studio.\n";
            MessageBoxW(nullptr, L"Failed to initialize GM3D Studio!", L"Error", MB_ICONERROR);
        } else {
            result = app.run();
        }
    }
    if (log) { std::cout << "GM3D Studio exiting with code " << result << "\n"; std::cout.flush(); std::cerr.flush(); }
    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    return result;
}
#endif
