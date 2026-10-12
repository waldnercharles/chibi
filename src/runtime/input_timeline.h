#pragma once

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

enum
{
    INPUT_MOTION_DISABLED,
    INPUT_MOTION_WAITING,
    INPUT_MOTION_READY,
    INPUT_MOTION_UNAVAILABLE,
    INPUT_MOTION_DENIED,
};

typedef struct
{
    float width, height; /* Viewport dimensions in CSS/logical pixels. */
    bool touch, active;  /* Touch capability and foreground state. */
    int motion_status;
    CF_V2 tilt; /* Screen-relative calibrated degrees, zero without ready motion. */
    bool haptics;
} input_device_state;

typedef struct
{
    bool motion, disable_motion, recenter;
    int vibration_ms; /* One pulse, 1..1000 ms; zero means no pending pulse. */
} input_device_requests;

static inline bool input_device_state_valid(input_device_state state)
{
    return isfinite(state.width) && isfinite(state.height) && state.width >= 0 &&
           state.height >= 0 && isfinite(state.tilt.x) && isfinite(state.tilt.y) &&
           state.motion_status >= INPUT_MOTION_DISABLED &&
           state.motion_status <= INPUT_MOTION_DENIED;
}

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
    input_device_state device;
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
           a->device.width == b->device.width &&
           a->device.height == b->device.height &&
           a->device.touch == b->device.touch &&
           a->device.active == b->device.active &&
           a->device.motion_status == b->device.motion_status &&
           a->device.tilt.x == b->device.tilt.x &&
           a->device.tilt.y == b->device.tilt.y &&
           a->device.haptics == b->device.haptics &&
           a->pointer.origin.x == b->pointer.origin.x &&
           a->pointer.origin.y == b->pointer.origin.y &&
           a->pointer.origin.z == b->pointer.origin.z &&
           a->pointer.direction.x == b->pointer.direction.x &&
           a->pointer.direction.y == b->pointer.direction.y &&
           a->pointer.direction.z == b->pointer.direction.z;
}
