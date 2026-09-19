#ifndef CHIBI_PARTICLE_EMITTERS_H
#define CHIBI_PARTICLE_EMITTERS_H

#include "ecs/ecs.h"
#include "render/particles.h"

struct engine;
typedef struct game_particle_emitters game_particle_emitters;

game_particle_emitters *game_particle_emitters_new(game_particles *destination);
void game_particle_emitters_free(game_particle_emitters *emitters);
void game_particle_emitters_reset(game_particle_emitters *emitters);
/* Emission and aging use interpolated simulation time alongside the rendered
 * entity pose. Without a runtime, time supplies that presentation clock. */
bool game_particle_emitters_collect(
    game_particle_emitters *emitters,
    ecs_world *world,
    const struct engine *runtime,
    double time,
    char *error,
    size_t error_size
);
const game_particles *game_particle_emitters_particles(const game_particle_emitters *emitters);
const game_particle *game_particle_emitters_heads(const game_particle_emitters *emitters, int *count);

#endif
