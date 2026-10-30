#include "EngineContext.h"
#include <iostream>
#include <cmath>
#include <cstring>

EngineContext& EngineContext::instance() {
    static EngineContext s_instance;
    return s_instance;
}

EngineContext::~EngineContext() {
    shutdown();
}

double EngineContext::initialize(void* buffer_ptr, double buffer_size_bytes, void* window_handle, double width, double height) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_is_initialized) {
        return GMResult::ERR_ALREADY_INITIALIZED;
    }

    if (buffer_ptr == nullptr || buffer_size_bytes < static_cast<double>(sizeof(SharedBufferHeader))) {
        return GMResult::ERR_INVALID_BUFFER;
    }

    m_shared_buffer = static_cast<uint8_t*>(buffer_ptr);
    m_shared_buffer_size = static_cast<size_t>(buffer_size_bytes);

    // Initial header check of shared buffer
    auto* header = reinterpret_cast<SharedBufferHeader*>(m_shared_buffer);
    if (header->magic != GM3D_BUFFER_MAGIC) {
        // If buffer was not initialized by GameMaker yet, write default header
        header->magic = GM3D_BUFFER_MAGIC;
        header->version = GM3D_PROTOCOL_VERSION;
        header->entity_count = 0;
        header->max_entities = static_cast<uint32_t>((m_shared_buffer_size - sizeof(SharedBufferHeader)) / sizeof(SharedEntityData));
    }

    // Cleanup here must not reacquire m_mutex: initialize already owns it.
    const auto reset_partial_initialization = [&]() {
        m_render_world.reset();
        m_physics_world.reset();
        m_shared_buffer = nullptr;
        m_shared_buffer_size = 0;
    };

    // Initialize subsystems
    m_physics_world = std::make_unique<PhysicsWorld>();
    if (!m_physics_world->initialize()) {
        reset_partial_initialization();
        return GMResult::ERR_GENERAL;
    }

    m_render_world = std::make_unique<RenderWorld>();
    RenderConfig render_cfg{};
    render_cfg.native_window_handle = window_handle;
    render_cfg.width = static_cast<uint32_t>(width > 0 ? width : 1280);
    render_cfg.height = static_cast<uint32_t>(height > 0 ? height : 720);
    render_cfg.vsync = true;

    if (!m_render_world->initialize(render_cfg)) {
        reset_partial_initialization();
        return GMResult::ERR_GENERAL;
    }

    m_is_initialized = true;
    return GMResult::OK;
}

double EngineContext::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_is_initialized) {
        return GMResult::OK;
    }

    if (m_render_world) {
        m_render_world->shutdown();
        m_render_world.reset();
    }

    if (m_physics_world) {
        m_physics_world->shutdown();
        m_physics_world.reset();
    }

    m_shared_buffer = nullptr;
    m_shared_buffer_size = 0;
    m_is_initialized = false;

    return GMResult::OK;
}

bool EngineContext::validate_buffer_safety(const SharedBufferHeader*& out_header, 
                                          SharedEntityData*& out_entities, 
                                          uint32_t& out_count) {
    if (!m_is_initialized || m_shared_buffer == nullptr) {
        return false;
    }

    if (m_shared_buffer_size < sizeof(SharedBufferHeader)) {
        return false;
    }

    auto* header = reinterpret_cast<SharedBufferHeader*>(m_shared_buffer);
    if (header->magic != GM3D_BUFFER_MAGIC || header->version != GM3D_PROTOCOL_VERSION) {
        return false;
    }

    // Compute maximum allowable objects with buffer overflow protection
    const size_t available_payload_bytes = m_shared_buffer_size - sizeof(SharedBufferHeader);
    const size_t max_possible_entities = available_payload_bytes / sizeof(SharedEntityData);

    if (header->entity_count > max_possible_entities) {
        // Clamp count to prevent out-of-bounds memory access
        out_count = static_cast<uint32_t>(max_possible_entities);
    } else {
        out_count = header->entity_count;
    }

    out_header = header;
    out_entities = reinterpret_cast<SharedEntityData*>(m_shared_buffer + sizeof(SharedBufferHeader));
    return true;
}

