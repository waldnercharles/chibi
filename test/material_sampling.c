#include "platform/cute.h"
#include "render/material_raster.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    BASE_W = 65,
    BASE_H = 49,
    PHASES = 8
};

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    int mantissa = value & 1023;
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

static bool read_texture(CF_Texture texture, void *pixels, int bytes)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    if (!canvas.id) {
        return false;
    }
    CF_Readback readback = cf_canvas_readback(canvas);
    bool ok = readback.id != 0;
    if (ok) {
        uint64_t start = cf_get_ticks();
        while (!cf_readback_ready(readback) &&
               cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
            cf_sleep(1);
        }
        ok = cf_readback_ready(readback) && cf_readback_size(readback) == bytes &&
             cf_readback_data(readback, pixels, bytes) == bytes;
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

static bool draw_material_view(
    render_material_raster *raster,
    const render_material_entity *entities,
    int count,
    render_material_view view
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(raster, entities, count, view);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "Material sampling raster draw: %s\n", render_material_raster_error(raster));
    }
    return ok;
}

static bool draw_material(render_material_raster *raster, const render_material_entity *entities, int count)
{
    return draw_material_view(raster, entities, count, (render_material_view){ -32, 24, 64, 48 });
}

static bool read_emission(CF_Texture texture, uint16_t *pixels, int bytes)
{
    return read_texture(texture, pixels, bytes);
}

static bool capture_energy(
    render_material_raster *raster,
    int width,
    int height,
    float radius,
    float phase,
    float *energy
)
{
    float unit_x = 64.0f / width;
    float unit_y = 48.0f / height;
    float pixel_x = (float)(width / 2) + 0.5f + phase;
    float pixel_y = (float)(height / 2) + 0.5f;
    render_material_entity source = {
        .inv_rot = { 1, 0, 0, 1 },
        .pos = { -32.0f + pixel_x * unit_x, 24.0f - pixel_y * unit_y },
        .extent = { radius + 4.0f * unit_x, radius + 4.0f * unit_y },
        .shape_type = RENDER_SHAPE_CIRCLE,
        .radius = radius,
        .emission = { 1, 0.5f, 0.25f, 1 },
    };
    if (!draw_material(raster, &source, 1)) {
        return false;
    }

    size_t count = (size_t)width * height * 4;
    int bytes = (int)(count * sizeof(uint16_t));
    uint16_t *pixels = xmalloc(count * sizeof *pixels);
    bool ok = read_emission(render_material_raster_textures(raster).emission, pixels, bytes);
    double sum = 0;
    for (int i = 0; ok && i < width * height; i++) {
        sum += half_value(pixels[i * 4]);
    }
    free(pixels);
    *energy = (float)(sum * unit_x * unit_y);
    return ok;
}

static bool stable_tile_order(render_material_raster *raster)
{
    render_material_entity entities[] = {
        {
            .inv_rot = { 1, 0, 0, 1 },
            .pos = { 0, 0 },
            .extent = { 8, 8 },
            .shape_type = RENDER_SHAPE_CIRCLE,
            .radius = 5,
            .emission = { 1, 0, 0, 0.5f },
        },
        {
            .inv_rot = { 1, 0, 0, 1 },
            .pos = { 0, 0 },
            .extent = { 8, 8 },
            .shape_type = RENDER_SHAPE_CIRCLE,
            .radius = 5,
            .emission = { 0, 0, 1, 0.5f },
        },
    };
    if (!draw_material(raster, entities, 2)) {
        return false;
    }

    uint16_t pixels[BASE_W * BASE_H * 4];
    if (!read_emission(render_material_raster_textures(raster).emission, pixels, sizeof pixels)) {
        return false;
    }

    /* The circles overlap across the x=32 tile boundary on this odd-sized target. */
    for (int y = 23; y <= 24; y++) {
        for (int x = 31; x <= 32; x++) {
            int i = (y * BASE_W + x) * 4;
            float red = half_value(pixels[i]);
            float blue = half_value(pixels[i + 2]);
            if (red < 0.19f || red > 0.31f || blue < 0.43f || blue > 0.56f ||
                blue <= red) {
                fprintf(stderr, "Material tile order at (%d,%d): %.4f, %.4f\n", x, y, red, blue);
                return false;
            }
        }
    }
    return true;
}

bool test_material_sampling()
{
    const int widths[] = { BASE_W, BASE_W * 2 };
    const int heights[] = { BASE_H, BASE_H * 2 };
    const float radii[] = { 0.2f, 0.7f, 1.333f };
    render_material_raster *raster = render_material_raster_new();
    bool ok = raster != NULL;
    float resolution_energy[2][3] = { 0 };

    for (int size = 0; size < 2 && ok; size++) {
        ok = render_material_raster_resize(raster, widths[size], heights[size]);
        for (int radius = 0; radius < 3 && ok; radius++) {
            float samples[PHASES];
            for (int phase = 0; phase < PHASES && ok; phase++) {
                ok = capture_energy(
                    raster,
                    widths[size],
                    heights[size],
                    radii[radius],
                    (float)phase / PHASES,
                    &samples[phase]
                );
            }
            if (!ok) {
                break;
            }
            float minimum = samples[0], maximum = samples[0], mean = 0;
            for (int phase = 0; phase < PHASES; phase++) {
                minimum = fminf(minimum, samples[phase]);
                maximum = fmaxf(maximum, samples[phase]);
                mean += samples[phase] / PHASES;
            }
            resolution_energy[size][radius] = mean;
            float variation = (maximum - minimum) / fmaxf(mean, 1e-8f);
            printf(
                "material emission radius %.3f resolution %dx%d: mean %.6f, "
                "phase range %.2f%%\n",
                radii[radius],
                widths[size],
                heights[size],
                mean,
                variation * 100
            );
            float expected = 3.14159265f * radii[radius] * radii[radius];
            float energy_error = fabsf(mean - expected) / expected;
            ok &= mean > 0 && variation < 0.12f && energy_error < 0.05f;
            if (energy_error >= 0.05f) {
                fprintf(stderr, "Material emission energy radius %.3f: got %.6f, expected %.6f\n", radii[radius], mean, expected);
            }
        }
    }

    for (int radius = 0; radius < 3 && ok; radius++) {
        float a = resolution_energy[0][radius];
        float b = resolution_energy[1][radius];
        float relative = fabsf(a - b) / fmaxf(fmaxf(a, b), 1e-8f);
        printf("material emission radius %.3f resolution change: %.2f%%\n", radii[radius], relative * 100);
        ok &= relative < 0.02f;
    }
    if (ok) {
        ok = render_material_raster_resize(raster, BASE_W, BASE_H) &&
             stable_tile_order(raster);
    }
    if (raster) {
        render_material_raster_free(raster);
    }
    printf("material sampling %s: phase/resolution energy and stable tile order\n", ok ? "PASS" : "FAIL");
    return ok;
}
