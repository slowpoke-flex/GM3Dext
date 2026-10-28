/// @description Clean up GM3D Engine resources
if (variable_instance_exists(id, "proc_sound_ids")) {
    for (var _i = 0; _i < array_length(proc_sound_ids); _i++) {
        audio_free_buffer_sound(proc_sound_ids[_i]);
    }
}
if (variable_instance_exists(id, "proc_sound_buffers")) {
    for (var _j = 0; _j < array_length(proc_sound_buffers); _j++) {
        if (buffer_exists(proc_sound_buffers[_j])) {
            buffer_delete(proc_sound_buffers[_j]);
        }
    }
}
gm3d_cleanup();
