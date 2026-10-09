#ifndef CHIBI_GAME_INPUT_DEFINITION_H
#define CHIBI_GAME_INPUT_DEFINITION_H

#include "runtime/input_action.h"
#include "runtime/native_definitions.h"

#define GAME_INPUT_PORT(X, NAME, STORAGE, TYPE_NAME)                           \
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
#define GAME_INPUT_ACTION_INPUTS(X)                                            \
    GAME_INPUT_PORT(X, "Action", PIN_STR, "const char *")
#define GAME_INPUT_HELD_OUTPUTS(X) GAME_INPUT_PORT(X, "Held", PIN_BOOL, "bool")
#define GAME_INPUT_PRESSED_OUTPUTS(X)                                          \
    GAME_INPUT_PORT(X, "Pressed", PIN_BOOL, "bool")
/* X(KEY, NAME, OUTPUTS). Scripts read them as input.<Action>.held and
 * input.<Action>.pressed, never by name. */
#define GAME_INPUT_OPERATIONS(X)                                               \
    X(HELD, INPUT_ACTION_HELD, GAME_INPUT_HELD_OUTPUTS)                        \
    X(PRESSED, INPUT_ACTION_PRESSED, GAME_INPUT_PRESSED_OUTPUTS)

#define GAME_INPUT_HELD_PRESENTATION                                           \
    .category = "Input", .keywords = "action button key down hold",            \
    .description = "True while any source bound to Action is down."
#define GAME_INPUT_PRESSED_PRESENTATION                                        \
    .category = "Input", .keywords = "action button key tap just pressed",     \
    .description = "True for the one fixed step after any source bound to "    \
                   "Action went down, even if it was released again before "   \
                   "the step."

extern const native_definition_pack GAME_INPUT_DEFINITION_PACK;

#endif
