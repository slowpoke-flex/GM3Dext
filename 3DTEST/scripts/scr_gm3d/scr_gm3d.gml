// ==============================================================================
// GM3D: High-Performance 3D Engine for GameMaker
// GML SDK & Wrapper Library
// ==============================================================================

#macro GM3D_MAGIC        $474D3344 // 'GM3D'
#macro GM3D_VERSION      1
#macro GM3D_HEADER_SIZE  64
#macro GM3D_ENTITY_SIZE  64

// Built-in Procedural Mesh IDs
#macro GM3D_MESH_CUBE     0
#macro GM3D_MESH_SPHERE   1
#macro GM3D_MESH_CYLINDER 2
#macro GM3D_MESH_PLANE    3
#macro GM3D_MESH_CAPSULE  4
#macro GM3D_MESH_TERRAIN  5
#macro GM3D_MESH_PINE_TREE 6

// Physics Motion Types
#macro GM3D_MOTION_STATIC    0
#macro GM3D_MOTION_KINEMATIC 1
#macro GM3D_MOTION_DYNAMIC   2

// Physics Shapes
#macro GM3D_SHAPE_BOX      0
#macro GM3D_SHAPE_SPHERE   1
#macro GM3D_SHAPE_CAPSULE  2
#macro GM3D_SHAPE_CYLINDER 3

// Entity Flags
#macro GM3D_FLAG_ACTIVE       1
#macro GM3D_FLAG_CAST_SHADOWS 2
#macro GM3D_FLAG_CAST_SHADOW  2
#macro GM3D_FLAG_RIGIDBODY    4
#macro GM3D_FLAG_KINEMATIC    8

/// @function gm3d_init(max_entities)
/// @desc Initializes the GM3D framework, allocates shared memory, and binds D3D11 to the window.
function gm3d_init(_max_entities = 10000) {
    if (variable_global_exists("gm3d_initialized") && global.gm3d_initialized) {
        return true;
    }

    global.gm3d_max_entities = _max_entities;
    var _total_bytes = GM3D_HEADER_SIZE + (_max_entities * GM3D_ENTITY_SIZE);

    // 1. Allocate continuous zero-copy byte buffer
    global.gm3d_buffer = buffer_create(_total_bytes, buffer_fixed, 1);
    var _ptr = buffer_get_address(global.gm3d_buffer);
    var _hwnd = window_handle();

    // 2. Setup initial header
    buffer_seek(global.gm3d_buffer, buffer_seek_start, 0);
    buffer_write(global.gm3d_buffer, buffer_u32, GM3D_MAGIC);
    buffer_write(global.gm3d_buffer, buffer_u32, GM3D_VERSION);
    buffer_write(global.gm3d_buffer, buffer_u32, 0); // count
    buffer_write(global.gm3d_buffer, buffer_u32, _max_entities);

    // Default Camera (Pos: 0, 15, -25, Target: 0, 2, 0, FOV: 60)
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);   // Cam X
    buffer_write(global.gm3d_buffer, buffer_f32, 15.0);  // Cam Y
    buffer_write(global.gm3d_buffer, buffer_f32, -25.0); // Cam Z
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);   // Target X
    buffer_write(global.gm3d_buffer, buffer_f32, 2.0);   // Target Y
    buffer_write(global.gm3d_buffer, buffer_f32, 0.0);   // Target Z
    buffer_write(global.gm3d_buffer, buffer_f32, 60.0);  // FOV
    buffer_write(global.gm3d_buffer, buffer_f32, 0.1);   // Near
    buffer_write(global.gm3d_buffer, buffer_f32, 1000.0);// Far
    buffer_write(global.gm3d_buffer, buffer_u32, 0);     // Flags
    buffer_write(global.gm3d_buffer, buffer_u32, 0);     // Reserved
    buffer_write(global.gm3d_buffer, buffer_u32, 0);

    var _w = window_get_width();
    var _h = window_get_height();
    if (_w <= 0) _w = 1280;
    if (_h <= 0) _h = 720;

    // 3. Call native DLL
    var _res = init_extension(_ptr, _total_bytes, _hwnd);
    if (_res < 0) {
        show_error("[GM3Dext] Initialization failed. Code: " + string(_res), true);
        return false;
    }

    global.gm3d_initialized = true;
    show_debug_message("[GM3Dext] Engine initialized successfully! Version: " + string(get_framework_version()));
    return true;
}

