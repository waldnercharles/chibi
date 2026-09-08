#ifndef BLUEPRINTS_INPUT_H
#define BLUEPRINTS_INPUT_H
#include <stdbool.h>

typedef enum
{
    APP_INPUT_FIRE,
    APP_INPUT_ESCAPE,
    APP_INPUT_UNDO,
    APP_INPUT_REDO,
    APP_INPUT_COPY,
    APP_INPUT_CUT,
    APP_INPUT_PASTE,
    APP_INPUT_DUPLICATE,
    APP_INPUT_SAVE,
    APP_INPUT_DELETE,
    APP_INPUT_SHIFT,
    APP_INPUT_CONTROL,
    APP_INPUT_COMMAND,
    APP_INPUT_COUNT
} app_input_action;

/* Bindings belong to the active application generation and Cute app lifetime.
 * Initialize before cf_app_update; destroy before Cute or module teardown. */
void app_input_initialize();
void app_input_shutdown();
/* Once per frame, immediately after cf_app_update. */
void app_input_frame();
bool app_input_pressed(app_input_action action);
bool app_input_down(app_input_action action);
void app_input_movement(float *x, float *y);
#endif
