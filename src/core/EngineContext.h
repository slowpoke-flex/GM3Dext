#pragma once

#include "GMExport.h"
#include "SharedMemory.h"
#include "physics/PhysicsWorld.h"
#include "render/RenderWorld.h"
#include <memory>
#include <mutex>

/**
 * @brief Central native extension context (Simulation, Rendering, Memory).
 * Ensures thread-safety and data validation before each operation.
 */
class EngineContext {
public:
    static EngineContext& instance();

    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;

    /**
     * @brief Initialize extension and bind GameMaker buffer and window handle.
     */
    double initialize(void* buffer_ptr, double buffer_size_bytes, void* window_handle, double width = 0.0, double height = 0.0);

    /**
     * @brief Release engine resources cleanly on game exit.
     */
    double shutdown();

    /**
     * @brief Main framework tick, called each frame from GameMaker.
     */
    double update(double delta_time);

    /**
     * @brief Create rigid body (invoked from GML).
     */
    double create_rigid_body(double shape_type, double motion_type, double mass, 
                             double px, double py, double pz,
                             double sx, double sy, double sz);

    /**
     * @brief Destroy rigid body.
     */
    double destroy_rigid_body(double body_id);

    double add_impulse(double body_id, double ix, double iy, double iz);
    double add_torque(double body_id, double tx, double ty, double tz);
    double add_force(double body_id, double fx, double fy, double fz);
    double set_velocity(double body_id, double vx, double vy, double vz);
    double set_gravity(double gx, double gy, double gz);
    double set_friction(double body_id, double friction);
    double set_restitution(double body_id, double restitution);
    double set_body_transform(double body_id, double x, double y, double z, double qx, double qy, double qz, double qw);
    double load_mesh_obj(const char* filepath);
    double load_mesh_glb(const char* filepath);
    double load_mesh_model(const char* filepath);
    double load_texture(const char* filepath);
    double set_normal_map(double target_tex_id, double normal_tex_id);

    // Dynamic PBR Materials
    double material_set_pbr(double mat_id, double r, double g, double b, double a, double roughness, double metallic, double emissive);
    double material_set_textures(double mat_id, double albedo_tex_id, double normal_tex_id);
    double material_set_uv(double mat_id, double tiling_u, double tiling_v, double offset_u, double offset_v);
    double material_set_alpha(double mat_id, double alpha_mode, double alpha_cutoff);
    double material_clear_all();

    double set_sun_light(double dx, double dy, double dz, double r, double g, double b, double intensity);
    double set_ambient_light(double r, double g, double b, double intensity);
    double set_point_light(double index, double x, double y, double z, double radius, double r, double g, double b, double intensity, double cast_shadow = 1.0);
    double clear_point_lights();
    double set_shadow_settings(double enabled, double strength, double softness, double coverage);
    double set_shadow_quality(double quality, double csm_enabled, double max_shadow_lights);
    double set_sky_color(double r, double g, double b);
    double set_fog(double start, double end, double r, double g, double b);
    double set_gizmo(double enabled, double x, double y, double z, double sx, double sy, double sz, double axis_len);

    double particle_spawn_burst(double type, double x, double y, double z, double count, double speed, double size, double r, double g, double b);
    double particle_spawn_ring(double type, double x, double y, double z, double radius, double count, double r, double g, double b);
    double particle_clear();
    double set_bloom(double enabled, double intensity, double threshold);

    // Procedural Terrain
    double terrain_generate(double grid_size, double world_size, double height_scale, double seed);
    double terrain_get_height(double x, double z);
    double terrain_apply_edit_data(const void* data, double size_bytes);

    // Procedural Grass & Foliage
    double grass_set_enabled(double enabled);
    double grass_set_wind(double dir_x, double dir_z, double speed, double strength);
    double grass_rebuild(double density, double seed);
    double grass_get_count();
    double grass_clear();
    double grass_load_file(const char* filepath);
    double grass_save_file(const char* filepath);

    double anim_play(double entity_id, double anim_index, double speed, double loop);
    double anim_stop(double entity_id);
    double anim_set_time(double entity_id, double time);
    double anim_get_count(double mesh_id);
    std::string anim_get_name(double mesh_id, double anim_index);
    double anim_get_duration(double mesh_id, double anim_index);

    double raycast(double fx, double fy, double fz, double tx, double ty, double tz);
    double get_raycast_x() const noexcept { return m_last_raycast.hit_x; }
    double get_raycast_y() const noexcept { return m_last_raycast.hit_y; }
    double get_raycast_z() const noexcept { return m_last_raycast.hit_z; }

    [[nodiscard]] void* get_window_handle() const noexcept { return m_render_world ? m_render_world->get_window_handle() : nullptr; }
    [[nodiscard]] bool is_initialized() const noexcept { return m_is_initialized; }

    PhysicsWorld* get_physics_world() noexcept { return m_physics_world.get(); }
    RenderWorld* get_render_world() noexcept { return m_render_world.get(); }

private:
    EngineContext() = default;
    ~EngineContext();

    bool validate_buffer_safety(const SharedBufferHeader*& out_header, 
                                SharedEntityData*& out_entities, 
                                uint32_t& out_count);

    std::mutex m_mutex;
    bool m_is_initialized = false;

    // Pointer to GameMaker shared memory
    uint8_t* m_shared_buffer = nullptr;
    size_t   m_shared_buffer_size = 0;

    // Subsystems
    std::unique_ptr<PhysicsWorld> m_physics_world;
    std::unique_ptr<RenderWorld>  m_render_world;
    RaycastHitResult m_last_raycast{};
};
