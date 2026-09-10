#ifndef CHIBI_RENDER_ARENA_EMITTERS_H
#define CHIBI_RENDER_ARENA_EMITTERS_H

#include "ecs/ecs.h"
#include "render/particles.h"

struct engine;
typedef struct arena_emitters arena_emitters;

arena_emitters *render_arena_emitters_new();
void render_arena_emitters_free(arena_emitters *emitters);
void render_arena_emitters_reset(arena_emitters *emitters);
/* Emission and aging use interpolated simulation time alongside the rendered
 * entity pose. Without a runtime, time supplies that presentation clock. */
bool render_arena_emitters_collect(
    arena_emitters *emitters,
    ecs_world *world,
    const struct engine *runtime,
    double time,
    char *error,
    size_t error_size
);
const game_particles *render_arena_emitters_particles(const arena_emitters *emitters);
const game_particle *render_arena_emitters_heads(const arena_emitters *emitters, int *count);

#endif
