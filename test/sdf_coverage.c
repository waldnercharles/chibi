/* The engine and Cute expose ABI-compatible vector types with the same name. */
#include "platform/cute.h"
#include "render/sdf_shapes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void test_draw_set_tiled(bool enabled);
bool test_draw_tiled_available();
void test_draw_tiled_auto();
void test_draw_tiled_stats(int *tiled, int *instanced);

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

static float coverage(float distance, float edge)
{
    float t = fminf(1, fmaxf(0, distance - edge));
    return 1 - t * t * (3 - 2 * t);
}

/* Independent transcription of Neon-Swarm's entity_raster.shd, followed by
 * the signed distance to a diamond with half-size 22. */
static float source_distance(const GameSdfMotion *motion, float x, float y)
{
    float angle = atan2f(y, x);
    float frequency = roundf(motion->angular_frequency);
    float displacement = motion->amplitude *
                         sinf(angle * frequency - motion->time * motion->temporal_frequency);
    if (motion->pluck_amplitude > 0.01f) {
        float distance = angle - motion->pluck_origin;
        distance -= 6.2831853f * floorf((distance + 3.1415926f) / 6.2831853f);
        float front = motion->pluck_age * motion->pluck_speed;
        float a = distance - front, b = distance + front;
        float envelope = 0.8f + frequency * 0.1f;
        displacement += motion->pluck_amplitude *
                        (sinf(distance * frequency - front) * expf(-a * a * envelope) +
                         sinf(distance * frequency + front) * expf(-b * b * envelope));
    }
    float radial_x = x + 0.0001f, radial_y = y + 0.0001f;
    float radius = hypotf(radial_x, radial_y);
    if (radius > 0) {
        x += radial_x / radius * displacement;
        y += radial_y / radius * displacement;
    }
    x = fabsf(x);
    y = fabsf(y);
    float t = fminf(1, fmaxf(0, (22 - x + y) / 44));
    float distance = hypotf(x - 22 * (1 - t), y - 22 * t);
    return x + y < 22 ? -distance : distance;
}

static int render(
    CF_Canvas canvas,
    app_sdf_shapes shapes,
    const GameSdfMotion *motion,
    bool tiled,
    bool border,
    CF_Pixel *pixels
)
{
    cf_app_update(NULL);
    test_draw_tiled_stats(NULL, NULL);
    test_draw_set_tiled(tiled);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    /* Cancel window-density compensation: this fixed canvas has one unit per pixel. */
    cf_draw_push_shape_aa(cf_app_get_pixel_scale());
    /* Drawn geometry, rather than a clear color, detects false opaque-cover
     * claims by the warped foreground during the tiled reverse walk. */
    cf_draw_push_color(cf_make_color_rgba_f(0, 1, 0, 1));
    cf_draw_box_fill((CF_Aabb){ { -SIDE / 2, -SIDE / 2 }, { SIDE / 2, SIDE / 2 } }, 0);
    cf_draw_pop_color();
    cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, border ? 0.4f : 1));
    if (border) {
        cf_draw_push_interior_border(cf_make_color_rgba_f(0, 0, 1, 0.75f), 3);
    }
    GameAppearance appearance = { .width = 44, .height = 44, .shape = GAME_SHAPE_DIAMOND };
    /* Fractional offset places distorted tips across tile boundaries. */
    GameTransform transform = { 7.25f, -3.75f };
    bool drawn = app_sdf_shapes_draw(shapes, &appearance, &transform, 0, motion);
    if (border) {
        cf_draw_pop_outline();
    }
    cf_draw_pop_color();
    cf_draw_pop_shape_aa();
    cf_render_to(canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    int tiled_batches, instanced_batches;
    test_draw_tiled_stats(&tiled_batches, &instanced_batches);
    if (!drawn || (tiled && (tiled_batches == 0 || instanced_batches != 0)) ||
        (!tiled && (instanced_batches == 0 || tiled_batches != 0))) {
        fprintf(
            stderr,
            "SDF coverage backend failed: requested tiled=%d, actual=%d/%d\n",
            tiled,
            tiled_batches,
            instanced_batches
        );
        return 1;
    }
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return 1;
    }
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        if (cf_get_ticks() - start > cf_get_tick_frequency() * 10) {
            cf_destroy_readback(readback);
            fprintf(stderr, "SDF coverage readback timed out\n");
            return 1;
        }
        cf_sleep(1);
    }
    int size = (int)(PIXELS * sizeof *pixels);
    int copied = cf_readback_data(readback, pixels, size);
    cf_destroy_readback(readback);
    return copied != size;
}

