#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The capabilities that read an action, as input.<Action>.held and
 * input.<Action>.pressed read them. */
#define INPUT_ACTION_HELD "Action Held"
#define INPUT_ACTION_PRESSED "Action Pressed"
/* The script types `input` and `input.<Action>` have. Both hold an action
 * name, so a module needs nothing from them at runtime. */
#define INPUT_MAP_TYPE "InputMap"
#define INPUT_ACTION_TYPE "InputAction"
/* The most actions an input map holds. */
#define INPUT_ACTION_MAX 256
#define INPUT_CONTROLLER_MAX 4

/* One action of a project's input map. Any of its controls holds it, and a
 * firing action also holds the Aim of Input owners. */
typedef struct
{
    const char *name;
    uint64_t controls; /* one bit per Control */
    bool firing;
    int controller;   /* zero-based controller slot for buttons */
    uint32_t buttons; /* one bit per PadControl, ORed with controls */
} input_action;
