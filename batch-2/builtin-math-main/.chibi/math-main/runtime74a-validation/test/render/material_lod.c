#include "render/material_lod.h"

#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Signed distance to a circle of radius 21 centered off the grid's center. */
static float circle(float x, float y)
{
    return hypotf(x - 3.3f, y + 1.7f) - 21;
}

static material_mask circle_mask(int side, float cell)
{
    float *field = xmalloc((size_t)side * side * sizeof *field);
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            field[y * side + x] = circle((x + .5f - side * .5f) * cell, (y + .5f - side * .5f) * cell);
        }
    }
    material_mask mask = material_mask_from_field(side, side, cell, field);
    free(field);
    return mask;
}

/* Whether a coordinate lies on a line through cell centers, where marching
 * squares places its polygons' corners. */
static bool on_centers(float value, const material_mask *mask)
{
    float index = value / mask->cell_size + mask->width * .5f - .5f;
    return fabsf(index - roundf(index)) < 1e-4f;
}

/* Masks draw at level 0 while cells span a pixel, and below that at the
 * level that keeps them between one and two, as a mipmap keeps texels, holding
 * a level across small zoom changes. */
TEST_CASE(material_lod_level_selection)
{
    const float pixel = RENDER_MATERIAL_LOD_PIXELS;
    REQUIRE(render_material_lod_pick(pixel, 0) == 0);
    REQUIRE(render_material_lod_pick(1.65f * pixel, 0) == 0);
    REQUIRE(render_material_lod_pick(20 * pixel, 0) == 0);
    REQUIRE(render_material_lod_pick(20 * pixel, 3) == 0);
    REQUIRE(render_material_lod_pick(.4f * pixel, 0) == 2);
    REQUIRE(render_material_lod_pick(.2f * pixel, 0) == 3);
    REQUIRE(render_material_lod_pick(1e-4f * pixel, 0) == RENDER_MATERIAL_LOD_LEVELS - 1);
    for (float cell = .035f * pixel; cell < .84f * pixel; cell *= 1.07f) {
        int level = render_material_lod_pick(cell, 0);
        float drawn = cell * (float)(1 << level);
        REQUIRE(drawn >= pixel && drawn < 2 * pixel);
    }
    /* Zooming out past a boundary by less than the hysteresis keeps the level. */
    REQUIRE(render_material_lod_pick(.95f * pixel, 0) == 0);
    REQUIRE(render_material_lod_pick(.8f * pixel, 0) == 1);
    REQUIRE(render_material_lod_pick(.45f * pixel, 1) == 1);
    /* Zooming back in keeps it until its cells grow past the next boundary
     * by as much. */
    REQUIRE(render_material_lod_pick(1.05f * pixel, 1) == 1);
    REQUIRE(render_material_lod_pick(1.25f * pixel, 1) == 0);

    render_material_lod *lod = render_material_lod_new();
    /* 2-unit terrain cells at a fifth of a pixel per unit span 0.4 pixels. */
    REQUIRE(render_material_lod_level(lod, 2, .2f * pixel) == 2);
    REQUIRE(render_material_lod_level(lod, .5f, .2f * pixel) == 4);
    REQUIRE(render_material_lod_level(lod, 2, pixel) == 0);
    render_material_lod_free(lod);
    return true;
}

/* A coarser level covers the same extent with cells twice the size, keeps
 * blocks that are wholly solid or wholly air, and keeps the silhouette within
 * half a cell of the finer level. */
