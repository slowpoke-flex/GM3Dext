#include "SceneSerializer.h"
#include "TerrainBrush.h"
#include "SceneHistory.h"
#include "ProjectData.h"
#include "ViewportMath.h"
#include "TransformMath.h"
#include "Diagnostics.h"
#include "Heightmap.h"
#include "../src/render/AnimationData.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace GM3D::Studio;
namespace fs = std::filesystem;

int main() {
    std::cout << "TEST 0: Main entry\n" << std::flush;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = fs::temp_directory_path() / ("gm3d_scene_test_" + std::to_string(stamp));
    fs::create_directories(dir);
    const auto file = dir / "scene.json";
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    auto write = [&](const char* text) { std::ofstream(file) << text; };
    check(GM3D::animation_sample_time(2.5f, 2.0f, true) == 0.5f &&
          GM3D::animation_sample_time(2.5f, 2.0f, false) == 2.0f &&
          GM3D::animation_sample_time(-0.5f, 2.0f, true) == 1.5f &&
          GM3D::animation_sample_time(-0.5f, 2.0f, false) == 0.0f,
          "animation loop and clamp sampling failed");
    {
        GM3D::SkeletonData skeleton;
        skeleton.joints.resize(129);
        std::vector<DirectX::XMFLOAT4X4> matrices;
        skeleton.evaluate_pose(nullptr, 0.0f, matrices);
        check(matrices.size() >= skeleton.joints.size(), "large skeleton pose buffer is too small");
    }

    {
        const auto export_dir = dir / "export";
        fs::create_directories(export_dir);
        std::ofstream(export_dir / "scene_gm3d.json") << "existing package";
        Scene scene;
        scene.has_terrain = false;
        Entity e; e.id = 1; e.model_path = (dir / "missing.obj").string();
        scene.entities.push_back(e);
        std::string summary;
        check(!SceneSerializer::export_for_gamemaker(scene, export_dir.string(), summary) &&
              summary.find("Missing resource") != std::string::npos, "missing export asset was not reported");
        std::ifstream existing(export_dir / "scene_gm3d.json");
        check(std::string((std::istreambuf_iterator<char>(existing)), {}) == "existing package",
              "missing asset damaged existing export");
        existing.close();
        const auto gltf = dir / "external.gltf";
        std::ofstream(gltf) << R"({"buffers":[{"uri":"mesh.bin"}]})";
        scene.entities[0].model_path = gltf.string();
        check(!SceneSerializer::export_for_gamemaker(scene, export_dir.string(), summary) &&
              summary.find("external resources") != std::string::npos, "external glTF dependency was silently omitted");
        const auto model = dir / "model.obj";
        std::ofstream(model) << "# test model\n";
        scene.entities[0].model_path = model.string();
        check(SceneSerializer::export_for_gamemaker(scene, export_dir.string(), summary), "valid export failed");
        Scene exported;
        check(SceneSerializer::load_from_file(exported, (export_dir / "scene_gm3d.json").string()) &&
              exported.entities.size() == 1 && fs::exists(fs::u8path(exported.entities[0].model_path)),
              "exported asset reference did not resolve");
        scene.has_terrain = true;
        scene.terrain_grid_size = 32;
        scene.terrain_heights.assign(32 * 32, 7.0f);
        scene.terrain_splatmap.assign(32 * 32 * 4, 42);
        scene.terrain_layers[0].color[0] = 0.73f;
        check(SceneSerializer::export_for_gamemaker(scene, export_dir.string(), summary), "terrain package export failed");
        check(std::distance(fs::directory_iterator(export_dir / "assets"), fs::directory_iterator{}) == 1,
              "repeated export duplicated unchanged assets");
        const auto other_dir = dir / "other";
        fs::create_directories(other_dir);
        const auto other_model = other_dir / "model.obj";
        std::ofstream(other_model) << "# different model\n";
        Entity second; second.id = 2; second.model_path = other_model.string();
        scene.entities.push_back(second);
        check(SceneSerializer::export_for_gamemaker(scene, export_dir.string(), summary) &&
              SceneSerializer::load_from_file(exported, (export_dir / "scene_gm3d.json").string()) &&
              exported.entities.size() == 2 && exported.entities[0].model_path != exported.entities[1].model_path &&
              std::distance(fs::directory_iterator(export_dir / "assets"), fs::directory_iterator{}) == 2,
              "different assets sharing a name collided during export");
        scene.entities.pop_back();
        check(SceneSerializer::load_from_file(exported, (export_dir / "scene_gm3d.json").string()) &&
              exported.terrain_heights == scene.terrain_heights && exported.terrain_splatmap == scene.terrain_splatmap &&
              exported.terrain_layers == scene.terrain_layers, "terrain package lost sculpting or painting data");
        std::ifstream script(export_dir / "scr_gm3d_load_scene.gml", std::ios::binary);
        const std::string loader((std::istreambuf_iterator<char>(script)), {});
        check(loader.find("terrain_apply_edit_data(buffer_get_address(_terrain_buf), _terrain_bytes)") != std::string::npos &&
              loader.find("_terrain_inst.mesh_id = GM3D_MESH_TERRAIN") != std::string::npos &&
              loader.find("buffer_seek(_buf, buffer_seek_end, 0)") != std::string::npos,
              "export loader is missing terrain restore, visible terrain, or JSON terminator");
        scene.terrain_layers.resize(1);
        check(SceneSerializer::export_for_gamemaker(scene, export_dir.string(), summary) &&
              SceneSerializer::load_from_file(exported, (export_dir / "scene_gm3d.json").string()) &&
              exported.terrain_layers.size() == 4 && exported.terrain_layers[0] == scene.terrain_layers[0] &&
              exported.terrain_layers[1] == Scene{}.terrain_layers[1],
              "export did not preserve partial terrain layers with default fallbacks");
    }

    {
        const auto map = dir / "terrain.pfm";
        std::vector<float> heights(32 * 32);
        for (size_t i = 0; i < heights.size(); ++i) heights[i] = static_cast<float>(i) * 0.125f - 20.0f;
        std::string error;
        check(save_heightmap(map, 32, heights, error), "heightmap save failed");
        uint32_t size = 0; std::vector<float> loaded_heights;
        check(load_heightmap(map, size, loaded_heights, error) && size == 32 && loaded_heights == heights,
              "heightmap round trip changed heights or orientation");
        std::ifstream before(map, std::ios::binary);
        const std::string previous((std::istreambuf_iterator<char>(before)), {});
        check(!save_heightmap(map, 33, heights, error), "invalid heightmap export succeeded");
        std::ifstream after(map, std::ios::binary);
        check(std::string((std::istreambuf_iterator<char>(after)), {}) == previous, "failed heightmap export damaged file");
        std::ofstream bad(map, std::ios::binary | std::ios::trunc);
        bad << "Pf\n32 32\n-1.0\n"; bad.close();
        size = 47; loaded_heights = { 123.0f };
        check(!load_heightmap(map, size, loaded_heights, error) && size == 47 && loaded_heights == std::vector<float>{123.0f},
              "truncated heightmap import changed output");
        std::ofstream big(map, std::ios::binary | std::ios::trunc);
        big << "Pf\n32 32\n2.0\n";
        const char one[] = { char(0x3f), char(0x80), 0, 0 };
        for (int i = 0; i < 1024; ++i) big.write(one, 4);
        big.close();
        check(load_heightmap(map, size, loaded_heights, error) && size == 32 && loaded_heights[0] == 2.0f,
              "big-endian PFM scale was not applied");
        std::ofstream invalid(map, std::ios::binary | std::ios::trunc);
        invalid << "Pf\n32 32\n-1.0\n";
        const char nan[] = { 0, 0, char(0xc0), char(0x7f) }; invalid.write(nan, 4); invalid.close();
        check(!load_heightmap(map, size, loaded_heights, error), "non-finite height accepted");
        std::ofstream color(map, std::ios::binary | std::ios::trunc); color << "PF\n32 32\n-1.0\n"; color.close();
        check(!load_heightmap(map, size, loaded_heights, error), "color PFM incorrectly accepted");
    }

    Scene original;
    original.name = "Round trip";
    original.sun_color[1] = 0.25f;
    original.has_terrain = false;
    original.shadows_enabled = false;
    original.shadow_strength = 0.42f;
    original.shadow_softness = 3.0f;
    original.shadow_coverage = 72.0f;
    original.terrain_grid_size = 32;
    original.terrain_heights.assign(32 * 32, 2.5f);
    Entity entity;
    entity.id = 42;
    entity.position[2] = 12.0f;
    entity.texture_id = 5;
    entity.editor_grid = true;
    entity.is_point_light = true;
    entity.light_color[0] = 0.3f;
    entity.light_intensity = 80.0f;
    entity.light_radius = 20.0f;
    const auto model_path = dir / "assets" / "model.obj";
    const auto texture_path = dir / "assets" / "texture.png";
    entity.model_path = model_path.string();
    entity.texture_path = texture_path.string();
    original.entities.push_back(entity);
    check(SceneSerializer::save_to_file(original, file.string()), "save failed");
    Scene loaded;
    check(SceneSerializer::load_from_file(loaded, file.string()), "round-trip load failed");
    check(loaded.name == original.name && loaded.sun_color[1] == 0.25f && !loaded.has_terrain &&
          loaded.entities.size() == 1 && loaded.entities[0].position[2] == 12.0f &&
          loaded.entities[0].texture_id == 5 && loaded.entities[0].editor_grid && loaded.next_entity_id == 43 &&
          !loaded.shadows_enabled && loaded.shadow_strength == 0.42f && loaded.shadow_softness == 3.0f && loaded.shadow_coverage == 72.0f &&
          loaded.entities[0].is_point_light && loaded.entities[0].light_color[0] == 0.3f &&
          loaded.entities[0].light_intensity == 80.0f && loaded.entities[0].light_radius == 20.0f &&
          loaded.terrain_heights.size() == 1024 && loaded.terrain_heights[100] == 2.5f &&
          fs::path(loaded.entities[0].model_path) == model_path && fs::path(loaded.entities[0].texture_path) == texture_path, "round-trip data mismatch");

    {
        std::ifstream before(file, std::ios::binary);
        const std::string previous((std::istreambuf_iterator<char>(before)), {});
        Scene invalid = original;
        invalid.name = std::string(1, static_cast<char>(0xff));
        check(!SceneSerializer::save_to_file(invalid, file.string()), "invalid scene save succeeded");
        std::ifstream after(file, std::ios::binary);
        const std::string current((std::istreambuf_iterator<char>(after)), {});
        check(current == previous, "failed save damaged the original scene");
    }
    {
        std::ifstream before(file, std::ios::binary);
        const std::string previous((std::istreambuf_iterator<char>(before)), {});
        before.close();
        HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "could not lock original scene for test");
        if (locked != INVALID_HANDLE_VALUE) {
            check(!SceneSerializer::save_to_file(original, file.string()), "replacement of locked scene succeeded");
            CloseHandle(locked);
            std::ifstream after(file, std::ios::binary);
            const std::string current((std::istreambuf_iterator<char>(after)), {});
            check(current == previous, "failed replacement damaged the original scene");
        }
    }
    {
        std::ifstream in(file);
        const std::string contents((std::istreambuf_iterator<char>(in)), {});
        check(contents.find("assets/model.obj") != std::string::npos || contents.find("assets\\\\model.obj") != std::string::npos,
              "asset path was not stored relative to scene");
    }
    write("{\"entities\":[{\"name\":\"Grid Floor\",\"transform\":{\"scale\":[240,1,240]},\"render\":{\"mesh_id\":3}}]}");
    check(SceneSerializer::load_from_file(loaded, file.string()) && loaded.entities.size() == 1 && loaded.entities[0].editor_grid, "legacy grid not recognized");

    write("{\"name\":\"Partial\"}");
    check(SceneSerializer::load_from_file(loaded, file.string()), "partial scene failed");
    {
        const Scene before = loaded;
        write("{\"version\":2,\"name\":\"Future\"}");
        check(!SceneSerializer::load_from_file(loaded, file.string()) && loaded == before,
              "future scene version was accepted or changed the current scene");
        write("{\"version\":1,\"materials\":[{\"id\":65536}]}");
        check(!SceneSerializer::load_from_file(loaded, file.string()) && loaded == before,
              "out-of-range material ID was accepted or changed the current scene");
    }
    check(loaded.name == "Partial" && loaded.has_terrain && loaded.entities.empty() &&
          loaded.sun_color[1] == Scene{}.sun_color[1] && loaded.next_entity_id == 1,
          "missing fields did not use scene defaults");

    write("{\"name\":\"Broken\",\"entities\":[{\"id\":5,\"transform\":{\"position\":[1,\"bad\",3]}}]}");
    check(!SceneSerializer::load_from_file(loaded, file.string()), "invalid scene accepted");
    check(loaded.name == "Partial" && loaded.entities.empty(), "failed load modified current scene");
    write("{\"entities\":[{\"id\":4294967295}]}");
    check(!SceneSerializer::load_from_file(loaded, file.string()), "overflowing entity ID accepted");
    check(loaded.name == "Partial", "overflowing ID modified current scene");
    const auto unicode_dir = dir / fs::path(L"\u043c\u043e\u0434\u0435\u043b\u0438");
    fs::create_directory(unicode_dir);
    const auto unicode_file = unicode_dir / fs::path(L"\u0441\u0446\u0435\u043d\u0430.json");
    const auto unicode_model = unicode_dir / fs::path(L"\u0434\u0435\u0440\u0435\u0432\u043e.obj");
    auto as_utf8 = [](const fs::path& value) { auto u = value.u8string(); return std::string(u.begin(), u.end()); };
    original.entities[0].model_path = as_utf8(unicode_model);
    check(SceneSerializer::save_to_file(original, as_utf8(unicode_file)), "unicode scene save failed");
    check(SceneSerializer::load_from_file(loaded, as_utf8(unicode_file)) && loaded.entities[0].model_path == as_utf8(unicode_model),
          "unicode asset path did not round-trip");
    check(!SceneSerializer::save_to_file(original, (dir / "missing" / "scene.json").string()),
          "save to missing directory succeeded");
    std::vector<float> heights(25, 0.0f);
    check(apply_terrain_brush(heights, 5, 4.0f, 0, 0, 1.5f, 2.0f, 1.0f, TerrainBrushMode::Raise, 0), "raise brush did not change heights");
    check(heights[12] == 2.0f && heights[0] == 0.0f && heights[11] > 0.0f && heights[11] < 2.0f, "raise brush falloff incorrect");
    check(apply_terrain_brush(heights, 5, 4.0f, 0, 0, 1.5f, 1.0f, 1.0f, TerrainBrushMode::Smooth, 0) && heights[12] < 2.0f, "smooth brush failed");
    check(apply_terrain_brush(heights, 5, 4.0f, 0, 0, 1.5f, 1.0f, 1.0f, TerrainBrushMode::Flatten, 3.0f) && heights[12] == 3.0f, "flatten brush failed");
    check(apply_terrain_brush(heights, 5, 4.0f, 0, 0, 1.5f, 1.0f, 1.0f, TerrainBrushMode::Lower, 0) && heights[12] == 2.0f, "lower brush failed");
    {
        std::vector<float> trail(33 * 33, 0.0f);
        check(apply_terrain_brush_segment(trail, 33, 32.0f, -8.0f, 0.0f, 8.0f, 0.0f,
                                          1.5f, 2.0f, 1.0f, TerrainBrushMode::Raise, 0.0f), "brush segment did not edit heights");
        bool continuous = true;
        for (int x = 9; x <= 24; ++x) continuous &= trail[16 * 33 + x] > 0.0f;
        check(continuous && trail[0] == 0.0f, "fast brush segment has a gap or edits unrelated samples");
        std::vector<float> dab(33 * 33, 0.0f), stationary(33 * 33, 0.0f);
        apply_terrain_brush(dab, 33, 32.0f, 0, 0, 2, 4, 0.25f, TerrainBrushMode::Raise, 0);
        apply_terrain_brush_segment(stationary, 33, 32.0f, 0, 0, 0, 0, 2, 4, 0.25f, TerrainBrushMode::Raise, 0);
        check(dab == stationary, "stationary brush strength changed");
    }
    write("{\"terrain\":{\"grid_size\":32,\"heights\":[1]}}");
    check(!SceneSerializer::load_from_file(loaded, file.string()), "invalid heightfield accepted");
    Scene history_scene;
    SceneHistory history(1024 * 1024);
    history.reset(history_scene);
    history_scene.name = "first"; history.observe(history_scene, true);
    history_scene.name = "second"; history.observe(history_scene, true);
    history.observe(history_scene, false);
    check(history.can_undo() && history.dirty(history_scene), "history did not record edit");
    check(history.undo(history_scene) && history_scene.name == "Untitled Scene" && !history.dirty(history_scene), "grouped undo failed");
    check(history.redo(history_scene) && history_scene.name == "second", "grouped redo failed");
    history.mark_saved(history_scene);
    history_scene.sun_intensity = 3.0f; history.observe(history_scene);
    check(history.dirty(history_scene), "saved scene not marked dirty after edit");
    check(history.undo(history_scene) && !history.dirty(history_scene), "undo did not restore saved state");
    history_scene.name = "branch"; history.observe(history_scene);
    check(!history.can_redo(), "new edit did not discard redo branch");
    history_scene.entities.push_back(Entity{}); history.observe(history_scene); history.mark_saved(history_scene);
    history_scene.entities[0].physics_body_id = 123;
    check(!history.dirty(history_scene), "runtime physics handle marked scene dirty");
    Scene bounded;
    SceneHistory tiny_history(1);
    tiny_history.reset(bounded);
    for (int i = 0; i < 20; ++i) { bounded.name = std::to_string(i); tiny_history.observe(bounded); }
    check(tiny_history.undo(bounded) && bounded.name == "18" && !tiny_history.undo(bounded), "history memory bound failed");
    Scene painted;
    painted.terrain_splatmap.assign(256 * 256 * 4, 0);
    SceneHistory paint_history(600 * 1024);
    paint_history.reset(painted);
    for (int i = 1; i <= 4; ++i) {
        painted.terrain_splatmap[0] = static_cast<uint8_t>(i);
        paint_history.observe(painted);
    }
    check(paint_history.undo(painted) && painted.terrain_splatmap[0] == 3 && !paint_history.undo(painted),
          "terrain painting was omitted from history memory accounting");
    const float camera[3] = {0, 0, 0};
    const float target[3] = {0, 0, 1};
    float ray[3]{};
    check(viewport_ray(camera, target, 100, 100, 200, 200, ray) && std::abs(ray[0]) < 0.0001f &&
          std::abs(ray[1]) < 0.0001f && ray[2] > 0.999f, "center viewport ray incorrect");
    check(viewport_ray(camera, target, 100, 0, 200, 200, ray) && ray[1] > 0.0f, "top viewport ray points down");
    check(viewport_ray(camera, target, 200, 100, 200, 200, ray) && ray[0] > 0.0f, "right viewport ray points left");
    check(!viewport_ray(camera, camera, 0, 0, 100, 100, ray), "degenerate camera accepted");
    check(!viewport_ray(camera, target, 0, 0, 0, 100, ray), "zero-width viewport accepted");
    Scene invalid_scene;
    Entity duplicate_a; duplicate_a.id = 5; duplicate_a.name = "A";
    Entity duplicate_b; duplicate_b.id = 5; duplicate_b.name = "B";
    duplicate_b.texture_path = (dir / "does-not-exist.png").string();
    invalid_scene.entities = {duplicate_a, duplicate_b};
    const auto issues = validate_scene(invalid_scene);
    bool duplicate_found = false, missing_found = false;
    for (const auto& issue : issues) {
        duplicate_found |= issue.error && issue.code == "entity-id";
        missing_found |= issue.error && issue.code == "missing-asset";
    }
    check(duplicate_found && missing_found, "scene diagnostics missed invalid references or duplicate IDs");
    Scene missing_material;
    missing_material.materials[0].normal_path = (dir / "missing-normal.png").string();
    const auto material_issues = validate_scene(missing_material);
    bool missing_material_found = false;
    for (const auto& issue : material_issues)
        missing_material_found |= issue.error && issue.code == "missing-asset" && issue.detail.find("normal") != std::string::npos;
    check(missing_material_found, "scene diagnostics missed a missing material map");
    ProjectData project;
    project.filepath = (dir / "test.gm3dproject").string();
    project.last_scene = file.string();
    project.assets = {{model_path.string(), true}, {texture_path.string(), false}};
    std::string project_error;
    check(ProjectSerializer::save(project, project_error), "project save failed");
    ProjectData reopened;
    check(ProjectSerializer::load(project.filepath, reopened, project_error) && reopened.assets.size() == 2 &&
          reopened.last_scene == file.string() && reopened.assets[0].path == model_path.string() && reopened.assets[0].is_model &&
          reopened.assets[1].path == texture_path.string() && !reopened.assets[1].is_model, "project asset registry round-trip failed");
    const auto bad_project = dir / "bad.gm3dproject";
    std::ofstream(bad_project) << "{\"format\":\"bad\"}";
    check(!ProjectSerializer::load(bad_project.string(), reopened, project_error) && reopened.assets.size() == 2,
          "invalid project overwrote existing registry");

    // Hierarchy and TransformMath Tests
    {
        Scene hier_scene;
        Entity parent_ent; parent_ent.id = 10; parent_ent.name = "Parent"; parent_ent.position[0] = 10.0f;
        Entity child_ent; child_ent.id = 11; child_ent.name = "Child"; child_ent.parent_id = 10; child_ent.position[1] = 5.0f;
        Entity grand_ent; grand_ent.id = 12; grand_ent.name = "Grandchild"; grand_ent.parent_id = 11; grand_ent.is_group = true;
        hier_scene.entities = { parent_ent, child_ent, grand_ent };

        check(hier_scene.is_descendant_of(11, 10), "child is descendant of parent");
        check(hier_scene.is_descendant_of(12, 10), "grandchild is descendant of root parent");
        check(!hier_scene.is_descendant_of(10, 12), "parent is not descendant of grandchild");

        std::vector<uint32_t> descendants;
        hier_scene.get_all_descendants(10, descendants);
        check(descendants.size() == 2 && descendants[0] == 11 && descendants[1] == 12, "descendants retrieval failed");

        float world_pos[3], world_rot[3], world_scale[3];
        ComputeWorldTransform(hier_scene, child_ent, world_pos, world_rot, world_scale);
        check(std::abs(world_pos[0] - 10.0f) < 0.001f && std::abs(world_pos[1] - 5.0f) < 0.001f, "child world transform position incorrect");

        Scene reparent = hier_scene;
        float before[16], after[16];
        ComputeWorldMatrix(reparent, *reparent.find_entity(11), before);
        check(ReparentEntityPreservingWorld(reparent, 11, 0), "unparent failed");
        ComputeWorldMatrix(reparent, *reparent.find_entity(11), after);
        check(std::abs(before[12] - after[12]) < 0.001f && std::abs(before[13] - after[13]) < 0.001f,
              "unparent changed world position");
        reparent.find_entity(10)->scale[0] = 0.0f;
        const Entity original_child = *reparent.find_entity(11);
        check(!ReparentEntityPreservingWorld(reparent, 11, 10) && *reparent.find_entity(11) == original_child,
              "singular parent changed child transform");
        check(!ReparentEntityPreservingWorld(reparent, 11, 999) && *reparent.find_entity(11) == original_child,
              "missing parent changed child transform");

        // Hierarchy save & load roundtrip
        const auto hier_file = dir / "hier_scene.json";
        check(SceneSerializer::save_to_file(hier_scene, hier_file.string()), "hierarchy scene save failed");
        Scene loaded_hier;
        check(SceneSerializer::load_from_file(loaded_hier, hier_file.string()), "hierarchy scene load failed");
        check(loaded_hier.entities.size() == 3 && loaded_hier.entities[1].parent_id == 10 && loaded_hier.entities[2].is_group,
              "hierarchy parent_id and is_group did not round trip");

        // Cycle breaking test
        const auto cycle_file = dir / "cycle_scene.json";
        std::ofstream(cycle_file) << "{\"entities\":[{\"id\":1,\"parent_id\":2},{\"id\":2,\"parent_id\":1}]}";
        Scene cycle_scene;
        check(SceneSerializer::load_from_file(cycle_scene, cycle_file.string()), "cycle scene load failed");
        check(!cycle_scene.is_descendant_of(1, 2) || !cycle_scene.is_descendant_of(2, 1), "cycle was not broken on load");

        Scene deep;
        for (uint32_t i = 1; i <= 300; ++i) {
            Entity node; node.id = i; node.parent_id = i == 1 ? 0 : i - 1;
            node.position[0] = 1.0f; deep.entities.push_back(node);
        }
        ComputeWorldTransform(deep, deep.entities.back(), world_pos, world_rot, world_scale);
        check(std::abs(world_pos[0] - 300.0f) < 0.01f && deep.is_descendant_of(300, 1),
              "deep hierarchy was truncated");
        deep.entities[0].parent_id = 300;
        descendants.clear(); deep.get_all_descendants(1, descendants);
        check(descendants.size() == 299 && !deep.is_descendant_of(1, 999),
              "cyclic hierarchy traversal did not terminate correctly");
    }

    // PBR Materials Serialization & Roundtrip Tests
    {
        Scene deletion;
        deletion.next_material_id = 65535;
        Material last_slot; last_slot.id = 65535; deletion.materials.push_back(last_slot);
        check(deletion.allocate_material_id() == 9, "material allocator did not reuse a free ID after overflow");
        Entity using_material; using_material.id = 1; using_material.material_id = 2;
        deletion.entities.push_back(using_material);
        SceneHistory deletion_history;
        deletion_history.reset(deletion);
        check(deletion.remove_material(2) && deletion.entities[0].material_id == 0,
              "deleting material left an invalid entity reference");
        deletion_history.observe(deletion);
        check(deletion_history.undo(deletion) && deletion.find_material(2) && deletion.entities[0].material_id == 2,
              "undo did not restore deleted material and assignment");
        Scene mat_scene;
        mat_scene.init_default_materials();
        check(mat_scene.materials.size() == 9, "default materials count mismatch");

        Material custom_mat;
        custom_mat.id = mat_scene.next_material_id++;
        custom_mat.name = "Molten Obsidian";
        custom_mat.base_color[0] = 0.12f; custom_mat.base_color[1] = 0.08f; custom_mat.base_color[2] = 0.06f; custom_mat.base_color[3] = 0.95f;
        custom_mat.roughness = 0.22f;
        custom_mat.metallic = 0.88f;
        custom_mat.emissive = 3.5f;
        custom_mat.emissive_color[0] = 1.0f; custom_mat.emissive_color[1] = 0.35f; custom_mat.emissive_color[2] = 0.05f;
        custom_mat.uv_tiling[0] = 4.0f; custom_mat.uv_tiling[1] = 4.0f;
        custom_mat.uv_offset[0] = 0.5f; custom_mat.uv_offset[1] = 0.25f;
        custom_mat.alpha_mode = 1; // Mask
        custom_mat.alpha_cutoff = 0.33f;
        custom_mat.is_builtin_preset = false;
        mat_scene.materials.push_back(custom_mat);

        Entity ent;
        ent.id = 100;
        ent.name = "Obsidian Pillar";
        ent.material_id = static_cast<uint16_t>(custom_mat.id);
        mat_scene.entities.push_back(ent);

        const auto mat_file = dir / "mat_scene.json";
        check(SceneSerializer::save_to_file(mat_scene, mat_file.string()), "material scene save failed");

        Scene loaded_mat_scene;
        check(SceneSerializer::load_from_file(loaded_mat_scene, mat_file.string()), "material scene load failed");
        check(loaded_mat_scene.materials.size() == 10, "loaded material count incorrect");

        const Material* found = loaded_mat_scene.find_material(custom_mat.id);
        check(found != nullptr, "custom material not found after round trip");
        if (found) {
            check(found->name == "Molten Obsidian", "material name corrupted");
            check(std::abs(found->roughness - 0.22f) < 0.001f, "roughness corrupted");
            check(std::abs(found->metallic - 0.88f) < 0.001f, "metallic corrupted");
            check(std::abs(found->emissive - 3.5f) < 0.001f, "emissive corrupted");
            check(std::abs(found->uv_tiling[0] - 4.0f) < 0.001f, "uv_tiling corrupted");
            check(found->alpha_mode == 1, "alpha_mode corrupted");
            check(std::abs(found->alpha_cutoff - 0.33f) < 0.001f, "alpha_cutoff corrupted");
            check(!found->is_builtin_preset, "is_builtin_preset corrupted");
        }
        check(loaded_mat_scene.entities.size() == 1 && loaded_mat_scene.entities[0].material_id == custom_mat.id,
              "entity material_id link corrupted");
    }

    // Terrain Material Layers & Paint Brush Serialization Tests
    {
        Scene terr_scene;
        terr_scene.terrain_grid_size = 32;
        terr_scene.terrain_world_size = 120.0f;
        check(terr_scene.terrain_layers.size() == 4, "default terrain layers count mismatch");
        terr_scene.terrain_layers[3].name = "Volcanic Basalt Path";
        terr_scene.terrain_layers[3].color[0] = 0.25f;
        terr_scene.terrain_layers[3].scale = 2.0f;

        // Paint layer 0 and then layer 3 at the origin
        check(apply_terrain_paint_segment(terr_scene.terrain_splatmap, 32, 120.0f, -10.0f, 0.0f, 10.0f, 0.0f,
                                          12.0f, 5.0f, 0.1f, 0),
              "terrain paint brush layer 0 did not modify splatmap");
        check(terr_scene.terrain_splatmap.size() == 32u * 32u * 4u, "splatmap size mismatch");

        check(apply_terrain_paint_segment(terr_scene.terrain_splatmap, 32, 120.0f, 0.0f, -10.0f, 0.0f, 10.0f,
                                          12.0f, 8.0f, 0.1f, 3),
              "terrain paint brush layer 3 did not modify splatmap");

        // Verify weight normalization (sum of RGBA channels <= 255 at every vertex)
        bool weights_valid = true;
        bool any_painted = false;
        for (size_t i = 0; i < 32u * 32u; ++i) {
            int sum = static_cast<int>(terr_scene.terrain_splatmap[i * 4 + 0]) +
                      static_cast<int>(terr_scene.terrain_splatmap[i * 4 + 1]) +
                      static_cast<int>(terr_scene.terrain_splatmap[i * 4 + 2]) +
                      static_cast<int>(terr_scene.terrain_splatmap[i * 4 + 3]);
            if (sum > 255) weights_valid = false;
            if (sum > 0) any_painted = true;
        }
        check(weights_valid, "splatmap weights exceeded 255 normalization bound");
        check(any_painted, "splatmap has no painted weights");

        const auto terr_file = dir / "terr_paint_scene.json";
        check(SceneSerializer::save_to_file(terr_scene, terr_file.string()), "terrain paint scene save failed");

        Scene loaded_terr;
        check(SceneSerializer::load_from_file(loaded_terr, terr_file.string()), "terrain paint scene load failed");
        check(loaded_terr.terrain_layers == terr_scene.terrain_layers, "terrain layers did not round trip");
        check(loaded_terr.terrain_splatmap == terr_scene.terrain_splatmap, "terrain splatmap did not round trip");
    }

    // Shadow Quality, CSM, Point Light Shadows & Component Serialization Tests
    {
        Scene sh_scene;
        sh_scene.csm_enabled = false;
        sh_scene.shadow_quality = 3;
        sh_scene.max_shadow_lights = 2;

        Entity pl;
        pl.id = 1;
        pl.name = "TorchLight";
        pl.is_point_light = true;
        pl.cast_shadow = true;
        pl.light_radius = 18.0f;
        pl.light_intensity = 42.0f;
        sh_scene.entities.push_back(pl);

        Entity actor;
        actor.id = 2;
        actor.name = "AnimatedKnight";
        actor.physics_type = 2;
        actor.collider_shape = 2;
        actor.collider_size[0] = 0.8f; actor.collider_size[1] = 1.8f; actor.collider_size[2] = 0.8f;
        actor.collider_offset[1] = 0.9f;
        actor.has_animator = true;
        actor.anim_clip_index = 2;
        actor.anim_speed = 1.35f;
        actor.anim_loop = false;
        actor.anim_playing = true;
        actor.anim_time = 0.75f;
        sh_scene.entities.push_back(actor);

        const auto sh_file = dir / "shadow_comp_scene.json";
        check(SceneSerializer::save_to_file(sh_scene, sh_file.string()), "shadow & component scene save failed");

        Scene loaded_sh;
        check(SceneSerializer::load_from_file(loaded_sh, sh_file.string()), "shadow & component scene load failed");
        check(loaded_sh.csm_enabled == false, "csm_enabled did not round trip");
        check(loaded_sh.shadow_quality == 3, "shadow_quality did not round trip");
        check(loaded_sh.max_shadow_lights == 2, "max_shadow_lights did not round trip");
        check(loaded_sh.entities.size() == 2, "entities count mismatch");
        if (loaded_sh.entities.size() == 2) {
            check(loaded_sh.entities[0].is_point_light && loaded_sh.entities[0].cast_shadow, "point light cast_shadow did not round trip");
            check(std::abs(loaded_sh.entities[1].collider_size[1] - 1.8f) < 0.001f, "collider_size did not round trip");
            check(std::abs(loaded_sh.entities[1].collider_offset[1] - 0.9f) < 0.001f, "collider_offset did not round trip");
            check(loaded_sh.entities[1].has_animator && loaded_sh.entities[1].anim_clip_index == 2, "animator clip_index did not round trip");
            check(std::abs(loaded_sh.entities[1].anim_speed - 1.35f) < 0.001f && !loaded_sh.entities[1].anim_loop, "animator speed/loop did not round trip");
        }
    }

    fs::remove_all(dir);
    return failures == 0 ? 0 : 1;
}
