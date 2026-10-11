#ifndef CHIBI_RHYTHM_H
#define CHIBI_RHYTHM_H

#include "ecs/ecs.h"
#include "execution/host.h"

typedef struct
{
    double phase, pulse, period, duration;
} beat;

#define RHYTHM_NO_SCENE "Scene Rhythm requires a playing scene"
#define RHYTHM_INVALID                                                         \
    "Scene Rhythm needs one clock with positive finite BPM and Pulse Width"

/* One optional scene clock. Simulation seconds freeze while paused. */
bool rhythm_sample(ecs_world *world, double time, beat *beat);
extern const node_host_call RHYTHM_CALL;
extern const node_access RHYTHM_ACCESS;

#endif
