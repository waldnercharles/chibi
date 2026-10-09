/* The engine and Cute expose ABI-compatible vector types with the same name. */
#include "platform/cute.h"
#include "render/sdf_shapes.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
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
    PIXELS = SIDE * SIDE,
    HALF = 36
};

/* Independent transcription of Neon-Swarm's entity_raster.shd, followed by
 * the signed distance to a diamond with half-size HALF. */
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
    float t = fminf(1, fmaxf(0, (HALF - x + y) / (2.0f * HALF)));
    float distance = hypotf(x - HALF * (1 - t), y - HALF * t);
    return x + y < HALF ? -distance : distance;
}

/* Cute antialiases a custom field from its local slope, d / |grad d|, so the
 * edge and border keep their widths however the warp stretches the field. */
static float field_distance(const GameSdfMotion *motion, float x, float y)
{
    const float h = 0.5f;
    float gx = source_distance(motion, x + h, y) - source_distance(motion, x - h, y);
    float gy = source_distance(motion, x, y + h) - source_distance(motion, x, y - h);
    float slope = hypotf(gx, gy) / (2 * h);
    return source_distance(motion, x, y) / fmaxf(slope, 1e-6f);
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
    GameAppearance appearance = { .width = 2 * HALF,
                                  .height = 2 * HALF,
                                  .shape = GAME_SHAPE_DIAMOND };
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

static bool matches(CF_Pixel pixel, const int expected[4])
{
    int actual[] = { pixel.colors.r,
                     pixel.colors.g,
                     pixel.colors.b,
                     pixel.colors.a };
    for (int channel = 0; channel < 4; channel++) {
        if (abs(actual[channel] - expected[channel]) > 4) {
            return false;
        }
    }
    return true;
}

/* Every pixel whose 3x3 neighborhood lies more than two pixels, to first
 * order, from the field's zero crossing must be untouched background outside
 * and fill inside (inside the border band when bordered). The GPU antialiases
 * from field differences across neighboring pixels, so a pixel next to the
 * edge, or in the warp's unresolved center, may blend. */
static int check_pixels(const CF_Pixel *pixels, const GameSdfMotion *motion, bool tiled, bool border)
{
    const float edge = 2, band = border ? 3 : 0;
    const int background[] = { 0, 255, 0, 255 };
    /* Red at 40% over the green background when bordered. */
    const int fill[] = { border ? 102 : 255, border ? 153 : 0, 0, 255 };
    float distances[PIXELS];
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            distances[y * SIDE + x] = field_distance(
                motion,
                x + 0.5f - SIDE / 2 - 7.25f,
                SIDE / 2 - y - 0.5f - 3.75f
            );
        }
    }
    int erased = 0, missing = 0, outside = 0, inside = 0;
    for (int y = 1; y < SIDE - 1; y++) {
        for (int x = 1; x < SIDE - 1; x++) {
            float nearest = INFINITY, farthest = -INFINITY;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    float distance = distances[(y + dy) * SIDE + x + dx];
                    nearest = fminf(nearest, distance);
                    farthest = fmaxf(farthest, distance);
                }
            }
            CF_Pixel pixel = pixels[y * SIDE + x];
            if (nearest > edge) {
                outside++;
                erased += !matches(pixel, background);
            } else if (farthest < -band - edge) {
                inside++;
                missing += !matches(pixel, fill);
            }
        }
    }
    if (erased || missing || !outside || !inside) {
        fprintf(
            stderr,
            "SDF coverage tiled=%d border=%d phase=%g: %d of %d background "
            "pixels erased, %d of %d inside pixels missing\n",
            tiled,
            border,
            motion->time,
            erased,
            outside,
            missing,
            inside
        );
        return 1;
    }
    return 0;
}

/* Reuse the production registration and its exact parameter packing. */
static int sdf_coverage(app_sdf_shapes shapes)
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
    CF_Pixel pixels[PIXELS];
    int failed = 0;
    for (int phase = 0; phase < 3 && !failed; phase++) {
        /* A warp the pixel grid resolves everywhere but at its center, so the
         * CPU field's first-order distances hold. */
        GameSdfMotion motion = {
            .amplitude = 6,
            .angular_frequency = 7,
            .temporal_frequency = 2.3f,
            .time = phase * 0.73f,
            .pluck_amplitude = phase ? 4 : 0,
            .pluck_origin = -0.8f,
            .pluck_age = phase * 0.2f,
            .pluck_speed = 1.7f,
        };
        for (int border = 0; border < 2 && !failed; border++) {
            for (int tiled = 0; tiled < 2 && !failed; tiled++) {
                failed = render(canvas, shapes, &motion, tiled, border, pixels);
                if (!failed) {
                    failed = check_pixels(pixels, &motion, tiled, border);
                }
            }
        }
    }
    const char *capture = getenv("CHIBI_SDF_SCREENSHOT");
    if (capture && !failed) {
        CF_Image image = { SIDE, SIDE, pixels };
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
    if (!failed) {
        puts("SDF coverage PASS: production radial/pluck shader matches the CPU field in both renderers");
    }
    return failed;
}

TEST_CASE(custom_shapes_match_the_cpu_reference)
{
    /* Only GLES lacks Cute's compute renderer, and this app never selects it. */
    REQUIRE(test_draw_tiled_available());
    REQUIRE(!sdf_coverage(app_sdf_shapes_init()));
    return true;
}

void test_render_sdf_coverage()
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
        "sdf-coverage",
        0,
        0,
        0,
        SIDE,
        SIDE,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        NULL
    );
    assert(!cf_is_error(made));
    RUN_TEST_CASE(custom_shapes_match_the_cpu_reference);
    cf_destroy_app();
}
