#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
struct Check { const char* source; const char* variable; const char* entry; const char* target; };
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: GM3DShaderCheck <project-root>\n"; return 2; }
    const std::filesystem::path root(argv[1]);
    const Check checks[] = {
        {"src/render/RenderWorld.cpp", "g_pbr_shader_hlsl", "VS_Main", "vs_5_0"},
        {"src/render/RenderWorld.cpp", "g_pbr_shader_hlsl", "PS_Main", "ps_5_0"},
        {"src/render/RenderWorld.cpp", "g_shadow_shader_hlsl", "VS_Shadow", "vs_5_0"},
        {"src/render/RenderWorld.cpp", "g_sky_shader_hlsl", "VS_Sky", "vs_5_0"},
        {"src/render/RenderWorld.cpp", "g_sky_shader_hlsl", "PS_Sky", "ps_5_0"},
        {"src/render/RenderWorld.cpp", "g_post_process_shader_hlsl", "VS_Fullscreen", "vs_5_0"},
        {"src/render/RenderWorld.cpp", "g_post_process_shader_hlsl", "PS_ExtractBloom", "ps_5_0"},
        {"src/render/RenderWorld.cpp", "g_post_process_shader_hlsl", "PS_BlurH", "ps_5_0"},
        {"src/render/RenderWorld.cpp", "g_post_process_shader_hlsl", "PS_BlurV", "ps_5_0"},
        {"src/render/RenderWorld.cpp", "g_post_process_shader_hlsl", "PS_Composite", "ps_5_0"},
        {"src/render/ParticleManager.cpp", "g_particle_shader_hlsl", "VS_Particle", "vs_5_0"},
        {"src/render/ParticleManager.cpp", "g_particle_shader_hlsl", "PS_Particle", "ps_5_0"}
    };
    int failures = 0;
    for (const auto& check : checks) {
        std::ifstream file(root / check.source, std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(file)), {});
        const std::string marker = std::string("static const char* ") + check.variable + " = R\"(";
        const auto begin = source.find(marker);
        const auto start = begin == std::string::npos ? begin : begin + marker.size();
        const auto end = start == std::string::npos ? start : source.find(")\";", start);
        if (!file || begin == std::string::npos || end == std::string::npos) {
            std::cerr << "ERROR " << check.source << ": cannot find " << check.variable << "\n"; ++failures; continue;
        }
        const std::string shader = source.substr(start, end - start);
        ComPtr<ID3DBlob> compiled, errors;
        const HRESULT result = D3DCompile(shader.data(), shader.size(), check.source, nullptr, nullptr,
            check.entry, check.target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0, compiled.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result)) {
            std::cerr << "ERROR " << check.variable << " " << check.entry << "\n";
            if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            ++failures;
        } else std::cout << "OK " << check.variable << " " << check.entry << "\n";
    }
    return failures ? 1 : 0;
}
