#include "runtime/material_terrain.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static material_mask full, coarse;
static material_terrain_chunk *chunks;
static material_mask halves[2];
static void setup() {}
static void teardown()
{
    material_mask_free(&full);
    material_mask_free(&coarse);
    material_terrain_chunks_free(chunks);
    chunks = NULL;
    for (int i = 0; i < 2; i++) {
        material_mask_free(&halves[i]);
    }
}
static bool covered(const material_mask *mask, float x, float y)
{
    material_surface_view surface = material_mask_surface(mask);
    for (int i = 0; i < surface.count; i++) {
        const material_polygon *p = &surface.polygons[i];
        bool inside = false;
        for (int a = 0, b = p->count - 1; a < p->count; b = a++) {
            float ax = p->points[2 * a], ay = p->points[2 * a + 1];
            float bx = p->points[2 * b], by = p->points[2 * b + 1];
            if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) {
                inside = !inside;
            }
        }
        if (inside) {
            return true;
        }
    }
    return false;
}
static bool split_covered(float x, float y)
{
    for (int i = 0; i < acount(chunks); i++) {
        if (covered(&chunks[i].mask, x - chunks[i].x, y - chunks[i].y)) {
            return true;
        }
    }
    return false;
}
static bool collision_cover(const material_mask *mask)
{
    material_collision_view collision = material_mask_collision(mask);
    int occupied = 0;
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            float px = (x + .5f - mask->width * .5f) * mask->cell_size;
            float py = (y + .5f - mask->height * .5f) * mask->cell_size;
            int hits = 0;
            for (int i = 0; i < collision.count; i++) {
                material_rect r = collision.rects[i];
                hits += fabsf(px - r.x) < r.width * .5f &&
                        fabsf(py - r.y) < r.height * .5f;
            }
            if (hits != mask->cells[y * mask->width + x]) {
                return false;
            }
            if (material_mask_contains(mask, px, py) != (hits == 1)) {
                return false;
            }
            occupied += hits;
        }
    }
    return occupied == mask->count;
}
static void generate(bool diagonal)
{
    float field[130 * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 130; x++) {
            float wx = -165 + x + .5f, wy = -2 + y + .5f;
            field[y * 130 + x] = wx + 37 + .125f + (diagonal ? wy : 0);
        }
    }
    full = material_mask_from_field(130, 4, 1, field);
    chunks = material_terrain_chunks(field, (field_grid){ 130, 4, 1, -100, 0 });
}
static void synchronize()
{
    material_terrain_view views[2];
    for (int i = 0; i < 2; i++) {
        views[i] = (material_terrain_view){ &chunks[i].mask,
                                            chunks[i].x,
                                            chunks[i].y };
    }
    material_terrain_sync_views(views, 2, 32, 1);
}
TEST_CASE(diagonal_and_cross_seam_cut_match_continuous_mask)
{
    generate(true);
    REQUIRE(acount(chunks) == 2 && chunks[0].mask.width == 128 && chunks[1].mask.width == 2);
    // Below-hardness damage preserves signed geometry until a hit breaks cells.
    float before[130 * 4];
    memcpy(before, full.distance, sizeof before);
    unsigned unchanged = full.revision;
    REQUIRE(material_mask_apply_damage(&full, 63, 0, 1.4f, .25f, 1) == 0);
    REQUIRE(!memcmp(before, full.distance, sizeof before) && full.revision == unchanged);
    // The old independently clamped edge contours disagree at this solid point.
    REQUIRE(covered(&full, 63.01f, -.375f));
    synchronize();
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            int removed = material_mask_carve(&full, 63, 0, 1.4f);
            int split_removed = 0;
            for (int i = 0; i < 2; i++) {
                split_removed += material_mask_carve(
                    &chunks[i].mask,
                    -37 - chunks[i].x,
                    -chunks[i].y,
                    1.4f
                );
            }
            REQUIRE(removed > 0 && split_removed == removed);
            synchronize();
        }
        for (int i = 0; i < 2; i++) {
            REQUIRE(collision_cover(&chunks[i].mask));
        }
        for (int y = 0; y < 31; y++) {
            for (int side = 0; side < 2; side++) {
                float wx = -37 + (side ? .013f : -.013f), wy = -1.97f + y * .127f;
                bool split = split_covered(wx, wy),
                     continuous = covered(&full, wx + 100, wy);
                if (split != continuous) {
                    fprintf(stderr, "terrain seam pass=%d x=%.9g y=%.9g split=%d continuous=%d\n", pass, wx, wy, split, continuous);
                }
                REQUIRE(split == continuous);
            }
        }
        unsigned revision = chunks[0].mask.revision;
        synchronize();
        REQUIRE(chunks[0].mask.revision == revision);
    }
    REQUIRE(!split_covered(-37.013f, .013f));
    return true;
}
TEST_CASE(reduced_halos_preserve_partial_chunk_contours)
{
    generate(true);
    synchronize();
    coarse = material_mask_reduce(&full);
    for (int i = 0; i < 2; i++) {
        halves[i] = material_mask_reduce(&chunks[i].mask);
        REQUIRE(halves[i].halo_radius == 16 && halves[i].lod_limit == 0);
        REQUIRE(collision_cover(&halves[i]));
    }
    for (int y = 0; y < 15; y++) {
        for (int side = 0; side < 2; side++) {
            float wx = -37 + (side ? .013f : -.013f), wy = -1.91f + y * .263f;
            bool split = false;
            for (int i = 0; i < 2; i++) {
                split |= covered(&halves[i], wx - chunks[i].x, wy - chunks[i].y);
            }
            REQUIRE(split == covered(&coarse, wx + 100, wy));
        }
    }
    return true;
}
TEST_CASE(known_empty_neighbor_is_not_missing_or_solid)
{
    generate(false);
    REQUIRE(acount(chunks) == 2 && chunks[1].mask.count == 0);
    synchronize();
    bool occupied;
    float distance;
    REQUIRE(material_mask_sample(&chunks[0].mask, 128, 1, &occupied, &distance));
    REQUIRE(!occupied && distance > 0);
    REQUIRE(!material_mask_sample(&chunks[0].mask, -1, 1, &occupied, &distance));
    REQUIRE(!split_covered(-37.013f, .013f));
    REQUIRE(split_covered(-37.3f, .013f));
    // Finite-world corners preserve the neighboring X sample when Y is outside.
    REQUIRE(!split_covered(-37.013f, -1.99f));
    REQUIRE(split_covered(-37.3f, -1.99f));
    halves[0] = material_mask_reduce(&chunks[0].mask);
    halves[1] = material_mask_reduce(&chunks[1].mask);
    REQUIRE(halves[1].count == 0 && collision_cover(&halves[1]));
    REQUIRE(material_mask_sample(&halves[0], 64, 0, &occupied, &distance));
    REQUIRE(!occupied && distance > 0);
    material_surface_view surface = material_mask_surface(&chunks[0].mask);
    for (int i = 0; i < surface.count; i++) {
        const material_polygon *p = &surface.polygons[i];
        REQUIRE(p->count >= 3 && p->count <= 6);
        for (int j = 0; j < p->count; j++) {
            REQUIRE(fabsf(p->points[2 * j]) <= chunks[0].mask.width * .5f);
            REQUIRE(fabsf(p->points[2 * j + 1]) <= chunks[0].mask.height * .5f);
        }
    }
    return true;
}
void test_tool_terrain_seams()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(diagonal_and_cross_seam_cut_match_continuous_mask);
    RUN_TEST_CASE(reduced_halos_preserve_partial_chunk_contours);
    RUN_TEST_CASE(known_empty_neighbor_is_not_missing_or_solid);
    pu_setup(NULL, NULL);
}
