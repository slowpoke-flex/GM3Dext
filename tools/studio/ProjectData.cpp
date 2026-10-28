#include "ProjectData.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "nlohmann/json.hpp"

namespace GM3D::Studio {
namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }
fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
}
bool ProjectSerializer::save(const ProjectData& project, std::string& error) {
    fs::path temporary;
    try {
        if (project.filepath.empty()) throw std::runtime_error("Project path is empty");
        const auto target = fs::absolute(from_utf8(project.filepath)).lexically_normal();
        nlohmann::ordered_json json = {{"format", "GM3DStudioProject"}, {"version", 1}, {"assets", nlohmann::ordered_json::array()}};
        if (!project.last_scene.empty()) {
            auto scene = fs::absolute(from_utf8(project.last_scene)).lexically_normal();
            auto relative = scene.lexically_relative(target.parent_path());
            json["last_scene"] = utf8(relative.empty() ? scene : relative);
        }
        for (const auto& asset : project.assets) {
            auto path = fs::absolute(from_utf8(asset.path)).lexically_normal();
            auto relative = path.lexically_relative(target.parent_path());
            json["assets"].push_back({{"path", utf8(relative.empty() ? path : relative)}, {"type", asset.is_model ? "model" : "texture"}});
        }
        const auto contents = json.dump(4);
        wchar_t name[MAX_PATH]{};
        if (!GetTempFileNameW(target.parent_path().c_str(), L"g3p", 0, name)) throw std::runtime_error("Cannot create temporary project file");
        temporary = fs::path(name);
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (out) out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        if (!out || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot write project file");
        error.clear(); return true;
    } catch (const std::exception& ex) {
        if (!temporary.empty()) { std::error_code ec; fs::remove(temporary, ec); }
        error = ex.what(); return false;
    }
}
bool ProjectSerializer::load(const std::string& path, ProjectData& project, std::string& error) {
    try {
        const auto target = fs::absolute(from_utf8(path)).lexically_normal();
        std::ifstream in(target, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open project file");
        nlohmann::json json; in >> json;
        if (!json.is_object() || json.value("format", "") != "GM3DStudioProject" || json.value("version", 0) != 1 || !json.contains("assets") || !json["assets"].is_array())
            throw std::runtime_error("Unsupported or invalid project format");
        ProjectData loaded; loaded.filepath = utf8(target);
        if (json.contains("last_scene")) {
            auto scene = from_utf8(json["last_scene"].get<std::string>());
            if (!scene.empty()) {
                if (scene.is_relative()) scene = target.parent_path() / scene;
                loaded.last_scene = utf8(scene.lexically_normal());
            }
        }
        for (const auto& item : json["assets"]) {
            const auto type = item.at("type").get<std::string>();
            const auto name = item.at("path").get<std::string>();
            if ((type != "model" && type != "texture") || name.empty()) throw std::runtime_error("Invalid asset entry");
            auto asset_path = from_utf8(name);
            if (asset_path.is_relative()) asset_path = target.parent_path() / asset_path;
            loaded.assets.push_back({utf8(asset_path.lexically_normal()), type == "model"});
        }
        project = std::move(loaded); error.clear(); return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
}
