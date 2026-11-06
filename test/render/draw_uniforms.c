#include "platform/cute.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>

enum
{
    SIDE = 32,
    PIXELS = SIDE * SIDE
};

static CF_Canvas canvas;

TEST_CASE(adjacent_draw_commands_without_uniforms)
{
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    /* Different layers force adjacent commands with no uniform payload. */
    for (int layer = 0; layer < 2; layer++) {
        cf_draw_push_layer(layer);
        cf_draw_push_color(cf_make_color_rgba_f(1 - layer, layer, 0, 1));
        cf_draw_circle_fill2((CF_V2){ layer * 16 - 8, 0 }, 5);
        cf_draw_pop_color();
        cf_draw_pop_layer();
    }
    cf_render_to(canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);

    CF_Readback readback = cf_canvas_readback(canvas);
    REQUIRE(readback.id);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    CF_Pixel pixels[PIXELS];
    int size = sizeof pixels;
    int copied = cf_readback_ready(readback)
                   ? cf_readback_data(readback, pixels, size)
                   : 0;
    cf_destroy_readback(readback);
    REQUIRE(copied == size);
    CF_Pixel left = pixels[16 * SIDE + 8];
    CF_Pixel right = pixels[16 * SIDE + 24];
    CF_Pixel background = pixels[0];
    REQUIRE(
        left.colors.r == 255 && left.colors.g == 0 && left.colors.b == 0 &&
        left.colors.a == 255
    );
    REQUIRE(
        right.colors.r == 0 && right.colors.g == 255 && right.colors.b == 0 &&
        right.colors.a == 255
    );
    REQUIRE(
        background.colors.r == 0 && background.colors.g == 0 &&
        background.colors.b == 0 && background.colors.a == 0
    );
    return true;
}

void test_render_draw_uniforms()
{
#ifdef __linux__
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    CF_Result made = cf_make_app(
        "draw-uniforms",
        0,
        0,
        0,
        SIDE,
        SIDE,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        NULL
    );
    assert(!cf_is_error(made));
    canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    assert(canvas.id);
    cf_canvas_set_clear_color(canvas, cf_make_color_rgba_f(0, 0, 0, 0));
    RUN_TEST_CASE(adjacent_draw_commands_without_uniforms);
    cf_destroy_canvas(canvas);
    cf_destroy_app();
}
