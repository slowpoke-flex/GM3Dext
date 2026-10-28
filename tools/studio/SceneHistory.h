#pragma once
#include "SceneData.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace GM3D::Studio {
// Scene snapshots deliberately exclude transient physics handles and file location.
class SceneHistory {
public:
    explicit SceneHistory(size_t budget = 128 * 1024 * 1024) : budget_(budget) {}
    void reset(const Scene& scene) {
        states_.clear(); states_.push_back(normalize(scene)); cursor_ = 0;
        saved_ = states_[0]; grouping_ = false; trim();
    }
    void observe(const Scene& scene, bool active = false) {
        Scene current = normalize(scene);
        if (states_.empty()) { reset(current); return; }
        if (!(states_[cursor_] == current)) {
            if (grouping_ && cursor_ + 1 == states_.size() && cursor_ > 0) {
                states_[cursor_] = std::move(current);
            } else {
                states_.erase(states_.begin() + cursor_ + 1, states_.end());
                states_.push_back(std::move(current)); ++cursor_;
            }
            trim();
        }
        grouping_ = active;
    }
    bool undo(Scene& scene) {
        grouping_ = false;
        if (!can_undo()) return false;
        apply(scene, states_[--cursor_]); return true;
    }
    bool redo(Scene& scene) {
        grouping_ = false;
        if (!can_redo()) return false;
        apply(scene, states_[++cursor_]); return true;
    }
    bool can_undo() const { return !states_.empty() && cursor_ > 0; }
    bool can_redo() const { return !states_.empty() && cursor_ + 1 < states_.size(); }
    void mark_saved(const Scene& scene) { saved_ = normalize(scene); }
    bool dirty(const Scene& scene) const { return !(normalize(scene) == saved_); }
private:
    static Scene normalize(Scene scene) {
        scene.filepath.clear(); scene.terrain_body_id = 0;
        for (auto& entity : scene.entities) entity.physics_body_id = 0;
        return scene;
    }
    static void apply(Scene& scene, const Scene& value) {
        auto path = scene.filepath; scene = value; scene.filepath = std::move(path);
    }
    static size_t bytes(const Scene& s) {
        size_t result = sizeof(Scene) + s.terrain_heights.size() * sizeof(float) + s.entities.size() * sizeof(Entity);
        for (const auto& e : s.entities) result += e.name.size() + e.tag.size() + e.model_path.size() + e.texture_path.size();
        return result;
    }
    void trim() {
        size_t total = 0;
        for (const auto& state : states_) total += bytes(state);
        while (states_.size() > 2 && cursor_ > 0 && total > budget_) {
            total -= bytes(states_.front()); states_.erase(states_.begin()); --cursor_;
        }
    }
    size_t budget_;
    size_t cursor_ = 0;
    bool grouping_ = false;
    std::vector<Scene> states_;
    Scene saved_;
};
}
