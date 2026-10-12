#include "render/device_controls.h"

#include "third_party/pico_headers/pico_unit.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_mouse.h>
#include <math.h>

/* Mirrors STICK_DEAD_ZONE so retuning it is a reviewed contract change. */
static const float dead_zone = 0.18f;

static bool close_to(CF_V2 v, float x, float y)
{
    return fabsf(v.x - x) < 1e-5f && fabsf(v.y - y) < 1e-5f;
}

TEST_CASE(desktop_keys_map_to_their_game_controls)
{
    REQUIRE(control_for_key(CF_KEY_A) == CONTROL_A);
    REQUIRE(control_for_key(CF_KEY_Z) == CONTROL_Z);
    REQUIRE(control_for_key(CF_KEY_0) == CONTROL_0);
    REQUIRE(control_for_key(CF_KEY_9) == CONTROL_9);
    /* The keys just outside the digit range stay unmapped. */
    REQUIRE(control_for_key(CF_KEY_SLASH) == CONTROL_NONE);
    REQUIRE(control_for_key(CF_KEY_COLON) == CONTROL_NONE);
    REQUIRE(control_for_key(CF_KEY_UP) == CONTROL_ARROW_UP);
    REQUIRE(control_for_key(CF_KEY_DOWN) == CONTROL_ARROW_DOWN);
    REQUIRE(control_for_key(CF_KEY_LEFT) == CONTROL_ARROW_LEFT);
    REQUIRE(control_for_key(CF_KEY_RIGHT) == CONTROL_ARROW_RIGHT);
    REQUIRE(control_for_key(CF_KEY_LCTRL) == CONTROL_CONTROL);
    REQUIRE(control_for_key(CF_KEY_RALT) == CONTROL_ALT);
    REQUIRE(control_for_key(CF_KEY_BACKSPACE) == CONTROL_BACKSPACE);
    /* Escape is reserved for pausing play. */
    REQUIRE(control_for_key(CF_KEY_ESCAPE) == CONTROL_NONE);
    REQUIRE(control_for_key(CF_KEY_F1) == CONTROL_NONE);
    return true;
}

TEST_CASE(stick_dead_zone_rescales_to_full_length)
{
    REQUIRE(close_to(controls_dead_zone((CF_V2){ 0.1f, -0.1f }), 0, 0));
    REQUIRE(close_to(controls_dead_zone((CF_V2){ dead_zone, 0 }), 0, 0));
    REQUIRE(close_to(controls_dead_zone((CF_V2){ 0, -dead_zone }), 0, 0));
    /* Halfway between the edge and full deflection reads half length. */
    float half = (1 + dead_zone) / 2;
    REQUIRE(close_to(controls_dead_zone((CF_V2){ half, 0 }), 0.5f, 0));
    REQUIRE(close_to(controls_dead_zone((CF_V2){ 1, 0 }), 1, 0));
    REQUIRE(close_to(controls_dead_zone((CF_V2){ -1, 0 }), -1, 0));
    REQUIRE(close_to(controls_dead_zone((CF_V2){ 0, 1 }), 0, 1));
    REQUIRE(close_to(controls_dead_zone((CF_V2){ 0, -1 }), 0, -1));
    /* A full corner keeps its direction but never exceeds length 1. */
    float corner = sqrtf(0.5f);
    REQUIRE(close_to(controls_dead_zone((CF_V2){ 1, 1 }), corner, corner));
    return true;
}

TEST_CASE(tilt_tracks_gravity_in_the_current_screen_axes)
{
    REQUIRE(close_to(controls_tilt(0, 0, 0), 0, 0));
    REQUIRE(close_to(controls_tilt(0, 30, 0), 30, 0));
    REQUIRE(close_to(controls_tilt(30, 0, 0), 0, 30));
    REQUIRE(close_to(controls_tilt(30, 0, 90), 30, 0));
    REQUIRE(close_to(controls_tilt(30, 0, -90), -30, 0));
    REQUIRE(close_to(controls_tilt(0, 30, 180), -30, 0));
    /* Upright landscape avoids Euler-angle singularities: a small beta
     * rotation still produces a small, correctly signed sideways tilt. */
    CF_V2 upright = controls_tilt(0, -90, 90);
    CF_V2 tilted = controls_tilt(12, -90, 90);
    REQUIRE(fabsf(upright.x) < 1e-4f);
    REQUIRE(fabsf(tilted.x - upright.x - 12) < 1e-4f);
    REQUIRE(isfinite(tilted.y));
    return true;
}

