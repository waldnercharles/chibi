#ifndef CHIBI_RHYTHM_H
#define CHIBI_RHYTHM_H

#include "ecs/ecs.h"
#include "graph/model/node.h"

typedef struct
{
    double phase, pulse, period, duration;
} game_beat;

#define GAME_RHYTHM_NO_SCENE "Scene Rhythm requires a playing scene"
#define GAME_RHYTHM_INVALID                                                    \
    "Scene Rhythm needs one clock with positive finite BPM and Pulse Width"
struct behavior_lower_builder;
int game_rhythm_lower(struct behavior_lower_builder *builder, node *node, int pin);

/* One optional scene clock. Simulation seconds freeze while paused. */
bool game_rhythm_sample(ecs_world *world, double time, game_beat *beat);
extern node_desc NODE_Scene_Rhythm;

#endif