/// @function gm3d_camera_set(x, y, z, tx, ty, tz, fov, near, far)
function gm3d_camera_set(_x, _y, _z, _tx, _ty, _tz, _fov = 60.0, _near = 0.1, _far = 1000.0) {
    if (!global.gm3d_initialized) return;
    var _buf = global.gm3d_buffer;
    buffer_poke(_buf, 16, buffer_f32, _x);
    buffer_poke(_buf, 20, buffer_f32, _y);
    buffer_poke(_buf, 24, buffer_f32, _z);
    buffer_poke(_buf, 28, buffer_f32, _tx);
    buffer_poke(_buf, 32, buffer_f32, _ty);
    buffer_poke(_buf, 36, buffer_f32, _tz);
    buffer_poke(_buf, 40, buffer_f32, _fov);
    buffer_poke(_buf, 44, buffer_f32, _near);
    buffer_poke(_buf, 48, buffer_f32, _far);
}

/// @function gm3d_body_create_box(x, y, z, sx, sy, sz, mass, motion_type)
function gm3d_body_create_box(_x, _y, _z, _sx, _sy, _sz, _mass = 1.0, _motion = GM3D_MOTION_DYNAMIC) {
    return create_physics_body(GM3D_SHAPE_BOX, _motion, _mass, _x, _y, _z, _sx, _sy, _sz);
}

/// @function gm3d_body_create_sphere(x, y, z, radius, mass, motion_type)
function gm3d_body_create_sphere(_x, _y, _z, _radius = 0.5, _mass = 1.0, _motion = GM3D_MOTION_DYNAMIC) {
    var _dia = _radius * 2.0;
    return create_physics_body(GM3D_SHAPE_SPHERE, _motion, _mass, _x, _y, _z, _dia, _dia, _dia);
}

/// @function gm3d_body_create_cylinder(x, y, z, radius, height, mass, motion_type)
function gm3d_body_create_cylinder(_x, _y, _z, _radius = 0.5, _height = 1.0, _mass = 1.0, _motion = GM3D_MOTION_DYNAMIC) {
    return create_physics_body(GM3D_SHAPE_CYLINDER, _motion, _mass, _x, _y, _z, _radius * 2.0, _height, _radius * 2.0);
}

/// @function gm3d_body_create_capsule(x, y, z, radius, height, mass, motion_type)
function gm3d_body_create_capsule(_x, _y, _z, _radius = 0.5, _height = 1.0, _mass = 1.0, _motion = GM3D_MOTION_DYNAMIC) {
    return create_physics_body(GM3D_SHAPE_CAPSULE, _motion, _mass, _x, _y, _z, _radius * 2.0, _height, _radius * 2.0);
}

/// @function gm3d_body_apply_impulse(body_id, ix, iy, iz)
function gm3d_body_apply_impulse(_body_id, _ix, _iy, _iz) {
    return physics_add_impulse(_body_id, _ix, _iy, _iz);
}

/// @function gm3d_body_apply_torque(body_id, tx, ty, tz)
function gm3d_body_apply_torque(_body_id, _tx, _ty, _tz) {
    return physics_add_torque(_body_id, _tx, _ty, _tz);
}

/// @function gm3d_body_apply_force(body_id, fx, fy, fz)
function gm3d_body_apply_force(_body_id, _fx, _fy, _fz) {
    return physics_add_force(_body_id, _fx, _fy, _fz);
}

/// @function gm3d_body_set_velocity(body_id, vx, vy, vz)
function gm3d_body_set_velocity(_body_id, _vx, _vy, _vz) {
    return physics_set_velocity(_body_id, _vx, _vy, _vz);
}

/// @function gm3d_set_gravity(gx, gy, gz)
function gm3d_set_gravity(_gx, _gy, _gz) {
    return physics_set_gravity(_gx, _gy, _gz);
}

