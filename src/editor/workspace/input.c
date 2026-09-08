#include "editor/workspace/input.h"

#include <assert.h>
#include <cute_binding.h>
#include <math.h>

static CF_ButtonBinding buttons[APP_INPUT_COUNT];
static CF_AxisBinding horizontal, vertical;
static bool ready;
static bool first_frame;
static bool held_at_reload[APP_INPUT_COUNT];

void app_input_initialize()
{
    if (ready) {
        return;
    }
    const CF_KeyButton keys[] = { CF_KEY_SPACE,  CF_KEY_ESCAPE, CF_KEY_Z,
                                  CF_KEY_Y,      CF_KEY_C,      CF_KEY_X,
                                  CF_KEY_V,      CF_KEY_D,      CF_KEY_S,
                                  CF_KEY_DELETE, CF_KEY_LSHIFT, CF_KEY_LCTRL,
#ifdef __APPLE__
                                  CF_KEY_LGUI
#else
                                  CF_KEY_LCTRL
#endif
    };
    _Static_assert(sizeof keys / sizeof *keys == APP_INPUT_COUNT, "action keys");
    for (int i = 0; i < APP_INPUT_COUNT; i++) {
        buttons[i] = cf_make_button_binding(0, 0);
        cf_button_binding_add_key(buttons[i], keys[i]);
        if (i >= APP_INPUT_UNDO && i <= APP_INPUT_DELETE) {
            cf_button_binding_set_repeat(buttons[i], 0.275f, 0.05f);
        }
    }
    cf_button_binding_add_key(buttons[APP_INPUT_DELETE], CF_KEY_BACKSPACE);
    cf_button_binding_add_key(buttons[APP_INPUT_SHIFT], CF_KEY_RSHIFT);
    cf_button_binding_add_key(buttons[APP_INPUT_CONTROL], CF_KEY_RCTRL);
#ifdef __APPLE__
    cf_button_binding_add_key(buttons[APP_INPUT_COMMAND], CF_KEY_RGUI);
#else
    cf_button_binding_add_key(buttons[APP_INPUT_COMMAND], CF_KEY_RCTRL);
#endif
    horizontal = cf_make_axis_binding(0);
    vertical = cf_make_axis_binding(0);
    cf_axis_binding_add_keys(horizontal, CF_KEY_A, CF_KEY_D);
    cf_axis_binding_add_keys(horizontal, CF_KEY_LEFT, CF_KEY_RIGHT);
    cf_axis_binding_add_keys(vertical, CF_KEY_W, CF_KEY_S);
    cf_axis_binding_add_keys(vertical, CF_KEY_UP, CF_KEY_DOWN);
    cf_axis_binding_set_conflict(horizontal, CF_AXIS_CONFLICT_CANCEL);
    cf_axis_binding_set_conflict(vertical, CF_AXIS_CONFLICT_CANCEL);
    first_frame = true;
    ready = true;
}

/* Sample after Cute updates its bindings, before any application consumer.
 * Held movement remains continuous; a held action must be released before it
 * can fire in the replacement generation. */
void app_input_frame()
{
    if (!ready) {
        return;
    }
    for (int i = 0; i < APP_INPUT_COUNT; i++) {
        bool down = cf_button_binding_down(buttons[i]);
        if (first_frame) {
            held_at_reload[i] = down;
        }
        if (!down) {
            held_at_reload[i] = false;
        }
    }
    first_frame = false;
}

void app_input_shutdown()
{
    if (!ready) {
        return;
    }
    for (int i = 0; i < APP_INPUT_COUNT; i++) {
        cf_destroy_button_binding(buttons[i]);
    }
    cf_destroy_axis_binding(horizontal);
    cf_destroy_axis_binding(vertical);
    ready = false;
}

bool app_input_pressed(app_input_action action)
{
    assert(action >= 0 && action < APP_INPUT_COUNT);
    return ready && !held_at_reload[action] &&
           (cf_button_binding_pressed(buttons[action]) ||
            cf_button_binding_repeated(buttons[action]));
}

bool app_input_down(app_input_action action)
{
    assert(action >= 0 && action < APP_INPUT_COUNT);
    return ready && cf_button_binding_down(buttons[action]);
}

void app_input_movement(float *x, float *y)
{
    *x = ready ? cf_axis_binding_value(horizontal) : 0;
    *y = ready ? cf_axis_binding_value(vertical) : 0;
    float length = hypotf(*x, *y);
    if (length > 1) {
        *x /= length;
        *y /= length;
    }
}
