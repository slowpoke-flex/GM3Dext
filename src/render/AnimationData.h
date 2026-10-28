#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

using namespace DirectX;

namespace GM3D {

enum class AnimPathType {
    Translation,
    Rotation,
    Scale
};

struct AnimChannel {
    int joint_index = -1;
    AnimPathType path = AnimPathType::Translation;
    std::vector<float> times;
    std::vector<XMFLOAT3> vec3_keys; // Translation or Scale
    std::vector<XMFLOAT4> quat_keys; // Rotation

    XMFLOAT3 sample_vec3(float time, const XMFLOAT3& default_val) const {
        if (times.empty() || vec3_keys.empty()) return default_val;
        if (time <= times.front() || times.size() == 1) return vec3_keys.front();
        if (time >= times.back()) return vec3_keys.back();

        // Binary search for upper bound
        auto it = std::upper_bound(times.begin(), times.end(), time);
        size_t idx1 = std::distance(times.begin(), it);
        size_t idx0 = idx1 - 1;

        float t0 = times[idx0];
        float t1 = times[idx1];
        float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        XMVECTOR v0 = XMLoadFloat3(&vec3_keys[idx0]);
        XMVECTOR v1 = XMLoadFloat3(&vec3_keys[idx1]);
        XMVECTOR res = XMVectorLerp(v0, v1, alpha);

        XMFLOAT3 out;
        XMStoreFloat3(&out, res);
        return out;
    }

    XMFLOAT4 sample_quat(float time, const XMFLOAT4& default_val) const {
        if (times.empty() || quat_keys.empty()) return default_val;
        if (time <= times.front() || times.size() == 1) return quat_keys.front();
        if (time >= times.back()) return quat_keys.back();

        auto it = std::upper_bound(times.begin(), times.end(), time);
        size_t idx1 = std::distance(times.begin(), it);
        size_t idx0 = idx1 - 1;

        float t0 = times[idx0];
        float t1 = times[idx1];
        float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        XMVECTOR q0 = XMLoadFloat4(&quat_keys[idx0]);
        XMVECTOR q1 = XMLoadFloat4(&quat_keys[idx1]);
        XMVECTOR res = XMQuaternionSlerp(q0, q1, alpha);

        XMFLOAT4 out;
        XMStoreFloat4(&out, res);
        return out;
    }
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimChannel> channels;
};

struct JointNode {
    std::string name;
    int parent_index = -1; // -1 for root
    XMFLOAT3 base_translation{0.0f, 0.0f, 0.0f};
    XMFLOAT4 base_rotation{0.0f, 0.0f, 0.0f, 1.0f};
    XMFLOAT3 base_scale{1.0f, 1.0f, 1.0f};
};

struct SkeletonData {
    std::string name;
    std::vector<JointNode> joints;
    std::vector<XMFLOAT4X4> inverse_bind_matrices; // row-major

    void evaluate_pose(const AnimationClip* clip, float time, std::vector<XMFLOAT4X4>& out_skin_matrices) const {
        size_t count = joints.size();
        if (count == 0) return;

        out_skin_matrices.resize(128);
        for (size_t b = count; b < 128; ++b) {
            XMStoreFloat4x4(&out_skin_matrices[b], XMMatrixIdentity());
        }

        // 1. Gather local TRS
        std::vector<XMFLOAT3> local_t(count);
        std::vector<XMFLOAT4> local_r(count);
        std::vector<XMFLOAT3> local_s(count);

        for (size_t i = 0; i < count; ++i) {
            local_t[i] = joints[i].base_translation;
            local_r[i] = joints[i].base_rotation;
            local_s[i] = joints[i].base_scale;
        }

        if (clip != nullptr && clip->duration > 0.0f) {
            float anim_time = std::fmod(time, clip->duration);
            if (anim_time < 0.0f) anim_time += clip->duration;

            for (const auto& ch : clip->channels) {
                if (ch.joint_index >= 0 && ch.joint_index < (int)count) {
                    int j = ch.joint_index;
                    if (ch.path == AnimPathType::Translation) {
                        local_t[j] = ch.sample_vec3(anim_time, joints[j].base_translation);
                    } else if (ch.path == AnimPathType::Rotation) {
                        local_r[j] = ch.sample_quat(anim_time, joints[j].base_rotation);
                    } else if (ch.path == AnimPathType::Scale) {
                        local_s[j] = ch.sample_vec3(anim_time, joints[j].base_scale);
                    }
                }
            }
        }

        // 2. Compute local and global matrices (memoized topological evaluation)
        std::vector<XMMATRIX> global_matrices(count);
        std::vector<bool> computed(count, false);

        auto eval_global = [&](auto& self, size_t idx) -> XMMATRIX {
            if (computed[idx]) return global_matrices[idx];

            XMVECTOR s = XMLoadFloat3(&local_s[idx]);
            XMVECTOR r = XMLoadFloat4(&local_r[idx]);
            XMVECTOR t = XMLoadFloat3(&local_t[idx]);

            XMMATRIX local_mat = XMMatrixScalingFromVector(s) *
                                 XMMatrixRotationQuaternion(r) *
                                 XMMatrixTranslationFromVector(t);

            int parent = joints[idx].parent_index;
            if (parent >= 0 && parent < (int)count && parent != (int)idx) {
                global_matrices[idx] = XMMatrixMultiply(local_mat, self(self, static_cast<size_t>(parent)));
            } else {
                global_matrices[idx] = local_mat;
            }

            computed[idx] = true;
            return global_matrices[idx];
        };

        for (size_t i = 0; i < count; ++i) {
            eval_global(eval_global, i);
        }

        // 3. Compute final skinning matrices: skin_matrix = inv_bind * global
        for (size_t i = 0; i < count; ++i) {
            if (i < inverse_bind_matrices.size()) {
                XMMATRIX inv_bind = XMLoadFloat4x4(&inverse_bind_matrices[i]);
                XMMATRIX skin_mat = XMMatrixMultiply(inv_bind, global_matrices[i]);
                XMStoreFloat4x4(&out_skin_matrices[i], skin_mat);
            } else {
                XMStoreFloat4x4(&out_skin_matrices[i], global_matrices[i]);
            }
        }
    }
};

struct SkinnedModelData {
    SkeletonData skeleton;
    std::vector<AnimationClip> animations;

    const AnimationClip* get_clip(uint32_t index) const {
        if (index < animations.size()) return &animations[index];
        return nullptr;
    }

    const AnimationClip* find_clip(const std::string& clip_name) const {
        for (const auto& a : animations) {
            if (a.name == clip_name) return &a;
        }
        return nullptr;
    }
};

} // namespace GM3D
