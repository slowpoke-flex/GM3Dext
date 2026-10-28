var _dt = delta_time / 1000000.0;

// ------------------------------------------------------------------------------------------------
// F5 Interactive Technology Menu & Quick Mode Hotkeys (1..5, Y)
// ------------------------------------------------------------------------------------------------
if (keyboard_check_pressed(vk_f5)) {
    var _cmd = gm3d_show_f5_menu(app_mode, invert_cam_y);
    if (_cmd == 0 || _cmd == 1 || _cmd == 2 || _cmd == 3 || _cmd == 4) {
        build_scene(_cmd);
    } else if (_cmd == 10) {
        invert_cam_y = !invert_cam_y;
    } else if (_cmd == 20) {
        // Physical explosion
        with (obj_3d_entity) {
            if ((flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                var _dx = x - other.cur_target_x;
                var _dz = z - other.cur_target_z;
                var _len = sqrt(_dx*_dx + _dz*_dz);
                if (_len > 0.001) { _dx /= _len; _dz /= _len; }
                gm3d_body_apply_impulse(physics_body_id, _dx * 35.0, 25.0, _dz * 35.0);
            }
        }
    } else if (_cmd == 21) {
        gravity_mode = (gravity_mode + 1) % 3;
        if (gravity_mode == 0) gm3d_set_gravity(0, -9.81, 0);
        else if (gravity_mode == 1) gm3d_set_gravity(0, 0, 0);
        else if (gravity_mode == 2) gm3d_set_gravity(0, -1.62, 0);
    } else if (_cmd == 22) {
        var _sp_x = cur_target_x + random_range(-3, 3);
        var _sp_y = 12.0;
        var _sp_z = cur_target_z + random_range(-3, 3);
        var _bid = gm3d_body_create_box(_sp_x, _sp_y, _sp_z, 1.6, 1.6, 1.6, 2.0, GM3D_MOTION_DYNAMIC);
        var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        _ent.x = _sp_x; _ent.y = _sp_y; _ent.z = _sp_z;
        _ent.image_xscale = 1.6; _ent.image_yscale = 1.6; _ent.image_zscale = 1.6;
        _ent.mesh_id = GM3D_MESH_CUBE;
        _ent.material_id = choose(1, 2, 3, 5);
        _ent.texture_id = choose(GM3D_TEX_CHECKER, GM3D_TEX_WOOD, GM3D_TEX_METAL);
        _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _ent.physics_body_id = _bid;
    } else if (_cmd == 23) {
        var _sp_x = cur_target_x + random_range(-3, 3);
        var _sp_y = 12.0;
        var _sp_z = cur_target_z + random_range(-3, 3);
        var _bid = gm3d_body_create_sphere(_sp_x, _sp_y, _sp_z, 0.8, 2.0, GM3D_MOTION_DYNAMIC);
        var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        _ent.x = _sp_x; _ent.y = _sp_y; _ent.z = _sp_z;
        _ent.image_xscale = 1.6; _ent.image_yscale = 1.6; _ent.image_zscale = 1.6;
        _ent.mesh_id = GM3D_MESH_SPHERE;
        _ent.material_id = choose(1, 2, 3, 5);
        _ent.texture_id = GM3D_TEX_GRID;
        _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _ent.physics_body_id = _bid;
    } else if (_cmd == 24) {
        var _bid = gm3d_body_create_box(cur_target_x, 16.0, cur_target_z, 1.0, 1.8, 0.6, 3.0, GM3D_MOTION_DYNAMIC);
        gm3d_body_apply_torque(_bid, random_range(-15, 15), random_range(10, 30), random_range(-15, 15));
        var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        _ent.x = cur_target_x; _ent.y = 16.0; _ent.z = cur_target_z;
        _ent.mesh_id = mannequin_rm_mesh_id;
        _ent.material_id = 4;
        _ent.physics_body_id = _bid;
        _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        gm3d_anim_play(_ent.id, 4, 1.0, false);
    } else if (_cmd == 30) {
        var _first = noone; var _pick = noone; var _found_curr = false;
        with (obj_3d_entity) {
            if (id != other.floor_entity) {
                if (_first == noone) _first = id;
                if (_found_curr && _pick == noone) _pick = id;
                if (id == other.selected_entity) _found_curr = true;
            }
        }
        if (_pick != noone) selected_entity = _pick;
        else if (_first != noone) selected_entity = _first;
    } else if (_cmd == 31 && selected_entity != noone) {
        selected_entity.x += 1.0;
        if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
    } else if (_cmd == 32 && selected_entity != noone) {
        selected_entity.x -= 1.0;
        if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
    } else if (_cmd == 33 && selected_entity != noone) {
        selected_entity.y += 1.0;
        if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
    } else if (_cmd == 34 && selected_entity != noone) {
        selected_entity.y -= 1.0;
        if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
    } else if (_cmd == 35 && selected_entity != noone) {
        selected_entity.z += 1.0;
        if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
    } else if (_cmd == 36 && selected_entity != noone) {
        selected_entity.z -= 1.0;
        if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
    } else if (_cmd == 37 && selected_entity != noone) {
        selected_entity.image_xscale *= 1.25;
        selected_entity.image_yscale *= 1.25;
        selected_entity.image_zscale *= 1.25;
    } else if (_cmd == 38 && selected_entity != noone) {
        selected_entity.image_xscale = max(0.2, selected_entity.image_xscale * 0.8);
        selected_entity.image_yscale = max(0.2, selected_entity.image_yscale * 0.8);
        selected_entity.image_zscale = max(0.2, selected_entity.image_zscale * 0.8);
    } else if (_cmd == 39 && selected_entity != noone) {
        var _c = instance_create_depth(0, 0, 0, obj_3d_entity);
        _c.x = selected_entity.x + 2.0; _c.y = selected_entity.y; _c.z = selected_entity.z;
        _c.qx = selected_entity.qx; _c.qy = selected_entity.qy; _c.qz = selected_entity.qz; _c.qw = selected_entity.qw;
        _c.image_xscale = selected_entity.image_xscale; _c.image_yscale = selected_entity.image_yscale; _c.image_zscale = selected_entity.image_zscale;
        _c.mesh_id = selected_entity.mesh_id; _c.material_id = selected_entity.material_id; _c.texture_id = selected_entity.texture_id;
        _c.flags = selected_entity.flags;
        if (selected_entity.physics_body_id > 0) {
            _c.physics_body_id = gm3d_body_create_box(_c.x, _c.y, _c.z, _c.image_xscale, _c.image_yscale, _c.image_zscale, 1.0, GM3D_MOTION_DYNAMIC);
        }
        selected_entity = _c;
    } else if (_cmd == 40 && selected_entity != noone) {
        if (selected_entity.physics_body_id > 0) destroy_physics_body(selected_entity.physics_body_id);
        with (selected_entity) instance_destroy();
        selected_entity = noone;
    } else if (_cmd == 50) {
        audio_play_sound(snd_fire, 10, false);
        audio_play_sound(snd_shockwave, 9, false);
        var _fx = (app_mode == MODE_ISLAND) ? player_x : cur_target_x;
        var _fy = (app_mode == MODE_ISLAND) ? player_y + 1.0 : cur_target_y + 1.0;
        var _fz = (app_mode == MODE_ISLAND) ? player_z : cur_target_z;
        gm3d_particle_burst(GM3D_PARTICLE_FIRE, _fx, _fy, _fz, 85, 8.0, 0.65);
        gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, _fx, _fy - 0.9, _fz, 9.0, 1, 1.0, 0.45, 0.1);
        with (obj_3d_entity) {
            if (physics_body_id > 0 && id != other.floor_entity && id != other.player_entity) {
                var _pdx = x - _fx; var _pdz = z - _fz;
                var _pdist = sqrt(_pdx * _pdx + _pdz * _pdz);
                if (_pdist < 12.0 && _pdist > 0.1) {
                    gm3d_body_apply_impulse(physics_body_id, (_pdx / _pdist) * 45.0, 20.0, (_pdz / _pdist) * 45.0);
                }
            }
        }
    } else if (_cmd == 51) {
        audio_play_sound(snd_arcane, 10, false);
        audio_play_sound(snd_shockwave, 9, false);
        var _fx = (app_mode == MODE_ISLAND) ? player_x : cur_target_x;
        var _fy = (app_mode == MODE_ISLAND) ? player_y + 1.0 : cur_target_y + 1.0;
        var _fz = (app_mode == MODE_ISLAND) ? player_z : cur_target_z;
        gm3d_particle_burst(GM3D_PARTICLE_ARCANE, _fx, _fy, _fz, 100, 7.5, 0.55);
        gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, _fx, _fy - 0.9, _fz, 11.0, 1, 0.8, 0.2, 1.0);
    } else if (_cmd == 52) {
        audio_play_sound(snd_holy, 10, false);
        var _fx = (app_mode == MODE_ISLAND) ? player_x : cur_target_x;
        var _fy = (app_mode == MODE_ISLAND) ? player_y + 0.5 : cur_target_y + 0.5;
        var _fz = (app_mode == MODE_ISLAND) ? player_z : cur_target_z;
        gm3d_particle_burst(GM3D_PARTICLE_HOLY, _fx, _fy, _fz, 90, 5.0, 0.70);
    } else if (_cmd == 53) {
        audio_play_sound(snd_frost, 10, false);
        var _fx = (app_mode == MODE_ISLAND) ? player_x : cur_target_x;
        var _fy = (app_mode == MODE_ISLAND) ? player_y + 1.0 : cur_target_y + 1.0;
        var _fz = (app_mode == MODE_ISLAND) ? player_z : cur_target_z;
        gm3d_particle_burst(GM3D_PARTICLE_FROST, _fx, _fy, _fz, 110, 8.5, 0.50);
        gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, _fx, _fy - 0.9, _fz, 8.0, 1, 0.4, 0.9, 1.0);
    } else if (_cmd == 60) {
        bloom_active = !bloom_active;
        gm3d_render_set_bloom(bloom_active, 0.85, 1.0);
        editor_notify_text = bloom_active ? "HDR Bloom & Glow: ENABLED" : "HDR Bloom & Glow: DISABLED";
        editor_notify_timer = 2.5;
    }
}

