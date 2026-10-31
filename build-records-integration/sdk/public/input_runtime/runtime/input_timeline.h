#pragma once

#include "runtime/input_action.h"

#include <cute_math.h>
#include <cute_math3d.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t buttons;
    bool connected;
} input_controller_state;

typedef struct
{
    CF_V3 origin, direction;
    bool active;
} input_pointer_ray;

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
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
    input_pointer_ray pointer;
} input_keyframe;

/* Whether two keys hold the same input, whatever their steps. */
static inline bool input_keyframe_same(const input_keyframe *a, const input_keyframe *b)
{
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        if (a->controllers[i].buttons != b->controllers[i].buttons ||
            a->controllers[i].connected != b->controllers[i].connected) {
            return false;
        }
    }
    return a->movement.x == b->movement.x && a->movement.y == b->movement.y &&
           a->aim.x == b->aim.x && a->aim.y == b->aim.y &&
           a->aim_point == b->aim_point && a->firing == b->firing &&
           a->controls == b->controls && a->pointer.active == b->pointer.active &&
           a->pointer.origin.x == b->pointer.origin.x &&
           a->pointer.origin.y == b->pointer.origin.y &&
           a->pointer.origin.z == b->pointer.origin.z &&
           a->pointer.direction.x == b->pointer.direction.x &&
           a->pointer.direction.y == b->pointer.direction.y &&
           a->pointer.direction.z == b->pointer.direction.z;
}