/// @function gm3d_body_set_friction(body_id, friction)
function gm3d_body_set_friction(_body_id, _friction) {
    return physics_set_friction(_body_id, _friction);
}

/// @function gm3d_body_set_restitution(body_id, restitution)
function gm3d_body_set_restitution(_body_id, _restitution) {
    return physics_set_restitution(_body_id, _restitution);
}

/// @function gm3d_mesh_load(filepath)
/// @desc Loads any 3D model (.glb, .gltf, .obj) into GPU memory. Returns mesh_id (100+).
function gm3d_mesh_load(_filepath) {
    return load_mesh_model(_filepath);
}

/// @function gm3d_mesh_load_glb(filepath)
/// @desc Loads binary glTF 2.0 (.glb) model into GPU memory. Returns mesh_id (100+).
function gm3d_mesh_load_glb(_filepath) {
    return load_mesh_glb(_filepath);
}

/// @function gm3d_mesh_load_obj(filepath)
function gm3d_mesh_load_obj(_filepath) {
    return load_mesh_obj(_filepath);
}

/// @function gm3d_raycast(from_x, from_y, from_z, to_x, to_y, to_z)
function gm3d_raycast(_fx, _fy, _fz, _tx, _ty, _tz) {
    var _hit_id = physics_raycast(_fx, _fy, _fz, _tx, _ty, _tz);
    if (_hit_id > 0) {
        return {
            has_hit: true,
            body_id: _hit_id,
            x: physics_get_raycast_x(),
            y: physics_get_raycast_y(),
            z: physics_get_raycast_z()
        };
    }
    return { has_hit: false };
}

/// @function gm3d_anim_play(entity_id, anim_index, speed, loop)
/// @desc Plays a skeletal animation on an entity.
function gm3d_anim_play(_entity_id, _anim_index, _speed = 1.0, _loop = true) {
    return anim_play(_entity_id, _anim_index, _speed, _loop ? 1.0 : 0.0);
}

/// @function gm3d_anim_stop(entity_id)
/// @desc Stops skeletal animation on an entity and returns to bind pose.
function gm3d_anim_stop(_entity_id) {
    return anim_stop(_entity_id);
}

/// @function gm3d_anim_set_time(entity_id, time)
/// @desc Sets current playback position of an animation in seconds.
function gm3d_anim_set_time(_entity_id, _time) {
    return anim_set_time(_entity_id, _time);
}

/// @function gm3d_anim_get_count(mesh_id)
/// @desc Returns the number of embedded animations in a 3D model.
function gm3d_anim_get_count(_mesh_id) {
    return anim_get_count(_mesh_id);
}

/// @function gm3d_anim_get_name(mesh_id, anim_index)
/// @desc Returns the name of an embedded animation (e.g. "Walk_Loop", "Dance_Loop").
function gm3d_anim_get_name(_mesh_id, _anim_index) {
    return anim_get_name(_mesh_id, _anim_index);
}

/// @function gm3d_anim_get_duration(mesh_id, anim_index)
/// @desc Returns duration of an animation clip in seconds.
function gm3d_anim_get_duration(_mesh_id, _anim_index) {
    return anim_get_duration(_mesh_id, _anim_index);
}

// ------------------------------------------------------------------------------------------------
// Texture Management Macros & API
// ------------------------------------------------------------------------------------------------
#macro GM3D_TEX_DEFAULT 0 // Pure White 1x1 (neutral PBR material)
#macro GM3D_TEX_CHECKER 1 // UV Test Checkerboard (512x512 Amber/Slate)
#macro GM3D_TEX_GRID    2 // Sci-Fi Neon Tech Grid (512x512 Cyan/Slate)
#macro GM3D_TEX_WOOD    3 // Weathered Wooden Planks (512x512)
#macro GM3D_TEX_METAL   4 // Industrial Hazard Stripes (512x512)
#macro GM3D_TEX_TERRAIN 5 // Procedural Multi-Biome Terrain (Grass, Rock, Snow, Dirt, Sand)

