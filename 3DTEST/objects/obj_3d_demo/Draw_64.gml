var _sw = window_get_width();
var _sh = window_get_height();
if (_sw <= 0) _sw = 1280;
if (_sh <= 0) _sh = 720;

// ------------------------------------------------------------------------------------------------
// TOP NAVIGATION BAR (5 Specialised Modes + Invert Y-Axis Toggle)
// ------------------------------------------------------------------------------------------------
draw_set_color(c_black);
draw_set_alpha(0.88);
draw_rectangle(0, 0, _sw, 48, false);
draw_set_alpha(1.0);
draw_set_color(c_dkgray);
draw_line(0, 48, _sw, 48);

if (ui_btn(15, 7, 160, 34, "1: Ocean Island", app_mode == MODE_ISLAND, c_lime)) {
    build_scene(MODE_ISLAND);
}
if (ui_btn(180, 7, 145, 34, "2: 3D Editor", app_mode == MODE_EDITOR, c_orange)) {
    build_scene(MODE_EDITOR);
}
if (ui_btn(330, 7, 145, 34, "3: Jolt Physics", app_mode == MODE_PHYSICS, c_yellow)) {
    build_scene(MODE_PHYSICS);
}
if (ui_btn(480, 7, 150, 34, "4: Lighting & Fog", app_mode == MODE_LIGHTING, c_aqua)) {
    build_scene(MODE_LIGHTING);
}
if (ui_btn(635, 7, 145, 34, "5: Materials", app_mode == MODE_MATERIAL, c_fuchsia)) {
    build_scene(MODE_MATERIAL);
}

// HDR Bloom Toggle Button on Top-Right
var _bloom_lbl = bloom_active ? "HDR Bloom: ON" : "HDR Bloom: OFF";
if (ui_btn(_sw - 330, 7, 155, 34, _bloom_lbl, bloom_active, c_fuchsia)) {
    bloom_active = !bloom_active;
    gm3d_render_set_bloom(bloom_active, 0.85, 1.0);
    editor_notify_text = bloom_active ? "HDR Bloom & Glow: ENABLED" : "HDR Bloom & Glow: DISABLED";
    editor_notify_timer = 2.5;
}

// Inverted Y-Axis Toggle Button on Top-Right
var _inv_lbl = invert_cam_y ? "Invert Y: ON" : "Invert Y: OFF";
if (ui_btn(_sw - 165, 7, 150, 34, _inv_lbl, invert_cam_y, c_yellow)) {
    invert_cam_y = !invert_cam_y;
}

// ------------------------------------------------------------------------------------------------
// FPS & Engine Status Banner (Top-Left)
// ------------------------------------------------------------------------------------------------
draw_set_color(c_black);
draw_set_alpha(0.70);
draw_rectangle(15, 52, 380, 76, false);
draw_set_alpha(1.0);

draw_set_color(c_lime);
draw_text(25, 56, "GM3D Engine | FPS: " + string(fps_real) + " (" + string(fps) + ") | Entities: " + string(instance_number(obj_3d_entity)));

// ------------------------------------------------------------------------------------------------
// Notification Toast Banner
// ------------------------------------------------------------------------------------------------
if (editor_notify_timer > 0 && editor_notify_text != "") {
    var _nw = string_width(editor_notify_text) + 40;
    var _nx = (_sw - _nw) * 0.5;
    draw_set_color(c_navy);
    draw_set_alpha(0.92);
    draw_rectangle(_nx, 85, _nx + _nw, 120, false);
    draw_set_color(c_aqua);
    draw_rectangle(_nx, 85, _nx + _nw, 120, true);
    draw_set_alpha(1.0);
    draw_set_color(c_yellow);
    draw_text(_nx + 20, 93, editor_notify_text);
}

