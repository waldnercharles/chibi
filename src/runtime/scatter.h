#ifndef CHIBI_GAME_SCATTER_H
#define CHIBI_GAME_SCATTER_H

#include "runtime/behaviors/entities.h"
#include "runtime/materials.h"

/* The cells every GameScatter has tried this run. */
typedef struct game_scatter game_scatter;

game_scatter *game_scatter_new();
void game_scatter_free(game_scatter *scatter);
/* A new run tries every cell again. */
void game_scatter_reset(game_scatter *scatter);
/* Tries the untried cells cameras have come near, at most a bounded number
 * per call, spawning what fits. NULL on success, else why. */
const char *game_scatter_update(
    game_scatter *scatter,
    ecs_world *world,
    game_entities *entities,
    game_materials *materials,
    int run_seed
);

#endif
