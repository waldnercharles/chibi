#include "platform/cute.h"
#include "render/material.h"
#include "render/material_raster.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    bool ok = read_emission(render_material_raster_textures(raster).source, pixels, bytes);
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
    if (!read_emission(render_material_raster_textures(raster).source, pixels, sizeof pixels)) {
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

/* A square ring two cells wide of one-unit boxes, the way material masks draw
 * their borders, turned by angle and shifted by a fraction of a pixel. */
static bool ring_energy(render_material_raster *raster, float group, float angle, float phase, float *energy)
{
    enum
    {
        SIDE = 10,
        WIDTH = 2
    };
    render_material_entity cells[SIDE * SIDE];
    int count = 0;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            if (x >= WIDTH && x < SIDE - WIDTH && y >= WIDTH && y < SIDE - WIDTH) {
                continue;
            }
            float ox = x - SIDE * .5f + .5f, oy = y - SIDE * .5f + .5f;
            float c = cosf(angle), s = sinf(angle);
            cells[count++] = (render_material_entity){
                .inv_rot = { c, s, -s, c },
                .pos = { c * ox - s * oy + phase, s * ox + c * oy + phase * .5f },
                .extent = { 2, 2 },
                .shape_type = RENDER_SHAPE_BOX,
                .points = { .5f, .5f },
                .aa = .1f,
                .emission = { 1, 1, 1, 1 },
                .tile_group = group,
            };
        }
    }
    /* One world unit per pixel. */
    if (!draw_material_view(raster, cells, count, (render_material_view){ -32, 24, BASE_W, BASE_H })) {
        return false;
    }
    uint16_t pixels[BASE_W * BASE_H * 4];
    bool ok = read_emission(render_material_raster_textures(raster).source, pixels, sizeof pixels);
    double sum = 0;
    for (int i = 0; ok && i < BASE_W * BASE_H; i++) {
        sum += half_value(pixels[i * 4]);
    }
    *energy = (float)sum;
    return ok;
}

/* Tile groups keep a moving mask's emission constant: its pieces' coverages
 * add, where layering them counted shared edges twice at some phases. */
static bool tiled_ring(render_material_raster *raster)
{
    static const char *const names[] = { "layered", "tile group", "tile group turned" };
    static const float groups[] = { 0, 1, 1 }, angles[] = { 0, 0, .3f };
    bool ok = true;
    float range[3];
    for (int ring = 0; ring < 3 && ok; ring++) {
        float minimum = INFINITY, maximum = 0;
        for (int phase = 0; phase < PHASES && ok; phase++) {
            float energy;
            ok = ring_energy(raster, groups[ring], angles[ring], (float)phase / PHASES, &energy);
            minimum = fminf(minimum, energy);
            maximum = fmaxf(maximum, energy);
        }
        range[ring] = (maximum - minimum) / fmaxf(maximum, 1e-8f);
        printf(
            "material ring %s: energy %.2f to %.2f of 64, phase range %.2f%%\n",
            names[ring],
            minimum,
            maximum,
            range[ring] * 100
        );
    }
    return ok && range[1] < .01f && range[2] < .01f;
}

/* Independent oracle: a border cell has an empty cell or the exterior within
 * reach along one of its cardinal rays. Scan all candidates, not renderer runs. */
static bool expected_border(const unsigned char *cells, int width, int height, int reach, int x, int y)
{
    if (!cells[y * width + x]) {
        return false;
    }
    if (x < reach || y < reach || width - 1 - x < reach || height - 1 - y < reach) {
        return true;
    }
    for (int other = 0; other < width; other++) {
        if (!cells[y * width + other] && abs(other - x) <= reach) {
            return true;
        }
    }
    for (int other = 0; other < height; other++) {
        if (!cells[other * width + x] && abs(other - y) <= reach) {
            return true;
        }
    }
    return false;
}

