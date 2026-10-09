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
    REQUIRE(game_control_for_key(CF_KEY_A) == GAME_CONTROL_A);
    REQUIRE(game_control_for_key(CF_KEY_Z) == GAME_CONTROL_Z);
    REQUIRE(game_control_for_key(CF_KEY_0) == GAME_CONTROL_0);
    REQUIRE(game_control_for_key(CF_KEY_9) == GAME_CONTROL_9);
    /* The keys just outside the digit range stay unmapped. */
    REQUIRE(game_control_for_key(CF_KEY_SLASH) == GAME_CONTROL_NONE);
    REQUIRE(game_control_for_key(CF_KEY_COLON) == GAME_CONTROL_NONE);
    REQUIRE(game_control_for_key(CF_KEY_UP) == GAME_CONTROL_ARROW_UP);
    REQUIRE(game_control_for_key(CF_KEY_DOWN) == GAME_CONTROL_ARROW_DOWN);
    REQUIRE(game_control_for_key(CF_KEY_LEFT) == GAME_CONTROL_ARROW_LEFT);
    REQUIRE(game_control_for_key(CF_KEY_RIGHT) == GAME_CONTROL_ARROW_RIGHT);
    REQUIRE(game_control_for_key(CF_KEY_LCTRL) == GAME_CONTROL_CONTROL);
    REQUIRE(game_control_for_key(CF_KEY_RALT) == GAME_CONTROL_ALT);
    REQUIRE(game_control_for_key(CF_KEY_BACKSPACE) == GAME_CONTROL_BACKSPACE);
    /* Escape is reserved for pausing play. */
    REQUIRE(game_control_for_key(CF_KEY_ESCAPE) == GAME_CONTROL_NONE);
    REQUIRE(game_control_for_key(CF_KEY_F1) == GAME_CONTROL_NONE);
    return true;
}

TEST_CASE(stick_dead_zone_rescales_to_full_length)
{
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ 0.1f, -0.1f }), 0, 0));
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ dead_zone, 0 }), 0, 0));
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ 0, -dead_zone }), 0, 0));
    /* Halfway between the edge and full deflection reads half length. */
    float half = (1 + dead_zone) / 2;
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ half, 0 }), 0.5f, 0));
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ 1, 0 }), 1, 0));
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ -1, 0 }), -1, 0));
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ 0, 1 }), 0, 1));
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ 0, -1 }), 0, -1));
    /* A full corner keeps its direction but never exceeds length 1. */
    float corner = sqrtf(0.5f);
    REQUIRE(close_to(game_controls_dead_zone((CF_V2){ 1, 1 }), corner, corner));
    return true;
}

void test_device_controls()
{
    RUN_TEST_CASE(desktop_keys_map_to_their_game_controls);
    RUN_TEST_CASE(stick_dead_zone_rescales_to_full_length);
}