// ================================================================================================
// MODE 0: OCEAN ISLAND (Third-Person Playable Character)
// ================================================================================================
if (app_mode == MODE_ISLAND) {
    var _cx = 15;
    var _cw = 720;
    var _ch = 150;
    var _cy = _sh - 165;

    draw_set_color(c_black);
    draw_set_alpha(0.85);
    draw_rectangle(_cx, _cy, _cx + _cw, _cy + _ch, false);
    draw_set_alpha(1.0);
    draw_set_color(c_aqua);
    draw_rectangle(_cx, _cy, _cx + _cw, _cy + _ch, true);

    draw_set_color(c_lime);
    draw_text(_cx + 15, _cy + 10, "MODE: PROCEDURAL ISLAND TERRAIN (240x240m MULTI-BIOME)");

    draw_set_color(c_white);
    draw_text(_cx + 15, _cy + 30, "- W, A, S, D: Move | SHIFT: Sprint | SPACE: Jump | RMB: Orbit Camera");
    draw_text(_cx + 15, _cy + 48, "- LMB: Shockwave Punch (Knocks back rigid bodies & rolling boulders)");
    draw_text(_cx + 15, _cy + 66, "- Q: Fire Blast | E: Arcane Nova | R: Holy Radiance | F: Frost Blizzard");
    draw_text(_cx + 15, _cy + 84, "- B: Toggle HDR Bloom | Y: Toggle Invert Y-Axis | F5: Open Menu");
    draw_set_color(c_yellow);
    draw_text(_cx + 15, _cy + 104, "Tech: 10-Octave fBm Erosion, Ridged Multifractals, Jolt HeightField, PBR Biomes");
    draw_set_color(c_aqua);
    draw_text(_cx + 15, _cy + 124, "Biomes: Sandy Beach -> Emerald Valleys -> Granite Cliffs -> Snow Peaks (Elevation: " + string_format(player_y, 1, 1) + "m)");
}

