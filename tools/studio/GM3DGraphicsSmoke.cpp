#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
extern "C" __declspec(dllimport) double __cdecl init_extension(void*, double, void*);
extern "C" __declspec(dllimport) double __cdecl shutdown_extension();
extern "C" __declspec(dllimport) double __cdecl update_framework(double);
extern "C" __declspec(dllimport) double __cdecl terrain_generate(double, double, double, double);
extern "C" __declspec(dllimport) double __cdecl terrain_apply_edit_data(const void*, double);
extern "C" __declspec(dllimport) double __cdecl terrain_get_height(double, double);
extern "C" __declspec(dllimport) double __cdecl physics_raycast(double, double, double, double, double, double);
extern "C" __declspec(dllimport) double __cdecl physics_get_raycast_y();
int main() {
    const wchar_t* cls = L"GM3DGraphicsSmokeWindow";
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = cls;
    if (!RegisterClassW(&wc)) { std::cerr << "Window class registration failed\n"; return 1; }
    HWND window = CreateWindowW(cls, L"GM3D smoke", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) { std::cerr << "Hidden window creation failed\n"; UnregisterClassW(cls, wc.hInstance); return 1; }
    int result = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::array<std::byte, 128> buffer{};
        double initialized = init_extension(buffer.data(), static_cast<double>(buffer.size()), window);
        if (initialized != 1.0) { std::cerr << "Initialization failed: " << initialized << "\n"; result = 1; break; }
        if (attempt == 0) {
            if (terrain_generate(32, 240, 32, 1337) <= 0) { std::cerr << "Terrain generation failed\n"; result = 1; }
            const uint32_t counts[] = { 32 * 32, 32 * 32 * 4, 4 };
            std::vector<uint8_t> data(sizeof(counts) + counts[0] * sizeof(float) + counts[1] + counts[2] * 5 * sizeof(float), 0);
            std::memcpy(data.data(), counts, sizeof(counts));
            const float height = 7.0f;
            for (size_t i = 0; i < counts[0]; ++i) std::memcpy(data.data() + sizeof(counts) + i * sizeof(float), &height, sizeof(height));
            const size_t layers_offset = sizeof(counts) + counts[0] * sizeof(float) + counts[1];
            const float layer[] = { 0.2f, 0.4f, 0.1f, 0.8f, 1.0f };
            for (size_t i = 0; i < counts[2]; ++i) std::memcpy(data.data() + layers_offset + i * sizeof(layer), layer, sizeof(layer));
            if (terrain_apply_edit_data(data.data(), static_cast<double>(data.size())) <= 0 ||
                std::abs(terrain_get_height(0, 0) - height) > 0.001 ||
                physics_raycast(0, 30, 0, 0, -30, 0) <= 0 || std::abs(physics_get_raycast_y() - height) > 0.1) {
                std::cerr << "Terrain edit restore or collider update failed\n"; result = 1;
            }
            if (terrain_apply_edit_data(data.data(), static_cast<double>(data.size() - 1)) != -5.0 ||
                std::abs(terrain_get_height(0, 0) - height) > 0.001) {
                std::cerr << "Invalid terrain data was not rejected safely\n"; result = 1;
            }
        }
        double frame = update_framework(1.0 / 60.0);
        if (frame != 1.0) { std::cerr << "Frame failed: " << frame << "\n"; result = 1; }
        if (shutdown_extension() != 1.0) { std::cerr << "Shutdown failed\n"; result = 1; }
        if (result) break;
    }
    DestroyWindow(window); UnregisterClassW(cls, wc.hInstance);
    if (!result) std::cout << "Two hidden-window Direct3D initialization/render/shutdown cycles passed\n";
    return result;
}
