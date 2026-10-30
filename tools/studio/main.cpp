#include "StudioApp.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <shellapi.h>

#ifdef _WIN32
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
    if (arguments) {
        for (int i = 1; i < argument_count; ++i) {
            if (std::wstring(arguments[i]) == L"--log") {
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
                break;
            }
        }
        LocalFree(arguments);
    }

    int result = 1;
    {
        GM3D::Studio::StudioApp app;
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
