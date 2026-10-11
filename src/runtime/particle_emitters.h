#ifndef CHIBI_PARTICLE_EMITTERS_H
#define CHIBI_PARTICLE_EMITTERS_H

#include "ecs/ecs.h"
#include "runtime/particles.h"

struct engine;
typedef struct particle_emitters particle_emitters;
struct materials;

particle_emitters *particle_emitters_new(particles *destination);
void particle_emitters_bind_materials(particle_emitters *emitters, struct materials *materials);
void particle_emitters_free(particle_emitters *emitters);
void particle_emitters_reset(particle_emitters *emitters);
/* Emission and aging use interpolated simulation time alongside the rendered
 * entity pose. Without a runtime, time supplies that presentation clock. */
bool particle_emitters_collect(
    particle_emitters *emitters,
    ecs_world *world,
    const struct engine *runtime,
    double time,
    char *error,
    size_t error_size
);
const particles *particle_emitters_particles(const particle_emitters *emitters);
const particle *particle_emitters_heads(const particle_emitters *emitters, int *count);

#endif
