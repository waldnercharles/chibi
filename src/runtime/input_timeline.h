#pragma once

#include "runtime/components.h"
#include "runtime/input_action.h"

#include <cute_math.h>
#include <cute_math3d.h>
#include <math.h>
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

/* Portable finite normalized gameplay key validation used by bounded hosts
 * before admitting a restart/replay. Does not mutate an input owner. */
static inline bool input_keyframe_valid(const input_keyframe *k)
{
    uint64_t controls = ((UINT64_C(1) << CONTROL_COUNT) - 1) & ~UINT64_C(1);
    uint32_t buttons = ((UINT32_C(1) << PAD_COUNT) - 1) & ~UINT32_C(1);
    if (!k || k->step < 0 || (k->controls & ~controls) || !isfinite(k->movement.x) ||
        !isfinite(k->movement.y) || fabsf(k->movement.x) > 1 ||
        fabsf(k->movement.y) > 1 || !isfinite(k->aim.x) || !isfinite(k->aim.y) ||
        !isfinite(k->pointer.origin.x) || !isfinite(k->pointer.origin.y) ||
        !isfinite(k->pointer.origin.z) || !isfinite(k->pointer.direction.x) ||
        !isfinite(k->pointer.direction.y) || !isfinite(k->pointer.direction.z)) {
        return false;
    }
    float length = cf_dot_v3(k->pointer.direction, k->pointer.direction);
    if (k->pointer.active && (!isfinite(length) || length <= 0)) {
        return false;
    }
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        if ((k->controllers[i].buttons & ~buttons) ||
            (!k->controllers[i].connected && k->controllers[i].buttons)) {
            return false;
        }
    }
    return true;
}

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
