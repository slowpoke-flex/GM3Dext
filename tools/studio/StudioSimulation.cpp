// Jolt preview: creating bodies from the scene, stopping it and reporting the result.
//
// Split out of StudioApp.cpp; the bodies are unchanged.
#include "StudioApp.h"
#include "StudioShared.h"
#include "SceneSerializer.h"
#include "ViewportMath.h"
#include "Heightmap.h"
#include "core/EngineContext.h"
#include "render/RenderWorld.h"
#include "physics/PhysicsWorld.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "ImGuizmo.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iostream>

namespace GM3D::Studio {

void StudioApp::start_simulation() {
    if (m_simulating) return;
    auto* pw = EngineContext::instance().get_physics_world();
    if (!pw) return;
    m_history.observe(m_scene);
    m_simulation_backup = m_scene;
    m_simulation_backup_valid = true;
    m_sim_backup_cam_pos[0] = m_cam_pos[0];
    m_sim_backup_cam_pos[1] = m_cam_pos[1];
    m_sim_backup_cam_pos[2] = m_cam_pos[2];
    m_sim_backup_cam_yaw = m_cam_yaw;
    m_sim_backup_cam_pitch = m_cam_pitch;
    m_simulation_bodies.clear();
    m_simulating = true;

    auto* rw = EngineContext::instance().get_render_world();
    int mesh_colliders = 0, hull_colliders = 0, mesh_fallbacks = 0;

    for (auto& e : m_scene.entities) {
        if (e.is_point_light || e.is_group || e.physics_type == 0 || (m_scene.has_terrain && e.editor_grid)) continue;

        float wpos[3], wrot[3], wscale[3], wmat[16];
        ComputeWorldTransform(m_scene, e, wpos, wrot, wscale, wmat);

        RigidBodyDesc desc{};
        desc.shape_type = static_cast<BodyShapeType>(std::clamp(e.collider_shape, 0, 4));
        desc.motion_type = (e.physics_type == 1) ? MotionType::Static :
                           (e.physics_type == 2) ? MotionType::Dynamic : MotionType::Kinematic;

        float ox = e.collider_offset[0];
        float oy = e.collider_offset[1];
        float oz = e.collider_offset[2];
        desc.position[0] = ox * wmat[0] + oy * wmat[4] + oz * wmat[8]  + wmat[12];
        desc.position[1] = ox * wmat[1] + oy * wmat[5] + oz * wmat[9]  + wmat[13];
        desc.position[2] = ox * wmat[2] + oy * wmat[6] + oz * wmat[10] + wmat[14];

        float qx, qy, qz, qw;
        EulerToQuat(wrot[0], wrot[1], wrot[2], qx, qy, qz, qw);
        desc.rotation[0] = qx;
        desc.rotation[1] = qy;
        desc.rotation[2] = qz;
        desc.rotation[3] = qw;

        float csx = std::max(0.05f, e.collider_size[0]);
        float csy = std::max(0.05f, e.collider_size[1]);
        float csz = std::max(0.05f, e.collider_size[2]);

        if (e.mesh_id == 3 && wscale[0] > 50.0f) {
            desc.shape_type = BodyShapeType::Box;
            desc.motion_type = MotionType::Static;
            desc.half_extents[0] = wscale[0] * csx * 0.5f;
            desc.half_extents[1] = 0.5f;
            desc.half_extents[2] = wscale[2] * csz * 0.5f;
            desc.position[1] = -0.5f;
        } else {
            desc.half_extents[0] = std::max(0.05f, wscale[0] * csx * 0.5f);
            desc.half_extents[1] = std::max(0.05f, wscale[1] * csy * 0.5f);
            desc.half_extents[2] = std::max(0.05f, wscale[2] * csz * 0.5f);
            desc.radius = std::max(0.05f, wscale[0] * csx * 0.5f);
            desc.half_height = std::max(0.05f, wscale[1] * csy * 0.5f);
        }

        // Mesh collider: feed the model's own triangles, scaled exactly like the primitive
        // shapes (object scale times the Collider Size multiplier), so with the default
        // multiplier the collider coincides with the rendered surface.
        std::vector<float> mesh_positions;
        if (e.collider_shape == 4 && !(e.mesh_id == 3 && wscale[0] > 50.0f)) {
            const CollisionMesh* collision = rw ? rw->get_mesh_manager().get_collision_mesh(e.mesh_id) : nullptr;
            if (collision && collision->valid()) {
                mesh_positions.resize(collision->positions.size());
                for (size_t v = 0; v < collision->vertex_count(); ++v) {
                    mesh_positions[v * 3 + 0] = collision->positions[v * 3 + 0] * wscale[0] * csx;
                    mesh_positions[v * 3 + 1] = collision->positions[v * 3 + 1] * wscale[1] * csy;
                    mesh_positions[v * 3 + 2] = collision->positions[v * 3 + 2] * wscale[2] * csz;
                }
                desc.shape_type = BodyShapeType::Mesh;
                desc.mesh_positions = mesh_positions.data();
                desc.mesh_vertex_count = collision->vertex_count();
                desc.mesh_indices = collision->indices.data();
                desc.mesh_index_count = static_cast<uint32_t>(collision->indices.size());
            }
        }

        desc.mass = e.mass;
        desc.friction = e.friction;
        desc.restitution = e.restitution;

        MeshColliderKind mesh_kind = MeshColliderKind::None;
        e.physics_body_id = pw->create_body(desc, &mesh_kind);
        if (e.physics_body_id) m_simulation_bodies.push_back(e.physics_body_id);

        if (e.collider_shape == 4) {
            if (mesh_kind == MeshColliderKind::Triangles) ++mesh_colliders;
            else if (mesh_kind == MeshColliderKind::ConvexHull) ++hull_colliders;
            else {
                ++mesh_fallbacks;
                std::cout << "[physics] mesh collider unavailable for '" << e.name
                          << "' (mesh " << e.mesh_id << "); using a box\n";
            }
        }
    }

    if (mesh_fallbacks > 0) {
        m_asset_message = std::to_string(mesh_fallbacks) +
            " mesh collider(s) fell back to a box: the model has no usable collision geometry.";
        m_asset_message_pending = true;
    }
    if (mesh_colliders + hull_colliders + mesh_fallbacks > 0) {
        std::cout << "[physics] mesh colliders: " << mesh_colliders << " trimesh, "
                  << hull_colliders << " convex hull, " << mesh_fallbacks << " box fallback\n";
    }
}

void StudioApp::report_simulation() {
    static const char* kShapeNames[] = { "box", "sphere", "capsule", "cylinder", "mesh" };
    std::cout << "[simulate] resting state after " << m_simulate_frame << " frames\n";
    for (const auto& e : m_scene.entities) {
        if (e.physics_body_id == 0) continue;
        const char* shape = (e.collider_shape >= 0 && e.collider_shape < 5) ? kShapeNames[e.collider_shape] : "?";
        const char* body = e.physics_type == 1 ? "static" : (e.physics_type == 2 ? "dynamic" : "kinematic");
        std::cout << "[simulate] " << body << " '" << e.name << "' mesh=" << e.mesh_id
                  << " shape=" << shape
                  << " pos=(" << e.position[0] << ", " << e.position[1] << ", " << e.position[2] << ")"
                  << " rot=(" << e.rotation[0] << ", " << e.rotation[1] << ", " << e.rotation[2] << ")\n";
    }
}

void StudioApp::stop_simulation() {
    if (!m_simulating) return;
    m_simulating = false;
    auto* pw = EngineContext::instance().get_physics_world();
    if (pw) for (uint32_t body : m_simulation_bodies) pw->destroy_body(body);
    m_simulation_bodies.clear();
    EngineContext::instance().particle_clear();

    if (m_simulation_backup_valid) {
        m_cam_pos[0] = m_sim_backup_cam_pos[0];
        m_cam_pos[1] = m_sim_backup_cam_pos[1];
        m_cam_pos[2] = m_sim_backup_cam_pos[2];
        m_cam_yaw = m_sim_backup_cam_yaw;
        m_cam_pitch = m_sim_backup_cam_pitch;

        float radYaw = m_cam_yaw * 0.0174532925f;
        float radPitch = m_cam_pitch * 0.0174532925f;
        float fx = cosf(radPitch) * sinf(radYaw);
        float fy = sinf(radPitch);
        float fz = cosf(radPitch) * cosf(radYaw);
        m_cam_target[0] = m_cam_pos[0] + fx * 10.0f;
        m_cam_target[1] = m_cam_pos[1] + fy * 10.0f;
        m_cam_target[2] = m_cam_pos[2] + fz * 10.0f;
        m_cam_velocity[0] = m_cam_velocity[1] = m_cam_velocity[2] = 0.0f;
        m_cam_zoom_velocity = 0.0f;

        const bool terrain_changed = m_scene.has_terrain != m_simulation_backup.has_terrain ||
            m_scene.terrain_grid_size != m_simulation_backup.terrain_grid_size ||
            m_scene.terrain_world_size != m_simulation_backup.terrain_world_size ||
            m_scene.terrain_height_scale != m_simulation_backup.terrain_height_scale ||
            m_scene.terrain_seed != m_simulation_backup.terrain_seed ||
            m_scene.terrain_heights != m_simulation_backup.terrain_heights ||
            m_scene.terrain_layers != m_simulation_backup.terrain_layers ||
            m_scene.terrain_splatmap != m_simulation_backup.terrain_splatmap;
        m_scene = std::move(m_simulation_backup);
        m_simulation_backup_valid = false;
        if (terrain_changed) rebuild_terrain();
        if (m_selected_entity_id >= 0 && !selected_entity()) m_selected_entity_id = -1;
        sync_scene_to_engine();
    }
}

} // namespace GM3D::Studio