double EngineContext::update(double delta_time) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_is_initialized) {
        return GMResult::ERR_NOT_INITIALIZED;
    }

    const SharedBufferHeader* header = nullptr;
    SharedEntityData* entities = nullptr;
    uint32_t entity_count = 0;

    if (!validate_buffer_safety(header, entities, entity_count)) {
        return GMResult::ERR_INVALID_BUFFER;
    }

    const float dt = static_cast<float>(delta_time);

    // 1. Step 1: Sync kinematic entities from GameMaker into physics
    m_physics_world->sync_from_shared_entities(entities, entity_count);

    // 2. Step 2: Step physics world (Jolt Physics fixed-step simulation)
    m_physics_world->update(dt);

    // 3. Step 3: Write dynamic rigid body simulation results back into shared buffer
    m_physics_world->sync_to_shared_entities(entities, entity_count);

    // 4. Step 4: Update graphics pipeline and render frame
    m_render_world->render_frame(*header, entities, entity_count, dt);

    return GMResult::OK;
}

double EngineContext::create_rigid_body(double shape_type, double motion_type, double mass, 
                                       double px, double py, double pz,
                                       double sx, double sy, double sz) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_is_initialized) {
        return GMResult::ERR_NOT_INITIALIZED;
    }

    RigidBodyDesc desc{};
    desc.shape_type = static_cast<BodyShapeType>(static_cast<uint32_t>(shape_type));
    desc.motion_type = static_cast<MotionType>(static_cast<uint32_t>(motion_type));
    desc.mass = static_cast<float>(mass);
    desc.position[0] = static_cast<float>(px);
    desc.position[1] = static_cast<float>(py);
    desc.position[2] = static_cast<float>(pz);
    desc.half_extents[0] = static_cast<float>(sx * 0.5);
    desc.half_extents[1] = static_cast<float>(sy * 0.5);
    desc.half_extents[2] = static_cast<float>(sz * 0.5);
    desc.radius = static_cast<float>(sx * 0.5);
    if (desc.shape_type == BodyShapeType::Capsule) {
        float r = desc.radius;
        float total_h = static_cast<float>(sy);
        desc.half_height = std::max(0.01f, (total_h - 2.0f * r) * 0.5f);
    } else {
        desc.half_height = static_cast<float>(sy * 0.5);
    }

    uint32_t id = m_physics_world->create_body(desc);
    return static_cast<double>(id);
}

double EngineContext::destroy_rigid_body(double body_id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_is_initialized) {
        return GMResult::ERR_NOT_INITIALIZED;
    }

    bool res = m_physics_world->destroy_body(static_cast<uint32_t>(body_id));
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::add_impulse(double body_id, double ix, double iy, double iz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->add_impulse(
        static_cast<uint32_t>(body_id),
        static_cast<float>(ix), static_cast<float>(iy), static_cast<float>(iz)
    );
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::add_torque(double body_id, double tx, double ty, double tz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->add_torque(
        static_cast<uint32_t>(body_id),
        static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz)
    );
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::add_force(double body_id, double fx, double fy, double fz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->add_force(
        static_cast<uint32_t>(body_id),
        static_cast<float>(fx), static_cast<float>(fy), static_cast<float>(fz)
    );
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::set_velocity(double body_id, double vx, double vy, double vz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->set_linear_velocity(
        static_cast<uint32_t>(body_id),
        static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)
    );
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::set_gravity(double gx, double gy, double gz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    m_physics_world->set_gravity(static_cast<float>(gx), static_cast<float>(gy), static_cast<float>(gz));
    return GMResult::OK;
}

double EngineContext::set_friction(double body_id, double friction) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->set_friction(static_cast<uint32_t>(body_id), static_cast<float>(friction));
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::set_restitution(double body_id, double restitution) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->set_restitution(static_cast<uint32_t>(body_id), static_cast<float>(restitution));
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::set_body_transform(double body_id, double x, double y, double z, double qx, double qy, double qz, double qw) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_physics_world) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_physics_world->set_position_and_rotation(
        static_cast<uint32_t>(body_id),
        static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
        static_cast<float>(qx), static_cast<float>(qy), static_cast<float>(qz), static_cast<float>(qw)
    );
    return res ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::load_mesh_obj(const char* filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world || filepath == nullptr) return 0.0;
    uint16_t id = m_render_world->load_model(filepath);
    return static_cast<double>(id);
}

double EngineContext::load_mesh_glb(const char* filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world || filepath == nullptr) return 0.0;
    uint16_t id = m_render_world->load_model(filepath);
    return static_cast<double>(id);
}

