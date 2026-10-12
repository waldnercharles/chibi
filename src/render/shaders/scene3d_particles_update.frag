layout(location = 0) out vec4 out_position;
layout(location = 1) out vec4 out_velocity;
layout(set = 2, binding = 0) uniform sampler2D particle_positions;
layout(set = 2, binding = 1) uniform sampler2D particle_velocities;
layout(set = 3, binding = 0) uniform uniform_block {
    vec4 particle_clock;
    vec4 particle_step;
    vec4 particle_rhythm;
    vec4 particle_parameters[32];
};

#define SG_SCENE_INFLUENCE 1
float sg_scene_beat_phase() { return particle_rhythm.x; }
float sg_scene_beat_pulse() { return particle_rhythm.y; }
vec3 sg_scene_influence(vec3 position) { return vec3(0.0); }

// SG_PARTICLE_PROGRAM is replaced by the prepared shader function.

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int index = pixel.y * int(particle_step.z) + pixel.x;
    out_position = vec4(0.0);
    out_velocity = vec4(0.0);
    if (index >= int(particle_clock.y)) return;
    bool reset = particle_step.y > 0.0;
    ChibiParticleState state;
    state.position = vec3(0.0);
    state.velocity = vec3(0.0);
    state.age = 0.0;
    if (!reset) {
        vec4 previous = texelFetch(particle_positions, pixel, 0);
        state.position = previous.xyz;
        state.age = previous.w;
        state.velocity = texelFetch(particle_velocities, pixel, 0).xyz;
    }
    SGSample context;
    context.time = particle_clock.x;
    vec4 parameters[32];
    for (int slot = 0; slot < 32; ++slot) parameters[slot] = particle_parameters[slot];
    parameters[0] = vec4(float(index));
    parameters[1] = vec4(particle_clock.y);
    parameters[2] = vec4(particle_clock.z);
    state = sg_particle_update(context, parameters, state, particle_step.x, reset);
    out_position = vec4(state.position, state.age);
    out_velocity = vec4(state.velocity, 0.0);
}
