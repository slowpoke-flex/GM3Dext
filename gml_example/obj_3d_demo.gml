// ==============================================================================
// obj_3d_demo: Interactive 3D Physics and Rendering Demo for GameMaker
// ==============================================================================

// --- CREATE EVENT ---
function demo_create() {
    // 1. Initialize 3D Engine
    gm3d_init(5000);

    // 2. Camera Controls Setup
    cam_dist = 28.0;
    cam_yaw = 45.0;
    cam_pitch = 30.0;
    mouse_prev_x = mouse_x;
    mouse_prev_y = mouse_y;

    // 3. Create Ground Floor (Static Plane, Size: 50x1x50)
    var _floor_body = gm3d_body_create_box(0, -0.5, 0, 50, 1, 50, 0.0, GM3D_MOTION_STATIC);
    var _floor = instance_create_depth(0, 0, 0, obj_3d_entity);
    _floor.x = 0; _floor.y = -0.5; _floor.z = 0;
    _floor.qx = 0; _floor.qy = 0; _floor.qz = 0; _floor.qw = 1;
    _floor.image_xscale = 50; _floor.image_yscale = 1; _floor.image_zscale = 50;
    _floor.mesh_id = GM3D_MESH_PLANE;
    _floor.flags = GM3D_FLAG_ACTIVE;
    _floor.physics_body_id = _floor_body;

    // 4. Spawn a tower of falling physics cubes and spheres!
    for (var _y = 1; _y <= 6; _y++) {
        for (var _x = -2; _x <= 2; _x++) {
            var _spawn_x = _x * 2.2 + random_range(-0.1, 0.1);
            var _spawn_y = _y * 2.5 + 2.0;
            var _spawn_z = random_range(-0.2, 0.2);

            var _is_sphere = (_y % 2 == 0);
            var _mesh = _is_sphere ? GM3D_MESH_SPHERE : GM3D_MESH_CUBE;
            var _shape = _is_sphere ? GM3D_SHAPE_SPHERE : GM3D_SHAPE_BOX;

            var _bid = create_physics_body(_shape, GM3D_MOTION_DYNAMIC, 1.0, _spawn_x, _spawn_y, _spawn_z, 1.5, 1.5, 1.5);
            
            var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
            _ent.x = _spawn_x; _ent.y = _spawn_y; _ent.z = _spawn_z;
            _ent.qx = 0; _ent.qy = 0; _ent.qz = 0; _ent.qw = 1;
            _ent.image_xscale = 1.5; _ent.image_yscale = 1.5; _ent.image_zscale = 1.5;
            _ent.mesh_id = _mesh;
            _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_RIGIDBODY;
            _ent.physics_body_id = _bid;
        }
    }
}

