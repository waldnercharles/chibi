#pragma once

#include <cute_math.h>
#include <stdbool.h>
#include <stdint.h>

/* Gameplay input held from one fixed step until the next key. A timeline of
 * keys, counted in fixed steps from a scene's start, replays play exactly
 * whatever the frame rate it was recorded at. */
typedef struct
{
    int step;
    CF_V2 movement;
    CF_V2 aim;
    bool aim_point; /* aim is a world position rather than a direction */
    bool firing;
    uint64_t controls; /* held Control bits */
} input_keyframe;

/* Whether two keys hold the same input, whatever their steps. */
static inline bool input_keyframe_same(const input_keyframe *a, const input_keyframe *b)
{
    return a->movement.x == b->movement.x && a->movement.y == b->movement.y &&
           a->aim.x == b->aim.x && a->aim.y == b->aim.y &&
           a->aim_point == b->aim_point && a->firing == b->firing &&
           a->controls == b->controls;
}