// Quick number keys for instant mode switching
if (keyboard_check_pressed(ord("1"))) build_scene(MODE_ISLAND);
if (keyboard_check_pressed(ord("2"))) build_scene(MODE_EDITOR);
if (keyboard_check_pressed(ord("3"))) build_scene(MODE_PHYSICS);
if (keyboard_check_pressed(ord("4"))) build_scene(MODE_LIGHTING);
if (keyboard_check_pressed(ord("5"))) build_scene(MODE_MATERIAL);
if (keyboard_check_pressed(ord("Y"))) invert_cam_y = !invert_cam_y;
if (keyboard_check_pressed(ord("B"))) {
    bloom_active = !bloom_active;
    gm3d_render_set_bloom(bloom_active, 0.85, 1.0);
    editor_notify_text = bloom_active ? "HDR Bloom & Glow: ENABLED" : "HDR Bloom & Glow: DISABLED";
    editor_notify_timer = 2.5;
}

// Camera Mode Toggle (Key C: Free Fly Spectator vs Third-Person)
if (keyboard_check_pressed(ord("C"))) {
    free_fly_active = !free_fly_active;
    if (free_fly_active) {
        fly_cam_x = cur_cam_x;
        fly_cam_y = max(cur_cam_y, 4.0);
        fly_cam_z = cur_cam_z;
        mouse_captured = true;
        window_set_cursor(cr_none);
        editor_notify_text = "CAMERA: FREE FLY SPECTATOR (WASD: Fly, Space/Shift: Up/Down, Mouse: Look)";
    } else {
        editor_notify_text = "CAMERA: THIRD-PERSON CHARACTER (WASD: Move, Shift: Sprint, Space: Jump)";
    }
    editor_notify_timer = 3.0;
}

// Mouse Look Lock Toggle (Key Tab or M)
if (keyboard_check_pressed(vk_tab) || keyboard_check_pressed(ord("M"))) {
    mouse_captured = !mouse_captured;
    window_set_cursor(mouse_captured ? cr_none : cr_default);
    editor_notify_text = mouse_captured ? "Mouse Look: LOCKED (Move mouse to turn | TAB to unlock)" : "Mouse Look: UNLOCKED (Cursor free | TAB to lock)";
    editor_notify_timer = 2.0;
}

// Escape Key: Release Mouse Capture
if (keyboard_check_pressed(vk_escape) && mouse_captured) {
    mouse_captured = false;
    window_set_cursor(cr_default);
    editor_notify_text = "Mouse Look: UNLOCKED";
    editor_notify_timer = 1.5;
}

// Update Window Title Caption
var _mode_name = "";
switch (app_mode) {
    case MODE_ISLAND:   _mode_name = "1: Ocean Island"; break;
    case MODE_EDITOR:   _mode_name = "2: 3D Scene Editor"; break;
    case MODE_PHYSICS:  _mode_name = "3: Jolt Physics Lab"; break;
    case MODE_LIGHTING: _mode_name = "4: Lighting & Fog Studio"; break;
    case MODE_MATERIAL: _mode_name = "5: Material Showcase"; break;
}
var _inv_str = invert_cam_y ? "Invert Y: ON" : "Invert Y: OFF";
var _fps_str = "FPS: " + string(round(fps_real)) + " (" + string(fps) + ")";

