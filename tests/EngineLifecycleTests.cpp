#include <array>
#include <cstddef>
#include <iostream>

extern "C" __declspec(dllimport) double __cdecl init_extension(void*, double, void*);
extern "C" __declspec(dllimport) double __cdecl shutdown_extension();
int main() {
    if (init_extension(nullptr, 0, nullptr) != -2.0) {
        std::cerr << "Invalid buffer was not rejected\n"; return 1;
    }
    std::array<std::byte, 128> buffer{};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const double result = init_extension(buffer.data(), static_cast<double>(buffer.size()), nullptr);
        if (result != -1.0) { std::cerr << "Invalid window init returned " << result << "\n"; return 1; }
        if (shutdown_extension() != 1.0) { std::cerr << "Cleanup failed\n"; return 1; }
    }
    std::cout << "Invalid initialization failed without deadlock and can be retried\n";
    return 0;
}
