#pragma once

#include <string>
#include <vector>

namespace GM3D::Studio {

// One DXGI adapter, as seen by D3D11CreateDevice when it is given an explicit adapter.
// The index is the EnumAdapters1 ordinal; the Studio stores it in settings.ini and passes it
// to RenderConfig::adapter_index at startup.
struct VideoAdapterInfo {
    int index = -1;
    std::string name;
    unsigned long long dedicated_memory_mb = 0;
    unsigned long long shared_memory_mb = 0;
    bool software = false;

    [[nodiscard]] std::string label() const;
};

// Enumerates the adapters in exactly the order RenderWorld indexes them. Returns an empty
// list when DXGI cannot be created; callers then simply keep the "Automatic" option.
std::vector<VideoAdapterInfo> enumerate_video_adapters();

} // namespace GM3D::Studio
