#include "VideoAdapters.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace GM3D::Studio {

std::string VideoAdapterInfo::label() const {
    std::string text = name.empty() ? "Unnamed adapter" : name;
    if (dedicated_memory_mb > 0) {
        text += " (" + std::to_string(dedicated_memory_mb) + " MB)";
    }
    if (software) text += " [software]";
    return text;
}

std::vector<VideoAdapterInfo> enumerate_video_adapters() {
    std::vector<VideoAdapterInfo> adapters;

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return adapters;
    }

    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) break;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;

        VideoAdapterInfo info;
        info.index = static_cast<int>(i);
        char name[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        info.name = name;
        info.dedicated_memory_mb = desc.DedicatedVideoMemory / (1024ull * 1024ull);
        info.shared_memory_mb = desc.SharedSystemMemory / (1024ull * 1024ull);
        info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        adapters.push_back(info);
    }

    return adapters;
}

} // namespace GM3D::Studio
