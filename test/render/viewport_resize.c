#include "platform/cute.h"
#include "render/viewport.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>

/* Draws one host frame of the viewport at the requested size and reports the
 * canvas size it drew into. */
static void frame(engine_viewport *viewport, int width, int height, int *drawn_width, int *drawn_height)
{
    cf_app_update(NULL);
    int rendered = engine_viewport_render(viewport, width, height);
    assert(rendered == 0);
    cf_app_draw_onto_screen(false);
    engine_viewport_pixel_size(viewport, drawn_width, drawn_height);
}

TEST_CASE(first_request_replaces_the_placeholder_at_once)
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    REQUIRE(viewport);
    int width, height;
    frame(viewport, 1530, 570, &width, &height);
    REQUIRE(width == 1530 && height == 570);
    engine_viewport_free(viewport);
    return true;
}

TEST_CASE(later_sizes_wait_until_held_for_two_draws)
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    REQUIRE(viewport);
    int width, height;
    frame(viewport, 1530, 570, &width, &height);
    for (int draw = 0; draw < 2; draw++) {
        frame(viewport, 800, 600, &width, &height);
        REQUIRE(width == 1530 && height == 570);
    }
    frame(viewport, 800, 600, &width, &height);
    REQUIRE(width == 800 && height == 600);
    engine_viewport_free(viewport);
    return true;
}

TEST_CASE(a_matching_first_request_keeps_later_sizes_waiting)
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    REQUIRE(viewport);
    int width, height;
    frame(viewport, 640, 360, &width, &height);
    frame(viewport, 800, 600, &width, &height);
    REQUIRE(width == 640 && height == 360);
    engine_viewport_free(viewport);
    return true;
}

void test_render_viewport_resize()
{
#ifdef __linux__
    /* As in interior_border.c: no display is needed on Linux agents. */
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    CF_Result made = cf_make_app("viewport-resize", 0, 0, 0, 64, 64, CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT, NULL);
    assert(!cf_is_error(made));
    RUN_TEST_CASE(first_request_replaces_the_placeholder_at_once);
    RUN_TEST_CASE(later_sizes_wait_until_held_for_two_draws);
    RUN_TEST_CASE(a_matching_first_request_keeps_later_sizes_waiting);
    cf_destroy_app();
}