double EngineContext::load_mesh_model(const char* filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world || filepath == nullptr) return 0.0;
    uint16_t id = m_render_world->load_model(filepath);
    return static_cast<double>(id);
}

double EngineContext::load_texture(const char* filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world || filepath == nullptr) return 0.0;
    uint16_t id = m_render_world->load_texture(filepath);
    return static_cast<double>(id);
}

double EngineContext::set_normal_map(double target_tex_id, double normal_tex_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    bool ok = m_render_world->set_normal_map(static_cast<uint16_t>(target_tex_id), static_cast<uint16_t>(normal_tex_id));
    return ok ? GMResult::OK : GMResult::ERR_INVALID_ARGUMENT;
}

double EngineContext::material_set_pbr(double mat_id, double r, double g, double b, double a, double roughness, double metallic, double emissive) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    uint16_t id = static_cast<uint16_t>(mat_id);
    MaterialGPU props{};
    const MaterialGPU* existing = m_render_world->get_material_properties(id);
    if (existing) props = *existing;
    props.base_color[0] = static_cast<float>(r);
    props.base_color[1] = static_cast<float>(g);
    props.base_color[2] = static_cast<float>(b);
    props.base_color[3] = static_cast<float>(a);
    props.roughness = static_cast<float>(roughness);
    props.metallic = static_cast<float>(metallic);
    props.emissive = static_cast<float>(emissive);
    props.preset_id = 0; // Custom PBR
    m_render_world->set_material_properties(id, props);
    return GMResult::OK;
}

double EngineContext::material_set_textures(double mat_id, double albedo_tex_id, double normal_tex_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    uint16_t id = static_cast<uint16_t>(mat_id);
    MaterialGPU props{};
    const MaterialGPU* existing = m_render_world->get_material_properties(id);
    if (existing) props = *existing;
    m_render_world->set_material_properties(id, props, static_cast<uint16_t>(albedo_tex_id), static_cast<uint16_t>(normal_tex_id));
    return GMResult::OK;
}

double EngineContext::material_set_uv(double mat_id, double tiling_u, double tiling_v, double offset_u, double offset_v) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    uint16_t id = static_cast<uint16_t>(mat_id);
    MaterialGPU props{};
    const MaterialGPU* existing = m_render_world->get_material_properties(id);
    if (existing) props = *existing;
    props.uv_tiling[0] = static_cast<float>(tiling_u);
    props.uv_tiling[1] = static_cast<float>(tiling_v);
    props.uv_offset[0] = static_cast<float>(offset_u);
    props.uv_offset[1] = static_cast<float>(offset_v);
    m_render_world->set_material_properties(id, props);
    return GMResult::OK;
}

double EngineContext::material_set_alpha(double mat_id, double alpha_mode, double alpha_cutoff) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    uint16_t id = static_cast<uint16_t>(mat_id);
    MaterialGPU props{};
    const MaterialGPU* existing = m_render_world->get_material_properties(id);
    if (existing) props = *existing;
    props.alpha_mode = static_cast<int32_t>(alpha_mode);
    props.alpha_cutoff = static_cast<float>(alpha_cutoff);
    m_render_world->set_material_properties(id, props);
    return GMResult::OK;
}

double EngineContext::material_clear_all() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->clear_materials();
    return GMResult::OK;
}

double EngineContext::set_sun_light(double dx, double dy, double dz, double r, double g, double b, double intensity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_sun_light(
        static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz),
        static_cast<float>(r), static_cast<float>(g), static_cast<float>(b),
        static_cast<float>(intensity)
    );
    return GMResult::OK;
}

double EngineContext::set_ambient_light(double r, double g, double b, double intensity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_ambient_light(
        static_cast<float>(r), static_cast<float>(g), static_cast<float>(b),
        static_cast<float>(intensity)
    );
    return GMResult::OK;
}

double EngineContext::set_point_light(double index, double x, double y, double z, double radius, double r, double g, double b, double intensity, double cast_shadow) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_point_light(
        static_cast<uint32_t>(index),
        static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
        static_cast<float>(radius),
        static_cast<float>(r), static_cast<float>(g), static_cast<float>(b),
        static_cast<float>(intensity),
        cast_shadow > 0.5
    );
    return GMResult::OK;
}

double EngineContext::clear_point_lights() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->clear_point_lights();
    return GMResult::OK;
}

