/// @desc Loads the GM3D Studio meadow scene shipped next to the game.
gm3d_init(5000);

scene = gm3d_load_scene_json("scene_gm3d.json");
if (is_undefined(scene)) {
    show_debug_message("[obj_meadow] scene_gm3d.json not found - was the export copied into datafiles?");
}

// The character is the only scene entity with a model mesh; primitives live below 100.
player = noone;
with (obj_3d_entity) {
    if (mesh_id >= 100) other.player = id;   // `other` = obj_meadow; a plain assignment would write to the entity
}

// Camera modes: 0 = the fixed Studio shot, 1 = third person behind the character.
// Press "1" to take control, like the showcase's camera toggle.
follow = false;
cam_yaw = 200;      // degrees; the scene spawns the character facing this way
cam_pitch = 12;
cam_dist = 6.0;
mouse_prev_x = window_mouse_get_x();
mouse_prev_y = window_mouse_get_y();

// Clip indices in UAL1_Standard.glb: 9 idle, 42 walk, 13 jog.
anim_clip = 9;
anim_running = false;
was_moving = false;

// The fixed shot the scene was authored for.
cam_px = -4.0;  cam_py = 12.62; cam_pz = -11.86;
cam_tx = 4.0;   cam_ty = 16.34; cam_tz = 2.0;
