#include "third_party/pico_headers/pico_unit.h"

#include <cute_time.h>

static int target_framerate, calls;

static void record_framerate(int rate)
{
    target_framerate = rate;
    calls++;
}

/* Compile the actual player initialization for both hosts. Only the Cute
 * setter is replaced, so no clocks, sleeps, window or GPU are needed. */
#pragma push_macro("__EMSCRIPTEN__")
#undef __EMSCRIPTEN__
#define cf_set_target_framerate record_framerate
#define player_clock_start native_clock_start
#include "host/player_clock.h"
#undef player_clock_start
#undef CHIBI_HOST_PLAYER_CLOCK_H
#define __EMSCRIPTEN__ 1
#define player_clock_start browser_clock_start
#include "host/player_clock.h"
#undef player_clock_start
#undef __EMSCRIPTEN__
#pragma pop_macro("__EMSCRIPTEN__")
#undef cf_set_target_framerate

static void setup()
{
    target_framerate = -1;
    calls = 0;
}

TEST_CASE(browser_frames_leave_the_framework_limiter_disabled)
{
    browser_clock_start();
    REQUIRE(calls == 0);
    REQUIRE(target_framerate == -1);
    return true;
}

TEST_CASE(native_frames_keep_the_sixty_fps_limit)
{
    native_clock_start();
    REQUIRE(calls == 1);
    REQUIRE(target_framerate == 60);
    return true;
}

void test_tool_player_clock()
{
    pu_setup(setup, NULL);
    RUN_TEST_CASE(browser_frames_leave_the_framework_limiter_disabled);
    RUN_TEST_CASE(native_frames_keep_the_sixty_fps_limit);
    pu_setup(NULL, NULL);
}
