#ifndef CHIBI_INPUT_DEFINITION_H
#define CHIBI_INPUT_DEFINITION_H

#include "runtime/input_action.h"
#include "runtime/native_definitions.h"

#define INPUT_PORT(X, NAME, STORAGE, TYPE_NAME)                                \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      TYPE_NAME,                                                               \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = STORAGE, .type_name = TYPE_NAME }))
#define INPUT_ACTION_INPUTS(X) INPUT_PORT(X, "Action", PIN_STR, "const char *")
#define INPUT_HELD_OUTPUTS(X) INPUT_PORT(X, "Held", PIN_BOOL, "bool")
#define INPUT_PRESSED_OUTPUTS(X) INPUT_PORT(X, "Pressed", PIN_BOOL, "bool")
/* X(KEY, NAME, OUTPUTS). Scripts read them as input.<Action>.held and
 * input.<Action>.pressed, never by name. */
#define INPUT_OPERATIONS(X)                                                    \
    X(HELD, INPUT_ACTION_HELD, INPUT_HELD_OUTPUTS)                             \
    X(PRESSED, INPUT_ACTION_PRESSED, INPUT_PRESSED_OUTPUTS)

#define INPUT_HELD_PRESENTATION                                                \
    .category = "Input", .keywords = "action button key down hold",            \
    .description = "True while any source bound to Action is down."
#define INPUT_PRESSED_PRESENTATION                                             \
    .category = "Input", .keywords = "action button key tap just pressed",     \
    .description = "True for the one fixed step after any source bound to "    \
                   "Action went down, even if it was released again before "   \
                   "the step."

#define INPUT_CONTROLLER_CONNECTED_NAME "Controller Connected"
#define INPUT_CONTROLLER_CONNECTED_INPUTS(X)                                   \
    INPUT_PORT(X, "Controller", PIN_I32, "int")
#define INPUT_CONTROLLER_CONNECTED_OUTPUTS(X)                                  \
    INPUT_PORT(X, "Connected", PIN_BOOL, "bool")
#define INPUT_CONTROLLER_CONNECTED_PRESENTATION                                \
    .category = "Input",                                                       \
    .keywords = "controller gamepad disconnect rejoin", .description = "True while the zero-based controller slot is connected, from 0 to 3."

#define INPUT_POINTER_RAY_NAME "Get Pointer Ray"
#define INPUT_POINTER_RAY_OUTPUTS(X)                                           \
    INPUT_PORT(X, "Active", PIN_BOOL, "bool")                                  \
    INPUT_PORT(X, "Origin", PIN_STRUCT, "vec3")                                \
    INPUT_PORT(X, "Direction", PIN_STRUCT, "vec3")
#define INPUT_POINTER_RAY_PRESENTATION                                         \
    .category = "Input", .keywords = "mouse cursor camera pick 3d",            \
    .description = "Reads the world-space pointer ray sampled by the host "    \
                   "for this fixed step. Inactive without a focused 3D view. " \
                   "Recordings retain the ray for replay."

extern const native_definition_pack INPUT_DEFINITION_PACK;

#endif
