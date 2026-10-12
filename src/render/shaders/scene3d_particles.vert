layout(location = 0) in vec3 in_pos;
layout(location = 1) in float in_instance_offset;
layout(location = 2) in vec3 in_normal;
layout(location = 0) out vec4 v_color;
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 u_view_projection;
    mat4 particle_frame;
    mat4 particle_normal_frame;
    vec4 particle_dimensions;
    vec4 particle_clock;
    vec4 particle_rhythm;
    vec4 particle_step;
    vec4 particle_parameters[32];
    vec4 scene_sun_direction;
    vec4 scene_sun_color;
    vec4 scene_ambient_sky;
    vec4 scene_ambient_ground;
    vec4 scene_light_count;
    vec4 scene_light_positions[32];
    vec4 scene_light_colors[32];
    vec4 scene_light_cones[32];
};

#define SG_SCENE_INFLUENCE 1
float sg_scene_beat_phase() { return particle_rhythm.x; }
float sg_scene_beat_pulse() { return particle_rhythm.y; }
vec3 sg_scene_influence(vec3 position) { return vec3(0.0); }

#ifdef SG_PARTICLE_STATEFUL
layout(set = 0, binding = 0) uniform sampler2D particle_positions;
layout(set = 0, binding = 1) uniform sampler2D particle_velocities;
void sg_particle_state(float index, out vec3 position, out vec3 velocity, out float age)
{
    int width = int(particle_step.z);
    ivec2 pixel = ivec2(int(index) % width, int(index) / width);
    vec4 value = texelFetch(particle_positions, pixel, 0);
    position = value.xyz;
    age = value.w;
    velocity = texelFetch(particle_velocities, pixel, 0).xyz;
}
#endif

// SG_PARTICLE_PROGRAM is replaced by the prepared shader function.

#ifndef SG_PARTICLE_OUTPUT_COUNT
#define SG_PARTICLE_OUTPUT_COUNT 3
#endif

mat3 particle_rotation(vec3 angle)
{
    vec3 c = cos(angle), s = sin(angle);
    mat3 x = mat3(1, 0, 0, 0, c.x, s.x, 0, -s.x, c.x);
    mat3 y = mat3(c.y, 0, -s.y, 0, 1, 0, s.y, 0, c.y);
    mat3 z = mat3(c.z, s.z, 0, -s.z, c.z, 0, 0, 0, 1);
    return z * y * x;
}

// Small instanced solids receive the scene's direct and hemisphere light.
// They do not sample shadow maps or screen-space occlusion.
vec3 particle_light(vec3 world, vec3 normal)
{
    vec3 radiance = mix(scene_ambient_ground.rgb, scene_ambient_sky.rgb,
                        clamp(normal.y * 0.5 + 0.5, 0.0, 1.0));
    radiance += scene_sun_color.rgb * max(dot(normal, scene_sun_direction.xyz), 0.0);
    for (int i = 0; i < int(scene_light_count.x); ++i) {
        vec3 delta = scene_light_positions[i].xyz - world;
        float radius = scene_light_positions[i].w;
        float squared = dot(delta, delta);
        if (squared >= radius * radius) continue;
        vec3 light = delta * inversesqrt(max(squared, 0.000001));
        float fade = 1.0 - squared / (radius * radius);
        float edge = clamp(dot(-light, scene_light_cones[i].xyz) + scene_light_cones[i].w, 0.0, 1.0);
        radiance += scene_light_colors[i].rgb * scene_light_colors[i].w *
                    fade * fade * edge * edge * max(dot(normal, light), 0.0);
    }
    return radiance;
}

void main()
{
    SGSample context;
    context.position = vec2(0.0);
    context.uv = vec2(0.0);
    context.object_position = vec3(0.0);
    context.part_position = vec3(0.0);
    context.world_position = vec3(0.0);
    context.normal = vec3(0.0);
    context.view_direction = vec3(0.0);
    for (int axis = 0; axis < 3; ++axis) {
        context.object_from_world[axis] = vec3(0.0);
        context.part_from_world[axis] = vec3(0.0);
    }
    context.uv_from_world[0] = vec3(0.0);
    context.uv_from_world[1] = vec3(0.0);
    context.time = particle_clock.x;
    vec4 parameters[32];
    for (int slot = 0; slot < 32; ++slot) parameters[slot] = particle_parameters[slot];
    parameters[0] = vec4(float(gl_InstanceIndex));
    parameters[1] = vec4(particle_clock.y);
    parameters[2] = vec4(particle_clock.z);
    vec4 result[9];
    sg_particle(context, parameters, result);
    float size = max(result[1].x, 0.0);
    vec3 scale = particle_dimensions.xyz;
    mat3 rotation = mat3(1.0);
#if SG_PARTICLE_OUTPUT_COUNT >= 4
    scale *= clamp(result[3].xyz, vec3(0.0001), vec3(10000.0));
#endif
#if SG_PARTICLE_OUTPUT_COUNT >= 5
    rotation = particle_rotation(result[4].xyz);
#endif
    vec3 local = result[0].xyz + rotation * (in_pos * scale * size) + vec3(in_instance_offset);
    vec4 world = particle_frame * vec4(local, 1.0);
    v_color = result[2];
#if SG_PARTICLE_OUTPUT_COUNT >= 6
    float lighting = clamp(result[5].x, 0.0, 1.0);
    if (lighting > 0.0) {
        vec3 normal = normalize((particle_normal_frame * vec4(rotation * (in_normal / scale), 0.0)).xyz);
        v_color.rgb *= mix(vec3(1.0), particle_light(world.xyz, normal), lighting);
    }
#endif
    v_color.rgb *= mix(clamp(v_color.a, 0.0, 1.0), 1.0, particle_clock.w);
    gl_Position = u_view_projection * world;
}
