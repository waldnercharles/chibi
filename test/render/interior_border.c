#include "platform/cute.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Cute exposes these test controls from its internal draw header. */
void test_draw_set_tiled(bool enabled);
bool test_draw_tiled_available();
void test_draw_tiled_auto();
void test_draw_tiled_stats(int *tiled, int *instanced);

enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE,
    BORDER = 3
};

static const float border_alpha = 0.75f;
static CF_Canvas canvas;

static float coverage(float distance, float edge)
{
    float t = fminf(1, fmaxf(0, distance - edge));
    return 1 - t * t * (3 - 2 * t);
}

static float shape_distance(bool diamond, float x, float y)
{
    if (!diamond) {
        return hypotf(x, y) - 16;
    }
    x = fabsf(x);
    y = fabsf(y);
    float t = fminf(1, fmaxf(0, (16 - x + y) / 32));
    float distance = hypotf(x - 16 * (1 - t), y - 16 * t);
    return x + y < 16 ? -distance : distance;
}

/* Draws a red shape with a translucent blue interior border on the requested
 * path and reads the canvas back. */
static bool render(bool tiled, bool diamond, float alpha, CF_Pixel *pixels)
{
    cf_app_update(NULL);
    test_draw_tiled_stats(NULL, NULL);
    test_draw_set_tiled(tiled);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    /* Cancel window-density compensation: this fixed canvas has one unit per pixel. */
    cf_draw_push_shape_aa(cf_app_get_pixel_scale());
    cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, alpha));
    cf_draw_push_interior_border(cf_make_color_rgba_f(0, 0, 1, border_alpha), BORDER);
    if (diamond) {
        cf_draw_quad_fill2((CF_V2){ 16, 0 }, (CF_V2){ 0, 16 }, (CF_V2){ -16, 0 }, (CF_V2){ 0, -16 }, 0);
    } else {
        cf_draw_circle_fill2((CF_V2){ 0 }, 16);
    }
    cf_draw_pop_outline();
    cf_draw_pop_color();
    cf_draw_pop_shape_aa();
    cf_render_to(canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    int tiled_batches, instanced_batches;
    test_draw_tiled_stats(&tiled_batches, &instanced_batches);
    if (tiled ? tiled_batches == 0 || instanced_batches
              : instanced_batches == 0 || tiled_batches) {
        fprintf(stderr, "Drew %d tiled and %d instanced batches\n", tiled_batches, instanced_batches);
        return false;
    }
    CF_Readback readback = cf_canvas_readback(canvas);
    assert(readback.id);
    int size = (int)(PIXELS * sizeof *pixels);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    int copied = cf_readback_ready(readback)
                   ? cf_readback_data(readback, pixels, size)
                   : 0;
    cf_destroy_readback(readback);
    return copied == size;
}

/* The fill erodes by the border width and the border takes the rest of the
 * shape's coverage, so a translucent border shows what is behind the shape. */
static bool matches_reference(const CF_Pixel *pixels, bool diamond, float alpha)
{
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            float distance = shape_distance(diamond, x + 0.5f - SIDE / 2, y + 0.5f - SIDE / 2);
            float fill = coverage(distance, -BORDER);
            float border = (coverage(distance, 0) - fill) * border_alpha;
            float red = fill * alpha * (1 - border);
            /* Premultiplied source-over against the transparent canvas. */
            float expected[] = { red, 0, border, border + red };
            CF_Pixel pixel = pixels[y * SIDE + x];
            unsigned actual[] = { pixel.colors.r,
                                  pixel.colors.g,
                                  pixel.colors.b,
                                  pixel.colors.a };
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(actual[channel] - expected[channel] * 255) > 3) {
                    fprintf(
                        stderr,
                        "%s alpha %g pixel (%d,%d) channel %d: got %u, "
                        "expected %.2f\n",
                        diamond ? "Diamond" : "Circle",
                        alpha,
                        x,
                        y,
                        channel,
                        actual[channel],
                        expected[channel] * 255
                    );
                    return false;
                }
            }
        }
    }
    return true;
}

static bool draws_border(bool tiled, bool diamond, float alpha)
{
    static CF_Pixel pixels[PIXELS];
    return render(tiled, diamond, alpha, pixels) &&
           matches_reference(pixels, diamond, alpha);
}

TEST_CASE(instanced_shapes_draw_an_interior_border)
{
    for (int shape = 0; shape < 2; shape++) {
        REQUIRE(draws_border(false, shape, 1));
        REQUIRE(draws_border(false, shape, 0.4f));
    }
    return true;
}

TEST_CASE(tiled_shapes_draw_the_same_interior_border)
{
    /* Only GLES lacks Cute's compute renderer, and this app never selects it. */
    REQUIRE(test_draw_tiled_available());
    for (int shape = 0; shape < 2; shape++) {
        REQUIRE(draws_border(true, shape, 1));
        REQUIRE(draws_border(true, shape, 0.4f));
    }
    return true;
}

static void teardown()
{
    test_draw_tiled_auto();
}

void test_render_interior_border()
{
#ifdef __linux__
    /* Linux agents may have no display, and the offscreen driver still reaches
     * Vulkan. Metal and D3D12 need the platform's own window. A caller's
     * driver wins, under either of SDL's names. */
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    CF_Result made = cf_make_app(
        "interior-border",
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
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(instanced_shapes_draw_an_interior_border);
    RUN_TEST_CASE(tiled_shapes_draw_the_same_interior_border);
    pu_clear_setup();
    cf_destroy_canvas(canvas);
    cf_destroy_app();
}
