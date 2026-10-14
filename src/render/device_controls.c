#include "render/device_controls.h"

#include "platform/cute.h"

#include <assert.h>
#include <math.h>

/* A stick reads as centered until it leaves this part of its range. */
static const float STICK_DEAD_ZONE = 0.18f;
/* Triggers count as held past a quarter of their travel. */
static const int TRIGGER_THRESHOLD = 8192;

CF_V2 controls_dead_zone(CF_V2 v)
{
    float length = sqrtf(v.x * v.x + v.y * v.y);
    if (length <= STICK_DEAD_ZONE) {
        return (CF_V2){ 0 };
    }
    float scale = fminf(1, (length - STICK_DEAD_ZONE) / (1 - STICK_DEAD_ZONE)) / length;
    return (CF_V2){ v.x * scale, v.y * scale };
}

/* Y is positive down, like gameplay coordinates. */
static CF_V2 stick(CF_JoypadAxis x, CF_JoypadAxis y)
{
    return controls_dead_zone((CF_V2){ cf_joypad_axis(0, x) / 32768.0f,
                                       cf_joypad_axis(0, y) / 32768.0f });
}

Control control_for_key(CF_KeyButton key)
{
    if (key >= CF_KEY_A && key <= CF_KEY_Z) {
        return CONTROL_A + key - CF_KEY_A;
    }
    if (key >= CF_KEY_0 && key <= CF_KEY_9) {
        return CONTROL_0 + key - CF_KEY_0;
    }
    switch (key) {
        case CF_KEY_SPACE: return CONTROL_SPACE;
        case CF_KEY_LSHIFT:
        case CF_KEY_RSHIFT: return CONTROL_SHIFT;
        case CF_KEY_LCTRL:
        case CF_KEY_RCTRL: return CONTROL_CONTROL;
        case CF_KEY_LALT:
        case CF_KEY_RALT: return CONTROL_ALT;
        case CF_KEY_TAB: return CONTROL_TAB;
        case CF_KEY_RETURN: return CONTROL_ENTER;
        case CF_KEY_BACKSPACE: return CONTROL_BACKSPACE;
        case CF_KEY_UP: return CONTROL_ARROW_UP;
        case CF_KEY_DOWN: return CONTROL_ARROW_DOWN;
        case CF_KEY_LEFT: return CONTROL_ARROW_LEFT;
        case CF_KEY_RIGHT: return CONTROL_ARROW_RIGHT;
        default: return CONTROL_NONE;
    }
}

static bool mouse_down(CF_MouseButton button)
{
    return cf_mouse_down(button) || cf_mouse_just_pressed(button);
}

static bool key_down(Control control)
{
    switch (control) {
        case CONTROL_SHIFT: return cf_key_shift();
        case CONTROL_CONTROL: return cf_key_ctrl();
        case CONTROL_ALT: return cf_key_alt();
        case CONTROL_SPACE: return cf_key_down(CF_KEY_SPACE);
        case CONTROL_TAB: return cf_key_down(CF_KEY_TAB);
        case CONTROL_ENTER: return cf_key_down(CF_KEY_RETURN);
        case CONTROL_BACKSPACE: return cf_key_down(CF_KEY_BACKSPACE);
        case CONTROL_ARROW_UP: return cf_key_down(CF_KEY_UP);
        case CONTROL_ARROW_DOWN: return cf_key_down(CF_KEY_DOWN);
        case CONTROL_ARROW_LEFT: return cf_key_down(CF_KEY_LEFT);
        case CONTROL_ARROW_RIGHT: return cf_key_down(CF_KEY_RIGHT);
        default: break;
    }
    if (control >= CONTROL_A && control <= CONTROL_Z) {
        return cf_key_down(CF_KEY_A + control - CONTROL_A);
    }
    return control >= CONTROL_0 && control <= CONTROL_9 &&
           cf_key_down(CF_KEY_0 + control - CONTROL_0);
}

bool control_down(Control control)
{
    if (control >= CONTROL_PAD_A && control <= CONTROL_PAD_Y) {
        return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_A + control - CONTROL_PAD_A);
    }
    switch (control) {
        case CONTROL_MOUSE_LEFT: return mouse_down(CF_MOUSE_BUTTON_LEFT);
        case CONTROL_MOUSE_RIGHT: return mouse_down(CF_MOUSE_BUTTON_RIGHT);
        case CONTROL_MOUSE_MIDDLE: return mouse_down(CF_MOUSE_BUTTON_MIDDLE);
        case CONTROL_PAD_LEFT_SHOULDER:
            return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_LEFTSHOULDER);
        case CONTROL_PAD_RIGHT_SHOULDER:
            return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_RIGHTSHOULDER);
        case CONTROL_PAD_LEFT_TRIGGER:
            return cf_joypad_axis(0, CF_JOYPAD_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD;
        case CONTROL_PAD_RIGHT_TRIGGER:
            return cf_joypad_axis(0, CF_JOYPAD_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD;
        case CONTROL_AIM_STICK: {
            CF_V2 aim = controls_aim();
            return aim.x || aim.y;
        }
        default: return key_down(control);
    }
}

