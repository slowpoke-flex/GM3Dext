/// @function draw_sprite_pbr(sprite, normal_sprite, subimg, x, y, l_x, l_y, l_z, l_col_r, l_col_g, l_col_b, l_radius)
/// @description Draws a 2D sprite with real-time 3D lighting, specular highlights, and surface relief.
/// @param sprite         The standard diffuse sprite (e.g. spr_h_paladin_idle)
/// @param normal_sprite  The generated normal-map sprite (e.g. spr_h_paladin_idle_normal)
/// @param subimg         Animation frame index (image_index)
/// @param x              World X
/// @param y              World Y
/// @param l_x            Light World X (e.g. mouse_x or fireball.x)
/// @param l_y            Light World Y (e.g. mouse_y or fireball.y)
/// @param l_z            Light Height Z above ground (e.g. 60 .. 120)
/// @param l_col_r        Light Color Red (0.0 .. 1.0)
/// @param l_col_g        Light Color Green (0.0 .. 1.0)
/// @param l_col_b        Light Color Blue (0.0 .. 1.0)
/// @param l_radius       Light Radius in pixels (e.g. 350 .. 600)

function draw_sprite_pbr(spr, spr_norm, subimg, _x, _y, lx, ly, lz, lr, lg, lb, lrad) {
    shader_set(sh_moba_pbr);
    
    // Pass normal texture to shader sampler
    var tex_norm = sprite_get_texture(spr_norm, subimg);
    var u_norm = shader_get_sampler_index(sh_moba_pbr, "u_normal_map");
    texture_set_stage(u_norm, tex_norm);
    
    // Pass light uniforms
    var u_lpos = shader_get_uniform(sh_moba_pbr, "u_light_pos");
    shader_set_uniform_f(u_lpos, lx, ly, lz);
    
    var u_lcol = shader_get_uniform(sh_moba_pbr, "u_light_color");
    shader_set_uniform_f(u_lcol, lr, lg, lb);
    
    var u_lrad = shader_get_uniform(sh_moba_pbr, "u_light_radius");
    shader_set_uniform_f(u_lrad, lrad);
    
    var u_amb = shader_get_uniform(sh_moba_pbr, "u_ambient_color");
    shader_set_uniform_f(u_amb, 0.28, 0.32, 0.40); // Natural ambient daylight/dusk
    
    // Draw the diffuse sprite through the PBR shader
    draw_sprite(spr, subimg, _x, _y);
    
    shader_reset();
}
