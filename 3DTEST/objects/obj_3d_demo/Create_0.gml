// Initialize 3D Engine
gm3d_init(5000);

// Camera & Viewport Setup
cam_dist = 7.0;
cam_yaw = 45.0;
cam_pitch = 22.0;
invert_cam_y = true; // Inverted Y-axis enabled by user request
mouse_prev_x = mouse_x;
mouse_prev_y = mouse_y;
mouse_captured = false;
free_fly_active = false;
fly_cam_x = 0.0;
fly_cam_y = 15.0;
fly_cam_z = -15.0;

cur_cam_x = 0.0;
cur_cam_y = 10.0;
cur_cam_z = -15.0;
cur_target_x = 0.0;
cur_target_y = 1.4;
cur_target_z = 0.0;

// Application Modes
#macro MODE_ISLAND   0
#macro MODE_EDITOR   1
#macro MODE_PHYSICS  2
#macro MODE_LIGHTING 3
#macro MODE_MATERIAL 4

app_mode = MODE_ISLAND;
cam_mode = 1; // 1 = Third-Person Character, 0 = Free Orbit Camera

// Shared Assets: Meshes
gem_mesh_id = GM3D_MESH_CUBE;
var _gem_path = working_directory + "gem.obj";
if (!file_exists(_gem_path)) _gem_path = "datafiles/gem.obj";
if (!file_exists(_gem_path)) _gem_path = "C:/GM3Dext/3DTEST/datafiles/gem.obj";
gem_mesh_id = gm3d_mesh_load_obj(_gem_path);
if (gem_mesh_id == 0) gem_mesh_id = GM3D_MESH_CUBE;

mannequin_mesh_id = GM3D_MESH_CAPSULE;
var _m_path = working_directory + "UAL1_Standard.glb";
if (!file_exists(_m_path)) _m_path = "datafiles/UAL1_Standard.glb";
if (!file_exists(_m_path)) _m_path = "C:/GM3Dext/3Dreadymodels/UAL1_Standard.glb";
if (!file_exists(_m_path)) _m_path = "C:/GM3Dext/3DTEST/datafiles/UAL1_Standard.glb";
mannequin_mesh_id = gm3d_mesh_load(_m_path);
if (mannequin_mesh_id == 0) mannequin_mesh_id = GM3D_MESH_CAPSULE;

mannequin_rm_mesh_id = mannequin_mesh_id;
var _m_rm_path = working_directory + "UAL1_Standard_RM.glb";
if (!file_exists(_m_rm_path)) _m_rm_path = "datafiles/UAL1_Standard_RM.glb";
if (!file_exists(_m_rm_path)) _m_rm_path = "C:/GM3Dext/3Dreadymodels/UAL1_Standard_RM.glb";
if (!file_exists(_m_rm_path)) _m_rm_path = "C:/GM3Dext/3DTEST/datafiles/UAL1_Standard_RM.glb";
mannequin_rm_mesh_id = gm3d_mesh_load(_m_rm_path);
if (mannequin_rm_mesh_id == 0) mannequin_rm_mesh_id = mannequin_mesh_id;

// Player Character Variables
player_entity = noone;
player_x = 0.0;
player_y = 0.75;
player_z = 0.0;
player_yaw = 0.0;
player_vy = 0.0;
player_is_grounded = true;
player_anim_state = 0;
player_action_timer = 0.0;

// Editor & Scene State
selected_entity = noone;
floor_entity = noone;
island_entity = noone;
editor_notify_text = "";
editor_notify_timer = 0.0;

// Dynamic Lighting State
light_timer = 0.0;
light_rotate_active = true;
light0_active = true;
light1_active = true;
light2_active = true;
light0_ent = noone;
light1_ent = noone;
light2_ent = noone;
time_of_day_mode = 0; // 0: Noon, 1: Sunset, 2: Night, 3: Dense Fog

// Physics State
gravity_mode = 0;

// Material Showcase State
mat_rot_active = true;
mat_pedestals = [];

// Visual Post-Processing State
bloom_active = true;
gm3d_render_set_bloom(true, 0.85, 1.0);

// Procedural 16-bit PCM Audio Synthesizer
proc_sound_buffers = [];
proc_sound_ids = [];
create_proc_sound = function(_generator_fn, _duration = 0.35, _sample_rate = 22050) {
    var _sample_count = floor(_sample_rate * _duration);
    var _byte_size = _sample_count * 2;
    var _buff = buffer_create(_byte_size, buffer_fixed, 2);
    buffer_seek(_buff, buffer_seek_start, 0);
    for (var _i = 0; _i < _sample_count; _i++) {
        var _t = _i / _sample_rate;
        var _sample = clamp(_generator_fn(_t, _duration), -1.0, 1.0);
        var _val16 = floor(_sample * 32760);
        buffer_write(_buff, buffer_s16, _val16);
    }
    buffer_seek(_buff, buffer_seek_start, 0);
    var _snd = audio_create_buffer_sound(_buff, buffer_s16, _sample_rate, 0, _byte_size, audio_mono);
    array_push(proc_sound_buffers, _buff);
    array_push(proc_sound_ids, _snd);
    return _snd;
};

