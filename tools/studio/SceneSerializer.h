#pragma once

#include "SceneData.h"
#include <string>

namespace GM3D::Studio {

class SceneSerializer {
public:
    static bool save_to_file(const Scene& scene, const std::string& filepath);
    static bool load_from_file(Scene& scene, const std::string& filepath);
};

} // namespace GM3D::Studio
