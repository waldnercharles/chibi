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
    float time_scale;       /* Set Time Scale; 1 runs in real time */
    float hit_stop_scale;   /* the time scale while hit-stop lasts */
    float hit_stop_seconds; /* real seconds of hit-stop left */
} game_run;

/* A new run on seed: no request, real-time speed. */
void game_run_start(game_run *run, int seed);
/* Simulation seconds per real second: hit-stop's while it lasts, else the
 * time scale. */
float game_run_time_scale(const game_run *run);
/* Spends real seconds of hit-stop. */
void game_run_advance(game_run *run, float real_seconds);

extern const registry_pack GAME_RUN_PACK;

#endif
