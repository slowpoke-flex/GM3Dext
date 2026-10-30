varying vec2 v_vTexcoord;
varying vec4 v_vColour;
varying vec2 v_vWorldPos;

// Uniforms supplied from GML
uniform sampler2D u_normal_map;
uniform vec3 u_light_pos;       // (world_x, world_y, height_z)
uniform vec3 u_light_color;     // (r, g, b) normalized 0.0 .. 1.0
uniform float u_light_radius;   // pixels
uniform vec3 u_ambient_color;   // (r, g, b) normalized 0.0 .. 1.0

void main()
{
    vec4 diffuse = v_vColour * texture2D(gm_BaseTexture, v_vTexcoord);
    if (diffuse.a < 0.05) discard;

    vec4 normal_sample = texture2D(u_normal_map, v_vTexcoord);
    
    // Unpack tangent-space normal from [0, 1] to [-1, 1]
    vec3 N = normalize(normal_sample.rgb * 2.0 - 1.0);

    // Direction vector from pixel surface to 3D light source
    vec3 L = vec3(u_light_pos.xy - v_vWorldPos, u_light_pos.z);
    float dist = length(L);
    L = normalize(L);

    // Smooth quadratic distance attenuation
    float atten = clamp(1.0 - (dist / u_light_radius), 0.0, 1.0);
    atten = atten * atten;

    // 1. Lambertian Diffuse (N . L)
    float NdotL = max(0.0, dot(N, L));

    // 2. Blinn-Phong Specular Highlight (metallic / shiny glints on armor and ice)
    vec3 V = vec3(0.0, 0.0, 1.0); // View direction in top-down camera
    vec3 H = normalize(L + V);
    float NdotH = max(0.0, dot(N, H));
    float spec = pow(NdotH, 24.0) * 0.65;

    // Composite illumination
    vec3 light_acc = u_ambient_color + (u_light_color * NdotL * atten * 1.5) + (u_light_color * spec * atten);

    gl_FragColor = vec4(diffuse.rgb * light_acc, diffuse.a);
}