/// @function gm3d_texture_load(filepath)
/// @desc Loads a 2D texture (PNG, JPG, BMP) from disk into GPU VRAM with full hardware mipmapping.
function gm3d_texture_load(_filepath) {
    return texture_load(_filepath);
}

/// @function gm3d_texture_set_normal(target_id, normal_id)
/// @desc Binds a normal map texture to an existing albedo texture slot.
function gm3d_texture_set_normal(_target_id, _normal_id) {
    return texture_set_normal_map(_target_id, _normal_id);
}

// ------------------------------------------------------------------------------------------------
// Dynamic Lighting API (Sun & Dynamic Point Lights)
// ------------------------------------------------------------------------------------------------

/// @function gm3d_light_set_sun(dx, dy, dz, r, g, b, intensity)
/// @desc Sets directional sunlight direction vector and RGB color multiplier.
function gm3d_light_set_sun(_dx, _dy, _dz, _r = 1.0, _g = 0.98, _b = 0.95, _intensity = 1.0) {
    return light_set_sun(_dx, _dy, _dz, _r, _g, _b, _intensity);
}

/// @function gm3d_light_set_ambient(r, g, b, intensity)
/// @desc Sets hemispheric/ambient environmental base lighting.
function gm3d_light_set_ambient(_r = 0.15, _g = 0.18, _b = 0.22, _intensity = 1.0) {
    return light_set_ambient(_r, _g, _b, _intensity);
}

/// @function gm3d_light_point_set(index, x, y, z, radius, r, g, b, intensity)
/// @desc Configures dynamic point light (indices 0..15) with Cook-Torrance PBR attenuation.
function gm3d_light_point_set(_index, _x, _y, _z, _radius, _r = 1.0, _g = 1.0, _b = 1.0, _intensity = 1.0) {
    return light_point_set(_index, _x, _y, _z, _radius, _r, _g, _b, _intensity);
}

/// @function gm3d_light_point_clear()
/// @desc Resets and disables all active dynamic point lights.
function gm3d_light_point_clear() {
    return light_point_clear();
}

/// @function gm3d_body_set_transform(body_id, x, y, z, qx, qy, qz, qw)
/// @desc Instantly teleports / updates rigid body position and quaternion orientation in Jolt Physics.
function gm3d_body_set_transform(_body_id, _x, _y, _z, _qx = 0, _qy = 0, _qz = 0, _qw = 1) {
    return physics_set_transform(_body_id, _x, _y, _z, _qx, _qy, _qz, _qw);
}

/// @function gm3d_set_sky_color(r, g, b)
/// @desc Sets the background viewport clear color and default horizon fog tint.
function gm3d_set_sky_color(_r, _g, _b) {
    return render_set_sky_color(_r, _g, _b);
}

/// @function gm3d_set_fog(start, end, r, g, b)
/// @desc Configures atmospheric distance fog range and RGB color.
function gm3d_set_fog(_start, _end, _r = -1.0, _g = -1.0, _b = -1.0) {
    return render_set_fog(_start, _end, _r, _g, _b);
}

