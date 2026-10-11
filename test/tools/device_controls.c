#include "render/device_controls.h"

#include "third_party/pico_headers/pico_unit.h"

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

void test_device_controls()
{
    RUN_TEST_CASE(desktop_keys_map_to_their_game_controls);
    RUN_TEST_CASE(stick_dead_zone_rescales_to_full_length);
}