// ================================================================================================
// MODE 1: 🛠️ 3D SCENE EDITOR (Visual Clickable UI Inspector + 3D Gizmo)
// ================================================================================================
if (app_mode == MODE_EDITOR) {
    // --------------------------------------------------------------------------------------------
    // 1. 3D Axis Gizmo (Projected onto Selected Entity in Viewport)
    // --------------------------------------------------------------------------------------------
    if (selected_entity != noone && instance_exists(selected_entity)) {
        var _proj_c = gm3d_world_to_screen(selected_entity.x, selected_entity.y, selected_entity.z, cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);
        if (_proj_c[2]) {
            var _gx = _proj_c[0];
            var _gy = _proj_c[1];

            var _px = gm3d_world_to_screen(selected_entity.x + 2.5, selected_entity.y, selected_entity.z, cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);
            var _py = gm3d_world_to_screen(selected_entity.x, selected_entity.y + 2.5, selected_entity.z, cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);
            var _pz = gm3d_world_to_screen(selected_entity.x, selected_entity.y, selected_entity.z + 2.5, cur_cam_x, cur_cam_y, cur_cam_z, cur_target_x, cur_target_y, cur_target_z, 60.0);

            // Red X-Axis Arrow
            if (_px[2]) {
                draw_set_color(c_red);
                draw_line_width(_gx, _gy, _px[0], _px[1], 4);
                draw_circle(_px[0], _px[1], 5, false);
                draw_text(_px[0] + 6, _px[1] - 8, "+X");
            }
            // Green Y-Axis Arrow
            if (_py[2]) {
                draw_set_color(c_lime);
                draw_line_width(_gx, _gy, _py[0], _py[1], 4);
                draw_circle(_py[0], _py[1], 5, false);
                draw_text(_py[0] + 6, _py[1] - 8, "+Y");
            }
            // Blue Z-Axis Arrow
            if (_pz[2]) {
                draw_set_color(c_aqua);
                draw_line_width(_gx, _gy, _pz[0], _pz[1], 4);
                draw_circle(_pz[0], _pz[1], 5, false);
                draw_text(_pz[0] + 6, _pz[1] - 8, "+Z");
            }

            // Target Focus Reticle & Corner Brackets
            draw_set_color(c_yellow);
            draw_rectangle(_gx - 18, _gy - 18, _gx + 18, _gy + 18, true);
            draw_circle(_gx, _gy, 4, false);

            // Floating Tooltip above Entity
            draw_set_color(c_black);
            draw_set_alpha(0.80);
            draw_rectangle(_gx + 25, _gy - 50, _gx + 320, _gy + 35, false);
            draw_set_alpha(1.0);
            draw_set_color(c_yellow);
            draw_text(_gx + 32, _gy - 44, "SELECTED: [ID " + string(selected_entity.id) + "] Mesh " + string(selected_entity.mesh_id));
            draw_set_color(c_white);
            draw_text(_gx + 32, _gy - 24, "Position: (" + string_format(selected_entity.x, 0, 1) + ", " + string_format(selected_entity.y, 0, 1) + ", " + string_format(selected_entity.z, 0, 1) + ")");
            draw_text(_gx + 32, _gy - 4, "Scale: (" + string_format(selected_entity.image_xscale, 0, 1) + ", " + string_format(selected_entity.image_yscale, 0, 1) + ", " + string_format(selected_entity.image_zscale, 0, 1) + ")");
            draw_text(_gx + 32, _gy + 16, "Material: " + string(selected_entity.material_id) + " | Texture: " + string(variable_instance_exists(selected_entity, "texture_id") ? selected_entity.texture_id : 0));
        }
    }

    // --------------------------------------------------------------------------------------------
    // 2. Clickable UI Inspector Panel on the Right
    // --------------------------------------------------------------------------------------------
    var _pw = 265;
    var _px = _sw - _pw - 10;
    var _py = 52;
    var _ph = _sh - 62;

    draw_set_color(c_black);
    draw_set_alpha(0.88);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, false);
    draw_set_alpha(1.0);
    draw_set_color(c_orange);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, true);

    var _y_cursor = _py + 10;
    draw_set_color(c_yellow);
    draw_set_halign(fa_center);
    draw_text(_px + _pw * 0.5, _y_cursor, "SCENE INSPECTOR");
    draw_set_halign(fa_left);
    _y_cursor += 24;

    // Entity Selector Buttons: [< Prev] [Next >]
    if (ui_btn(_px + 10, _y_cursor, 118, 26, "< Prev", false)) {
        var _first = noone; var _prev = noone; var _pick = noone;
        with (obj_3d_entity) {
            if (id != other.floor_entity) {
                if (_first == noone) _first = id;
                if (id == other.selected_entity) {
                    _pick = (_prev != noone) ? _prev : id;
                }
                _prev = id;
            }
        }
        if (_pick != noone) selected_entity = _pick;
    }
    if (ui_btn(_px + 137, _y_cursor, 118, 26, "Next >", false)) {
        var _found_curr = false; var _pick = noone; var _first = noone;
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
    _y_cursor += 34;

    // Transform: Position
    draw_set_color(c_white);
    draw_text(_px + 10, _y_cursor, "Position (X, Y, Z):");
    _y_cursor += 20;

    var _move_delta = 0.5;
    if (selected_entity != noone && instance_exists(selected_entity)) {
        if (ui_btn(_px + 10, _y_cursor, 118, 26, "- X")) {
            selected_entity.x -= _move_delta;
            if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
        }
        if (ui_btn(_px + 137, _y_cursor, 118, 26, "+ X")) {
            selected_entity.x += _move_delta;
            if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
        }
        _y_cursor += 30;

        if (ui_btn(_px + 10, _y_cursor, 118, 26, "- Y")) {
            selected_entity.y -= _move_delta;
            if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
        }
        if (ui_btn(_px + 137, _y_cursor, 118, 26, "+ Y")) {
            selected_entity.y += _move_delta;
            if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
        }
        _y_cursor += 30;

        if (ui_btn(_px + 10, _y_cursor, 118, 26, "- Z")) {
            selected_entity.z -= _move_delta;
            if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
        }
        if (ui_btn(_px + 137, _y_cursor, 118, 26, "+ Z")) {
            selected_entity.z += _move_delta;
            if (selected_entity.physics_body_id > 0) gm3d_body_set_transform(selected_entity.physics_body_id, selected_entity.x, selected_entity.y, selected_entity.z, selected_entity.qx, selected_entity.qy, selected_entity.qz, selected_entity.qw);
        }
        _y_cursor += 32;

        // Scale
        draw_set_color(c_white);
        draw_text(_px + 10, _y_cursor, "Scale:");
        _y_cursor += 20;

        if (ui_btn(_px + 10, _y_cursor, 118, 26, "Scale -")) {
            selected_entity.image_xscale = max(0.2, selected_entity.image_xscale * 0.85);
            selected_entity.image_yscale = max(0.2, selected_entity.image_yscale * 0.85);
            selected_entity.image_zscale = max(0.2, selected_entity.image_zscale * 0.85);
        }
        if (ui_btn(_px + 137, _y_cursor, 118, 26, "Scale +")) {
            selected_entity.image_xscale *= 1.15;
            selected_entity.image_yscale *= 1.15;
            selected_entity.image_zscale *= 1.15;
        }
        _y_cursor += 32;

        // PBR Materials (Concrete, Ruby, Gold, Emerald, Cobalt, Amethyst)
        draw_set_color(c_white);
        draw_text(_px + 10, _y_cursor, "PBR Material:");
        _y_cursor += 20;

        if (ui_btn(_px + 10, _y_cursor, 76, 24, "Concrete", selected_entity.material_id == 0)) selected_entity.material_id = 0;
        if (ui_btn(_px + 91, _y_cursor, 76, 24, "Ruby", selected_entity.material_id == 1, c_red)) selected_entity.material_id = 1;
        if (ui_btn(_px + 172, _y_cursor, 83, 24, "Gold", selected_entity.material_id == 2, c_yellow)) selected_entity.material_id = 2;
        _y_cursor += 28;

        if (ui_btn(_px + 10, _y_cursor, 76, 24, "Emerald", selected_entity.material_id == 3, c_lime)) selected_entity.material_id = 3;
        if (ui_btn(_px + 91, _y_cursor, 76, 24, "Cobalt", selected_entity.material_id == 4, c_aqua)) selected_entity.material_id = 4;
        if (ui_btn(_px + 172, _y_cursor, 83, 24, "Amethyst", selected_entity.material_id == 5, c_fuchsia)) selected_entity.material_id = 5;
        _y_cursor += 32;

        // Textures & Normal Maps
        draw_set_color(c_white);
        draw_text(_px + 10, _y_cursor, "Texture & Normal Map:");
        _y_cursor += 20;

        var _cur_tex = variable_instance_exists(selected_entity, "texture_id") ? selected_entity.texture_id : 0;
        if (ui_btn(_px + 10, _y_cursor, 76, 24, "White", _cur_tex == 0)) selected_entity.texture_id = 0;
        if (ui_btn(_px + 91, _y_cursor, 76, 24, "Checker", _cur_tex == 1)) selected_entity.texture_id = 1;
        if (ui_btn(_px + 172, _y_cursor, 83, 24, "Grid", _cur_tex == 2)) selected_entity.texture_id = 2;
        _y_cursor += 28;

        if (ui_btn(_px + 10, _y_cursor, 118, 24, "Wood", _cur_tex == 3)) selected_entity.texture_id = 3;
        if (ui_btn(_px + 137, _y_cursor, 118, 24, "Metal", _cur_tex == 4)) selected_entity.texture_id = 4;
        _y_cursor += 32;
    }

    // Geometry Spawner
    draw_set_color(c_white);
    draw_text(_px + 10, _y_cursor, "Spawn Object:");
    _y_cursor += 20;

    if (ui_btn(_px + 10, _y_cursor, 76, 26, "+ Cube")) {
        var _b = gm3d_body_create_box(0, 10.0, 0, 1.8, 1.8, 1.8, 1.0, GM3D_MOTION_DYNAMIC);
        var _e = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e.x = 0; _e.y = 10.0; _e.z = 0;
        _e.qx = 0; _e.qy = 0; _e.qz = 0; _e.qw = 1;
        _e.image_xscale = 1.8; _e.image_yscale = 1.8; _e.image_zscale = 1.8;
        _e.mesh_id = GM3D_MESH_CUBE;
        _e.material_id = 0;
        _e.texture_id = GM3D_TEX_WOOD;
        _e.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e.physics_body_id = _b;
        selected_entity = _e;
    }
    if (ui_btn(_px + 91, _y_cursor, 76, 26, "+ Sphere")) {
        var _b = gm3d_body_create_box(0, 10.0, 0, 1.8, 1.8, 1.8, 1.0, GM3D_MOTION_DYNAMIC);
        var _e = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e.x = 0; _e.y = 10.0; _e.z = 0;
        _e.qx = 0; _e.qy = 0; _e.qz = 0; _e.qw = 1;
        _e.image_xscale = 1.8; _e.image_yscale = 1.8; _e.image_zscale = 1.8;
        _e.mesh_id = GM3D_MESH_SPHERE;
        _e.material_id = 1;
        _e.texture_id = GM3D_TEX_CHECKER;
        _e.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e.physics_body_id = _b;
        selected_entity = _e;
    }
    if (ui_btn(_px + 172, _y_cursor, 83, 26, "+ Cylinder")) {
        var _b = gm3d_body_create_box(0, 10.0, 0, 1.8, 2.5, 1.8, 1.0, GM3D_MOTION_DYNAMIC);
        var _e = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e.x = 0; _e.y = 10.0; _e.z = 0;
        _e.qx = 0; _e.qy = 0; _e.qz = 0; _e.qw = 1;
        _e.image_xscale = 1.8; _e.image_yscale = 2.5; _e.image_zscale = 1.8;
        _e.mesh_id = GM3D_MESH_CYLINDER;
        _e.material_id = 0;
        _e.texture_id = GM3D_TEX_METAL;
        _e.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e.physics_body_id = _b;
        selected_entity = _e;
    }
    _y_cursor += 30;

    if (ui_btn(_px + 10, _y_cursor, 118, 26, "+ Crystal")) {
        var _b = gm3d_body_create_box(0, 10.0, 0, 1.6, 2.2, 1.6, 1.0, GM3D_MOTION_DYNAMIC);
        var _e = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e.x = 0; _e.y = 10.0; _e.z = 0;
        _e.qx = 0; _e.qy = 0; _e.qz = 0; _e.qw = 1;
        _e.image_xscale = 1.6; _e.image_yscale = 2.2; _e.image_zscale = 1.6;
        _e.mesh_id = gem_mesh_id;
        _e.material_id = 3;
        _e.texture_id = GM3D_TEX_DEFAULT;
        _e.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e.physics_body_id = _b;
        selected_entity = _e;
    }
    if (ui_btn(_px + 137, _y_cursor, 118, 26, "+ Mannequin")) {
        var _b = gm3d_body_create_box(0, 10.0, 0, 1.0, 1.8, 0.6, 3.0, GM3D_MOTION_DYNAMIC);
        var _e = instance_create_depth(0, 0, 0, obj_3d_entity);
        _e.x = 0; _e.y = 10.0; _e.z = 0;
        _e.qx = 0; _e.qy = 0; _e.qz = 0; _e.qw = 1;
        _e.image_xscale = 1.0; _e.image_yscale = 1.0; _e.image_zscale = 1.0;
        _e.mesh_id = mannequin_mesh_id;
        _e.material_id = 4;
        _e.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _e.physics_body_id = _b;
        gm3d_anim_play(_e.id, 3, 1.0, true);
        selected_entity = _e;
    }
    _y_cursor += 34;

    // Operations: Clone, Delete, Save, Load
    if (selected_entity != noone && instance_exists(selected_entity)) {
        if (ui_btn(_px + 10, _y_cursor, 118, 26, "Clone", false, c_aqua)) {
            var _dup = instance_create_depth(0, 0, 0, obj_3d_entity);
            _dup.x = selected_entity.x + 2.0;
            _dup.y = selected_entity.y;
            _dup.z = selected_entity.z + 2.0;
            _dup.qx = selected_entity.qx; _dup.qy = selected_entity.qy; _dup.qz = selected_entity.qz; _dup.qw = selected_entity.qw;
            _dup.image_xscale = selected_entity.image_xscale;
            _dup.image_yscale = selected_entity.image_yscale;
            _dup.image_zscale = selected_entity.image_zscale;
            _dup.mesh_id = selected_entity.mesh_id;
            _dup.material_id = selected_entity.material_id;
            _dup.texture_id = selected_entity.texture_id;
            _dup.flags = selected_entity.flags;
            if (selected_entity.physics_body_id > 0) {
                _dup.physics_body_id = create_physics_body(GM3D_SHAPE_BOX, GM3D_MOTION_DYNAMIC, 1.0, _dup.x, _dup.y, _dup.z, _dup.image_xscale, _dup.image_yscale, _dup.image_zscale);
            }
            selected_entity = _dup;
            editor_notify_text = "Entity cloned!";
            editor_notify_timer = 2.0;
        }
        if (ui_btn(_px + 137, _y_cursor, 118, 26, "Delete", false, c_red)) {
            if (selected_entity != floor_entity) {
                if (selected_entity.physics_body_id > 0) destroy_physics_body(selected_entity.physics_body_id);
                instance_destroy(selected_entity);
                selected_entity = noone;
                editor_notify_text = "Entity deleted.";
                editor_notify_timer = 2.0;
            }
        }
        _y_cursor += 32;
    }

    if (ui_btn(_px + 10, _y_cursor, 118, 28, "Save JSON", false, c_lime)) {
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
            editor_notify_text = "Saved " + string(array_length(_scene_list)) + " objects to scene_data.json!";
            editor_notify_timer = 3.5;
        }
    }
    if (ui_btn(_px + 137, _y_cursor, 118, 28, "Load JSON", false, c_yellow)) {
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
                editor_notify_text = "Loaded " + string(array_length(_loaded_list)) + " objects from scene_data.json!";
                editor_notify_timer = 3.5;
            }
        }
    }
}

