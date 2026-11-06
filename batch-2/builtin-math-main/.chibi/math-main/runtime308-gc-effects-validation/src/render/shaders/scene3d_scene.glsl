#define SG_SCENE_INFLUENCE 1

layout(set = 3, binding = 0) uniform uniform_block {
    // Where surfaces are seen from: the camera, or the light a shadow pass
    // draws for. A point when w is 1; when w is 0, the direction toward a
    // distant viewer such as the sun.
    vec4 scene_eye;
    vec4 scene_time;
    vec4 scene_sun_direction;
    vec4 scene_sun_color;
    vec4 scene_ambient_sky;
    vec4 scene_ambient_ground;
    vec4 scene_light_count;
    vec4 scene_light_positions[32];
    vec4 scene_light_colors[32];
    vec4 scene_light_cones[32];
    vec4 scene_influence_count;
    vec4 scene_influence_positions[64];
    vec4 scene_influence_colors[64];
    mat4 scene_shadow_matrices[4];
    vec4 scene_shadow;
    vec4 scene_shadow_texels;
    vec4 scene_shadow_ranges;
    vec4 scene_shadow_soft;
    vec4 scene_light_shadow;
    vec4 scene_light_shadow_sizes;
};

vec3 sg_scene_influence(vec3 position)
{
    vec3 activity = vec3(0);
    for (int i = 0; i < int(scene_influence_count.x); i++) {
        vec4 source = scene_influence_positions[i];
        vec2 delta = (position.xz - source.xy) / source.z;
        float distance_squared = dot(delta, delta);
        if (distance_squared >= 1.0) continue;
        float fade = 1.0 - distance_squared;
        if (source.w > 0.0) {
            float middle = (1.0 + source.w) * 0.5;
            float half_width = (1.0 - source.w) * 0.5;
            fade = max(0.0, 1.0 - abs(sqrt(distance_squared) - middle) / half_width);
        }
        activity = max(activity, scene_influence_colors[i].rgb * fade * fade);
    }
    return activity;
}