/// @function gm3d_world_to_screen(wx, wy, wz, cx, cy, cz, tx, ty, tz, fov, screen_w, screen_h)
/// @desc Projects a 3D world space coordinate into 2D screen coordinates [x, y, visible].
function gm3d_world_to_screen(_wx, _wy, _wz, _cx, _cy, _cz, _tx, _ty, _tz, _fov = 60.0, _sw = 0, _sh = 0) {
    if (_sw <= 0) _sw = window_get_width();
    if (_sh <= 0) _sh = window_get_height();
    if (_sw <= 0) _sw = 1280;
    if (_sh <= 0) _sh = 720;

    // 1. Z-axis (Forward in LH): target - eye
    var _zx = _tx - _cx;
    var _zy = _ty - _cy;
    var _zz = _tz - _cz;
    var _lenZ = sqrt(_zx * _zx + _zy * _zy + _zz * _zz);
    if (_lenZ > 0.0001) { _zx /= _lenZ; _zy /= _lenZ; _zz /= _lenZ; } else { _zz = 1.0; }

    // 2. X-axis (Right in LH): Up x Z
    var _xx = _zz;
    var _xy = 0.0;
    var _xz = -_zx;
    var _lenX = sqrt(_xx * _xx + _xz * _xz);
    if (_lenX < 0.0001) {
        _xx = 1.0; _xy = 0.0; _xz = 0.0;
    } else {
        _xx /= _lenX; _xz /= _lenX;
    }

    // 3. Y-axis (Up in LH): Z x X
    var _yx = _zy * _xz - _zz * _xy;
    var _yy = _zz * _xx - _zx * _xz;
    var _yz = _zx * _xy - _zy * _xx;

    // 4. View space translation: (WorldPos - CamPos) * ViewBasis
    var _px = _wx - _cx;
    var _py = _wy - _cy;
    var _pz = _wz - _cz;

    var _vx = _px * _xx + _py * _xy + _pz * _xz;
    var _vy = _px * _yx + _py * _yy + _pz * _yz;
    var _vz = _px * _zx + _py * _zy + _pz * _zz;

    if (_vz <= 0.1) return [0, 0, false];

    // 5. Perspective Projection
    var _fovRad = degtorad(_fov);
    var _h = 1.0 / tan(_fovRad * 0.5);
    var _aspect = _sw / _sh;
    var _w = _h / _aspect;

    var _ndc_x = (_vx * _w) / _vz;
    var _ndc_y = (_vy * _h) / _vz;

    var _sx = (_ndc_x + 1.0) * 0.5 * _sw;
    var _sy = (1.0 - _ndc_y) * 0.5 * _sh;

    return [_sx, _sy, true];
}

/// @function gm3d_screen_to_ray(sx, sy, cx, cy, cz, tx, ty, tz, fov, screen_w, screen_h)
/// @desc Computes normalized 3D ray direction vector [dx, dy, dz] from screen coordinates.
function gm3d_screen_to_ray(_sx, _sy, _cx, _cy, _cz, _tx, _ty, _tz, _fov = 60.0, _sw = 0, _sh = 0) {
    if (_sw <= 0) _sw = window_get_width();
    if (_sh <= 0) _sh = window_get_height();
    if (_sw <= 0) _sw = 1280;
    if (_sh <= 0) _sh = 720;

    var _zx = _tx - _cx;
    var _zy = _ty - _cy;
    var _zz = _tz - _cz;
    var _lenZ = sqrt(_zx * _zx + _zy * _zy + _zz * _zz);
    if (_lenZ > 0.0001) { _zx /= _lenZ; _zy /= _lenZ; _zz /= _lenZ; } else { _zz = 1.0; }

    var _xx = _zz;
    var _xy = 0.0;
    var _xz = -_zx;
    var _lenX = sqrt(_xx * _xx + _xz * _xz);
    if (_lenX < 0.0001) {
        _xx = 1.0; _xy = 0.0; _xz = 0.0;
    } else {
        _xx /= _lenX; _xz /= _lenX;
    }

    var _yx = _zy * _xz - _zz * _xy;
    var _yy = _zz * _xx - _zx * _xz;
    var _yz = _zx * _xy - _zy * _xx;

    var _fovRad = degtorad(_fov);
    var _h = 1.0 / tan(_fovRad * 0.5);
    var _aspect = _sw / _sh;
    var _w = _h / _aspect;

    var _ndc_x = (_sx / _sw) * 2.0 - 1.0;
    var _ndc_y = 1.0 - (_sy / _sh) * 2.0;

    var _vx = _ndc_x / _w;
    var _vy = _ndc_y / _h;
    var _vz = 1.0;

    var _dx = _vx * _xx + _vy * _yx + _vz * _zx;
    var _dy = _vx * _xy + _vy * _yy + _vz * _zy;
    var _dz = _vx * _xz + _vy * _yz + _vz * _zz;

    var _dlen = sqrt(_dx * _dx + _dy * _dy + _dz * _dz);
    if (_dlen > 0.0001) { _dx /= _dlen; _dy /= _dlen; _dz /= _dlen; }

    return [_dx, _dy, _dz];
}