// 1. snd_punch (Heavy physical impact with quick bass drop)
snd_punch = create_proc_sound(function(_t, _dur) {
    var _env = power(max(0.0, 1.0 - (_t / _dur)), 2.5);
    var _freq = 140.0 - 100.0 * (_t / _dur);
    var _sine = sin(2.0 * pi * _freq * _t);
    var _noise = (random(2.0) - 1.0) * power(max(0.0, 1.0 - (_t / (_dur * 0.35))), 3.0);
    return (_sine * 0.75 + _noise * 0.25) * _env;
}, 0.25);

// 2. snd_fire (Explosive roar and fiery white-noise burst)
snd_fire = create_proc_sound(function(_t, _dur) {
    var _env = power(max(0.0, 1.0 - (_t / _dur)), 1.8);
    var _noise = (random(2.0) - 1.0);
    var _rumble = sin(2.0 * pi * (65.0 - 30.0 * (_t / _dur)) * _t);
    return (_noise * 0.65 + _rumble * 0.35) * _env;
}, 0.45);

// 3. snd_arcane (Crystalline harmonic chime 523Hz + 1046Hz)
snd_arcane = create_proc_sound(function(_t, _dur) {
    var _env = power(max(0.0, 1.0 - (_t / _dur)), 1.2);
    var _s1 = sin(2.0 * pi * 523.25 * _t);
    var _s2 = sin(2.0 * pi * 1046.50 * _t) * 0.5;
    var _s3 = sin(2.0 * pi * 1567.98 * _t) * 0.25;
    return (_s1 + _s2 + _s3) * 0.6 * _env;
}, 0.50);

// 4. snd_holy (Ascending radiant light chord 440Hz -> 880Hz)
snd_holy = create_proc_sound(function(_t, _dur) {
    var _env = power(max(0.0, 1.0 - (_t / _dur)), 1.0);
    var _freq = 440.0 + 440.0 * (_t / _dur);
    var _s1 = sin(2.0 * pi * _freq * _t);
    var _s2 = sin(2.0 * pi * (_freq * 1.5) * _t) * 0.4;
    return (_s1 + _s2) * 0.7 * _env;
}, 0.60);

// 5. snd_frost (Icy shimmering crackle + blizzard wind)
snd_frost = create_proc_sound(function(_t, _dur) {
    var _env = power(max(0.0, 1.0 - (_t / _dur)), 1.5);
    var _ice = sin(2.0 * pi * 1800.0 * _t) * sin(2.0 * pi * 45.0 * _t);
    var _wind = (random(2.0) - 1.0) * 0.4;
    return (_ice * 0.6 + _wind) * _env;
}, 0.40);

// 6. snd_shockwave (Sub-bass rumble 55Hz -> 25Hz)
snd_shockwave = create_proc_sound(function(_t, _dur) {
    var _env = power(max(0.0, 1.0 - (_t / _dur)), 2.0);
    var _freq = 55.0 - 30.0 * (_t / _dur);
    return sin(2.0 * pi * _freq * _t) * _env;
}, 0.55);

// Reusable UI Button Helper Function
ui_btn = function(_bx, _by, _bw, _bh, _label, _is_selected = false, _accent_color = c_aqua) {
    var _mx = device_mouse_x_to_gui(0);
    var _my = device_mouse_y_to_gui(0);
    var _hover = (_mx >= _bx && _mx <= _bx + _bw && _my >= _by && _my <= _by + _bh);
    var _clicked = _hover && mouse_check_button_pressed(mb_left);

    draw_set_alpha(0.85);
    if (_is_selected) {
        draw_set_color(c_navy);
    } else if (_hover) {
        draw_set_color(c_dkgray);
    } else {
        draw_set_color(c_black);
    }
    draw_rectangle(_bx, _by, _bx + _bw, _by + _bh, false);

    draw_set_alpha(1.0);
    if (_is_selected) {
        draw_set_color(_accent_color);
        draw_rectangle(_bx, _by, _bx + _bw, _by + _bh, true);
        draw_rectangle(_bx + 1, _by + 1, _bx + _bw - 1, _by + _bh - 1, true);
    } else if (_hover) {
        draw_set_color(c_white);
        draw_rectangle(_bx, _by, _bx + _bw, _by + _bh, true);
    } else {
        draw_set_color(c_gray);
        draw_rectangle(_bx, _by, _bx + _bw, _by + _bh, true);
    }

    if (_is_selected) {
        draw_set_color(c_yellow);
    } else if (_hover) {
        draw_set_color(c_white);
    } else {
        draw_set_color(c_ltgray);
    }
    draw_set_halign(fa_center);
    draw_set_valign(fa_middle);
    draw_text(_bx + _bw * 0.5, _by + _bh * 0.5, _label);
    draw_set_halign(fa_left);
    draw_set_valign(fa_top);

    return _clicked;
};