TEST_CASE(material_lod_merged_bounds)
{
    material_mask fine = circle_mask(64, 1);
    material_mask half = material_mask_reduce(&fine);
    REQUIRE(half.width == 32 && half.height == 32 && half.cell_size == 2);
    REQUIRE(half.width * half.cell_size == fine.width * fine.cell_size);
    REQUIRE(half.revision && half.incarnation && half.incarnation != fine.incarnation);
    int solid = 0;
    for (int y = 0; y < half.height; y++) {
        for (int x = 0; x < half.width; x++) {
            const unsigned char *block = fine.cells + 2 * y * fine.width + 2 * x;
            int cells = block[0] + block[1] + block[fine.width] +
                        block[fine.width + 1];
            unsigned char merged = half.cells[y * half.width + x];
            REQUIRE(cells != 4 || merged);
            REQUIRE(cells != 0 || !merged);
            solid += merged;
        }
    }
    REQUIRE(solid == half.count);
    /* Equal area, up to the cells along the rim. */
    REQUIRE(fabsf(4.f * half.count - fine.count) < 2 * 3.1416f * 21);
    material_surface_view surface = material_mask_surface(&half);
    float limit = half.width * half.cell_size * .5f;
    for (int i = 0; i < surface.count; i++) {
        const material_polygon *polygon = &surface.polygons[i];
        for (int j = 0; j < polygon->count; j++) {
            float x = polygon->points[2 * j], y = polygon->points[2 * j + 1];
            REQUIRE(fabsf(x) <= limit && fabsf(y) <= limit);
            /* Points the silhouette interpolates between cell centers lie
             * on the circle. */
            if (on_centers(x, &half) != on_centers(y, &half)) {
                REQUIRE(fabsf(circle(x, y)) < .5f * fine.cell_size);
            }
        }
    }
    material_mask_free(&half);
    material_mask_free(&fine);
    return true;
}

/* Cutting a mask rebuilds its levels; other masks keep theirs; a mask the
 * view stops drawing releases them; odd dimensions stop at the level they
 * still halve into. */
TEST_CASE(material_lod_cached_levels)
{
    render_material_lod *lod = render_material_lod_new();
    material_mask terrain = material_mask_new(64, 64, 1, false);
    material_mask other = material_mask_new(64, 64, 1, false);
    material_mask odd = material_mask_new(12, 10, 1, false);
    render_material_lod_begin(lod);
    REQUIRE(render_material_lod_get(lod, &terrain, 0) == &terrain);
    const material_mask *coarse = render_material_lod_get(lod, &terrain, 2);
    const material_mask *kept = render_material_lod_get(lod, &other, 2);
    REQUIRE(coarse->width == 16 && coarse->cell_size == 4 && coarse->count == 256);
    REQUIRE(render_material_lod_get(lod, &terrain, 2) == coarse);
    const material_mask *half = render_material_lod_get(lod, &odd, 3);
    REQUIRE(half->width == 6 && half->height == 5 && half->cell_size == 2);

    render_material_lod_begin(lod);
    uint64_t before = coarse->incarnation;
    REQUIRE(material_mask_carve(&terrain, 0, 0, 9) > 0);
    coarse = render_material_lod_get(lod, &terrain, 2);
    REQUIRE(coarse->incarnation != before && coarse->count < 256);
    material_mask first = material_mask_reduce(&terrain);
    material_mask second = material_mask_reduce(&first);
    REQUIRE(
        second.count == coarse->count &&
        !memcmp(second.cells, coarse->cells, (size_t)coarse->width * coarse->height)
    );
    material_mask_free(&second);
    material_mask_free(&first);
    REQUIRE(render_material_lod_get(lod, &other, 2) == kept);

    /* Two frames without drawing it release the levels. */
    before = coarse->incarnation;
    render_material_lod_begin(lod);
    render_material_lod_begin(lod);
    coarse = render_material_lod_get(lod, &terrain, 2);
    REQUIRE(coarse->incarnation != before);

    render_material_lod_free(lod);
    material_mask_free(&odd);
    material_mask_free(&other);
    material_mask_free(&terrain);
    return true;
}

void test_render_material_lod()
{
    RUN_TEST_CASE(material_lod_level_selection);
    RUN_TEST_CASE(material_lod_merged_bounds);
    RUN_TEST_CASE(material_lod_cached_levels);
}