TEST_CASE(complete_pointer_taps_retain_the_press_edge_and_location)
{
    REQUIRE(SDL_Init(SDL_INIT_EVENTS));
    REQUIRE(controls_pointer_start());
    SDL_Event down = { .type = SDL_EVENT_MOUSE_BUTTON_DOWN };
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = 80;
    down.button.y = 120;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    up.button.x = 300;
    up.button.y = 400;
    REQUIRE(SDL_PushEvent(&down));
    REQUIRE(SDL_PushEvent(&up));
    /* Both events precede one host sample, with the release over a different
     * control. Gameplay keeps the press; UI continues sampling Cute directly. */
    device_pointer_sample tap = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(tap.pressed[CF_MOUSE_BUTTON_LEFT]);
    REQUIRE(control_for_mouse(CF_MOUSE_BUTTON_LEFT) == CONTROL_MOUSE_LEFT);
    REQUIRE(close_to(tap.position, 80, 120));
    device_pointer_sample next = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(!next.pressed[CF_MOUSE_BUTTON_LEFT]);
    /* A render frame with no fixed step must not retarget the pending press. */
    REQUIRE(close_to(next.position, 80, 120));
    controls_pointer_consumed();
    next = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(close_to(next.position, 300, 400));
    REQUIRE(SDL_PushEvent(&down));
    device_pointer_sample drag = controls_pointer_take((CF_V2){ 150, 180 });
    REQUIRE(drag.pressed[CF_MOUSE_BUTTON_LEFT]);
    REQUIRE(close_to(drag.position, 80, 120));
    controls_pointer_consumed();
    device_pointer_sample moved = controls_pointer_take((CF_V2){ 150, 180 });
    REQUIRE(!moved.pressed[CF_MOUSE_BUTTON_LEFT]);
    REQUIRE(close_to(moved.position, 150, 180));
    /* Consuming a step must not erase events delivered since the last sample. */
    REQUIRE(SDL_PushEvent(&down));
    controls_pointer_consumed();
    device_pointer_sample later = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(later.pressed[CF_MOUSE_BUTTON_LEFT]);
    REQUIRE(close_to(later.position, 80, 120));
    /* Focus loss cancels both raw edges and a press awaiting simulation. */
    REQUIRE(SDL_PushEvent(&down));
    SDL_Event focus = { .type = SDL_EVENT_WINDOW_FOCUS_LOST };
    REQUIRE(SDL_PushEvent(&focus));
    device_pointer_sample canceled = controls_pointer_take((CF_V2){ 0 });
    REQUIRE(!canceled.pressed[CF_MOUSE_BUTTON_LEFT]);
    REQUIRE(close_to(canceled.position, 0, 0));
    REQUIRE(SDL_PushEvent(&down));
    canceled = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(close_to(canceled.position, 80, 120));
    /* The host also cancels after transport stop or UI capture. */
    controls_pointer_consumed();
    canceled = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(close_to(canceled.position, 300, 400));
    REQUIRE(SDL_PushEvent(&down));
    canceled = controls_pointer_take((CF_V2){ 300, 400 });
    REQUIRE(close_to(canceled.position, 80, 120));
    controls_pointer_stop();
    REQUIRE(SDL_PushEvent(&down));
    device_pointer_sample stopped = controls_pointer_take((CF_V2){ 0 });
    REQUIRE(!stopped.pressed[CF_MOUSE_BUTTON_LEFT]);
    REQUIRE(close_to(stopped.position, 0, 0));
    SDL_FlushEvents(SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_MOUSE_BUTTON_UP);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    return true;
}

void test_device_controls()
{
    RUN_TEST_CASE(desktop_keys_map_to_their_game_controls);
    RUN_TEST_CASE(stick_dead_zone_rescales_to_full_length);
    RUN_TEST_CASE(tilt_tracks_gravity_in_the_current_screen_axes);
    RUN_TEST_CASE(complete_pointer_taps_retain_the_press_edge_and_location);
}
