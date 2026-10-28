#include "Diagnostics.h"
#include "SceneSerializer.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace GM3D::Studio;
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || (std::wstring(argv[1]) != L"scene" && std::wstring(argv[1]) != L"project")) {
        std::cerr << "Usage: GM3DCheck scene <scene.json> | project <project.gm3dproject>\n";
        return 2;
    }
    const auto u = std::filesystem::path(argv[2]).u8string();
    const std::string path(u.begin(), u.end());
    std::vector<Diagnostic> diagnostics;
    if (std::wstring(argv[1]) == L"scene") {
        Scene scene;
        if (!SceneSerializer::load_from_file(scene, path)) { std::cerr << "ERROR [parse] Could not load scene\n"; return 1; }
        diagnostics = validate_scene(scene);
        std::cout << "Scene: " << scene.name << "; entities: " << scene.entities.size() << "\n";
    } else {
        ProjectData project; std::string error;
        if (!ProjectSerializer::load(path, project, error)) { std::cerr << "ERROR [parse] " << error << "\n"; return 1; }
        diagnostics = validate_project(project);
        std::cout << "Project assets: " << project.assets.size() << "\n";
        if (!project.last_scene.empty()) {
            Scene scene;
            if (!SceneSerializer::load_from_file(scene, project.last_scene))
                diagnostics.push_back({true, "scene-parse", "Cannot load the project's last scene"});
            else {
                auto scene_errors = validate_scene(scene);
                diagnostics.insert(diagnostics.end(), scene_errors.begin(), scene_errors.end());
            }
        }
    }
    bool errors = false;
    for (const auto& item : diagnostics) {
        std::cout << (item.error ? "ERROR" : "WARNING") << " [" << item.code << "] " << item.detail << "\n";
        errors |= item.error;
    }
    if (diagnostics.empty()) std::cout << "OK\n";
    return errors ? 1 : 0;
}