// --- STEP EVENT ---
function demo_step() {
    var _dt = delta_time / 1000000.0;

    // --- Camera Orbit Control ---
    if (mouse_check_button(mb_right)) {
        var _dx = mouse_x - mouse_prev_x;
        var _dy = mouse_y - mouse_prev_y;
        cam_yaw += _dx * 0.4;
        cam_pitch = clamp(cam_pitch - _dy * 0.4, 5.0, 85.0);
    }
    mouse_prev_x = mouse_x;
    mouse_prev_y = mouse_y;

    if (keyboard_check(vk_left))  cam_yaw -= 1.5;
    if (keyboard_check(vk_right)) cam_yaw += 1.5;
    if (keyboard_check(vk_up))    cam_pitch = min(cam_pitch + 1.0, 85.0);
    if (keyboard_check(vk_down))  cam_pitch = max(cam_pitch - 1.0, 5.0);

    // Zoom (Mouse Wheel)
    if (mouse_wheel_up())   cam_dist = max(5.0, cam_dist - 2.0);
    if (mouse_wheel_down()) cam_dist = min(80.0, cam_dist + 2.0);

    // Calculate Camera Position
    var _yaw_rad = degtorad(cam_yaw);
    var _pitch_rad = degtorad(cam_pitch);
    var _cam_x = cam_dist * cos(_pitch_rad) * sin(_yaw_rad);
    var _cam_y = cam_dist * sin(_pitch_rad);
    var _cam_z = -cam_dist * cos(_pitch_rad) * cos(_yaw_rad);

    gm3d_camera_set(_cam_x, _cam_y, _cam_z, 0, 2.0, 0, 60.0);

    // --- Spawn New Object on SPACE ---
    if (keyboard_check_pressed(vk_space)) {
        var _spawn_x = random_range(-5, 5);
        var _spawn_y = 15.0;
        var _spawn_z = random_range(-5, 5);
        var _mesh = choose(GM3D_MESH_CUBE, GM3D_MESH_SPHERE, GM3D_MESH_CYLINDER);
        var _shape = (_mesh == GM3D_MESH_SPHERE) ? GM3D_SHAPE_SPHERE : GM3D_SHAPE_BOX;

        var _bid = create_physics_body(_shape, GM3D_MOTION_DYNAMIC, 2.0, _spawn_x, _spawn_y, _spawn_z, 1.8, 1.8, 1.8);
        gm3d_body_apply_impulse(_bid, random_range(-2, 2), -5, random_range(-2, 2));

        var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        _ent.x = _spawn_x; _ent.y = _spawn_y; _ent.z = _spawn_z;
        _ent.qx = 0; _ent.qy = 0; _ent.qz = 0; _ent.qw = 1;
        _ent.image_xscale = 1.8; _ent.image_yscale = 1.8; _ent.image_zscale = 1.8;
        _ent.mesh_id = _mesh;
        _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_RIGIDBODY;
        _ent.physics_body_id = _bid;
    }

    // --- Shoot Physics Projectile on LEFT CLICK ---
    if (mouse_check_button_pressed(mb_left)) {
        var _proj_bid = create_physics_body(GM3D_SHAPE_SPHERE, GM3D_MOTION_DYNAMIC, 10.0, _cam_x, _cam_y, _cam_z, 1.2, 1.2, 1.2);
        
        // Direction towards target center
        var _dir_x = (0 - _cam_x);
        var _dir_y = (2.0 - _cam_y);
        var _dir_z = (0 - _cam_z);
        var _len = point_distance_3d(0, 0, 0, _dir_x, _dir_y, _dir_z);
        if (_len > 0) {
            _dir_x = (_dir_x / _len) * 35.0;
            _dir_y = (_dir_y / _len) * 35.0;
            _dir_z = (_dir_z / _len) * 35.0;
        }

        gm3d_body_set_velocity(_proj_bid, _dir_x, _dir_y, _dir_z);

        var _p_ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        _p_ent.x = _cam_x; _p_ent.y = _cam_y; _p_ent.z = _cam_z;
        _p_ent.qx = 0; _p_ent.qy = 0; _p_ent.qz = 0; _p_ent.qw = 1;
        _p_ent.image_xscale = 1.2; _p_ent.image_yscale = 1.2; _p_ent.image_zscale = 1.2;
        _p_ent.mesh_id = GM3D_MESH_SPHERE;
        _p_ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_RIGIDBODY;
        _p_ent.physics_body_id = _proj_bid;
    }

    // --- ZERO-COPY SHARED MEMORY SYNC ---
    var _buff = global.gm3d_buffer;
    var _count = instance_number(obj_3d_entity);
    buffer_poke(_buff, 8, buffer_u32, _count);

    var _idx = 0;
    with (obj_3d_entity) {
        var _offset = GM3D_HEADER_SIZE + (_idx * GM3D_ENTITY_SIZE);
        buffer_seek(_buff, buffer_seek_start, _offset);

        // Position
        buffer_write(_buff, buffer_f32, x);
        buffer_write(_buff, buffer_f32, y);
        buffer_write(_buff, buffer_f32, z);

        // Quaternion
        buffer_write(_buff, buffer_f32, qx);
        buffer_write(_buff, buffer_f32, qy);
        buffer_write(_buff, buffer_f32, qz);
        buffer_write(_buff, buffer_f32, qw);

        // Scale
        buffer_write(_buff, buffer_f32, image_xscale);
        buffer_write(_buff, buffer_f32, image_yscale);
        buffer_write(_buff, buffer_f32, image_zscale);

        // IDs & Flags
        buffer_write(_buff, buffer_u32, id);
        buffer_write(_buff, buffer_u16, mesh_id);
        buffer_write(_buff, buffer_u16, 0); // Material ID
        buffer_write(_buff, buffer_u32, flags);
        buffer_write(_buff, buffer_u32, physics_body_id);
        buffer_write(_buff, buffer_f32, 1.0); // Param1
        buffer_write(_buff, buffer_f32, 0.0); // Param2

        _idx++;
    }

    // --- Step Physics and Render via DLL ---
    update_framework(_dt);

    // --- Read back updated dynamic physics transforms ---
    _idx = 0;
    with (obj_3d_entity) {
        if (flags & GM3D_FLAG_RIGIDBODY) {
            var _offset = GM3D_HEADER_SIZE + (_idx * GM3D_ENTITY_SIZE);
            x  = buffer_peek(_buff, _offset + 0,  buffer_f32);
            y  = buffer_peek(_buff, _offset + 4,  buffer_f32);
            z  = buffer_peek(_buff, _offset + 8,  buffer_f32);
            qx = buffer_peek(_buff, _offset + 12, buffer_f32);
            qy = buffer_peek(_buff, _offset + 16, buffer_f32);
            qz = buffer_peek(_buff, _offset + 20, buffer_f32);
            qw = buffer_peek(_buff, _offset + 24, buffer_f32);
        }
        _idx++;
    }
}

// --- DRAW GUI EVENT ---
function demo_draw_gui() {
    draw_set_color(c_black);
    draw_set_alpha(0.6);
    draw_rectangle(10, 10, 480, 150, false);
    draw_set_alpha(1.0);

    draw_set_color(c_white);
    draw_text(20, 20, "GM3D Engine (Jolt Physics + Direct3D 11 PBR)");
    draw_text(20, 40, "FPS: " + string(fps_real) + " | Step FPS: " + string(fps));
    draw_text(20, 60, "Active 3D Entities: " + string(instance_number(obj_3d_entity)));
    draw_text(20, 85, "Controls:");
    draw_text(20, 105, "- SPACE: Spawn random falling physics body");
    draw_text(20, 125, "- LEFT CLICK: Shoot heavy physics cannonball");
    draw_text(20, 145, "- RIGHT CLICK DRAG / ARROWS: Orbit Camera | WHEEL: Zoom");
}

// --- CLEANUP EVENT ---
function demo_cleanup() {
    gm3d_cleanup();
}