// ================================================================================================
// MODE 2: ⚙️ JOLT PHYSICS LAB (Interactive Control Panel)
// ================================================================================================
if (app_mode == MODE_PHYSICS) {
    var _px = 15;
    var _py = 85;
    var _pw = 250;
    var _ph = 435;

    draw_set_color(c_black);
    draw_set_alpha(0.85);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, false);
    draw_set_alpha(1.0);
    draw_set_color(c_yellow);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, true);

    var _y_cursor = _py + 10;
    draw_set_color(c_yellow);
    draw_set_halign(fa_center);
    draw_text(_px + _pw * 0.5, _y_cursor, "JOLT PHYSICS LAB");
    draw_set_halign(fa_left);
    _y_cursor += 25;

    draw_set_color(c_white);
    draw_text(_px + 12, _y_cursor, "Gravity:");
    _y_cursor += 18;

    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Earth (-9.81 m/s²)", gravity_mode == 0)) {
        gravity_mode = 0; gm3d_set_gravity(0, -9.81, 0);
    }
    _y_cursor += 32;
    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Zero-G (0.0)", gravity_mode == 1)) {
        gravity_mode = 1; gm3d_set_gravity(0, 0, 0);
    }
    _y_cursor += 32;
    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Moon (-1.62 m/s²)", gravity_mode == 2)) {
        gravity_mode = 2; gm3d_set_gravity(0, -1.62, 0);
    }
    _y_cursor += 32;
    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Reverse (+9.81)", gravity_mode == 3)) {
        gravity_mode = 3; gm3d_set_gravity(0, 9.81, 0);
    }
    _y_cursor += 38;

    draw_set_color(c_white);
    draw_text(_px + 12, _y_cursor, "Forces & Impulses:");
    _y_cursor += 18;

    if (ui_btn(_px + 12, _y_cursor, 226, 32, "RADIAL EXPLOSION", false, c_red)) {
        with (obj_3d_entity) {
            if ((flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                var _dx = x; var _dz = z;
                var _d = point_distance(0, 0, _dx, _dz);
                if (_d > 0.01) { _dx /= _d; _dz /= _d; } else { _dx = 1; _dz = 0; }
                gm3d_body_apply_impulse(physics_body_id, _dx * 35.0, 25.0, _dz * 35.0);
            }
        }
    }
    _y_cursor += 36;

    if (ui_btn(_px + 12, _y_cursor, 226, 32, "VORTEX / TORNADO", false, c_yellow)) {
        with (obj_3d_entity) {
            if ((flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                gm3d_body_apply_torque(physics_body_id, random_range(-25, 25), random_range(40, 80), random_range(-25, 25));
                gm3d_body_apply_impulse(physics_body_id, random_range(-5, 5), 8.0, random_range(-5, 5));
            }
        }
    }
    _y_cursor += 38;

    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Spawn Pyramid")) {
        for (var _y = 1; _y <= 4; _y++) {
            for (var _x = -1; _x <= 1; _x++) {
                var _bid = create_physics_body(GM3D_SHAPE_BOX, GM3D_MOTION_DYNAMIC, 1.0, _x * 2.0, 12.0 + _y * 2.0, random_range(-0.1, 0.1), 1.6, 1.6, 1.6);
                var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
                _ent.x = _x * 2.0; _ent.y = 12.0 + _y * 2.0; _ent.z = 0;
                _ent.qx = 0; _ent.qy = 0; _ent.qz = 0; _ent.qw = 1;
                _ent.image_xscale = 1.6; _ent.image_yscale = 1.6; _ent.image_zscale = 1.6;
                _ent.mesh_id = GM3D_MESH_CUBE;
                _ent.material_id = choose(1, 2, 3);
                _ent.texture_id = choose(GM3D_TEX_CHECKER, GM3D_TEX_WOOD);
                _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
                _ent.physics_body_id = _bid;
            }
        }
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Falling Mannequin")) {
        var _bid = create_physics_body(GM3D_SHAPE_BOX, GM3D_MOTION_DYNAMIC, 3.0, random_range(-3, 3), 16.0, random_range(-3, 3), 1.0, 1.8, 0.6);
        gm3d_body_apply_torque(_bid, random_range(-15, 15), random_range(10, 30), random_range(-15, 15));
        var _ent = instance_create_depth(0, 0, 0, obj_3d_entity);
        _ent.x = 0; _ent.y = 16.0; _ent.z = 0;
        _ent.qx = 0; _ent.qy = 0; _ent.qz = 0; _ent.qw = 1;
        _ent.image_xscale = 1.0; _ent.image_yscale = 1.0; _ent.image_zscale = 1.0;
        _ent.mesh_id = mannequin_mesh_id;
        _ent.material_id = choose(1, 4);
        _ent.flags = GM3D_FLAG_ACTIVE | GM3D_FLAG_CAST_SHADOW | GM3D_FLAG_RIGIDBODY;
        _ent.physics_body_id = _bid;
        gm3d_anim_play(_ent.id, 4, 1.0, false);
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 226, 28, "Clear Rigid Bodies", false, c_orange)) {
        with (obj_3d_entity) {
            if ((flags & GM3D_FLAG_RIGIDBODY) && physics_body_id > 0) {
                destroy_physics_body(physics_body_id);
                instance_destroy();
            }
        }
    }
}

