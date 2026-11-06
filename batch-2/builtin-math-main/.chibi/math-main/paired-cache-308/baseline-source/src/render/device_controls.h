#ifndef CHIBI_RENDER_DEVICE_CONTROLS_H
#define CHIBI_RENDER_DEVICE_CONTROLS_H

#include "runtime/components.h"
#include "runtime/input_timeline.h"

#include <cute_input.h>

/* Control sources read from the desktop's keyboard, mouse and first
 * controller, shared by every host so play feels the same everywhere. */

/* The control a key press latches, so a press released within one frame still
 * counts, or CONTROL_NONE. */
Control control_for_key(CF_KeyButton key);
bool control_down(Control control);
/* Every held control as Control bits. */
uint64_t controls_down();
/* A controller slot's connection and action buttons, independent of keyboard.
 * Slots follow the device host: new pads occupy the first vacant slot. */
input_controller_state controller_state(int controller);
/* The left stick, else WASD or the arrow keys; at most length 1. */
CF_V2 controls_movement();
/* The right stick's unit direction, or zero inside its dead zone. */
CF_V2 controls_aim();
/* A stick deflection in [-1, 1] with its radial dead zone removed: zero
 * inside it, then rescaled so the edge reads 0 and full deflection reads 1. */
CF_V2 controls_dead_zone(CF_V2 stick);

#endif
