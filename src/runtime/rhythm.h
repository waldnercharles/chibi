#ifndef CHIBI_RHYTHM_H
#define CHIBI_RHYTHM_H

#include "ecs/ecs.h"
#include "graph/model/node.h"

typedef struct
{
    double phase, pulse, period, duration;
} game_beat;

/* One optional scene clock. Simulation seconds freeze while paused. */
bool game_rhythm_sample(ecs_world *world, double time, game_beat *beat);
extern node_desc NODE_Scene_Rhythm;

#endif
