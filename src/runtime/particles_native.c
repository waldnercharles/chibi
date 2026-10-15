#include "runtime/particles_native.h"

#include "runtime/particles.h"

#include <cute_math.h>

static const char missing_particles[] = "Emit Particles requires a game world";
static const char
    missing_directed[] = "Emit Directed Particles requires a game world";

static const char *burst_call(void *service, const pin_value *values, pin_value *)
{
    particles *particles = service;
    if (!particles) {
        return missing_particles;
    }
    if (!values[0].ptr) {
        return "Emit Particles requires a position";
    }
    const CF_V2 *position = values[0].ptr;
    particle_burst burst = {
        .position = { position->x, position->y },
        .color = values[1].u,
        .count = values[2].i,
        .speed = values[3].f,
        .lifetime = values[4].f,
    };
    return particles_emit(particles, &burst) < 0
             ? "Emit Particles requires finite position, nonnegative count and "
               "speed, and positive lifetime"
             : NULL;
}
const node_host_call PARTICLES_BURST_CALL = { .service = "game.particles",
                                              .invoke = burst_call,
                                              .missing_service = missing_particles };
static const char *directed_call(void *service, const pin_value *v, pin_value *)
{
    particles *particles = service;
    if (!particles) {
        return missing_directed;
    }
    if (!v[0].ptr || !v[1].ptr) {
        return "Position and Direction are required";
    }
    const CF_V2 *position = v[0].ptr, *direction = v[1].ptr;
    particle_spray spray = {
        .position = { position->x, position->y },
        .direction = { direction->x, direction->y },
        .count = v[2].i,
        .color = v[3].u,
        .end_color = v[4].u,
        .speed_min = v[5].f,
        .speed_max = v[6].f,
        .lifetime_min = v[7].f,
        .lifetime_max = v[8].f,
        .size_min = v[9].f,
        .size_max = v[10].f,
        .spread = v[11].f,
        .drag_min = v[12].f,
        .drag_max = v[13].f,
        .emission_min = v[14].f,
        .emission_max = v[15].f,
        .size_exponent = v[16].f,
        .elevation = v[17].f,
        .shape = v[18].i,
        .antialias = 1.5f,
    };
    if (particles_emit_directed(particles, &spray) < 0) {
        return "Directed particle ranges and direction must be finite and "
               "valid";
    }
    return NULL;
}

const node_host_call PARTICLES_DIRECTED_CALL = { .service = "game.particles",
                                                 .invoke = directed_call,
                                                 .missing_service = missing_directed };

const node_access PARTICLES_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
