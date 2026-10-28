#include "Heightmap.h"
#include <windows.h>
#include <bit>
#include <cmath>
#include <fstream>
#include <sstream>

namespace GM3D::Studio {
namespace {
bool finite_bits(float value) { return (std::bit_cast<uint32_t>(value) & 0x7f800000u) != 0x7f800000u; }
bool valid_size(uint32_t n) { return n >= 32 && n <= 512; }
bool read_line(std::istream& stream, std::string& line) {
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') return true;
    }
    return false;
}
}

bool load_heightmap(const std::filesystem::path& path, uint32_t& size, std::vector<float>& heights, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    std::string line, extra;
    unsigned width = 0, height = 0;
    float scale = 0;
    if (!file || !read_line(file, line) || line != "Pf" || !read_line(file, line)) {
        error = "Expected a grayscale PFM file."; return false;
    }
    std::istringstream dimensions(line);
    if (!(dimensions >> width >> height) || (dimensions >> extra) || width != height || !valid_size(width) ||
        !read_line(file, line)) {
        error = "Heightmap must be square, between 32 and 512 pixels."; return false;
    }
    std::istringstream scale_line(line);
    if (!(scale_line >> scale) || (scale_line >> extra) || !finite_bits(scale) || scale == 0.0f) {
        error = "Invalid PFM scale."; return false;
    }
    std::vector<float> result(static_cast<size_t>(width) * height);
    const bool little_endian = scale < 0;
    const float multiplier = std::fabs(scale);
    for (uint32_t row = height; row-- > 0;) {
        for (uint32_t x = 0; x < width; ++x) {
            unsigned char b[4];
            if (!file.read(reinterpret_cast<char*>(b), 4)) { error = "Heightmap data is truncated."; return false; }
            const uint32_t bits = little_endian
                ? uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24
                : uint32_t(b[3]) | uint32_t(b[2]) << 8 | uint32_t(b[1]) << 16 | uint32_t(b[0]) << 24;
            float value = std::bit_cast<float>(bits);
            if (!finite_bits(value)) { error = "Heightmap contains a non-finite height."; return false; }
            value *= multiplier;
            if (!finite_bits(value)) { error = "Heightmap height is out of range."; return false; }
            result[static_cast<size_t>(row) * width + x] = value;
        }
    }
    size = width; heights = std::move(result); error.clear(); return true;
}

bool save_heightmap(const std::filesystem::path& path, uint32_t size, const std::vector<float>& heights, std::string& error) {
    if (!valid_size(size) || heights.size() != static_cast<size_t>(size) * size) { error = "Invalid terrain dimensions."; return false; }
    for (float value : heights) if (!finite_bits(value)) { error = "Terrain contains a non-finite height."; return false; }
    std::error_code ec;
    const auto dest = std::filesystem::absolute(path, ec);
    if (ec) { error = "Invalid destination path."; return false; }
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(dest.parent_path().c_str(), L"g3h", 0, temporary)) { error = "Could not create a temporary file."; return false; }
    const std::filesystem::path temp(temporary);
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << "Pf\n" << size << ' ' << size << "\n-1.0\n";
    for (uint32_t row = size; row-- > 0;) {
        for (uint32_t x = 0; x < size; ++x) {
            const uint32_t bits = std::bit_cast<uint32_t>(heights[static_cast<size_t>(row) * size + x]);
            const char b[] = { static_cast<char>(bits), static_cast<char>(bits >> 8), static_cast<char>(bits >> 16), static_cast<char>(bits >> 24) };
            file.write(b, 4);
        }
    }
    file.close();
    if (!file || !MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp, ec); error = "Could not write the heightmap."; return false;
    }
    error.clear(); return true;
}
}
