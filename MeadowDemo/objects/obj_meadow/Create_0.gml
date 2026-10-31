/// @desc Loads the GM3D Studio meadow scene shipped next to the game.
gm3d_init(5000);

scene = gm3d_load_scene_json("scene_gm3d.json");
if (is_undefined(scene)) {
    show_debug_message("[obj_meadow] scene_gm3d.json not found - was the export copied into datafiles?");
}

// The character is the only scene entity with a model mesh; primitives live below 100.
// `other` matters: a plain assignment inside `with` writes the variable on the entity.
player = noone;
with (obj_3d_entity) {
    if (mesh_id >= 100) other.player = id;
}

// Camera modes: 0 = the fixed Studio shot, 1 = third person behind the character.
follow = false;
cam_yaw = 200;          // degrees, direction the camera looks along
cam_pitch = 14;         // degrees, positive = camera above the character looking down
cam_dist = 6.5;         // metres, wheel zooms 2..14
mouse_free = true;

// Character facing, smoothed: WoW turns the model over a few frames instead of snapping.
char_yaw = cam_yaw;
move_speed = 0;         // current horizontal speed, eased towards the input target
vy = 0;                 // vertical velocity while jumping
grounded = true;

// Clip indices in UAL1_Standard.glb.
CLIP_IDLE = 9;
CLIP_WALK = 42;
CLIP_JOG = 13;
CLIP_AIR = 15;
anim_state = "";

// The fixed shot the scene was authored for.
cam_px = -4.0;  cam_py = 12.62; cam_pz = -11.86;
cam_tx = 4.0;   cam_ty = 16.34; cam_tz = 2.0;