double EngineContext::set_shadow_settings(double enabled, double strength, double softness, double coverage) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_shadow_settings(
        enabled > 0.5,
        static_cast<float>(strength),
        static_cast<float>(softness),
        static_cast<float>(coverage)
    );
    return GMResult::OK;
}

double EngineContext::set_shadow_quality(double quality, double csm_enabled, double max_shadow_lights) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_shadow_quality(
        static_cast<int>(quality),
        csm_enabled > 0.5,
        static_cast<int>(max_shadow_lights)
    );
    return GMResult::OK;
}

double EngineContext::set_sky_color(double r, double g, double b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_sky_color(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
    return GMResult::OK;
}

double EngineContext::set_fog(double start, double end, double r, double g, double b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_fog(static_cast<float>(start), static_cast<float>(end), static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
    return GMResult::OK;
}

double EngineContext::set_gizmo(double enabled, double x, double y, double z, double sx, double sy, double sz, double axis_len) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_gizmo(enabled > 0.5,
                              static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                              static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz),
                              static_cast<float>(axis_len));
    return GMResult::OK;
}

double EngineContext::particle_spawn_burst(double type, double x, double y, double z, double count, double speed, double size, double r, double g, double b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->particle_burst(static_cast<uint32_t>(type),
                                   static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                   static_cast<uint32_t>(count), static_cast<float>(speed), static_cast<float>(size),
                                   static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
    return GMResult::OK;
}

double EngineContext::particle_spawn_ring(double type, double x, double y, double z, double radius, double count, double r, double g, double b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->particle_ring(static_cast<uint32_t>(type),
                                  static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                  static_cast<float>(radius), static_cast<uint32_t>(count),
                                  static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
    return GMResult::OK;
}

double EngineContext::particle_clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->particle_clear();
    return GMResult::OK;
}

double EngineContext::set_bloom(double enabled, double intensity, double threshold) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_bloom(enabled > 0.5, static_cast<float>(intensity), static_cast<float>(threshold));
    return GMResult::OK;
}

double EngineContext::terrain_generate(double grid_size, double world_size, double height_scale, double seed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world || !m_physics_world) return 0.0;
    uint32_t body_id = m_render_world->generate_terrain(
        static_cast<uint32_t>(grid_size),
        static_cast<float>(world_size),
        static_cast<float>(height_scale),
        static_cast<uint32_t>(seed),
        *m_physics_world
    );
    return static_cast<double>(body_id);
}

double EngineContext::terrain_get_height(double x, double z) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return 0.0;
    return static_cast<double>(m_render_world->get_terrain_height(static_cast<float>(x), static_cast<float>(z)));
}

double EngineContext::terrain_apply_edit_data(const void* data, double size_bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world || !m_physics_world) return GMResult::ERR_NOT_INITIALIZED;
    auto& terrain = m_render_world->get_terrain_system();
    if (!terrain.is_generated() || !data || !std::isfinite(size_bytes) || size_bytes < 12 ||
        size_bytes > 3 * 1024 * 1024 || std::floor(size_bytes) != size_bytes)
        return GMResult::ERR_INVALID_ARGUMENT;

    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t counts[3];
    std::memcpy(counts, bytes, sizeof(counts));
    const size_t samples = static_cast<size_t>(terrain.get_config().grid_size) * terrain.get_config().grid_size;
    if ((counts[0] != 0 && counts[0] != samples) ||
        (counts[1] != 0 && counts[1] != samples * 4) || (counts[2] != 0 && counts[2] != 4))
        return GMResult::ERR_INVALID_ARGUMENT;
    const size_t expected = sizeof(counts) + size_t(counts[0]) * sizeof(float) + counts[1] + size_t(counts[2]) * 5 * sizeof(float);
    if (size_bytes != static_cast<double>(expected)) return GMResult::ERR_INVALID_ARGUMENT;

    const uint8_t* cursor = bytes + sizeof(counts);
    std::vector<float> heights(counts[0]);
    if (!heights.empty()) std::memcpy(heights.data(), cursor, heights.size() * sizeof(float));
    cursor += heights.size() * sizeof(float);
    for (float h : heights) if (!std::isfinite(h)) return GMResult::ERR_INVALID_ARGUMENT;
    std::vector<uint8_t> splat(cursor, cursor + counts[1]);
    cursor += counts[1];
    std::vector<GM3D::TerrainLayer> layers(counts[2]);
    for (auto& layer : layers) {
        float values[5];
        std::memcpy(values, cursor, sizeof(values)); cursor += sizeof(values);
        for (float value : values) if (!std::isfinite(value)) return GMResult::ERR_INVALID_ARGUMENT;
        for (size_t i = 0; i < 3; ++i) layer.color[i] = values[i];
        layer.roughness = values[3]; layer.scale = values[4];
    }

    if (!heights.empty() && !terrain.replace_heights(heights)) return GMResult::ERR_GENERAL;
    terrain.set_splatmap(splat);
    terrain.set_layers(layers);
    auto* device = m_render_world->get_device();
    auto* context = m_render_world->get_context();
    if (!heights.empty() && !terrain.update_mesh(device, m_render_world->get_mesh_manager())) return GMResult::ERR_GENERAL;
    const float half = terrain.get_config().world_size * 0.5f;
    if (!terrain.update_albedo(device, context, m_render_world->get_texture_manager(), -half, -half, half, half))
        return GMResult::ERR_GENERAL;
    if (!heights.empty()) terrain.update_collider(*m_physics_world);
    const auto body = terrain.get_physics_body_id();
    return body ? static_cast<double>(body) : GMResult::ERR_GENERAL;
}