// Scene Builder Function for All 5 Modes
build_scene = function(_mode) {
    // 1. Destroy existing entities and native physics bodies
    with (obj_3d_entity) {
        if (physics_body_id > 0) {
            destroy_physics_body(physics_body_id);
        }
        instance_destroy();
    }
    selected_entity = noone;
    player_entity = noone;
    floor_entity = noone;
    island_entity = noone;
    light0_ent = noone;
    light1_ent = noone;
    light2_ent = noone;
    mat_pedestals = [];
    gm3d_light_point_clear();
    free_fly_active = false;
    mouse_captured = false;
    window_set_cursor(cr_default);

    app_mode = _mode;

    // 2. Build Specific Scene
    if (_mode == MODE_ISLAND) {
        // --- 🏝️ OCEAN ISLAND ---
        cam_mode = 1; // Third-person character controller
        cam_dist = 7.0;
        cam_pitch = 22.0;

        // Warm Golden Sunlight & Natural Atmospheric Horizon Mist (NewEra3D Palette)
        gm3d_light_set_sun(-0.35, -0.90, 0.25, 1.0, 0.96, 0.88, 1.85);
        gm3d_light_set_ambient(0.45, 0.58, 0.78, 1.15);
        gm3d_set_sky_color(0.12, 0.38, 0.85);
        gm3d_set_fog(160.0, 600.0, 0.42, 0.65, 0.88);

        // Ocean Water Plane (320x320, animated tropical waves & sea foam)
        var _water_body = gm3d_body_create_box(0, -0.5, 0, 320, 1, 320, 0.0, GM3D_MOTION_STATIC);
        var _water = instance_create_depth(0, 0, 0, obj_3d_entity);
        _water.x = 0; _water.y = 0.0; _water.z = 0;
        _water.qx = 0; _water.qy = 0; _water.qz = 0; _water.qw = 1;
        _water.image_xscale = 320; _water.image_yscale = 1; _water.image_zscale = 320;
        _water.mesh_id = GM3D_MESH_PLANE;
        _water.material_id = 4; // Cobalt Blue / Ocean Azure
        _water.texture_id = GM3D_TEX_DEFAULT; // Default texture lets procedural water shader shine
        _water.flags = GM3D_FLAG_ACTIVE;
        _water.physics_body_id = _water_body;
        floor_entity = _water;

        // Grand Procedural Multi-Biome Island Terrain (240x240m, 32m mountain peaks)
        var _terrain_body = gm3d_terrain_generate(128, 240.0, 32.0, 1337);
        var _terrain = instance_create_depth(0, 0, 0, obj_3d_entity);
        _terrain.x = 0; _terrain.y = 0.0; _terrain.z = 0;
        _terrain.qx = 0; _terrain.qy = 0; _terrain.qz = 0; _terrain.qw = 1;
        _terrain.image_xscale = 1.0; _terrain.image_yscale = 1.0; _terrain.image_zscale = 1.0;
        _terrain.mesh_id = GM3D_MESH_TERRAIN;
        _terrain.material_id = 0; // PBR with generated albedo and normal maps
        _terrain.texture_id = GM3D_TEX_TERRAIN;
        _terrain.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
        _terrain.physics_body_id = _terrain_body;
        island_entity = _terrain;

        // Ancient Plaza Columns & Shrines in Center
        var _col_coords = [[-6, 0, -6], [6, 0, -6], [-6, 0, 6], [6, 0, 6]];
        for (var _k = 0; _k < array_length(_col_coords); _k++) {
            var _cc = _col_coords[_k];
            var _ch = gm3d_terrain_get_height(_cc[0], _cc[2]);
            var _cb = gm3d_body_create_cylinder(_cc[0], _ch + 1.8, _cc[2], 0.6, 3.6, 0.0, GM3D_MOTION_STATIC);
            var _col = instance_create_depth(0, 0, 0, obj_3d_entity);
            _col.x = _cc[0]; _col.y = _ch + 1.8; _col.z = _cc[2];
            _col.qx = 0; _col.qy = 0; _col.qz = 0; _col.qw = 1;
            _col.image_xscale = 1.2; _col.image_yscale = 3.6; _col.image_zscale = 1.2;
            _col.mesh_id = GM3D_MESH_CYLINDER;
            _col.material_id = 0;
            _col.texture_id = GM3D_TEX_CHECKER;
            _col.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
            _col.physics_body_id = _cb;

            // Glowing Magic Gem atop each column
            var _gem = instance_create_depth(0, 0, 0, obj_3d_entity);
            _gem.x = _cc[0]; _gem.y = _ch + 4.2; _gem.z = _cc[2];
            _gem.qx = 0; _gem.qy = 0; _gem.qz = 0; _gem.qw = 1;
            _gem.image_xscale = 1.0; _gem.image_yscale = 1.4; _gem.image_zscale = 1.0;
            _gem.mesh_id = gem_mesh_id;
            _gem.material_id = (_k % 2 == 0) ? 3 : 5; // Emerald and Magenta
            _gem.texture_id = GM3D_TEX_DEFAULT;
            _gem.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
        }

        // Mountain Slope Boulders (Dynamic physical spheres that tumble down slopes!)
        var _boulder_seeds = [[-18, -18], [22, -15], [-20, 24], [25, 20]];
        for (var _b = 0; _b < array_length(_boulder_seeds); _b++) {
            var _bs = _boulder_seeds[_b];
            var _bx = _bs[0]; var _bz = _bs[1];
            var _by = gm3d_terrain_get_height(_bx, _bz) + 2.4;
            var _bb = gm3d_body_create_sphere(_bx, _by, _bz, 1.2, 8.0, GM3D_MOTION_DYNAMIC);
            var _b_ent = instance_create_depth(0, 0, 0, obj_3d_entity);
            _b_ent.x = _bx; _b_ent.y = _by; _b_ent.z = _bz;
            _b_ent.image_xscale = 2.4; _b_ent.image_yscale = 2.4; _b_ent.image_zscale = 2.4;
            _b_ent.mesh_id = GM3D_MESH_SPHERE;
            _b_ent.material_id = 8; // Weathered Alpine Granite Boulder
            _b_ent.texture_id = GM3D_TEX_DEFAULT;
            _b_ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
            _b_ent.physics_body_id = _bb;
        }

        // Procedural Pine Forests in the Emerald Valleys
        var _tree_count = 110;
        for (var _t = 0; _t < _tree_count; _t++) {
            var _ang = (_t / _tree_count) * 2.0 * pi * 4.3 + (_t * 0.37);
            var _dist = 22.0 + (_t * 0.85) + sin(_t * 1.9) * 8.0;
            if (_dist > 95.0) _dist = 22.0 + (_dist % 72.0);

            var _tx = cos(_ang) * _dist;
            var _tz = sin(_ang) * _dist;
            var _th = gm3d_terrain_get_height(_tx, _tz);

            // Plant trees only in valleys and gentle foothills (elevation 1.5m to 14.5m, outside plaza)
            if (_th >= 1.5 && _th <= 14.5 && point_distance(0, 0, _tx, _tz) > 16.0) {
                var _tree = instance_create_depth(0, 0, 0, obj_3d_entity);
                _tree.x = _tx;
                _tree.y = _th;
                _tree.z = _tz;
                var _tr_yaw = degtorad((_t * 53.0) % 360.0);
                _tree.qx = 0;
                _tree.qy = sin(_tr_yaw * 0.5);
                _tree.qz = 0;
                _tree.qw = cos(_tr_yaw * 0.5);
                var _scl = 1.0 + sin(_t * 3.3) * 0.35; // Size variation 0.65 to 1.35
                _tree.image_xscale = _scl;
                _tree.image_yscale = _scl * (0.9 + cos(_t * 2.1) * 0.2);
                _tree.image_zscale = _scl;
                _tree.mesh_id = GM3D_MESH_PINE_TREE;
                _tree.material_id = 6; // Pine Tree Material (bark trunk + evergreen needles)
                _tree.texture_id = GM3D_TEX_DEFAULT;
                _tree.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
                var _tb = gm3d_body_create_cylinder(_tx, _th + 1.2 * _scl, _tz, 0.35 * _scl, 2.4 * _scl, 0.0, GM3D_MOTION_STATIC);
                _tree.physics_body_id = _tb;
            }
        }

        // Spawn Playable Character on the Island Plaza
        player_x = 0.0;
        player_z = 0.0;
        player_y = max(1.5, gm3d_terrain_get_height(player_x, player_z));
        player_vy = 0.0;
        player_is_grounded = true;
        var _p_body = gm3d_body_create_capsule(player_x, player_y + 0.92, player_z, 0.45, 1.84, 0.0, GM3D_MOTION_KINEMATIC);
        var _player = instance_create_depth(0, 0, 0, obj_3d_entity);
        _player.x = player_x; _player.y = player_y; _player.z = player_z;
        _player.qx = 0; _player.qy = 0; _player.qz = 0; _player.qw = 1;
        _player.image_xscale = 1.0; _player.image_yscale = 1.0; _player.image_zscale = 1.0;
        _player.mesh_id = mannequin_mesh_id;
        _player.material_id = 7; // Cobalt Royal Steel Armor with Golden Luster
        _player.texture_id = GM3D_TEX_DEFAULT;
        _player.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_KINEMATIC;
        _player.physics_body_id = _p_body;
        player_entity = _player;
        gm3d_anim_play(_player.id, 9, 1.0, true); // Idle_Loop

        editor_notify_text = "MODE: Ocean Island (WASD to run, Shift to sprint, Space to jump, LMB to punch)";
        editor_notify_timer = 3.5;

    } else if (_mode == MODE_EDITOR) {
        // --- 🛠️ 3D SCENE EDITOR ---
        cam_mode = 0; // Orbit camera
        cam_dist = 22.0;
        cam_pitch = 28.0;

        gm3d_light_set_sun(0.577, -0.707, 0.408, 1.0, 0.96, 0.88, 1.0);
        gm3d_light_set_ambient(0.18, 0.20, 0.25, 0.8);
        gm3d_set_sky_color(0.20, 0.24, 0.30);
        gm3d_set_fog(45.0, 160.0, 0.20, 0.24, 0.30);

        // Floor
        var _fl_body = gm3d_body_create_box(0, -0.5, 0, 60, 1, 60, 0.0, GM3D_MOTION_STATIC);
        var _fl = instance_create_depth(0, 0, 0, obj_3d_entity);
        _fl.x = 0; _fl.y = 0.0; _fl.z = 0;
        _fl.qx = 0; _fl.qy = 0; _fl.qz = 0; _fl.qw = 1;
        _fl.image_xscale = 60; _fl.image_yscale = 1; _fl.image_zscale = 60;
        _fl.mesh_id = GM3D_MESH_PLANE;
        _fl.material_id = 0;
        _fl.texture_id = GM3D_TEX_CHECKER;
        _fl.flags = GM3D_FLAG_ACTIVE;
        _fl.physics_body_id = _fl_body;
        floor_entity = _fl;

        // Default Editable Items
        // 1. Central Wood Cube
        var _b1 = gm3d_body_create_box(0, 1.0, 0, 2.0, 2.0, 2.0, 1.0, GM3D_MOTION_DYNAMIC);
        var _e1 = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e1.x = 0; _e1.y = 1.0; _e1.z = 0;
        _e1.qx = 0; _e1.qy = 0; _e1.qz = 0; _e1.qw = 1;
        _e1.image_xscale = 2.0; _e1.image_yscale = 2.0; _e1.image_zscale = 2.0;
        _e1.mesh_id = GM3D_MESH_CUBE;
        _e1.material_id = 0;
        _e1.texture_id = GM3D_TEX_WOOD;
        _e1.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e1.physics_body_id = _b1;
        selected_entity = _e1; // Auto-select so 3D Gizmo is IMMEDIATELY VISIBLE!

        // 2. Ruby Sphere
        var _b2 = gm3d_body_create_box(-4.5, 1.0, 0, 2.0, 2.0, 2.0, 1.0, GM3D_MOTION_DYNAMIC);
        var _e2 = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e2.x = -4.5; _e2.y = 1.0; _e2.z = 0;
        _e2.qx = 0; _e2.qy = 0; _e2.qz = 0; _e2.qw = 1;
        _e2.image_xscale = 2.0; _e2.image_yscale = 2.0; _e2.image_zscale = 2.0;
        _e2.mesh_id = GM3D_MESH_SPHERE;
        _e2.material_id = 1; // Ruby
        _e2.texture_id = GM3D_TEX_GRID;
        _e2.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e2.physics_body_id = _b2;

        // 3. Emerald Gem
        var _b3 = gm3d_body_create_box(4.5, 1.0, 0, 1.8, 2.2, 1.8, 1.0, GM3D_MOTION_DYNAMIC);
        var _e3 = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e3.x = 4.5; _e3.y = 1.0; _e3.z = 0;
        _e3.qx = 0; _e3.qy = 0; _e3.qz = 0; _e3.qw = 1;
        _e3.image_xscale = 1.8; _e3.image_yscale = 1.8; _e3.image_zscale = 1.8;
        _e3.mesh_id = gem_mesh_id;
        _e3.material_id = 3; // Emerald
        _e3.texture_id = GM3D_TEX_DEFAULT;
        _e3.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e3.physics_body_id = _b3;

        // 4. Hazard Cylinder
        var _b4 = gm3d_body_create_box(0, 1.25, 4.5, 2.0, 2.5, 2.0, 1.0, GM3D_MOTION_DYNAMIC);
        var _e4 = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e4.x = 0; _e4.y = 1.25; _e4.z = 4.5;
        _e4.qx = 0; _e4.qy = 0; _e4.qz = 0; _e4.qw = 1;
        _e4.image_xscale = 2.0; _e4.image_yscale = 2.5; _e4.image_zscale = 2.0;
        _e4.mesh_id = GM3D_MESH_CYLINDER;
        _e4.material_id = 0;
        _e4.texture_id = GM3D_TEX_METAL;
        _e4.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e4.physics_body_id = _b4;

        editor_notify_text = "3D SCENE EDITOR: Select objects with Tab, move with IJKL/UO, scale with +/-";
        editor_notify_timer = 3.5;

    } else if (_mode == MODE_PHYSICS) {
        // --- ⚙️ JOLT PHYSICS LAB ---
        cam_mode = 0;
        cam_dist = 28.0;
        cam_pitch = 24.0;

        gm3d_light_set_sun(0.577, -0.707, 0.408, 1.0, 0.96, 0.88, 1.0);
        gm3d_light_set_ambient(0.16, 0.18, 0.22, 0.8);
        gm3d_set_sky_color(0.25, 0.35, 0.48);
        gm3d_set_fog(40.0, 140.0, 0.25, 0.35, 0.48);

        var _fl_body = gm3d_body_create_box(0, -0.5, 0, 60, 1, 60, 0.0, GM3D_MOTION_STATIC);
        var _fl = instance_create_depth(0, 0, 0, obj_3d_entity);
        _fl.x = 0; _fl.y = 0.0; _fl.z = 0;
        _fl.qx = 0; _fl.qy = 0; _fl.qz = 0; _fl.qw = 1;
        _fl.image_xscale = 60; _fl.image_yscale = 1; _fl.image_zscale = 60;
        _fl.mesh_id = GM3D_MESH_PLANE;
        _fl.material_id = 0;
        _fl.texture_id = GM3D_TEX_GRID;
        _fl.flags = GM3D_FLAG_ACTIVE;
        _fl.physics_body_id = _fl_body;
        floor_entity = _fl;

        // Build Stacking Physics Pyramid
        for (var _y = 1; _y <= 5; _y++) {
            for (var _x = -2; _x <= 2; _x++) {
                var _sx = _x * 2.1 + random_range(-0.02, 0.02);
                var _sy = _y * 2.1;
                var _sz = random_range(-0.02, 0.02);
                var _is_sph = (_y % 2 == 0);
                var _mesh = _is_sph ? GM3D_MESH_SPHERE : GM3D_MESH_CUBE;
                var _shape = _is_sph ? GM3D_SHAPE_SPHERE : GM3D_SHAPE_BOX;
                var _bid = create_physics_body(_shape, GM3D_MOTION_DYNAMIC, 1.0, _sx, _sy, _sz, 1.6, 1.6, 1.6);
                var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
                _ent.x = _sx; _ent.y = _sy; _ent.z = _sz;
                _ent.qx = 0; _ent.qy = 0; _ent.qz = 0; _ent.qw = 1;
                _ent.image_xscale = 1.6; _ent.image_yscale = 1.6; _ent.image_zscale = 1.6;
                _ent.mesh_id = _mesh;
                _ent.material_id = _is_sph ? 1 : 2; // Ruby and Gold
                _ent.texture_id = _is_sph ? GM3D_TEX_CHECKER : GM3D_TEX_WOOD;
                _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
                _ent.physics_body_id = _bid;
            }
        }

        // 2 Animated Dancing Mannequins on Sides
        var _mb1 = gm3d_body_create_capsule(-8.0, 0.92, 0, 0.45, 1.0, 0.0, GM3D_MOTION_KINEMATIC);
        var _m1 = instance_create_depth(0, 0, 0, obj_3d_entity);
        _m1.x = -8.0; _m1.y = 0.0; _m1.z = 0;
        _m1.qx = 0; _m1.qy = 0; _m1.qz = 0; _m1.qw = 1;
        _m1.image_xscale = 1.0; _m1.image_yscale = 1.0; _m1.image_zscale = 1.0;
        _m1.mesh_id = mannequin_mesh_id;
        _m1.material_id = 4;
        _m1.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
        _m1.physics_body_id = _mb1;
        gm3d_anim_play(_m1.id, 3, 1.0, true);

        var _mb2 = gm3d_body_create_capsule(8.0, 0.92, 0, 0.45, 1.0, 0.0, GM3D_MOTION_KINEMATIC);
        var _m2 = instance_create_depth(0, 0, 0, obj_3d_entity);
        _m2.x = 8.0; _m2.y = 0.0; _m2.z = 0;
        _m2.qx = 0; _m2.qy = 0; _m2.qz = 0; _m2.qw = 1;
        _m2.image_xscale = 1.0; _m2.image_yscale = 1.0; _m2.image_zscale = 1.0;
        _m2.mesh_id = mannequin_mesh_id;
        _m2.material_id = 2;
        _m2.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
        _m2.physics_body_id = _mb2;
        gm3d_anim_play(_m2.id, 9, 1.0, true);

        editor_notify_text = "JOLT PHYSICS LAB: 25 dynamic rigid bodies, impulses, dancing mannequins";
        editor_notify_timer = 3.5;

    } else if (_mode == MODE_LIGHTING) {
        // --- 💡 LIGHTING & ATMOSPHERE STUDIO ---
        cam_mode = 0;
        cam_dist = 24.0;
        cam_pitch = 22.0;

        gm3d_light_set_sun(0.577, -0.707, 0.408, 0.25, 0.4, 0.75, 0.08);
        gm3d_light_set_ambient(0.02, 0.03, 0.05, 0.25);
        gm3d_set_sky_color(0.02, 0.03, 0.06);
        gm3d_set_fog(25.0, 85.0, 0.02, 0.03, 0.06);

        var _fl_body = gm3d_body_create_box(0, -0.5, 0, 60, 1, 60, 0.0, GM3D_MOTION_STATIC);
        var _fl = instance_create_depth(0, 0, 0, obj_3d_entity);
        _fl.x = 0; _fl.y = 0.0; _fl.z = 0;
        _fl.qx = 0; _fl.qy = 0; _fl.qz = 0; _fl.qw = 1;
        _fl.image_xscale = 60; _fl.image_yscale = 1; _fl.image_zscale = 60;
        _fl.mesh_id = GM3D_MESH_PLANE;
        _fl.material_id = 0;
        _fl.texture_id = GM3D_TEX_GRID;
        _fl.flags = GM3D_FLAG_ACTIVE;
        _fl.physics_body_id = _fl_body;
        floor_entity = _fl;

        // Central Pedestal + Gem
        var _ped_b = gm3d_body_create_box(0, 1.25, 0, 3.0, 2.5, 3.0, 0.0, GM3D_MOTION_STATIC);
        var _ped = instance_create_depth(0, 0, 0, obj_3d_entity);
        _ped.x = 0; _ped.y = 1.25; _ped.z = 0;
        _ped.qx = 0; _ped.qy = 0; _ped.qz = 0; _ped.qw = 1;
        _ped.image_xscale = 3.0; _ped.image_yscale = 2.5; _ped.image_zscale = 3.0;
        _ped.mesh_id = GM3D_MESH_CUBE;
        _ped.material_id = 0;
        _ped.texture_id = GM3D_TEX_CHECKER;
        _ped.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
        _ped.physics_body_id = _ped_b;

        var _cg_b = gm3d_body_create_box(0, 3.8, 0, 1.6, 2.2, 1.6, 0.0, GM3D_MOTION_STATIC);
        var _cg = instance_create_depth(0, 0, 0, obj_3d_entity);
        _cg.x = 0; _cg.y = 3.8; _cg.z = 0;
        _cg.qx = 0; _cg.qy = 0; _cg.qz = 0; _cg.qw = 1;
        _cg.image_xscale = 1.6; _cg.image_yscale = 2.2; _cg.image_zscale = 1.6;
        _cg.mesh_id = gem_mesh_id;
        _cg.material_id = 3; // Emerald
        _cg.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
        _cg.physics_body_id = _cg_b;

        // Point Light Visual Indicator Orbs
        light0_ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        light0_ent.x = 0; light0_ent.y = 5.0; light0_ent.z = 0;
        light0_ent.qx = 0; light0_ent.qy = 0; light0_ent.qz = 0; light0_ent.qw = 1;
        light0_ent.image_xscale = 0.45; light0_ent.image_yscale = 0.45; light0_ent.image_zscale = 0.45;
        light0_ent.mesh_id = GM3D_MESH_SPHERE;
        light0_ent.material_id = 2; // Amber
        light0_ent.texture_id = GM3D_TEX_DEFAULT;
        light0_ent.flags = GM3D_FLAG_ACTIVE;

        light1_ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        light1_ent.x = -6.0; light1_ent.y = 3.0; light1_ent.z = 0;
        light1_ent.qx = 0; light1_ent.qy = 0; light1_ent.qz = 0; light1_ent.qw = 1;
        light1_ent.image_xscale = 0.35; light1_ent.image_yscale = 0.35; light1_ent.image_zscale = 0.35;
        light1_ent.mesh_id = GM3D_MESH_SPHERE;
        light1_ent.material_id = 4; // Neon Cyan
        light1_ent.texture_id = GM3D_TEX_DEFAULT;
        light1_ent.flags = GM3D_FLAG_ACTIVE;

        light2_ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        light2_ent.x = 6.0; light2_ent.y = 3.0; light2_ent.z = 0;
        light2_ent.qx = 0; light2_ent.qy = 0; light2_ent.qz = 0; light2_ent.qw = 1;
        light2_ent.image_xscale = 0.35; light2_ent.image_yscale = 0.35; light2_ent.image_zscale = 0.35;
        light2_ent.mesh_id = GM3D_MESH_SPHERE;
        light2_ent.material_id = 5; // Magenta
        light2_ent.texture_id = GM3D_TEX_DEFAULT;
        light2_ent.flags = GM3D_FLAG_ACTIVE;

        editor_notify_text = "LIGHTING & FOG STUDIO: Orbiting dynamic point lights & volumetric atmospheric fog";
        editor_notify_timer = 3.5;

    } else if (_mode == MODE_MATERIAL) {
        // --- 🔮 MATERIALS & NORMAL MAPS SHOWCASE ---
        cam_mode = 0;
        cam_dist = 22.0;
        cam_pitch = 20.0;

        gm3d_light_set_sun(0.2, -0.6, 0.4, 0.5, 0.5, 0.6, 0.5);
        gm3d_light_set_ambient(0.12, 0.14, 0.18, 0.6);
        gm3d_set_sky_color(0.12, 0.15, 0.20);
        gm3d_set_fog(35.0, 120.0, 0.12, 0.15, 0.20);

        var _fl_body = gm3d_body_create_box(0, -0.5, 0, 60, 1, 60, 0.0, GM3D_MOTION_STATIC);
        var _fl = instance_create_depth(0, 0, 0, obj_3d_entity);
        _fl.x = 0; _fl.y = 0.0; _fl.z = 0;
        _fl.qx = 0; _fl.qy = 0; _fl.qz = 0; _fl.qw = 1;
        _fl.image_xscale = 60; _fl.image_yscale = 1; _fl.image_zscale = 60;
        _fl.mesh_id = GM3D_MESH_PLANE;
        _fl.material_id = 0;
        _fl.texture_id = GM3D_TEX_DEFAULT;
        _fl.flags = GM3D_FLAG_ACTIVE;
        _fl.physics_body_id = _fl_body;
        floor_entity = _fl;

        // 5 Exhibition Pedestals along X: [-10, -5, 0, 5, 10]
        var _configs = [
            [-10, GM3D_MESH_CUBE, 0, GM3D_TEX_CHECKER],
            [-5,  GM3D_MESH_CUBE, 0, GM3D_TEX_GRID],
            [0,   GM3D_MESH_CUBE, 0, GM3D_TEX_WOOD],
            [5,   GM3D_MESH_CUBE, 0, GM3D_TEX_METAL],
            [10,  gem_mesh_id,    5, GM3D_TEX_DEFAULT]
        ];

        for (var _m = 0; _m < array_length(_configs); _m++) {
            var _cfg = _configs[_m];
            var _px = _cfg[0];

            // Pedestal base
            var _pb = instance_create_depth(0, 0, 0, obj_3d_entity);
            _pb.x = _px; _pb.y = 0.5; _pb.z = 0;
            _pb.qx = 0; _pb.qy = 0; _pb.qz = 0; _pb.qw = 1;
            _pb.image_xscale = 2.4; _pb.image_yscale = 1.0; _pb.image_zscale = 2.4;
            _pb.mesh_id = GM3D_MESH_CYLINDER;
            _pb.material_id = 0;
            _pb.texture_id = GM3D_TEX_DEFAULT;
            _pb.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;

            // Rotating Showcase Object
            var _so = instance_create_depth(0, 0, 0, obj_3d_entity);
            _so.x = _px; _so.y = 2.2; _so.z = 0;
            _so.qx = 0; _so.qy = 0; _so.qz = 0; _so.qw = 1;
            _so.image_xscale = 1.7; _so.image_yscale = 1.7; _so.image_zscale = 1.7;
            _so.mesh_id = _cfg[1];
            _so.material_id = _cfg[2];
            _so.texture_id = _cfg[3];
            _so.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW;
            array_push(mat_pedestals, _so);
        }

        // Grazing angle spotlight orb
        light0_ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        light0_ent.x = 0; light0_ent.y = 3.2; light0_ent.z = 4.0;
        light0_ent.qx = 0; light0_ent.qy = 0; light0_ent.qz = 0; light0_ent.qw = 1;
        light0_ent.image_xscale = 0.4; light0_ent.image_yscale = 0.4; light0_ent.image_zscale = 0.4;
        light0_ent.mesh_id = GM3D_MESH_SPHERE;
        light0_ent.material_id = 2; // Gold
        light0_ent.texture_id = GM3D_TEX_DEFAULT;
        light0_ent.flags = GM3D_FLAG_ACTIVE;

        editor_notify_text = "MATERIAL SHOWCASE: Procedural Sobel normal-mapped pedestals under grazing light";
        editor_notify_timer = 3.5;
    }
};

// Initial Setup: Ocean Island Mode
build_scene(MODE_ISLAND);
window_set_caption("GM3D [Press F5 for Menu] | Mode: 1: Ocean Island (Keys 1-5) | Invert Y: ON (Key Y) | FPS: 60");
