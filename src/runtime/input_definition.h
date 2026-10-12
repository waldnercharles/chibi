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
    INPUT_PORT(X, "Origin", PIN_STRUCT, "CF_V3")                               \
    INPUT_PORT(X, "Direction", PIN_STRUCT, "CF_V3")
#define INPUT_POINTER_RAY_PRESENTATION                                         \
    .category = "Input", .keywords = "mouse cursor camera pick 3d",            \
    .description = "Reads the world-space pointer ray sampled by the host "    \
                   "for this fixed step. Inactive without a focused 3D view. " \
                   "Recordings retain the ray for replay."

extern const native_definition_pack INPUT_DEFINITION_PACK;

#define INPUT_DEVICE_STATE_NAME "Get Device State"
#define INPUT_DEVICE_STATE_OUTPUTS(X)                                          \
    INPUT_PORT(X, "Width", PIN_F32, "float")                                   \
    INPUT_PORT(X, "Height", PIN_F32, "float")                                  \
    INPUT_PORT(X, "Touch", PIN_BOOL, "bool")                                   \
    INPUT_PORT(X, "Active", PIN_BOOL, "bool")                                  \
    INPUT_PORT(X, "MotionStatus", PIN_I32, "int")                              \
    INPUT_PORT(X, "Tilt", PIN_STRUCT, "CF_V2")                                 \
    INPUT_PORT(X, "Haptics", PIN_BOOL, "bool")
#define INPUT_DEVICE_STATE_PRESENTATION                                          \
    .category = "Input", .keywords = "viewport phone touch tilt motion haptics", \
    .description = "Reads viewport width and height in logical pixels, touch "   \
                   "capability, foreground state, motion status, calibrated "    \
                   "screen-relative tilt in degrees and haptics capability. "    \
                   "Motion status is disabled (0), waiting (1), ready (2), "     \
                   "unavailable (3) or denied (4)."
#define INPUT_REQUEST_MOTION_NAME "Request Device Motion"
#define INPUT_DISABLE_MOTION_NAME "Disable Device Motion"
#define INPUT_RECENTER_MOTION_NAME "Recenter Device Motion"
#define INPUT_VIBRATE_NAME "Vibrate Device"
#define INPUT_DEVICE_EXEC(X, NAME)                                             \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define INPUT_DEVICE_INPUTS(X) INPUT_DEVICE_EXEC(X, "Exec")
#define INPUT_DEVICE_OUTPUTS(X) INPUT_DEVICE_EXEC(X, "Then")
#define INPUT_VIBRATE_INPUTS(X)                                                \
    INPUT_DEVICE_INPUTS(X) INPUT_PORT(X, "Milliseconds", PIN_I32, "int")
#define INPUT_REQUEST_MOTION_PRESENTATION                                      \
    .category = "Input",                                                       \
    .keywords = "phone sensor permission tilt", .description = "Asks the host to enable device motion from a user gesture."
#define INPUT_DISABLE_MOTION_PRESENTATION                                      \
    .category = "Input",                                                       \
    .keywords = "phone sensor off stop tilt", .description = "Disables device motion and cancels a pending permission request."
#define INPUT_RECENTER_MOTION_PRESENTATION                                     \
    .category = "Input", .keywords = "phone sensor calibrate zero tilt",       \
    .description = "Uses the current device orientation as neutral tilt."
#define INPUT_VIBRATE_PRESENTATION                                             \
    .category = "Input", .keywords = "phone haptics vibration pulse",          \
    .description = "Queues the longest requested vibration pulse, capped at "  \
                   "1000 milliseconds, until the host drains requests. "       \
                   "Zero clears the queued pulse; negative durations fail."

#endif