if (app_mode == MODE_ISLAND) {
    var _cam_str = free_fly_active ? "CAM: FREE FLY (WASD/QE)" : "CAM: TPS (WASD/Space)";
    var _look_str = mouse_captured ? "Mouse: LOCKED (Tab)" : "Mouse: HOLD RMB (Tab to lock)";
    window_set_caption("GM3D Island | " + _fps_str + " | " + _cam_str + " | " + _look_str + " | C: Cam Mode | LMB: Attack | Q/E/R/F: Spells | F5: Menu");
} else if (app_mode == MODE_EDITOR && selected_entity != noone && instance_exists(selected_entity)) {
    var _mesh_name = "Mesh " + string(selected_entity.mesh_id);
    if (selected_entity.mesh_id == GM3D_MESH_CUBE) _mesh_name = "Cube";
    else if (selected_entity.mesh_id == GM3D_MESH_SPHERE) _mesh_name = "Sphere";
    else if (selected_entity.mesh_id == GM3D_MESH_CYLINDER) _mesh_name = "Cylinder";
    else if (selected_entity.mesh_id == gem_mesh_id) _mesh_name = "Gem";
    var _coord_str = "(" + string_format(selected_entity.x, 0, 1) + ", " + string_format(selected_entity.y, 0, 1) + ", " + string_format(selected_entity.z, 0, 1) + ")";
    window_set_caption("GM3D Editor | " + _fps_str + " | Selected: [" + _mesh_name + "] " + _coord_str + " | Click/Tab: Select | Arrows: Move | Wheel/Page: Elevate | +/-: Scale | F5: Menu");
} else {
    window_set_caption("GM3D [" + _mode_name + "] | " + _fps_str + " | Keys 1-5: Modes | B: Bloom | Y: Invert Y | F5: Menu");
}

if (editor_notify_timer > 0) {
    editor_notify_timer -= _dt;
}

// ------------------------------------------------------------------------------------------------
// Camera Mouse Look & Orbit (With Invertible Y-Axis & Mouse Center Locking)
// ------------------------------------------------------------------------------------------------
var _center_x = window_get_width() * 0.5;
var _center_y = window_get_height() * 0.5;

if (mouse_captured && _center_x > 10 && _center_y > 10) {
    var _cur_mx = display_mouse_get_x();
    var _cur_my = display_mouse_get_y();
    var _win_cx = window_get_x() + _center_x;
    var _win_cy = window_get_y() + _center_y;
    var _dx = _cur_mx - _win_cx;
    var _dy = _cur_my - _win_cy;
    display_mouse_set(_win_cx, _win_cy);

    cam_yaw += _dx * 0.22;
    var _inv = invert_cam_y ? 1.0 : -1.0;
    cam_pitch = clamp(cam_pitch + _dy * 0.22 * _inv, -80.0, 85.0);
} else if (mouse_check_button(mb_right)) {
    var _dx = mouse_x - mouse_prev_x;
    var _dy = mouse_y - mouse_prev_y;
    cam_yaw += _dx * 0.4;
    var _inv = invert_cam_y ? 1.0 : -1.0;
    cam_pitch = clamp(cam_pitch + _dy * 0.4 * _inv, 5.0, 85.0);
}
mouse_prev_x = mouse_x;
mouse_prev_y = mouse_y;

if (app_mode != MODE_EDITOR || selected_entity == noone || !instance_exists(selected_entity)) {
    if (keyboard_check(vk_left))  cam_yaw -= 1.5;
    if (keyboard_check(vk_right)) cam_yaw += 1.5;
    var _kinv = invert_cam_y ? 1.0 : -1.0;
    if (keyboard_check(vk_up))    cam_pitch = clamp(cam_pitch - 1.0 * _kinv, -80.0, 85.0);
    if (keyboard_check(vk_down))  cam_pitch = clamp(cam_pitch + 1.0 * _kinv, -80.0, 85.0);
}

if (app_mode == MODE_EDITOR && selected_entity != noone && instance_exists(selected_entity) && (keyboard_check(vk_shift) || keyboard_check(vk_control))) {
    var _elev_delta = keyboard_check(vk_shift) ? 0.75 : 0.25;
    if (mouse_wheel_up()) {
        selected_entity.y += _elev_delta;
        if (selected_entity.physics_body_id > 0) {
            gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
            gm3d_body_set_velocity(selected_entity.physics_body_id, 0, 0, 0);
        }
    }
    if (mouse_wheel_down()) {
        selected_entity.y -= _elev_delta;
        if (selected_entity.physics_body_id > 0) {
            gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
            gm3d_body_set_velocity(selected_entity.physics_body_id, 0, 0, 0);
        }
    }
} else {
    if (mouse_wheel_up())   cam_dist = max(2.5, cam_dist - 1.5);
    if (mouse_wheel_down()) cam_dist = min(90.0, cam_dist + 1.5);
}

