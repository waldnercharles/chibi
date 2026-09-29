#ifndef CHIBI_GAME_RUN_H
#define CHIBI_GAME_RUN_H

#include "graph/model/registry.h"

#include <stdbool.h>

/* What a restart request asks the host for. */
typedef enum
{
    GAME_RUN_NO_RESTART,
    GAME_RUN_RESTART_RUN,   /* the scene again on a new seed */
    GAME_RUN_RESTART_SCENE, /* the scene again on the same seed */
} game_run_restart;

/* Run controls behaviors reach through the "game.run" service. The engine
 * chooses the seed and services the requests. */
typedef struct game_run
{
    int seed;
    game_run_restart restart;
} game_run;

extern const registry_pack GAME_RUN_PACK;

#endif