uint64_t controls_down()
{
    _Static_assert(CONTROL_COUNT <= 64, "control bitset");
    uint64_t controls = 0;
    for (int i = CONTROL_NONE + 1; i < CONTROL_COUNT; i++) {
        if (control_down(i)) {
            controls |= UINT64_C(1) << i;
        }
    }
    return controls;
}

CF_V2 controls_movement()
{
    CF_V2 movement = stick(CF_JOYPAD_AXIS_LEFTX, CF_JOYPAD_AXIS_LEFTY);
    if (movement.x || movement.y) {
        return movement;
    }
    movement.x = (cf_key_down(CF_KEY_D) || cf_key_down(CF_KEY_RIGHT)) -
                 (cf_key_down(CF_KEY_A) || cf_key_down(CF_KEY_LEFT));
    movement.y = (cf_key_down(CF_KEY_S) || cf_key_down(CF_KEY_DOWN)) -
                 (cf_key_down(CF_KEY_W) || cf_key_down(CF_KEY_UP));
    float length = sqrtf(movement.x * movement.x + movement.y * movement.y);
    if (length > 1) {
        movement.x /= length;
        movement.y /= length;
    }
    return movement;
}

CF_V2 controls_aim()
{
    CF_V2 aim = stick(CF_JOYPAD_AXIS_RIGHTX, CF_JOYPAD_AXIS_RIGHTY);
    float length = sqrtf(aim.x * aim.x + aim.y * aim.y);
    return length ? (CF_V2){ aim.x / length, aim.y / length } : aim;
}

input_controller_state controller_state(int controller)
{
    _Static_assert(INPUT_CONTROLLER_MAX <= CF_MAX_JOYPADS, "controller slots");
    assert(controller >= 0 && controller < INPUT_CONTROLLER_MAX);
    input_controller_state state = { .connected = cf_joypad_is_connected(controller) };
    if (!state.connected) {
        return state;
    }
    static const struct
    {
        PadControl control;
        CF_JoypadButton button;
    } buttons[] = {
        { PAD_A, CF_JOYPAD_BUTTON_A },
        { PAD_B, CF_JOYPAD_BUTTON_B },
        { PAD_X, CF_JOYPAD_BUTTON_X },
        { PAD_Y, CF_JOYPAD_BUTTON_Y },
        { PAD_BACK, CF_JOYPAD_BUTTON_BACK },
        { PAD_START, CF_JOYPAD_BUTTON_START },
        { PAD_LEFT_SHOULDER, CF_JOYPAD_BUTTON_LEFTSHOULDER },
        { PAD_RIGHT_SHOULDER, CF_JOYPAD_BUTTON_RIGHTSHOULDER },
        { PAD_DPAD_UP, CF_JOYPAD_BUTTON_DPAD_UP },
        { PAD_DPAD_DOWN, CF_JOYPAD_BUTTON_DPAD_DOWN },
        { PAD_DPAD_LEFT, CF_JOYPAD_BUTTON_DPAD_LEFT },
        { PAD_DPAD_RIGHT, CF_JOYPAD_BUTTON_DPAD_RIGHT },
    };
    for (int i = 0; i < (int)(sizeof buttons / sizeof *buttons); i++) {
        if (cf_joypad_button_down(controller, buttons[i].button)) {
            state.buttons |= UINT32_C(1) << buttons[i].control;
        }
    }
    CF_V2 movement = controls_dead_zone((CF_V2){
        cf_joypad_axis(controller, CF_JOYPAD_AXIS_LEFTX) / 32768.0f,
        cf_joypad_axis(controller, CF_JOYPAD_AXIS_LEFTY) / 32768.0f,
    });
    static const float direction_threshold = 0.5f;
    if (movement.x < -direction_threshold) {
        state.buttons |= UINT32_C(1) << PAD_LEFT_STICK_LEFT;
    }
    if (movement.x > direction_threshold) {
        state.buttons |= UINT32_C(1) << PAD_LEFT_STICK_RIGHT;
    }
    if (movement.y < -direction_threshold) {
        state.buttons |= UINT32_C(1) << PAD_LEFT_STICK_UP;
    }
    if (movement.y > direction_threshold) {
        state.buttons |= UINT32_C(1) << PAD_LEFT_STICK_DOWN;
    }
    if (cf_joypad_axis(controller, CF_JOYPAD_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD) {
        state.buttons |= UINT32_C(1) << PAD_LEFT_TRIGGER;
    }
    if (cf_joypad_axis(controller, CF_JOYPAD_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD) {
        state.buttons |= UINT32_C(1) << PAD_RIGHT_TRIGGER;
    }
    return state;
}
