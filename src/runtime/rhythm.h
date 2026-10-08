#ifndef CHIBI_RHYTHM_H
#define CHIBI_RHYTHM_H

#include "ecs/ecs.h"
#include "execution/host.h"

typedef struct
{
    double phase, pulse, period, duration;
} game_beat;

#define GAME_RHYTHM_NO_SCENE "Scene Rhythm requires a playing scene"
#define GAME_RHYTHM_INVALID                                                    \
    "Scene Rhythm needs one clock with positive finite BPM and Pulse Width"

/* One optional scene clock. Simulation seconds freeze while paused. */
bool game_rhythm_sample(ecs_world *world, double time, game_beat *beat);
extern const node_host_call GAME_RHYTHM_CALL;
extern const node_access GAME_RHYTHM_ACCESS;

#endif