/// @function gm3d_cleanup()
function gm3d_cleanup() {
    if (variable_global_exists("gm3d_initialized") && global.gm3d_initialized) {
        shutdown_extension();
        if (variable_global_exists("gm3d_buffer") && buffer_exists(global.gm3d_buffer)) {
            buffer_delete(global.gm3d_buffer);
        }
        global.gm3d_initialized = false;
        show_debug_message("[GM3Dext] Engine shut down.");
    }
}

/// @function gm3d_show_f5_menu(current_mode, invert_y)
/// @desc Displays a native Windows interactive popup menu for technology testing & mode selection.
function gm3d_show_f5_menu(_current_mode, _invert_y) {
    return show_f5_menu(_current_mode, _invert_y ? 1.0 : 0.0);
}

/// @function gm3d_set_gizmo(enabled, x, y, z, sx, sy, sz, axis_len)
/// @desc Renders a 3D transform gizmo and selection box directly in Direct3D 11.
function gm3d_set_gizmo(_enabled, _x, _y, _z, _sx = 1.0, _sy = 1.0, _sz = 1.0, _axis_len = 2.8) {
    return set_gizmo(_enabled ? 1.0 : 0.0, _x, _y, _z, _sx, _sy, _sz, _axis_len);
}

// ------------------------------------------------------------------------------------------------
// GPU Magic Particle System API & Presets
// ------------------------------------------------------------------------------------------------
#macro GM3D_PARTICLE_FIRE      0
#macro GM3D_PARTICLE_ARCANE    1
#macro GM3D_PARTICLE_HOLY      2
#macro GM3D_PARTICLE_FROST     3
#macro GM3D_PARTICLE_SHOCKWAVE 4

/// @function gm3d_particle_burst(type, x, y, z, count, speed, size, r, g, b)
/// @desc Emits a high-speed GPU particle burst with additive glowing blending.
function gm3d_particle_burst(_type, _x, _y, _z, _count = 35, _speed = 4.5, _size = 0.45, _r = -1.0, _g = -1.0, _b = -1.0) {
    return particle_spawn_burst(_type, _x, _y, _z, _count, _speed, _size, _r, _g, _b);
}

/// @function gm3d_particle_ring(type, x, y, z, radius, count, r, g, b)
/// @desc Spawns an expanding horizontal shockwave ring on the ground.
function gm3d_particle_ring(_type, _x, _y, _z, _radius = 6.0, _count = 1, _r = -1.0, _g = -1.0, _b = -1.0) {
    return particle_spawn_ring(_type, _x, _y, _z, _radius, _count, _r, _g, _b);
}

/// @function gm3d_particle_clear()
/// @desc Clears all active particles from GPU and CPU memory.
function gm3d_particle_clear() {
    return particle_clear();
}

// ------------------------------------------------------------------------------------------------
// HDR Bloom & Tone Mapping Post-Processing API
// ------------------------------------------------------------------------------------------------

/// @function gm3d_render_set_bloom(enabled, intensity, threshold)
/// @desc Configures HDR bloom extraction, 2-pass gaussian blur, and ACES filmic tonemapping.
function gm3d_render_set_bloom(_enabled, _intensity = 0.85, _threshold = 1.0) {
    return render_set_bloom(_enabled ? 1.0 : 0.0, _intensity, _threshold);
}

// ------------------------------------------------------------------------------------------------
// Procedural Terrain Generation & Heightfield API (from NewEra3D Algorithms)
// ------------------------------------------------------------------------------------------------

/// @function gm3d_terrain_generate(grid_size, world_size, height_scale, seed)
/// @desc Procedurally generates an eroded terrain mesh, multi-biome albedo + normal map, and Jolt heightfield body.
function gm3d_terrain_generate(_grid_size = 128, _world_size = 240.0, _height_scale = 32.0, _seed = 1337) {
    return terrain_generate(_grid_size, _world_size, _height_scale, _seed);
}

/// @function gm3d_terrain_get_height(x, z)
/// @desc Instant O(1) bilinear query of terrain elevation at any world (X, Z) coordinate.
function gm3d_terrain_get_height(_x, _z) {
    return terrain_get_height(_x, _z);
}
