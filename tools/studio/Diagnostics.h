#pragma once
#include "SceneData.h"
#include "ProjectData.h"
#include <string>
#include <vector>

namespace GM3D::Studio {
struct Diagnostic {
    bool error;
    std::string code;
    std::string detail;
};
std::vector<Diagnostic> validate_scene(const Scene& scene);
std::vector<Diagnostic> validate_project(const ProjectData& project);
}
