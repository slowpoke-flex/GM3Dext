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
    var _res = init_extension(_ptr, _total_bytes, _hwnd, _w, _h);
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

/// @function gm3d_body_apply_impulse(body_id, ix, iy, iz)
function gm3d_body_apply_impulse(_body_id, _ix, _iy, _iz) {
    return physics_add_impulse(_body_id, _ix, _iy, _iz);
}

/// @function gm3d_body_set_velocity(body_id, vx, vy, vz)
function gm3d_body_set_velocity(_body_id, _vx, _vy, _vz) {
    return physics_set_velocity(_body_id, _vx, _vy, _vz);
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
