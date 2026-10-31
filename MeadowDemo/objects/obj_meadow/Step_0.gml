var _dt = delta_time / 1000000.0;

// ---------------------------------------------------------------------------------------------
// Camera + controller
//
// "1" takes control: the camera drops behind the character and WASD walks him around the
// meadow, WoW style - strafe on A/D, right mouse drags the camera around him.
// ---------------------------------------------------------------------------------------------
if (keyboard_check_pressed(ord("1"))) {
    follow = true;
}

if (follow && instance_exists(player)) {
    if (mouse_check_button(mb_right)) {
        cam_yaw -= (window_mouse_get_x() - mouse_prev_x) * 0.25;
        cam_pitch = clamp(cam_pitch + (window_mouse_get_y() - mouse_prev_y) * 0.20, -10, 55);
    }
    mouse_prev_x = window_mouse_get_x();
    mouse_prev_y = window_mouse_get_y();

    var _fwd = keyboard_check(ord("W")) - keyboard_check(ord("S"));
    var _strafe = keyboard_check(ord("D")) - keyboard_check(ord("A"));
    var _running = keyboard_check(vk_shift);
    var _speed = _running ? 4.6 : 1.7;

    var _rad = degtorad(cam_yaw);
    var _fx = sin(_rad), _fz = cos(_rad);        // camera forward on the ground plane
    var _rx = cos(_rad), _rz = -sin(_rad);       // camera right

    var _mx = _fx * _fwd + _rx * _strafe;
    var _mz = _fz * _fwd + _rz * _strafe;
    var _len = point_distance(0, 0, _mx, _mz);
    var _moving = _len > 0.001;

    if (_moving) {
        _mx /= _len;
        _mz /= _len;
        player.x += _mx * _speed * _dt;
        player.z += _mz * _speed * _dt;
        player.y = gm3d_terrain_get_height(player.x, player.z) + 0.02;

        // Face the direction of travel (quaternion around Y).
        var _yaw = darctan2(_mx, _mz) * 0.5;
        player.qx = 0;
        player.qy = sin(_yaw);
        player.qz = 0;
        player.qw = cos(_yaw);
    }

    if (_moving != was_moving || _running != anim_running) {
        var _clip = _moving ? (_running ? 13 : 42) : 9;
        gm3d_anim_play(player.id, _clip, _running ? 1.0 : 1.1, true);
        was_moving = _moving;
        anim_running = _running;
    }

    // Orbit camera: sit on the ground-plane forward vector, lifted by the pitch.
    var _dist = cam_dist;
    var _pitch = degtorad(cam_pitch);
    cam_px = player.x - _fx * _dist * cos(_pitch);
    cam_pz = player.z - _fz * _dist * cos(_pitch);
    cam_py = player.y + 1.4 + _dist * sin(_pitch);
    cam_tx = player.x;
    cam_ty = player.y + 1.35;
    cam_tz = player.z;
}

gm3d_camera_set(cam_px, cam_py, cam_pz, cam_tx, cam_ty, cam_tz, 60.0);

// ---------------------------------------------------------------------------------------------
// Push the entities into the shared buffer the DLL reads.
//
// The showcase object (obj_3d_demo) used to be the only thing doing this, and it also called
// update_framework(). Without the first block the DLL draws terrain and grass but nothing else,
// which is exactly what a missing character looks like.
// ---------------------------------------------------------------------------------------------
var _buff = global.gm3d_buffer;
var _count = instance_number(obj_3d_entity);
buffer_poke(_buff, 8, buffer_u32, _count);

var _idx = 0;
with (obj_3d_entity) {
    var _offset = GM3D_HEADER_SIZE + (_idx * GM3D_ENTITY_SIZE);
    buffer_seek(_buff, buffer_seek_start, _offset);

    buffer_write(_buff, buffer_f32, x);
    buffer_write(_buff, buffer_f32, y);
    buffer_write(_buff, buffer_f32, z);
    buffer_write(_buff, buffer_f32, qx);
    buffer_write(_buff, buffer_f32, qy);
    buffer_write(_buff, buffer_f32, qz);
    buffer_write(_buff, buffer_f32, qw);
    buffer_write(_buff, buffer_f32, image_xscale);
    buffer_write(_buff, buffer_f32, image_yscale);
    buffer_write(_buff, buffer_f32, image_zscale);
    buffer_write(_buff, buffer_u32, id);
    buffer_write(_buff, buffer_u16, mesh_id);
    buffer_write(_buff, buffer_u16, material_id);
    buffer_write(_buff, buffer_u32, flags);
    buffer_write(_buff, buffer_u32, physics_body_id);
    buffer_write(_buff, buffer_f32, material_id);   // custom_param1: material for PBR
    buffer_write(_buff, buffer_f32, texture_id);    // custom_param2: texture override

    _idx++;
}

// Ticks the simulation and presents the frame. Without this the window stays black.
update_framework(_dt);
