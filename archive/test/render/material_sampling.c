#include "hidden_app.h"
#include "material_borders.h"
#include "render/material.h"
#include "render/material_raster.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

/* How the material raster samples shapes, in a hidden app: emission that keeps
 * its energy wherever a shape lands on the pixel grid, the draw order of
 * overlapping tiles, tile groups, and merged borders that draw as the cells
 * they merge. */
enum
{
    /* Odd, so the center falls inside a tile rather than on its edge. */
    WIDTH = 65,
    HEIGHT = 49,
    PIXELS = WIDTH * HEIGHT,
    /* Sub-pixel offsets each shape is drawn at. */
    PHASES = 8
};

static render_material_raster *raster;
/* The largest target, at twice the base size. */
static uint16_t halves[PIXELS * 4 * 4];

static void setup()
{
    raster = render_material_raster_new();
    assert(raster);
    assert(render_material_raster_resize(raster, WIDTH, HEIGHT));
}

static void teardown()
{
    render_material_raster_free(raster);
}

/* Draws `count` entities into the raster in `view`. */
static bool draw(const render_material_entity *entities, int count, render_material_view view)
{
    cf_app_update(NULL);
    bool drawn = render_material_raster_draw(raster, entities, count, view);
    cf_app_draw_onto_screen(false);
    return drawn;
}

/* 64 x 48 world units, over a target of any size. */
static const render_material_view WORLD = { .min_x = -32,
                                            .max_y = 24,
                                            .width = 64,
                                            .height = 48 };
/* One world unit per pixel of the base size. */
static const render_material_view UNIT = { .min_x = -32,
                                           .max_y = 24,
                                           .width = WIDTH,
                                           .height = HEIGHT };

/* The red emission over a width x height target drawn in `view`, in world
 * units. */
static bool red_energy(render_material_view view, int width, int height, float *energy)
{
    REQUIRE(
        test_read_texture(render_material_raster_textures(raster).source, halves, width * height)
    );
    double sum = 0;
    for (int i = 0; i < width * height; i++) {
        sum += test_half(halves[i * 4]);
    }
    *energy = (float)(sum * view.width / width * view.height / height);
    return true;
}

/* The mean and the spread over PHASES of the red energy of a circle of
 * `radius` moved right by a fraction of a pixel of a width x height target. */
static bool circle_energy(int width, int height, float radius, float *mean, float *spread)
{
    float unit[2] = { WORLD.width / width, WORLD.height / height },
          low = INFINITY, high = 0;
    *mean = 0;
    for (int phase = 0; phase < PHASES; phase++) {
        float x = width / 2 + .5f + (float)phase / PHASES, y = height / 2 + .5f;
        render_material_entity circle = {
            .inv_rot = { 1, 0, 0, 1 },
            .pos = { WORLD.min_x + x * unit[0], WORLD.max_y - y * unit[1] },
            .extent = { radius + 4 * unit[0], radius + 4 * unit[1] },
            .shape_type = RENDER_SHAPE_CIRCLE,
            .radius = radius,
            .emission = { 1, .5f, .25f, 1 },
        };
        float energy;
        REQUIRE(draw(&circle, 1, WORLD) && red_energy(WORLD, width, height, &energy));
        low = fminf(low, energy);
        high = fmaxf(high, energy);
        *mean += energy / PHASES;
    }
    *spread = (high - low) / *mean;
    return true;
}

/* A small circle emits its area, within 5%, wherever it lands inside a pixel,
 * within 12%, and the same at twice the resolution, within 2%. */
TEST_CASE(emission_keeps_its_energy_on_the_pixel_grid)
{
    static const float radii[] = { .2f, .7f, 1.333f };
    float means[2][3];
    for (int scale = 1; scale <= 2; scale++) {
        REQUIRE(render_material_raster_resize(raster, WIDTH * scale, HEIGHT * scale));
        for (int r = 0; r < 3; r++) {
            float spread, area = 3.14159265f * radii[r] * radii[r];
            REQUIRE(
                circle_energy(WIDTH * scale, HEIGHT * scale, radii[r], &means[scale - 1][r], &spread)
            );
            REQUIRE(fabsf(means[scale - 1][r] - area) < .05f * area && spread < .12f);
        }
    }
    for (int r = 0; r < 3; r++) {
        REQUIRE(fabsf(means[0][r] - means[1][r]) < .02f * fmaxf(means[0][r], means[1][r]));
    }
    return true;
}

