#ifndef CHIBI_GAME_MEDIUM_H
#define CHIBI_GAME_MEDIUM_H

#include "ecs/ecs.h"
#include "runtime/components.h"

typedef struct game_medium game_medium;
typedef struct
{
    int cells;
    float mass, minimum, maximum;
    uint64_t hash;
} game_medium_state;

game_medium *game_medium_new(ecs_world *world);
void game_medium_free(game_medium *medium);
void game_medium_reset(game_medium *medium);
const char *game_medium_update(game_medium *medium, float seconds);
/* Borrowed row-major density in game Y-down coordinates. NULL when disabled. */
const float *game_medium_cells(const game_medium *medium, GameMedium *settings);
game_medium_state game_medium_inspect(const game_medium *medium);

#endif