static bool border_partition(render_material_border *border, material_occupancy_view mask, int reach)
{
    render_material_border_build(border, mask, reach);
    unsigned char covered[100] = { 0 };
    for (int i = 0; i < acount(border->rects); i++) {
        render_material_border_rect r = border->rects[i];
        if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 ||
            r.x + r.width > mask.width || r.y + r.height > mask.height) {
            return false;
        }
        for (int y = r.y; y < r.y + r.height; y++) {
            for (int x = r.x; x < r.x + r.width; x++) {
                if (++covered[y * mask.width + x] != 1) {
                    return false;
                }
            }
        }
    }
    for (int y = 0; y < mask.height; y++) {
        for (int x = 0; x < mask.width; x++) {
            if (covered[y * mask.width + x] !=
                expected_border(mask.cells, mask.width, mask.height, reach, x, y)) {
                return false;
            }
        }
    }
    return true;
}

/* Visible actors reuse one scratch object despite differing masks. Warm the
 * whole workload, then require both correct geometry and no allocation calls. */
static bool border_reuse(render_material_border *border)
{
    const int widths[] = { 32, 1, 100, 8, 10 };
    const int heights[] = { 3, 100, 1, 12, 10 };
    xalloc_stats before = { 0 }, after;
    bool ok = true;
    for (int round = 0; round < 4 && ok; round++) {
        if (round == 1) {
            xalloc_stats_get(&before);
        }
        for (int size = 0; size < 5 && ok; size++) {
            unsigned char cells[100];
            for (int pattern = 0; pattern < 4 && ok; pattern++) {
                for (int y = 0; y < heights[size]; y++) {
                    for (int x = 0; x < widths[size]; x++) {
                        cells[y * widths[size] + x] = pattern == 1 ||
                                                      (pattern == 2 &&
                                                       (x + y) % 2 == 0) ||
                                                      (pattern == 3 &&
                                                       (x * 3 + y) % 5 != 0);
                    }
                }
                material_occupancy_view mask = { .cells = cells,
                                                 .width = widths[size],
                                                 .height = heights[size],
                                                 .cell_size = 1 };
                ok = border_partition(border, mask, 1) &&
                     border_partition(border, mask, 3);
                if (pattern == 0) {
                    ok &= acount(border->rects) == 0;
                }
            }
        }
    }
    xalloc_stats_get(&after);
    ok &= after.calls == before.calls && after.requested_bytes == before.requested_bytes;
    printf(
        "merged border scratch reuse %s: width changes, empty/thin masks, "
        "warmed allocations %llu\n",
        ok ? "PASS" : "FAIL",
        (unsigned long long)(after.calls - before.calls)
    );
    return ok;
}

static render_material_entity border_box(render_material_border_rect r, float angle, float phase)
{
    float x = r.x + r.width * .5f - 5, y = r.y + r.height * .5f - 5;
    float c = cosf(angle), s = sinf(angle);
    return (render_material_entity){
        .inv_rot = { c, s, -s, c },
        .pos = { c * x - s * y + phase, s * x + c * y + phase * .5f },
        .extent = { r.width * .5f + 2, r.height * .5f + 2 },
        .shape_type = RENDER_SHAPE_BOX,
        .points = { r.width * .5f, r.height * .5f },
        .aa = .1f,
        .scene = { .2f, .5f, .8f, .7f },
        .emission = { .8f, .4f, .2f, .7f },
        .absorption = { .2f, .3f, .4f, 1 },
        .albedo = { .3f, .6f, .9f, .8f },
        .reserved = { 0, 0, .4f },
        .tile_group = 1,
    };
}

