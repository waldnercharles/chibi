#include "render/device_controls.h"

#include "platform/cute.h"

#include <math.h>

/* A stick reads as centered until it leaves this part of its range. */
static const float STICK_DEAD_ZONE = 0.18f;
/* Triggers count as held past a quarter of their travel. */
static const int TRIGGER_THRESHOLD = 8192;

CF_V2 game_controls_dead_zone(CF_V2 v)
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
    return game_controls_dead_zone((CF_V2){ cf_joypad_axis(0, x) / 32768.0f,
                                            cf_joypad_axis(0, y) / 32768.0f });
}

GameControl game_control_for_key(CF_KeyButton key)
{
    if (key >= CF_KEY_A && key <= CF_KEY_Z) {
        return GAME_CONTROL_A + key - CF_KEY_A;
    }
    if (key >= CF_KEY_0 && key <= CF_KEY_9) {
        return GAME_CONTROL_0 + key - CF_KEY_0;
    }
    switch (key) {
        case CF_KEY_SPACE: return GAME_CONTROL_SPACE;
        case CF_KEY_LSHIFT:
        case CF_KEY_RSHIFT: return GAME_CONTROL_SHIFT;
        case CF_KEY_LCTRL:
        case CF_KEY_RCTRL: return GAME_CONTROL_CONTROL;
        case CF_KEY_LALT:
        case CF_KEY_RALT: return GAME_CONTROL_ALT;
        case CF_KEY_TAB: return GAME_CONTROL_TAB;
        case CF_KEY_RETURN: return GAME_CONTROL_ENTER;
        case CF_KEY_BACKSPACE: return GAME_CONTROL_BACKSPACE;
        case CF_KEY_UP: return GAME_CONTROL_ARROW_UP;
        case CF_KEY_DOWN: return GAME_CONTROL_ARROW_DOWN;
        case CF_KEY_LEFT: return GAME_CONTROL_ARROW_LEFT;
        case CF_KEY_RIGHT: return GAME_CONTROL_ARROW_RIGHT;
        default: return GAME_CONTROL_NONE;
    }
}

static bool mouse_down(CF_MouseButton button)
{
    return cf_mouse_down(button) || cf_mouse_just_pressed(button);
}

static bool key_down(GameControl control)
{
    switch (control) {
        case GAME_CONTROL_SHIFT: return cf_key_shift();
        case GAME_CONTROL_CONTROL: return cf_key_ctrl();
        case GAME_CONTROL_ALT: return cf_key_alt();
        case GAME_CONTROL_SPACE: return cf_key_down(CF_KEY_SPACE);
        case GAME_CONTROL_TAB: return cf_key_down(CF_KEY_TAB);
        case GAME_CONTROL_ENTER: return cf_key_down(CF_KEY_RETURN);
        case GAME_CONTROL_BACKSPACE: return cf_key_down(CF_KEY_BACKSPACE);
        case GAME_CONTROL_ARROW_UP: return cf_key_down(CF_KEY_UP);
        case GAME_CONTROL_ARROW_DOWN: return cf_key_down(CF_KEY_DOWN);
        case GAME_CONTROL_ARROW_LEFT: return cf_key_down(CF_KEY_LEFT);
        case GAME_CONTROL_ARROW_RIGHT: return cf_key_down(CF_KEY_RIGHT);
        default: break;
    }
    if (control >= GAME_CONTROL_A && control <= GAME_CONTROL_Z) {
        return cf_key_down(CF_KEY_A + control - GAME_CONTROL_A);
    }
    return control >= GAME_CONTROL_0 && control <= GAME_CONTROL_9 &&
           cf_key_down(CF_KEY_0 + control - GAME_CONTROL_0);
}

bool game_control_down(GameControl control)
{
    if (control >= GAME_CONTROL_PAD_A && control <= GAME_CONTROL_PAD_Y) {
        return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_A + control - GAME_CONTROL_PAD_A);
    }
    switch (control) {
        case GAME_CONTROL_MOUSE_LEFT: return mouse_down(CF_MOUSE_BUTTON_LEFT);
        case GAME_CONTROL_MOUSE_RIGHT: return mouse_down(CF_MOUSE_BUTTON_RIGHT);
        case GAME_CONTROL_MOUSE_MIDDLE:
            return mouse_down(CF_MOUSE_BUTTON_MIDDLE);
        case GAME_CONTROL_PAD_LEFT_SHOULDER:
            return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_LEFTSHOULDER);
        case GAME_CONTROL_PAD_RIGHT_SHOULDER:
            return cf_joypad_button_down(0, CF_JOYPAD_BUTTON_RIGHTSHOULDER);
        case GAME_CONTROL_PAD_LEFT_TRIGGER:
            return cf_joypad_axis(0, CF_JOYPAD_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD;
        case GAME_CONTROL_PAD_RIGHT_TRIGGER:
            return cf_joypad_axis(0, CF_JOYPAD_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD;
        case GAME_CONTROL_AIM_STICK: {
            CF_V2 aim = game_controls_aim();
            return aim.x || aim.y;
        }
        default: return key_down(control);
    }
}

uint64_t game_controls_down()
{
    _Static_assert(GAME_CONTROL_COUNT <= 64, "control bitset");
    uint64_t controls = 0;
    for (int i = GAME_CONTROL_NONE + 1; i < GAME_CONTROL_COUNT; i++) {
        if (game_control_down(i)) {
            controls |= UINT64_C(1) << i;
        }
    }
    return controls;
}

CF_V2 game_controls_movement()
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

CF_V2 game_controls_aim()
{
    CF_V2 aim = stick(CF_JOYPAD_AXIS_RIGHTX, CF_JOYPAD_AXIS_RIGHTY);
    float length = sqrtf(aim.x * aim.x + aim.y * aim.y);
    return length ? (CF_V2){ aim.x / length, aim.y / length } : aim;
}
