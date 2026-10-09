#define SG_SCENE_INFLUENCE 1

#ifndef SG_RAMP_COUNT
#define SG_RAMP_COUNT 512
#endif

layout(set = 3, binding = 0) uniform uniform_block {
    vec4 scene_camera_time;
    vec4 scene_rhythm;
    vec4 scene_sun_direction;
    vec4 scene_sun_color;
    vec4 scene_ambient_sky;
    vec4 scene_ambient_ground;
    vec4 scene_light_count;
    vec4 scene_light_positions[32];
    vec4 scene_light_colors[32];
    vec4 scene_influence_count;
    vec4 scene_influence_positions[64];
    vec4 scene_influence_colors[64];
    mat4 scene_shadow_matrix;
    vec4 scene_shadow;
    vec4 sg_ramp_data[SG_RAMP_COUNT];
};

float sg_scene_beat_phase() { return scene_rhythm.x; }
float sg_scene_beat_pulse() { return scene_rhythm.y; }

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
