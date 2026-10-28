#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <iostream>
extern "C" __declspec(dllimport) double __cdecl init_extension(void*, double, void*);
extern "C" __declspec(dllimport) double __cdecl shutdown_extension();
extern "C" __declspec(dllimport) double __cdecl update_framework(double);
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
        double frame = update_framework(1.0 / 60.0);
        if (frame != 1.0) { std::cerr << "Frame failed: " << frame << "\n"; result = 1; }
        if (shutdown_extension() != 1.0) { std::cerr << "Shutdown failed\n"; result = 1; }
        if (result) break;
    }
    DestroyWindow(window); UnregisterClassW(cls, wc.hInstance);
    if (!result) std::cout << "Two hidden-window Direct3D initialization/render/shutdown cycles passed\n";
    return result;
}