// ================================================================================================
// MODE 3: LIGHTING & ATMOSPHERE STUDIO
// ================================================================================================
if (app_mode == MODE_LIGHTING) {
    var _px = 15;
    var _py = 85;
    var _pw = 265;
    var _ph = 390;

    draw_set_color(c_black);
    draw_set_alpha(0.85);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, false);
    draw_set_alpha(1.0);
    draw_set_color(c_aqua);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, true);

    var _y_cursor = _py + 10;
    draw_set_color(c_yellow);
    draw_set_halign(fa_center);
    draw_text(_px + _pw * 0.5, _y_cursor, "LIGHTING & FOG STUDIO");
    draw_set_halign(fa_left);
    _y_cursor += 25;

    draw_set_color(c_white);
    draw_text(_px + 12, _y_cursor, "Time of Day & Atmosphere:");
    _y_cursor += 18;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Midday Sun", time_of_day_mode == 0)) {
        time_of_day_mode = 0;
        gm3d_light_set_sun(-0.35, -0.90, 0.25, 1.0, 0.96, 0.88, 1.85);
        gm3d_light_set_ambient(0.45, 0.58, 0.78, 1.15);
        gm3d_set_sky_color(0.12, 0.38, 0.85);
        gm3d_set_fog(160.0, 600.0, 0.42, 0.65, 0.88);
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Sunset Horizon", time_of_day_mode == 1, c_orange)) {
        time_of_day_mode = 1;
        gm3d_light_set_sun(0.9, -0.25, 0.35, 1.0, 0.45, 0.15, 1.2);
        gm3d_light_set_ambient(0.12, 0.08, 0.15, 0.7);
        gm3d_set_sky_color(0.45, 0.18, 0.25);
        gm3d_set_fog(30.0, 110.0, 0.45, 0.18, 0.25);
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Midnight Dark", time_of_day_mode == 2, c_navy)) {
        time_of_day_mode = 2;
        gm3d_light_set_sun(0.577, -0.707, 0.408, 0.25, 0.4, 0.75, 0.08);
        gm3d_light_set_ambient(0.02, 0.03, 0.05, 0.25);
        gm3d_set_sky_color(0.02, 0.03, 0.06);
        gm3d_set_fog(25.0, 85.0, 0.02, 0.03, 0.06);
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Dense Fog", time_of_day_mode == 3, c_ltgray)) {
        time_of_day_mode = 3;
        gm3d_light_set_sun(0.577, -0.707, 0.408, 0.6, 0.6, 0.65, 0.4);
        gm3d_light_set_ambient(0.2, 0.22, 0.25, 0.9);
        gm3d_set_sky_color(0.3, 0.32, 0.35);
        gm3d_set_fog(12.0, 45.0, 0.3, 0.32, 0.35);
    }
    _y_cursor += 38;

    draw_set_color(c_white);
    draw_text(_px + 12, _y_cursor, "Dynamic Point Lights:");
    _y_cursor += 18;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Light 1 (Amber): " + (light0_active ? "ON" : "OFF"), light0_active, c_yellow)) {
        light0_active = !light0_active;
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Light 2 (Cyan): " + (light1_active ? "ON" : "OFF"), light1_active, c_aqua)) {
        light1_active = !light1_active;
    }
    _y_cursor += 32;

    if (ui_btn(_px + 12, _y_cursor, 241, 28, "Light 3 (Magenta): " + (light2_active ? "ON" : "OFF"), light2_active, c_fuchsia)) {
        light2_active = !light2_active;
    }
    _y_cursor += 34;

    if (ui_btn(_px + 12, _y_cursor, 241, 30, "Light Rotation: " + (light_rotate_active ? "ON" : "OFF"), light_rotate_active)) {
        light_rotate_active = !light_rotate_active;
    }
}

