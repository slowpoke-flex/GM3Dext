var _dt = delta_time / 1000000.0;
if (_dt > 0.1) _dt = 0.1;   // a hitch must not teleport the character

var _desired = char_yaw;     // facing target; the body keeps its heading unless told otherwise

// ---------------------------------------------------------------------------------------------
// Camera + controller, modelled on WoW's default bindings:
//   W/S forward and backpedal, A/D and Q/E strafe, Shift runs, Space jumps,
//   right mouse turns camera *and* character (cursor captured),
//   wheel zooms, and the body keeps facing the camera while strafing or backing up.
// ---------------------------------------------------------------------------------------------
if (keyboard_check_pressed(ord("1"))) {
    follow = true;
    cam_yaw = char_yaw;
    cam_pitch = 14;
}

if (follow && instance_exists(player)) {
    var _win_w = window_get_width();
    var _win_h = window_get_height();
    var _cx = _win_w * 0.5;
    var _cy = _win_h * 0.5;

    // --- mouse look ---------------------------------------------------------------------
    // Deltas from the previous position, not "distance to the window centre": recentring the
    // cursor every frame only works if the warp really lands where we asked, and when it does
    // not (scaled window, lost focus) the residual offset turns into a constant spin. The
    // cursor is only pushed back when it reaches the edge, where deltas would stop.
    var _cur_x = window_mouse_get_x();
    var _cur_y = window_mouse_get_y();

    if (mouse_check_button_pressed(mb_right)) {
        mouse_free = false;
        window_set_cursor(cr_none);
        mouse_prev_x = _cur_x;
        mouse_prev_y = _cur_y;
    }

    if (!mouse_free && mouse_check_button(mb_right) && window_has_focus()) {
        var _dx = _cur_x - mouse_prev_x;
        var _dy = _cur_y - mouse_prev_y;
        cam_yaw += _dx * 0.22;                                // mouse right turns right
        cam_pitch = clamp(cam_pitch + _dy * 0.18, -20, 60);   // mouse up (dy < 0) tilts the view up
        mouse_prev_x = _cur_x;
        mouse_prev_y = _cur_y;

        if (_cur_x < 48 || _cur_x > _win_w - 48 || _cur_y < 48 || _cur_y > _win_h - 48) {
            window_mouse_set(_cx, _cy);
            mouse_prev_x = _cx;
            mouse_prev_y = _cy;
        }
    }

    if (!mouse_free && (mouse_check_button_released(mb_right) || !window_has_focus())) {
        mouse_free = true;
        window_set_cursor(cr_default);
    }

    cam_dist = clamp(cam_dist - (mouse_wheel_up() - mouse_wheel_down()) * 0.8, 2.0, 14.0);

    // --- movement, relative to the camera -----------------------------------------------
    var _fwd = keyboard_check(ord("W")) - keyboard_check(ord("S"));
    var _strafe = clamp((keyboard_check(ord("D")) + keyboard_check(ord("E"))) -
                        (keyboard_check(ord("A")) + keyboard_check(ord("Q"))), -1, 1);
    var _sprint = keyboard_check(vk_shift);

    var _yaw = degtorad(cam_yaw);
    var _fx = sin(_yaw), _fz = cos(_yaw);
    var _rx = cos(_yaw), _rz = -sin(_yaw);

    var _vx = _fx * _fwd + _rx * _strafe;
    var _vz = _fz * _fwd + _rz * _strafe;
    var _len = point_distance(0, 0, _vx, _vz);
    var _moving = _len > 0.001;
    if (_moving) {
        _vx /= _len;
        _vz /= _len;
    }

    var _target = 0;
    if (_moving) _target = (_fwd < 0) ? 4.1 : (_sprint ? 7.2 : 4.4);
    move_speed += (_target - move_speed) * min(1, 14 * _dt);

    if (_moving) {
        player.x += _vx * move_speed * _dt;
        player.z += _vz * move_speed * _dt;
    }

    // --- facing -------------------------------------------------------------------------
    // WoW rules: forward+strafe faces the diagonal, pure strafe keeps the body on the camera
    // heading, backpedal keeps it forward, and stopping leaves the body facing where it went.
    // Only the right mouse button swings the body round while standing still.
    if (_moving) {
        _desired = (_fwd > 0) ? darctan2(_vx, _vz) : cam_yaw;
    }
    if (!mouse_free) _desired = cam_yaw;

    // NB: GameMaker's angle_difference(a, b) returns a - b (the delta FROM b TO a), not the
    // delta from a to b. Passing (char_yaw, _desired) here makes the body chase its target in
    // the wrong direction and spin 360 degrees over and over - the shake seen on W+D.
    var _turn = 720 * _dt;
    var _diff = angle_difference(_desired, char_yaw);
    char_yaw += (_diff > _turn) ? _turn : ((_diff < -_turn) ? -_turn : _diff);

    var _half = degtorad(char_yaw) * 0.5;
    player.qx = 0;
    player.qy = sin(_half);
    player.qz = 0;
    player.qw = cos(_half);

    // --- ground and jump ----------------------------------------------------------------
    var _ground = gm3d_terrain_get_height(player.x, player.z);
    if (grounded && keyboard_check_pressed(vk_space)) {
        vy = 6.2;
        grounded = false;
    }
    if (!grounded) {
        vy -= 18.0 * _dt;
        player.y += vy * _dt;
        if (player.y <= _ground) {
            player.y = _ground;
            vy = 0;
            grounded = true;
        }
    } else {
        player.y = _ground;
    }

    // --- animation ----------------------------------------------------------------------
    var _state = "idle";
    if (!grounded) _state = "air";
    else if (move_speed > 0.15) _state = _sprint ? "jog" : "walk";

    if (_state != anim_state) {
        anim_state = _state;
        switch (_state) {
            case "air":  gm3d_anim_play(player.id, CLIP_AIR, 1.0, true);  break;
            case "jog":  gm3d_anim_play(player.id, CLIP_JOG, 1.0, true);  break;
            case "walk": gm3d_anim_play(player.id, CLIP_WALK, 1.0, true); break;
            default:     gm3d_anim_play(player.id, CLIP_IDLE, 1.0, true);
        }
    }

    // --- camera -------------------------------------------------------------------------
    var _pitch = degtorad(cam_pitch);
    var _cam_x = player.x - _fx * cam_dist * cos(_pitch);
    var _cam_z = player.z - _fz * cam_dist * cos(_pitch);
    var _cam_y = player.y + 1.55 + cam_dist * sin(_pitch);

    // Keep the camera above the hill it would otherwise end up inside.
    _cam_y = max(_cam_y, gm3d_terrain_get_height(_cam_x, _cam_z) + 0.45);

    cam_px = _cam_x;
    cam_py = _cam_y;
    cam_pz = _cam_z;
    cam_tx = player.x;
    cam_ty = player.y + 1.45;
    cam_tz = player.z;
}

gm3d_camera_set(cam_px, cam_py, cam_pz, cam_tx, cam_ty, cam_tz, 60.0);

// ---------------------------------------------------------------------------------------------
// Push the entities into the shared buffer the DLL reads, then tick it.
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

update_framework(_dt);