static int check_pixels(const CF_Pixel *pixels, const GameSdfMotion *motion, bool tiled, bool border)
{
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            float distance = source_distance(
                motion,
                x + 0.5f - SIDE / 2 - 7.25f,
                SIDE / 2 - y - 0.5f - 3.75f
            );
            float fill = coverage(distance, border ? -3 : 0);
            float edge = border ? (coverage(distance, 0) - fill) * 0.75f : 0;
            float red = fill * (border ? 0.4f : 1) * (1 - edge);
            float expected[] = { red, 1 - red - edge, edge, 1 };
            CF_Pixel pixel = pixels[y * SIDE + x];
            unsigned actual[] = { pixel.colors.r,
                                  pixel.colors.g,
                                  pixel.colors.b,
                                  pixel.colors.a };
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(actual[channel] - expected[channel] * 255) > 4) {
                    fprintf(
                        stderr,
                        "SDF coverage tiled=%d border=%d phase=%g "
                        "pixel=(%d,%d) "
                        "channel=%d: got %u expected %.2f\n",
                        tiled,
                        border,
                        motion->time,
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

/* Reuse the production registration and its exact parameter packing. */
int test_sdf_coverage(app_sdf_shapes shapes)
{
    if (!shapes.id) {
        fprintf(stderr, "SDF coverage production shader unavailable\n");
        return 1;
    }
    if (app_sdf_shapes_init().id != shapes.id) {
        fprintf(stderr, "Repeated production SDF registration must reuse its resident shape\n");
        return 1;
    }
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    if (!canvas.id) {
        return 1;
    }
    cf_canvas_set_clear_color(canvas, cf_make_color_rgba_f(0, 0, 0, 0));
    CF_Pixel instanced[PIXELS], tiled[PIXELS];
    bool available = test_draw_tiled_available();
    int failed = 0;
    for (int phase = 0; phase < 3 && !failed; phase++) {
        GameSdfMotion motion = {
            .amplitude = 18,
            .angular_frequency = 17,
            .temporal_frequency = 2.3f,
            .time = phase * 0.73f,
            .pluck_amplitude = phase ? 6 : 0,
            .pluck_origin = -0.8f,
            .pluck_age = phase * 0.2f,
            .pluck_speed = 1.7f,
        };
        for (int border = 0; border < 2 && !failed; border++) {
            failed = render(canvas, shapes, &motion, false, border, instanced);
            if (!failed) {
                failed = check_pixels(instanced, &motion, false, border);
            }
            if (!available || failed) {
                continue;
            }
            failed = render(canvas, shapes, &motion, true, border, tiled);
            if (!failed) {
                failed = check_pixels(tiled, &motion, true, border);
            }
            for (int i = 0; i < PIXELS && !failed; i++) {
                for (int channel = 0; channel < 4; channel++) {
                    const unsigned char *a = (const unsigned char *)&instanced[i];
                    const unsigned char *b = (const unsigned char *)&tiled[i];
                    if (abs((int)a[channel] - b[channel]) > 3) {
                        fprintf(stderr, "SDF coverage backend mismatch at pixel %d\n", i);
                        failed = 1;
                    }
                }
            }
        }
    }
    const char *capture = getenv("CBLUEPRINT_SDF_SCREENSHOT");
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
        puts("SDF coverage tiled SKIP: compute renderer unavailable");
    }
    if (!failed) {
        puts("SDF coverage PASS: production radial/pluck shader matches CPU and both renderers");
    }
    return failed;
}