// ================================================================================================
// MODE 4: MATERIALS & NORMAL MAPS SHOWCASE
// ================================================================================================
if (app_mode == MODE_MATERIAL) {
    var _px = 15;
    var _py = 85;
    var _pw = 280;
    var _ph = 300;

    draw_set_color(c_black);
    draw_set_alpha(0.85);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, false);
    draw_set_alpha(1.0);
    draw_set_color(c_fuchsia);
    draw_rectangle(_px, _py, _px + _pw, _py + _ph, true);

    var _y_cursor = _py + 10;
    draw_set_color(c_yellow);
    draw_set_halign(fa_center);
    draw_text(_px + _pw * 0.5, _y_cursor, "NORMAL MAP SHOWCASE");
    draw_set_halign(fa_left);
    _y_cursor += 25;

    draw_set_color(c_white);
    draw_text(_px + 12, _y_cursor, "Pedestal Exhibits:");
    _y_cursor += 18;
    draw_text(_px + 12, _y_cursor, "1. Beveled Tiles (Texture 1)");
    _y_cursor += 18;
    draw_text(_px + 12, _y_cursor, "2. Tech Circuit Grid (Texture 2)");
    _y_cursor += 18;
    draw_text(_px + 12, _y_cursor, "3. Wood Planks & Nails (Texture 3)");
    _y_cursor += 18;
    draw_text(_px + 12, _y_cursor, "4. Hazard Steel Plate (Texture 4)");
    _y_cursor += 18;
    draw_text(_px + 12, _y_cursor, "5. Faceted Emerald Crystal (OBJ)");
    _y_cursor += 28;

    if (ui_btn(_px + 12, _y_cursor, 256, 32, "Sample Rotation: " + (mat_rot_active ? "ON" : "OFF"), mat_rot_active)) {
        mat_rot_active = !mat_rot_active;
    }
    _y_cursor += 38;

    if (ui_btn(_px + 12, _y_cursor, 256, 32, "Cycle Materials", false, c_aqua)) {
        for (var _p = 0; _p < array_length(mat_pedestals); _p++) {
            var _ped_ent = mat_pedestals[_p];
            if (instance_exists(_ped_ent)) {
                _ped_ent.material_id = (_ped_ent.material_id + 1) % 6;
            }
        }
    }
}