// ------------------------------------------------------------------------------------------------
// Mode-Specific Logic & Camera Tracking
// ------------------------------------------------------------------------------------------------
if (app_mode == MODE_ISLAND) {
    if (free_fly_active) {
        // ----------------------------------------------------------------------------------------
        // MODE 0: OCEAN ISLAND - FREE FLY 6DOF SPECTATOR DRONE
        // ----------------------------------------------------------------------------------------
        var _yaw_rad = degtorad(cam_yaw);
        var _pitch_rad = degtorad(cam_pitch);
        var _fwd_x = cos(_pitch_rad) * sin(_yaw_rad);
        var _fwd_y = -sin(_pitch_rad);
        var _fwd_z = cos(_pitch_rad) * cos(_yaw_rad);

        var _right_x = cos(_yaw_rad);
        var _right_z = -sin(_yaw_rad);

        var _in_fwd = (keyboard_check(ord("W")) ? 1 : 0) - (keyboard_check(ord("S")) ? 1 : 0);
        var _in_right = (keyboard_check(ord("D")) ? 1 : 0) - (keyboard_check(ord("A")) ? 1 : 0);
        var _in_up = (keyboard_check(ord("E")) || keyboard_check(vk_space) ? 1 : 0) - (keyboard_check(ord("Q")) || keyboard_check(vk_control) ? 1 : 0);

        var _fly_speed = (keyboard_check(vk_shift) ? 65.0 : 22.0) * _dt;

        fly_cam_x += (_fwd_x * _in_fwd + _right_x * _in_right) * _fly_speed;
        fly_cam_y += (_fwd_y * _in_fwd + _in_up) * _fly_speed;
        fly_cam_z += (_fwd_z * _in_fwd + _right_z * _in_right) * _fly_speed;

        var _ter_h = gm3d_terrain_get_height(fly_cam_x, fly_cam_z);
        if (fly_cam_y < _ter_h + 0.6) {
            fly_cam_y = _ter_h + 0.6;
        }

        cur_cam_x = fly_cam_x;
        cur_cam_y = fly_cam_y;
        cur_cam_z = fly_cam_z;
        cur_target_x = fly_cam_x + _fwd_x * 10.0;
        cur_target_y = fly_cam_y + _fwd_y * 10.0;
        cur_target_z = fly_cam_z + _fwd_z * 10.0;
        gm3d_camera_set(cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);

        if (player_entity != noone && instance_exists(player_entity)) {
            if (player_anim_state != 0) {
                player_anim_state = 0;
                gm3d_anim_play(player_entity.id, 9, 1.0, true);
            }
        }
    } else if (player_entity != noone && instance_exists(player_entity)) {
        // ----------------------------------------------------------------------------------------
        // MODE 0: OCEAN ISLAND (THIRD-PERSON CHARACTER CONTROLLER)
        // ----------------------------------------------------------------------------------------
        var _fwd_x = sin(degtorad(cam_yaw));
        var _fwd_z = cos(degtorad(cam_yaw));
        var _right_x = cos(degtorad(cam_yaw));
        var _right_z = -sin(degtorad(cam_yaw));

        var _in_fwd = (keyboard_check(ord("W")) ? 1 : 0) - (keyboard_check(ord("S")) ? 1 : 0);
        var _in_right = (keyboard_check(ord("D")) ? 1 : 0) - (keyboard_check(ord("A")) ? 1 : 0);

        var _is_sprinting = keyboard_check(vk_shift);
        var _move_speed = _is_sprinting ? 9.5 : 4.8;

        if (player_action_timer > 0) {
            player_action_timer -= _dt;
        }

        // Punch Attack on LMB
        if (mouse_check_button_pressed(mb_left) && player_action_timer <= 0 && mouse_y > 50) {
            player_action_timer = 0.70;
            player_anim_state = 3;
            gm3d_anim_play(player_entity.id, 24, 1.25, false); // Anim 24: Punch_Cross!
            audio_play_sound(snd_punch, 10, false);
            audio_play_sound(snd_shockwave, 9, false);
            
            // Blast nearby physics bodies with explosive forward shockwave!
            var _p_dir_x = sin(degtorad(player_yaw));
            var _p_dir_z = cos(degtorad(player_yaw));
            gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, player_x + _p_dir_x * 1.5, player_y, player_z + _p_dir_z * 1.5, 7.5, 1, 0.4, 0.9, 1.0);
            gm3d_particle_burst(GM3D_PARTICLE_FIRE, player_x + _p_dir_x * 1.5, player_y + 0.8, player_z + _p_dir_z * 1.5, 40, 6.5, 0.50);

            with (obj_3d_entity) {
                if (id != other.player_entity.id && (flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                    var _dist = point_distance(other.player_x, other.player_z, x, z);
                    if (_dist < 5.0) {
                        gm3d_body_apply_impulse(physics_body_id, _p_dir_x * 45.0 + random_range(-5, 5), 18.0, _p_dir_z * 45.0 + random_range(-5, 5));
                    }
                }
            }
        }

        // Magic Spell: Fire Blast (Key Q)
        if (keyboard_check_pressed(ord("Q")) && player_action_timer <= 0) {
            player_action_timer = 0.60;
            player_anim_state = 3;
            gm3d_anim_play(player_entity.id, 24, 1.4, false);
            audio_play_sound(snd_fire, 10, false);
            audio_play_sound(snd_shockwave, 9, false);
            var _p_dir_x = sin(degtorad(player_yaw));
            var _p_dir_z = cos(degtorad(player_yaw));
            gm3d_particle_burst(GM3D_PARTICLE_FIRE, player_x + _p_dir_x * 2.0, player_y + 1.2, player_z + _p_dir_z * 2.0, 90, 8.5, 0.70);
            gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, player_x + _p_dir_x * 2.0, player_y, player_z + _p_dir_z * 2.0, 9.0, 1, 1.0, 0.4, 0.05);
            with (obj_3d_entity) {
                if (id != other.player_entity.id && (flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                    var _dist = point_distance(other.player_x, other.player_z, x, z);
                    if (_dist < 9.0) {
                        gm3d_body_apply_impulse(physics_body_id, _p_dir_x * 60.0, 25.0, _p_dir_z * 60.0);
                    }
                }
            }
        }

        // Magic Spell: Arcane Nova (Key E)
        if (keyboard_check_pressed(ord("E")) && player_action_timer <= 0) {
            player_action_timer = 0.60;
            player_anim_state = 3;
            gm3d_anim_play(player_entity.id, 24, 1.4, false);
            audio_play_sound(snd_arcane, 10, false);
            audio_play_sound(snd_shockwave, 9, false);
            gm3d_particle_burst(GM3D_PARTICLE_ARCANE, player_x, player_y + 1.2, player_z, 110, 8.0, 0.55);
            gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, player_x, player_y, player_z, 12.0, 1, 0.85, 0.25, 1.0);
            with (obj_3d_entity) {
                if (id != other.player_entity.id && (flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                    var _dist = point_distance(other.player_x, other.player_z, x, z);
                    if (_dist < 10.0) {
                        var _pdx = (x - other.player_x) / max(0.1, _dist);
                        var _pdz = (z - other.player_z) / max(0.1, _dist);
                        gm3d_body_apply_impulse(physics_body_id, _pdx * 45.0, 30.0, _pdz * 45.0);
                    }
                }
            }
        }

        // Magic Spell: Holy Radiance (Key R)
        if (keyboard_check_pressed(ord("R")) && player_action_timer <= 0) {
            player_action_timer = 0.60;
            audio_play_sound(snd_holy, 10, false);
            gm3d_particle_burst(GM3D_PARTICLE_HOLY, player_x, player_y + 0.4, player_z, 100, 5.5, 0.75);
        }

        // Magic Spell: Frost Blizzard (Key F)
        if (keyboard_check_pressed(ord("F")) && player_action_timer <= 0) {
            player_action_timer = 0.60;
            audio_play_sound(snd_frost, 10, false);
            var _p_dir_x = sin(degtorad(player_yaw));
            var _p_dir_z = cos(degtorad(player_yaw));
            gm3d_particle_burst(GM3D_PARTICLE_FROST, player_x + _p_dir_x * 2.5, player_y + 1.2, player_z + _p_dir_z * 2.5, 120, 9.0, 0.50);
            gm3d_particle_ring(GM3D_PARTICLE_SHOCKWAVE, player_x + _p_dir_x * 2.5, player_y, player_z + _p_dir_z * 2.5, 8.5, 1, 0.4, 0.85, 1.0);
        }

        // Movement & Smooth Turning
        var _mx = (_fwd_x * _in_fwd + _right_x * _in_right);
        var _mz = (_fwd_z * _in_fwd + _right_z * _in_right);
        var _mlen = point_distance(0, 0, _mx, _mz);
        if (_mlen > 0.01) {
            _mx /= _mlen; _mz /= _mlen;
            var _target_yaw = radtodeg(arctan2(_mx, _mz));
            player_yaw += angle_difference(_target_yaw, player_yaw) * clamp(_dt * 15.0, 0.08, 0.8);
            if (player_action_timer <= 0) {
                var _step_x = _mx * _move_speed * _dt;
                var _step_z = _mz * _move_speed * _dt;
                var _new_x = player_x + _step_x;
                var _new_z = player_z + _step_z;
                var _p_rad = 0.52; // Player capsule collision radius

                // Physical obstacle collision & continuous sliding resolution
                with (obj_3d_entity) {
                    if (id != other.player_entity.id && physics_body_id > 0) {
                        var _obs_rad = 0.5;
                        if (mesh_id == GM3D_MESH_CYLINDER) _obs_rad = image_xscale * 0.5;
                        else if (mesh_id == GM3D_MESH_SPHERE) _obs_rad = image_xscale * 0.5;
                        else if (mesh_id == GM3D_MESH_PINE_TREE) _obs_rad = 0.35 * image_xscale;
                        else if (mesh_id == GM3D_MESH_CUBE) _obs_rad = max(image_xscale, image_zscale) * 0.52;

                        var _obs_bot = y - image_yscale * 0.5;
                        var _obs_top = y + image_yscale * 0.5;
                        if (mesh_id == GM3D_MESH_PINE_TREE) {
                            _obs_bot = y;
                            _obs_top = y + 3.5 * image_yscale;
                        }

                        if (other.player_y + 1.8 >= _obs_bot && other.player_y <= _obs_top) {
                            var _min_d = _p_rad + _obs_rad;
                            var _d = point_distance(_new_x, _new_z, x, z);
                            if (_d < _min_d && _d > 0.0001) {
                                var _nx = (_new_x - x) / _d;
                                var _nz = (_new_z - z) / _d;
                                var _push_out = _min_d - _d;
                                _new_x += _nx * _push_out;
                                _new_z += _nz * _push_out;

                                // Push dynamic rigid bodies (boulders)
                                if (flags & GM3D_FLAG_RIGIDBODY) {
                                    var _force = _is_sprinting ? 35.0 : 18.0;
                                    gm3d_body_apply_impulse(physics_body_id, -_nx * _force * _dt, 0.0, -_nz * _force * _dt);
                                }
                            }
                        }
                    }
                }

                player_x = clamp(_new_x, -118.0, 118.0);
                player_z = clamp(_new_z, -118.0, 118.0);
            }
        }

        // Terrain & Physical Surface Elevation Query
        var _floor_h = gm3d_terrain_get_height(player_x, player_z);
        if (_floor_h < 0.0) _floor_h = 0.0;

        // Check if player stands on top of any rigid body (boulder dome, crate, platform)
        with (obj_3d_entity) {
            if (id != other.player_entity.id && physics_body_id > 0) {
                var _obs_rad = (mesh_id == GM3D_MESH_SPHERE) ? (image_xscale * 0.5) : (max(image_xscale, image_zscale) * 0.5);
                var _hdist = point_distance(other.player_x, other.player_z, x, z);
                if (_hdist < _obs_rad) {
                    var _top_y = y + image_yscale * 0.5;
                    if (mesh_id == GM3D_MESH_SPHERE) {
                        var _r = image_xscale * 0.5;
                        var _r_sq = _r * _r - _hdist * _hdist;
                        if (_r_sq > 0) _top_y = y + sqrt(_r_sq);
                    }
                    if (_top_y > _floor_h && other.player_y >= _top_y - 0.45) {
                        _floor_h = _top_y;
                    }
                }
            }
        }

        // Jump on Space
        if (keyboard_check_pressed(vk_space) && player_is_grounded && player_action_timer <= 0) {
            player_vy = 8.0;
            player_is_grounded = false;
        }

        // Gravity & Ground Elevation Tracking
        if (!player_is_grounded) {
            player_vy -= 24.0 * _dt;
            player_y += player_vy * _dt;
            if (player_y <= _floor_h) {
                player_y = _floor_h;
                player_vy = 0.0;
                player_is_grounded = true;
            }
        } else {
            // Player is grounded: track terrain elevation smoothly or drop off cliffs
            var _diff = _floor_h - player_y;
            if (_diff < -1.4) {
                // Stepped off a steep cliff or ledge
                player_is_grounded = false;
                player_vy = -1.5;
            } else {
                // Smoothly climb or descend hills
                player_y = _floor_h;
            }
        }

        // Biome-Aware Footstep Particles
        if (_mlen > 0.01 && _is_sprinting && player_is_grounded && irandom(2) == 0) {
            var _spl_type = GM3D_PARTICLE_HOLY;
            if (player_y <= 0.25) {
                _spl_type = GM3D_PARTICLE_FROST; // Ocean splash
            } else if (player_y >= 18.0) {
                _spl_type = GM3D_PARTICLE_FROST; // Mountain snow puff
            } else {
                _spl_type = GM3D_PARTICLE_HOLY;  // Golden valley dust
            }
            gm3d_particle_burst(_spl_type, player_x, player_y + 0.1, player_z, 2, 1.8, 0.25);
        }

        // Animation Transitions
        if (player_action_timer <= 0) {
            if (_mlen > 0.01) {
                if (_is_sprinting) {
                    if (player_anim_state != 2) {
                        player_anim_state = 2;
                        gm3d_anim_play(player_entity.id, 36, 1.2, true); // Sprint_Loop
                    }
                } else {
                    if (player_anim_state != 1) {
                        player_anim_state = 1;
                        gm3d_anim_play(player_entity.id, 42, 1.0, true); // Walk_Loop
                    }
                }
            } else {
                if (player_anim_state != 0) {
                    player_anim_state = 0;
                    gm3d_anim_play(player_entity.id, 9, 1.0, true);  // Idle_Loop
                }
            }
        }

        // Update Player Transform in Render Graph & Jolt Kinematic Body
        var _pyaw_rad = degtorad(player_yaw);
        var _pqy = sin(_pyaw_rad * 0.5);
        var _pqw = cos(_pyaw_rad * 0.5);

        player_entity.x = player_x;
        player_entity.y = player_y;
        player_entity.z = player_z;
        player_entity.qx = 0;
        player_entity.qy = _pqy;
        player_entity.qz = 0;
        player_entity.qw = _pqw;
        if (player_entity.physics_body_id > 0) {
            gm3d_body_set_transform(player_entity.physics_body_id, player_x, player_y + 0.92, player_z, 0, _pqy, 0, _pqw);
        }

        // Over-The-Shoulder Camera Tracking with Spring-Arm Collision Check
        var _yaw_rad = degtorad(cam_yaw);
        var _pitch_rad = degtorad(cam_pitch);
        var _cam_dir_x = -cos(_pitch_rad) * sin(_yaw_rad);
        var _cam_dir_y = sin(_pitch_rad);
        var _cam_dir_z = -cos(_pitch_rad) * cos(_yaw_rad);
        var _head_x = player_x + _cam_dir_x * 0.65;
        var _head_y = player_y + 1.4 + _cam_dir_y * 0.65;
        var _head_z = player_z + _cam_dir_z * 0.65;
        var _cam_x = player_x - cam_dist * cos(_pitch_rad) * sin(_yaw_rad);
        var _cam_y = player_y + 1.8 + cam_dist * sin(_pitch_rad);
        var _cam_z = player_z - cam_dist * cos(_pitch_rad) * cos(_yaw_rad);

        // Raycast against Jolt physics bodies including terrain heightfield
        var _ray = gm3d_raycast(_head_x, _head_y, _head_z, _cam_x, _cam_y, _cam_z);
        if (_ray.has_hit && _ray.body_id != player_entity.physics_body_id) {
            var _hx = _ray.x;
            var _hy = _ray.y;
            var _hz = _ray.z;
            var _d_hit = point_distance_3d(_head_x, _head_y, _head_z, _hx, _hy, _hz);
            var _d_cam = point_distance_3d(_head_x, _head_y, _head_z, _cam_x, _cam_y, _cam_z);
            if (_d_cam > 0.001) {
                var _t = clamp((_d_hit - 0.35) / _d_cam, 0.15, 1.0);
                _cam_x = lerp(_head_x, _cam_x, _t);
                _cam_y = lerp(_head_y, _cam_y, _t);
                _cam_z = lerp(_head_z, _cam_z, _t);
            }
        }

        // Guarantee camera stays above terrain ground level
        var _cam_terrain_h = gm3d_terrain_get_height(_cam_x, _cam_z);
        if (_cam_y < _cam_terrain_h + 0.5) {
            _cam_y = _cam_terrain_h + 0.5;
        }

        cur_cam_x = _cam_x; cur_cam_y = _cam_y; cur_cam_z = _cam_z;
        cur_target_x = player_x; cur_target_y = player_y + 1.4; cur_target_z = player_z;
        gm3d_camera_set(_cam_x, _cam_y, _cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);
    }

} else {
    // --------------------------------------------------------------------------------------------
    // MODES 1..4: ORBIT CAMERA SETUP WITH SPRING-ARM COLLISION CHECK
    // --------------------------------------------------------------------------------------------
    if (app_mode == MODE_EDITOR && selected_entity != noone && instance_exists(selected_entity)) {
        cur_target_x = lerp(cur_target_x, selected_entity.x, 0.12);
        cur_target_y = lerp(cur_target_y, selected_entity.y + 0.8, 0.12);
        cur_target_z = lerp(cur_target_z, selected_entity.z, 0.12);
    }

    var _yaw_rad = degtorad(cam_yaw);
    var _pitch_rad = degtorad(cam_pitch);
    var _cam_x = cur_target_x + cam_dist * cos(_pitch_rad) * sin(_yaw_rad);
    var _cam_y = cur_target_y + cam_dist * sin(_pitch_rad);
    var _cam_z = cur_target_z - cam_dist * cos(_pitch_rad) * cos(_yaw_rad);

    var _ray = gm3d_raycast(cur_target_x, cur_target_y, cur_target_z, _cam_x, _cam_y, _cam_z);
    if (_ray.has_hit && (selected_entity == noone || _ray.body_id != selected_entity.physics_body_id)) {
        var _hx = _ray.x;
        var _hy = _ray.y;
        var _hz = _ray.z;
        var _d_hit = point_distance_3d(cur_target_x, cur_target_y, cur_target_z, _hx, _hy, _hz);
        var _d_cam = point_distance_3d(cur_target_x, cur_target_y, cur_target_z, _cam_x, _cam_y, _cam_z);
        if (_d_cam > 0.001) {
            var _t = clamp((_d_hit - 0.35) / _d_cam, 0.20, 1.0);
            _cam_x = lerp(cur_target_x, _cam_x, _t);
            _cam_y = lerp(cur_target_y, _cam_y, _t);
            _cam_z = lerp(cur_target_z, _cam_z, _t);
        }
    }

    cur_cam_x = _cam_x; cur_cam_y = _cam_y; cur_cam_z = _cam_z;
    gm3d_camera_set(_cam_x, _cam_y, _cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);

    // --------------------------------------------------------------------------------------------
    // MODE 1: 3D SCENE EDITOR (Viewport Raycast Pick)
    // --------------------------------------------------------------------------------------------
    if (app_mode == MODE_EDITOR) {
        // Ensure an entity is selected so gizmo is always visible
        if (selected_entity == noone || !instance_exists(selected_entity)) {
            with (obj_3d_entity) {
                if (id != other.floor_entity) {
                    other.selected_entity = id;
                    break;
                }
            }
        }

        // Tab Key: Cycle to next entity in scene
        if (keyboard_check_pressed(vk_tab)) {
            var _first = noone; var _pick = noone; var _found_curr = false;
            with (obj_3d_entity) {
                if (id != other.floor_entity) {
                    if (_first == noone) _first = id;
                    if (_found_curr && _pick == noone) _pick = id;
                    if (id == other.selected_entity) _found_curr = true;
                }
            }
            if (_pick != noone) selected_entity = _pick;
            else if (_first != noone) selected_entity = _first;
        }

        // Continuous Transform Controls (Arrows or I/K/J/L/U/O, PageUp/PageDown, Q/E Rotation)
        if (selected_entity != noone && instance_exists(selected_entity)) {
            var _moved = false;
            var _m_spd = (keyboard_check(vk_shift) ? 14.0 : 4.5) * _dt;
            if (keyboard_check(vk_right)    || keyboard_check(ord("L"))) { selected_entity.x += _m_spd; _moved = true; }
            if (keyboard_check(vk_left)     || keyboard_check(ord("J"))) { selected_entity.x -= _m_spd; _moved = true; }
            if (keyboard_check(vk_up)       || keyboard_check(ord("I"))) { selected_entity.z += _m_spd; _moved = true; }
            if (keyboard_check(vk_down)     || keyboard_check(ord("K"))) { selected_entity.z -= _m_spd; _moved = true; }
            if (keyboard_check(vk_pageup)   || keyboard_check(ord("O"))) { selected_entity.y += _m_spd; _moved = true; }
            if (keyboard_check(vk_pagedown) || keyboard_check(ord("U"))) { selected_entity.y -= _m_spd; _moved = true; }

            // Yaw Rotation (Q / E)
            if (keyboard_check(ord("E"))) {
                var _rot_spd = (keyboard_check(vk_shift) ? 90.0 : 45.0) * _dt;
                var _curr_yaw = 2.0 * radtodeg(arctan2(selected_entity.qy, selected_entity.qw)) + _rot_spd;
                var _rad = degtorad(_curr_yaw * 0.5);
                selected_entity.qy = sin(_rad);
                selected_entity.qw = cos(_rad);
                _moved = true;
            }
            if (keyboard_check(ord("Q"))) {
                var _rot_spd = (keyboard_check(vk_shift) ? 90.0 : 45.0) * _dt;
                var _curr_yaw = 2.0 * radtodeg(arctan2(selected_entity.qy, selected_entity.qw)) - _rot_spd;
                var _rad = degtorad(_curr_yaw * 0.5);
                selected_entity.qy = sin(_rad);
                selected_entity.qw = cos(_rad);
                _moved = true;
            }

            if (_moved && selected_entity.physics_body_id > 0) {
                gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
                gm3d_body_set_velocity(selected_entity.physics_body_id, 0, 0, 0);
            }

            // Scale Controls (+ / -)
            if (keyboard_check(ord("=")) || keyboard_check(vk_add)) {
                var _sc = 1.0 + 1.2 * _dt;
                selected_entity.image_xscale *= _sc; selected_entity.image_yscale *= _sc; selected_entity.image_zscale *= _sc;
            }
            if (keyboard_check(ord("-")) || keyboard_check(vk_subtract)) {
                var _sc = max(0.2, 1.0 - 1.2 * _dt);
                selected_entity.image_xscale *= _sc; selected_entity.image_yscale *= _sc; selected_entity.image_zscale *= _sc;
            }

            // Cycle Material [ / ]
            if (keyboard_check_pressed(ord("["))) selected_entity.material_id = (selected_entity.material_id + 5) % 6;
            if (keyboard_check_pressed(ord("]"))) selected_entity.material_id = (selected_entity.material_id + 1) % 6;

            // Delete (Del)
            if (keyboard_check_pressed(vk_delete)) {
                if (selected_entity.physics_body_id > 0) destroy_physics_body(selected_entity.physics_body_id);
                with (selected_entity) instance_destroy();
                selected_entity = noone;
            }

            // Duplicate (Ctrl+D / Insert)
            if ((keyboard_check(vk_control) && keyboard_check_pressed(ord("D"))) || keyboard_check_pressed(vk_insert)) {
                var _cl = instance_create_depth(0, 0, 0, obj_3d_entity);
                _cl.x = selected_entity.x + 2.5; _cl.y = selected_entity.y; _cl.z = selected_entity.z;
                _cl.qx = selected_entity.qx; _cl.qy = selected_entity.qy; _cl.qz = selected_entity.qz; _cl.qw = selected_entity.qw;
                _cl.image_xscale = selected_entity.image_xscale; _cl.image_yscale = selected_entity.image_yscale; _cl.image_zscale = selected_entity.image_zscale;
                _cl.mesh_id = selected_entity.mesh_id; _cl.material_id = selected_entity.material_id; _cl.texture_id = selected_entity.texture_id;
                _cl.flags = selected_entity.flags;
                if (selected_entity.physics_body_id > 0) {
                    _cl.physics_body_id = gm3d_body_create_box(_cl.x, _cl.y, _cl.z, _cl.image_xscale, _cl.image_yscale, _cl.image_zscale, 1.0, GM3D_MOTION_DYNAMIC);
                }
                selected_entity = _cl;
            }
        }

        // 3D Raycast Picking (only when clicking in the 3D viewport, outside UI panel)
        if (mouse_check_button_pressed(mb_left) && mouse_x < window_get_width() - 280 && mouse_y > 52) {
            var _ray = gm3d_screen_to_ray(mouse_x, mouse_y, cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0, window_get_width(), window_get_height());
            var _rx = _ray[0]; var _ry = _ray[1]; var _rz = _ray[2];
            var _to_x = cur_cam_x + _rx * 1000.0;
            var _to_y = cur_cam_y + _ry * 1000.0;
            var _to_z = cur_cam_z + _rz * 1000.0;
            var _hit_body = physics_raycast(cur_cam_x, cur_cam_y, cur_cam_z, _to_x, _to_y, _to_z);

            var _found = noone;
            if (_hit_body > 0) {
                with (obj_3d_entity) {
                    if (physics_body_id == _hit_body && id != other.floor_entity) {
                        _found = id;
                        break;
                    }
                }
            }

            if (_found == noone) {
                var _best_dist = 999999;
                var _cand = noone;
                with (obj_3d_entity) {
                    if (id != other.floor_entity) {
                        var _vx = x - other.cur_cam_x;
                        var _vy = y - other.cur_cam_y;
                        var _vz = z - other.cur_cam_z;
                        var _proj = _vx * _rx + _vy * _ry + _vz * _rz;
                        if (_proj > 0.5) {
                            var _perp_x = _vx - _rx * _proj;
                            var _perp_y = _vy - _ry * _proj;
                            var _perp_z = _vz - _rz * _proj;
                            var _dist_sq = _perp_x * _perp_x + _perp_y * _perp_y + _perp_z * _perp_z;
                            var _rad = max(image_xscale, image_yscale, image_zscale) * 1.5;
                            if (_dist_sq < _rad * _rad && _proj < _best_dist) {
                                _best_dist = _proj;
                                _cand = id;
                            }
                        }
                    }
                }
                if (_cand != noone) _found = _cand;
            }

            if (_found != noone) selected_entity = _found;
        }

        // Hotkey shortcuts for convenience:
        if (keyboard_check(vk_control) && keyboard_check_pressed(ord("S"))) {
            // Save Scene
            var _scene_list = [];
            with (obj_3d_entity) {
                if (id != other.floor_entity) {
                    var _item = {
                        mesh_id: mesh_id,
                        material_id: material_id,
                        texture_id: variable_instance_exists(id, "texture_id") ? texture_id : 0,
                        x: x, y: y, z: z,
                        qx: qx, qy: qy, qz: qz, qw: qw,
                        sx: image_xscale, sy: image_yscale, sz: image_zscale,
                        flags: flags,
                        has_physics: (physics_body_id > 0)
                    };
                    array_push(_scene_list, _item);
                }
            }
            var _json_str = json_stringify(_scene_list, true);
            var _file = file_text_open_write(working_directory + "scene_data.json");
            if (_file != -1) {
                file_text_write_string(_file, _json_str);
                file_text_close(_file);
                editor_notify_text = "Scene saved (" + string(array_length(_scene_list)) + " objects) to scene_data.json";
                editor_notify_timer = 3.5;
            }
        }
        if (keyboard_check(vk_control) && (keyboard_check_pressed(ord("O")) || keyboard_check_pressed(ord("L")))) {
            // Load Scene
            var _path = working_directory + "scene_data.json";
            if (file_exists(_path)) {
                var _file = file_text_open_read(_path);
                var _str = "";
                while (!file_text_eof(_file)) {
                    _str += file_text_read_string(_file);
                    file_text_readln(_file);
                }
                file_text_close(_file);
                var _loaded_list = json_parse(_str);
                if (is_array(_loaded_list)) {
                    with (obj_3d_entity) {
                        if (id != other.floor_entity) {
                            if (physics_body_id > 0) destroy_physics_body(physics_body_id);
                            instance_destroy();
                        }
                    }
                    selected_entity = noone;
                    for (var _i = 0; _i < array_length(_loaded_list); _i++) {
                        var _it = _loaded_list[_i];
                        var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
                        _ent.x = _it.x; _ent.y = _it.y; _ent.z = _it.z;
                        _ent.qx = _it.qx; _ent.qy = _it.qy; _ent.qz = _it.qz; _ent.qw = _it.qw;
                        _ent.image_xscale = _it.sx; _ent.image_yscale = _it.sy; _ent.image_zscale = _it.sz;
                        _ent.mesh_id = _it.mesh_id;
                        _ent.material_id = _it.material_id;
                        _ent.texture_id = _it.texture_id;
                        _ent.flags = _it.flags;
                        if (_it.has_physics) {
                            var _shape = (_ent.mesh_id == GM3D_MESH_SPHERE) ? GM3D_SHAPE_SPHERE : GM3D_SHAPE_BOX;
                            _ent.physics_body_id = create_physics_body(_shape, GM3D_MOTION_DYNAMIC, 1.0, _ent.x, _ent.y, _ent.z, _ent.image_xscale, _ent.image_yscale, _ent.image_zscale);
                        }
                    }
                    editor_notify_text = "Scene loaded (" + string(array_length(_loaded_list)) + " objects) from scene_data.json";
                    editor_notify_timer = 3.5;
                }
            }
        }
    }

    // --------------------------------------------------------------------------------------------
    // MODE 2: JOLT PHYSICS LAB (Cannonball & Spawn Hotkeys)
    // --------------------------------------------------------------------------------------------
    if (app_mode == MODE_PHYSICS) {
        if (mouse_check_button_pressed(mb_left) && mouse_x > 260 && mouse_y > 52) {
            var _ray = gm3d_screen_to_ray(mouse_x, mouse_y, cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);
            var _nx = _ray[0]; var _ny = _ray[1]; var _nz = _ray[2];
            var _sp_x = cur_cam_x + _nx * 2.5;
            var _sp_y = cur_cam_y + _ny * 2.5;
            var _sp_z = cur_cam_z + _nz * 2.5;
            var _pbid = create_physics_body(GM3D_SHAPE_SPHERE, GM3D_MOTION_DYNAMIC, 8.0, _sp_x, _sp_y, _sp_z, 1.2, 1.2, 1.2);
            gm3d_body_set_velocity(_pbid, _nx * 42.0, _ny * 42.0, _nz * 42.0);
            var _pent = instance_create_depth(0, 0, 0, obj_3d_entity);
            _pent.x = _sp_x; _pent.y = _sp_y; _pent.z = _sp_z;
            _pent.qx = 0; _pent.qy = 0; _pent.qz = 0; _pent.qw = 1;
            _pent.image_xscale = 1.2; _pent.image_yscale = 1.2; _pent.image_zscale = 1.2;
            _pent.mesh_id = GM3D_MESH_SPHERE;
            _pent.material_id = 4;
            _pent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
            _pent.physics_body_id = _pbid;
        }

        if (keyboard_check_pressed(vk_space)) {
            var _sp_x = random_range(-3, 3);
            var _sp_y = 15.0;
            var _sp_z = random_range(-3, 3);
            var _bid = create_physics_body(GM3D_SHAPE_BOX, GM3D_MOTION_DYNAMIC, 1.5, _sp_x, _sp_y, _sp_z, 1.8, 1.8, 1.8);
            var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
            _ent.x = _sp_x; _ent.y = _sp_y; _ent.z = _sp_z;
            _ent.qx = 0; _ent.qy = 0; _ent.qz = 0; _ent.qw = 1;
            _ent.image_xscale = 1.8; _ent.image_yscale = 1.8; _ent.image_zscale = 1.8;
            _ent.mesh_id = GM3D_MESH_CUBE;
            _ent.material_id = choose(1, 2, 3);
            _ent.texture_id = choose(GM3D_TEX_CHECKER, GM3D_TEX_WOOD, GM3D_TEX_METAL);
            _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
            _ent.physics_body_id = _bid;
        }
    }

    // --------------------------------------------------------------------------------------------
    // MODE 3: LIGHTING STUDIO (Orbiting Dynamic Lights)
    // --------------------------------------------------------------------------------------------
    if (app_mode == MODE_LIGHTING) {
        if (light_rotate_active) light_timer += _dt * 1.5;

        // Light 0: Amber
        if (light0_active) {
            var _lx0 = cos(light_timer) * 9.5;
            var _ly0 = 4.0 + sin(light_timer * 2.0) * 1.8;
            var _lz0 = sin(light_timer) * 9.5;
            gm3d_light_point_set(0, _lx0, _ly0, _lz0, 18.0, 1.0, 0.55, 0.1, 4.5);
            if (light0_ent != noone && instance_exists(light0_ent)) {
                light0_ent.x = _lx0; light0_ent.y = _ly0; light0_ent.z = _lz0;
            }
        } else {
            gm3d_light_point_set(0, 0, 0, 0, 0, 0, 0, 0, 0);
        }

        // Light 1: Cyan
        if (light1_active) {
            var _ly1 = 2.8 + sin(light_timer * 2.5) * 0.9;
            var _lz1 = sin(light_timer * 1.8) * 2.0;
            gm3d_light_point_set(1, -6.0, _ly1, _lz1, 14.0, 0.1, 0.8, 1.0, 3.8);
            if (light1_ent != noone && instance_exists(light1_ent)) {
                light1_ent.x = -6.0; light1_ent.y = _ly1; light1_ent.z = _lz1;
            }
        } else {
            gm3d_light_point_set(1, 0, 0, 0, 0, 0, 0, 0, 0);
        }

        // Light 2: Magenta
        if (light2_active) {
            var _ly2 = 2.8 + cos(light_timer * 2.5) * 0.9;
            var _lz2 = cos(light_timer * 1.8) * 2.0;
            gm3d_light_point_set(2, 6.0, _ly2, _lz2, 14.0, 1.0, 0.15, 0.85, 3.8);
            if (light2_ent != noone && instance_exists(light2_ent)) {
                light2_ent.x = 6.0; light2_ent.y = _ly2; light2_ent.z = _lz2;
            }
        } else {
            gm3d_light_point_set(2, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }

    // --------------------------------------------------------------------------------------------
    // MODE 4: MATERIALS & NORMAL MAPS SHOWCASE
    // --------------------------------------------------------------------------------------------
    if (app_mode == MODE_MATERIAL) {
        if (mat_rot_active) {
            for (var _p = 0; _p < array_length(mat_pedestals); _p++) {
                var _ped_ent = mat_pedestals[_p];
                if (instance_exists(_ped_ent)) {
                    var _ang = (current_time * 0.05 + _p * 45) * (pi / 180.0);
                    _ped_ent.qy = sin(_ang * 0.5);
                    _ped_ent.qw = cos(_ang * 0.5);
                }
            }
        }

        // Sweeping low grazing spotlight to emphasize normal bump maps
        light_timer += _dt * 1.2;
        var _sl_x = sin(light_timer) * 12.0;
        var _sl_y = 3.0 + sin(light_timer * 1.5) * 0.8;
        var _sl_z = 3.5 + cos(light_timer * 0.8) * 1.5;
        gm3d_light_point_set(0, _sl_x, _sl_y, _sl_z, 16.0, 1.0, 0.95, 0.85, 5.0);
        if (light0_ent != noone && instance_exists(light0_ent)) {
            light0_ent.x = _sl_x; light0_ent.y = _sl_y; light0_ent.z = _sl_z;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Memory Sync to DLL (Shared Zero-Copy Buffer)
// ------------------------------------------------------------------------------------------------
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
    buffer_write(_buff, buffer_f32, material_id); // custom_param1 = material_id for PBR
    buffer_write(_buff, buffer_f32, variable_instance_exists(id, "texture_id") ? texture_id : 0.0); // custom_param2 = texture_id

    _idx++;
}

// ------------------------------------------------------------------------------------------------
// Update 3D Transform Gizmo in Native Direct3D 11 Pipeline
// ------------------------------------------------------------------------------------------------
if (app_mode == MODE_EDITOR && selected_entity != noone && instance_exists(selected_entity)) {
    gm3d_set_gizmo(true, selected_entity.x, selected_entity.y, selected_entity.z,
                   selected_entity.image_xscale, selected_entity.image_yscale, selected_entity.image_zscale, 2.8);
} else {
    gm3d_set_gizmo(false, 0, 0, 0, 1, 1, 1, 0);
}

// ------------------------------------------------------------------------------------------------
// Tick Direct3D 11 & Jolt Physics Engine
// ------------------------------------------------------------------------------------------------
update_framework(_dt);

// Read back updated dynamic physics transforms
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