/* Two half-covering circles across a tile edge blend in draw order: the blue
 * one drawn second stays on top on both sides of the edge. */
TEST_CASE(overlapping_tiles_keep_their_draw_order)
{
    render_material_entity under = {
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 8, 8 },
        .shape_type = RENDER_SHAPE_CIRCLE,
        .radius = 5,
        .emission = { 1, 0, 0, .5f },
    };
    render_material_entity over = under;
    over.emission[0] = 0;
    over.emission[2] = 1;
    REQUIRE(draw((render_material_entity[]){ under, over }, 2, WORLD));
    REQUIRE(test_read_texture(render_material_raster_textures(raster).source, halves, PIXELS));
    /* The tiles meet between x 31 and 32. */
    for (int y = 23; y <= 24; y++) {
        for (int x = 31; x <= 32; x++) {
            float red = test_half(halves[4 * (y * WIDTH + x)]),
                  blue = test_half(halves[4 * (y * WIDTH + x) + 2]);
            REQUIRE(red > .19f && red < .31f && blue > .43f && blue < .56f);
        }
    }
    return true;
}

/* The phase spread of the emission of a square ring of unit boxes two cells
 * wide in one tile group, the way material masks draw their borders, turned by
 * `angle`. */
static bool ring_spread(float angle, float *spread)
{
    enum
    {
        SIDE = 10,
        DEPTH = 2
    };
    float low = INFINITY, high = 0;
    for (int phase = 0; phase < PHASES; phase++) {
        float shift = (float)phase / PHASES, c = cosf(angle), s = sinf(angle);
        render_material_entity cells[SIDE * SIDE];
        int count = 0;
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE; x++) {
                if (x >= DEPTH && x < SIDE - DEPTH && y >= DEPTH && y < SIDE - DEPTH) {
                    continue;
                }
                float ox = x - SIDE * .5f + .5f, oy = y - SIDE * .5f + .5f;
                cells[count++] = (render_material_entity){
                    .inv_rot = { c, s, -s, c },
                    .pos = { c * ox - s * oy + shift, s * ox + c * oy + shift * .5f },
                    .extent = { 2, 2 },
                    .shape_type = RENDER_SHAPE_BOX,
                    .points = { .5f, .5f },
                    .aa = .1f,
                    .emission = { 1, 1, 1, 1 },
                    .tile_group = 1,
                };
            }
        }
        float energy;
        REQUIRE(draw(cells, count, UNIT) && red_energy(UNIT, WIDTH, HEIGHT, &energy));
        low = fminf(low, energy);
        high = fmaxf(high, energy);
    }
    *spread = (high - low) / high;
    return true;
}

/* A moving ring of boxes in one tile group keeps its emission within 1%,
 * straight or turned: their coverages add, where layering them counts their
 * shared edges twice at some phases. */
TEST_CASE(tile_groups_keep_moving_masks_even)
{
    float straight, turned;
    REQUIRE(ring_spread(0, &straight) && ring_spread(.3f, &turned));
    REQUIRE(straight < .01f && turned < .01f);
    return true;
}

/* A box of `rect`, in cells of a fixture centered at the origin, turned by
 * `angle` and moved by `shift` of a pixel, with every channel set. */
static render_material_entity border_box(render_material_border_rect rect, float angle, float shift)
{
    float x = rect.x + rect.width * .5f - TEST_BORDER_SIDE * .5f,
          y = rect.y + rect.height * .5f - TEST_BORDER_SIDE * .5f;
    float c = cosf(angle), s = sinf(angle);
    return (render_material_entity){
        .inv_rot = { c, s, -s, c },
        .pos = { c * x - s * y + shift, s * x + c * y + shift * .5f },
        .extent = { rect.width * .5f + 2, rect.height * .5f + 2 },
        .shape_type = RENDER_SHAPE_BOX,
        .points = { rect.width * .5f, rect.height * .5f },
        .aa = .1f,
        .scene = { .2f + shift * .1f, .5f, .8f - shift * .1f, .7f },
        .emission = { .8f, .4f + shift * .1f, .2f, .7f },
        .absorption = { .2f, .3f, .4f, 1 },
        .albedo = { .3f, .6f, .9f, .8f },
        .reserved = { 0, 0, .4f },
        .tile_group = 1,
    };
}

