layout(location = 0) in vec3 in_pos;
layout(location = 1) in float in_instance_offset;
layout(location = 0) out vec4 v_color;
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 u_view_projection;
    mat4 particle_frame;
    vec4 particle_dimensions;
    vec4 particle_clock;
    vec4 particle_parameters[32];
};

#define SG_SCENE_INFLUENCE 1
vec3 sg_scene_influence(vec3 position) { return vec3(0.0); }

// SG_PARTICLE_PROGRAM is replaced by the prepared shader function.

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
    vec3 local = result[0].xyz + in_pos * particle_dimensions.xyz * size + vec3(in_instance_offset);
    v_color = result[2];
    v_color.rgb *= mix(clamp(v_color.a, 0.0, 1.0), 1.0, particle_clock.w);
    gl_Position = u_view_projection * particle_frame * vec4(local, 1.0);
}