double EngineContext::anim_play(double entity_id, double anim_index, double speed, double loop) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_render_world->play_animation(
        static_cast<uint32_t>(entity_id),
        static_cast<int32_t>(anim_index),
        static_cast<float>(speed),
        loop > 0.5
    );
    return res ? GMResult::OK : GMResult::ERR_GENERAL;
}

double EngineContext::anim_stop(double entity_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_render_world->stop_animation(static_cast<uint32_t>(entity_id));
    return res ? GMResult::OK : GMResult::ERR_GENERAL;
}

double EngineContext::anim_set_time(double entity_id, double time) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    bool res = m_render_world->set_animation_time(static_cast<uint32_t>(entity_id), static_cast<float>(time));
    return res ? GMResult::OK : GMResult::ERR_GENERAL;
}

double EngineContext::anim_get_count(double mesh_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return 0.0;
    return static_cast<double>(m_render_world->get_animation_count(static_cast<uint16_t>(mesh_id)));
}

std::string EngineContext::anim_get_name(double mesh_id, double anim_index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return "";
    return m_render_world->get_animation_name(static_cast<uint16_t>(mesh_id), static_cast<uint32_t>(anim_index));
}

double EngineContext::anim_get_duration(double mesh_id, double anim_index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return 0.0;
    return static_cast<double>(m_render_world->get_animation_duration(static_cast<uint16_t>(mesh_id), static_cast<uint32_t>(anim_index)));
}

double EngineContext::raycast(double fx, double fy, double fz, double tx, double ty, double tz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized) return 0.0;
    m_last_raycast = m_physics_world->cast_ray(
        static_cast<float>(fx), static_cast<float>(fy), static_cast<float>(fz),
        static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz)
    );
    return m_last_raycast.has_hit ? static_cast<double>(m_last_raycast.body_id) : 0.0;
}


double EngineContext::grass_set_enabled(double enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_grass_enabled(enabled > 0.5);
    return GMResult::OK;
}

double EngineContext::grass_set_wind(double dir_x, double dir_z, double speed, double strength) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->set_grass_wind(static_cast<float>(dir_x), static_cast<float>(dir_z),
                                   static_cast<float>(speed), static_cast<float>(strength));
    return GMResult::OK;
}

double EngineContext::grass_rebuild(double density, double seed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->rebuild_grass(static_cast<float>(density), static_cast<uint32_t>(seed));
    return GMResult::OK;
}

double EngineContext::grass_get_count() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return 0.0;
    return static_cast<double>(m_render_world->get_grass_system().get_instance_count());
}

double EngineContext::grass_clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    m_render_world->clear_grass();
    return GMResult::OK;
}

double EngineContext::grass_load_file(const char* filepath) {
    if (!filepath) return GMResult::ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    bool ok = m_render_world->load_grass_file(filepath);
    return ok ? GMResult::OK : GMResult::ERR_GENERAL;
}

double EngineContext::grass_save_file(const char* filepath) {
    if (!filepath) return GMResult::ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_is_initialized || !m_render_world) return GMResult::ERR_NOT_INITIALIZED;
    bool ok = m_render_world->save_grass_file(filepath);
    return ok ? GMResult::OK : GMResult::ERR_GENERAL;
}