/* Boxes for the border cells of `mask`, one per run of a row, turned by
 * `angle` and moved by `shift`, into `boxes`. Returns their count. */
static int cell_boxes(material_occupancy_view mask, int reach, float angle, float shift, render_material_entity *boxes)
{
    int count = 0;
    for (int y = 0; y < mask.height; y++) {
        for (int x = 0; x < mask.width; x++) {
            int run = 0;
            while (x + run < mask.width && test_border_cell(mask, reach, x + run, y)) {
                run++;
            }
            if (run) {
                boxes[count++] = border_box((render_material_border_rect){ x, y, run, 1 }, angle, shift);
                x += run - 1;
            }
        }
    }
    return count;
}

/* Draws `count` boxes and reads the raster's five targets into `targets`. */
static bool draw_targets(
    const render_material_entity *boxes,
    int count,
    uint16_t (*targets)[PIXELS * 4]
)
{
    REQUIRE(draw(boxes, count, UNIT));
    render_material_textures t = render_material_raster_textures(raster);
    CF_Texture textures[] = { t.scene, t.source, t.albedo, t.normal, t.cover };
    for (int i = 0; i < 5; i++) {
        REQUIRE(test_read_texture(textures[i], targets[i], PIXELS));
    }
    return true;
}

/* Every fixture's cached border, merged into rectangles, writes the same five
 * targets as its cells drawn row by row, to two half steps at unit scale, at
 * two reaches, two angles and three sub-pixel shifts. */
TEST_CASE(merged_borders_draw_as_their_cells_do)
{
    static uint16_t by_cell[5][PIXELS * 4], by_rect[5][PIXELS * 4];
    render_material_entity boxes[TEST_BORDER_SIDE * TEST_BORDER_SIDE];
    render_material_border_cache *cache = render_material_border_cache_new(16, 8192);
    float worst = 0;
    bool finite = true;
    for (int fixture = 0; fixture < TEST_BORDER_FIXTURES; fixture++) {
        unsigned char fill[TEST_BORDER_SIDE * TEST_BORDER_SIDE];
        test_border_fixture(fixture, fill);
        material_mask owned = material_mask_from_cells(TEST_BORDER_SIDE, TEST_BORDER_SIDE, 1, fill);
        material_occupancy_view mask = material_mask_occupancy(&owned);
        for (int reach = 1; reach <= 2; reach++) {
            for (int turn = 0; turn < 2; turn++) {
                for (int shift = 0; shift < 3; shift++) {
                    float angle = turn * .3f, offset = shift / 3.f;
                    int count = cell_boxes(mask, reach, angle, offset, boxes);
                    REQUIRE(draw_targets(boxes, count, by_cell));
                    render_material_border_view
                        border = render_material_border_cache_get(cache, mask, reach);
                    for (int i = 0; i < border.count; i++) {
                        boxes[i] = border_box(border.rects[i], angle, offset);
                    }
                    REQUIRE(draw_targets(boxes, border.count, by_rect));
                    for (int t = 0; t < 5; t++) {
                        for (int i = 0; i < PIXELS * 4; i++) {
                            float error = fabsf(
                                test_half(by_cell[t][i]) - test_half(by_rect[t][i])
                            );
                            /* fmaxf drops NaN, so check it apart. */
                            finite &= isfinite(error);
                            worst = fmaxf(worst, error);
                        }
                    }
                }
            }
        }
        material_mask_free(&owned);
    }
    render_material_border_cache_free(cache);
    REQUIRE(finite && worst <= .002f);
    return true;
}

void test_render_material_sampling()
{
    test_open_hidden_app("material sampling", WIDTH, HEIGHT);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(emission_keeps_its_energy_on_the_pixel_grid);
    RUN_TEST_CASE(overlapping_tiles_keep_their_draw_order);
    RUN_TEST_CASE(tile_groups_keep_moving_masks_even);
    RUN_TEST_CASE(merged_borders_draw_as_their_cells_do);
    pu_clear_setup();
    cf_destroy_app();
}
