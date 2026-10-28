#pragma once
#include <string>
#include <vector>

namespace GM3D::Studio {
struct ProjectAsset {
    std::string path; // absolute UTF-8 path in memory
    bool is_model = false;
};
struct ProjectData {
    std::string filepath;
    std::string last_scene;
    std::vector<ProjectAsset> assets;
};
class ProjectSerializer {
public:
    static bool save(const ProjectData& project, std::string& error);
    static bool load(const std::string& path, ProjectData& project, std::string& error);
};
}