static bool border_targets(
    render_material_raster *raster,
    material_occupancy_view mask,
    int reach,
    const render_material_border *border,
    float angle,
    float phase,
    float *largest_error
)
{
    uint16_t original[5][BASE_W * BASE_H * 4];
    render_material_entity entities[100];
    for (int merged = 0; merged < 2; merged++) {
        int count = 0;
        if (merged) {
            for (int i = 0; i < acount(border->rects); i++) {
                entities[count++] = border_box(border->rects[i], angle, phase);
            }
        } else {
            for (int y = 0; y < mask.height; y++) {
                for (int x = 0; x < mask.width; x++) {
                    if (!expected_border(mask.cells, mask.width, mask.height, reach, x, y)) {
                        continue;
                    }
                    int width = 1;
                    while (x + width < mask.width &&
                           expected_border(mask.cells, mask.width, mask.height, reach, x + width, y)) {
                        width++;
                    }
                    entities[count++] = border_box(
                        (render_material_border_rect){ x, y, width, 1 },
                        angle,
                        phase
                    );
                    x += width - 1;
                }
            }
        }
        if (!draw_material_view(raster, entities, count, (render_material_view){ -32, 24, BASE_W, BASE_H })) {
            return false;
        }
        render_material_textures textures = render_material_raster_textures(raster);
        CF_Texture targets[] = { textures.scene,
                                 textures.source,
                                 textures.albedo,
                                 textures.normal,
                                 textures.cover };
        for (int target = 0; target < 5; target++) {
            int values = BASE_W * BASE_H * (target == 4 ? 1 : 4);
            uint16_t pixels[BASE_W * BASE_H * 4];
            if (!read_texture(targets[target], pixels, values * (int)sizeof *pixels)) {
                return false;
            }
            if (!merged) {
                memcpy(original[target], pixels, (size_t)values * sizeof *pixels);
                continue;
            }
            for (int i = 0; i < values; i++) {
                float error = fabsf(
                    half_value(original[target][i]) - half_value(pixels[i])
                );
                *largest_error = fmaxf(*largest_error, error);
                /* Two half-precision steps at unit magnitude; only decomposition
                 * and floating accumulation order differ between the draws. */
                if (!isfinite(error) || error > .002f) {
                    fprintf(
                        stderr,
                        "Merged border target %d value %d differs by %.6f "
                        "(angle %.3f phase %.3f)\n",
                        target,
                        i,
                        error,
                        angle,
                        phase
                    );
                    return false;
                }
            }
        }
    }
    return true;
}

static bool merged_borders(render_material_raster *raster)
{
    render_material_border border = { 0 };
    bool ok = border_reuse(&border);
    float largest_error = 0;
    for (int fixture = 0; fixture < 7 && ok; fixture++) {
        unsigned char cells[100];
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                bool filled = true;
                if (fixture == 1) {
                    filled = x + y < 14 && !(x == 3 && y >= 3);
                }
                if (fixture == 2) {
                    filled = !(x >= 3 && x <= 6 && y >= 3 && y <= 6);
                }
                if (fixture == 3) {
                    filled = (x < 3 && y < 4) || (x >= 6 && y >= 5) ||
                             (x == 4 && y == 2);
                }
                int dx = 2 * x - 9, dy = 2 * y - 9;
                if (fixture == 4) {
                    filled = dx * dx + dy * dy <= 100;
                }
                if (fixture == 5) {
                    filled = dx * dx + 4 * dy * dy <= 100;
                }
                if (fixture == 6) {
                    filled = abs(dx) + abs(dy) <= 10;
                }
                cells[y * 10 + x] = filled;
            }
        }
        material_occupancy_view mask = { .cells = cells,
                                         .width = 10,
                                         .height = 10,
                                         .cell_size = 1 };
        const int reaches[] = { 1, 2, 4, 12 };
        for (int i = 0; i < 4 && ok; i++) {
            ok = border_partition(&border, mask, reaches[i]);
            if (fixture == 0 && reaches[i] == 2) {
                ok &= acount(border.rects) == 4;
            }
        }
        for (int reach = 1; reach <= 2 && ok; reach++) {
            render_material_border_build(&border, mask, reach);
            for (int rotation = 0; rotation < 2 && ok; rotation++) {
                for (int phase = 0; phase < 3 && ok; phase++) {
                    ok = border_targets(raster, mask, reach, &border, rotation * .3f, phase / 3.0f, &largest_error);
                }
            }
        }
    }
    render_material_border_free(&border);
    printf("merged material borders %s: exact partition and five targets, maximum error %.6f\n", ok ? "PASS" : "FAIL", largest_error);
    return ok;
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
             stable_tile_order(raster) && tiled_ring(raster) &&
             merged_borders(raster);
    }
    if (raster) {
        render_material_raster_free(raster);
    }
    printf("material sampling %s: phase/resolution energy, stable tile order and tiled rings\n", ok ? "PASS" : "FAIL");
    return ok;
}
