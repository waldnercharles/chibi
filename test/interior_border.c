#include "platform/cute.h"

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
    PIXELS = SIDE * SIDE
};

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

static int render(CF_Canvas canvas, bool tiled, bool diamond, float alpha, CF_Pixel *pixels)
{
    cf_app_update(NULL);
    test_draw_tiled_stats(NULL, NULL);
    test_draw_set_tiled(tiled);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    /* Cancel window-density compensation: this fixed canvas has one unit per pixel. */
    cf_draw_push_shape_aa(cf_app_get_pixel_scale());
    cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, alpha));
    cf_draw_push_interior_border(cf_make_color_rgba_f(0, 0, 1, 0.75f), 3);
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
    if ((tiled && (tiled_batches == 0 || instanced_batches != 0)) ||
        (!tiled && (instanced_batches == 0 || tiled_batches != 0))) {
        fprintf(
            stderr,
            "interior border wrong backend: requested %s, tiled=%d "
            "instanced=%d\n",
            tiled ? "tiled" : "instanced",
            tiled_batches,
            instanced_batches
        );
        return 1;
    }
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return 1;
    }
    int size = (int)(PIXELS * sizeof *pixels);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        if (cf_get_ticks() - start > cf_get_tick_frequency() * 10) {
            cf_destroy_readback(readback);
            fprintf(stderr, "interior border readback timed out\n");
            return 1;
        }
        cf_sleep(1);
    }
    int copied = cf_readback_data(readback, pixels, size);
    cf_destroy_readback(readback);
    return copied != size;
}

static int check_pixels(const CF_Pixel *pixels, bool tiled, bool diamond, float alpha)
{
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            float distance = shape_distance(diamond, x + 0.5f - SIDE / 2, y + 0.5f - SIDE / 2);
            float fill = coverage(distance, -3);
            float border = (coverage(distance, 0) - fill) * 0.75f;
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
                        "interior border %s %s alpha=%g pixel=(%d,%d) "
                        "channel=%d: got %u expected %.2f\n",
                        tiled ? "tiled" : "instanced",
                        diamond ? "diamond" : "circle",
                        alpha,
                        x,
                        y,
                        channel,
                        actual[channel],
                        expected[channel] * 255
                    );
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Call after cf_make_app, before the host queues its first draw commands. */
int test_interior_border()
{
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    if (!canvas.id) {
        return 1;
    }
    cf_canvas_set_clear_color(canvas, cf_make_color_rgba_f(0, 0, 0, 0));
    CF_Pixel instanced[PIXELS], tiled[PIXELS];
    bool available = test_draw_tiled_available();
    int failed = 0;
    for (int shape = 0; shape < 2; shape++) {
        for (int translucent = 0; translucent < 2; translucent++) {
            float alpha = translucent ? 0.4f : 1;
            failed |= render(canvas, false, shape, alpha, instanced);
            if (!failed) {
                failed |= check_pixels(instanced, false, shape, alpha);
            }
            if (!available) {
                continue;
            }
            failed |= render(canvas, true, shape, alpha, tiled);
            if (!failed) {
                failed |= check_pixels(tiled, true, shape, alpha);
                for (int i = 0; i < PIXELS; i++) {
                    for (int channel = 0; channel < 4; channel++) {
                        const unsigned char *a = (const unsigned char *)&instanced[i];
                        const unsigned char *b = (const unsigned char *)&tiled[i];
                        if (abs((int)a[channel] - b[channel]) > 2) {
                            fprintf(stderr, "interior border backend mismatch at pixel %d\n", i);
                            failed = 1;
                        }
                    }
                }
            }
        }
    }
    const char *capture = getenv("CHIBI_BORDER_SCREENSHOT");
    if (capture && !failed) {
        CF_Image image = { SIDE, SIDE, available ? tiled : instanced };
        void *data = NULL;
        int size = 0;
        CF_Result result = cf_image_save_png_to_memory(&image, &data, &size);
        if (cf_is_error(result)) {
            failed = 1;
        } else {
            FILE *file = fopen(capture, "wb");
            if (!file) {
                failed = 1;
            } else {
                failed |= fwrite(data, 1, (size_t)size, file) != (size_t)size;
                fclose(file);
            }
        }
        cf_free(data);
    }
    test_draw_tiled_auto();
    cf_destroy_canvas(canvas);
    if (!available) {
        puts("interior border tiled SKIP: compute renderer unavailable");
    }
    if (!failed) {
        puts("interior border PASS: GPU circle/diamond coverage and translucent compositing");
    }
    return failed;
}
