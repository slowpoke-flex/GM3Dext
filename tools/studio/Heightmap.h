#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace GM3D::Studio {
// Grayscale PFM stores IEEE floats in world units, bottom row first.
bool load_heightmap(const std::filesystem::path& path, uint32_t& size, std::vector<float>& heights, std::string& error);
bool save_heightmap(const std::filesystem::path& path, uint32_t size, const std::vector<float>& heights, std::string& error);
}
