#include "material_borders.h"

#include "render/material.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Material borders: the rectangles that cover a mask's cells near empty space,
 * their build scratch and their cache, without a GPU. */

void test_border_fixture(int fixture, unsigned char *cells)
{
    assert(fixture >= 0 && fixture < TEST_BORDER_FIXTURES);
    for (int y = 0; y < TEST_BORDER_SIDE; y++) {
        for (int x = 0; x < TEST_BORDER_SIDE; x++) {
            int dx = 2 * x - (TEST_BORDER_SIDE - 1),
                dy = 2 * y - (TEST_BORDER_SIDE - 1);
            bool filled = true;
            switch (fixture) {
                case 1: filled = x + y < 14 && !(x == 3 && y >= 3); break;
                case 2: filled = !(x >= 3 && x <= 6 && y >= 3 && y <= 6); break;
                case 3:
                    filled = (x < 3 && y < 4) || (x >= 6 && y >= 5) ||
                             (x == 4 && y == 2);
                    break;
                case 4: filled = dx * dx + dy * dy <= 100; break;
                case 5: filled = dx * dx + 4 * dy * dy <= 100; break;
                case 6: filled = abs(dx) + abs(dy) <= 10; break;
            }
            cells[y * TEST_BORDER_SIDE + x] = filled;
        }
    }
}

bool test_border_cell(material_occupancy_view mask, int reach, int x, int y)
{
    int width = mask.width, height = mask.height;
    if (!mask.cells[y * width + x]) {
        return false;
    }
    if (x < reach || y < reach || width - 1 - x < reach || height - 1 - y < reach) {
        return true;
    }
    for (int other = 0; other < width; other++) {
        if (!mask.cells[y * width + other] && abs(other - x) <= reach) {
            return true;
        }
    }
    for (int other = 0; other < height; other++) {
        if (!mask.cells[other * width + x] && abs(other - y) <= reach) {
            return true;
        }
    }
    return false;
}

/* Whether `border` lies inside `mask` and covers each of its border cells
 * once and nothing else. */
static bool covers_border(render_material_border_view border, material_occupancy_view mask, int reach)
{
    static unsigned char covered[256 * 256];
    memset(covered, 0, (size_t)mask.width * mask.height);
    for (int i = 0; i < border.count; i++) {
        render_material_border_rect r = border.rects[i];
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
            if (covered[y * mask.width + x] != test_border_cell(mask, reach, x, y)) {
                return false;
            }
        }
    }
    return true;
}

/* Builds `mask`'s border into `border` and checks what it covers. */
static bool builds_border(render_material_border *border, material_occupancy_view mask, int reach)
{
    render_material_border_build(border, mask, reach);
    return covers_border(
        (render_material_border_view){ border->rects, acount(border->rects) },
        mask,
        reach
    );
}

/* Whether `cache` gives `mask` a correct border equal to a fresh build. */
static bool caches_border(render_material_border_cache *cache, const material_mask *mask, int reach)
{
    material_occupancy_view view = material_mask_occupancy(mask);
    render_material_border_view cached = render_material_border_cache_get(cache, view, reach);
    render_material_border fresh = { 0 };
    render_material_border_build(&fresh, view, reach);
    bool same = covers_border(cached, view, reach) &&
                cached.count == acount(fresh.rects) &&
                (!cached.count || !memcmp(
                                      cached.rects,
                                      fresh.rects,
                                      (size_t)cached.count * sizeof *cached.rects
                                  ));
    render_material_border_free(&fresh);
    return same;
}

/* Whether no allocation was made since `before`. */
static bool allocated_nothing(xalloc_stats before)
{
    xalloc_stats after;
    xalloc_stats_get(&after);
    return after.calls == before.calls && after.requested_bytes == before.requested_bytes;
}

/* Every fixture's border covers exactly its cells within each reach of empty
 * space or the outside, built fresh or cached. */
TEST_CASE(borders_cover_exactly_the_cells_near_empty_space)
{
    render_material_border border = { 0 };
    render_material_border_cache *cache = render_material_border_cache_new(16, 8192);
    static const int reaches[] = { 1, 2, 4, 12 };
    for (int fixture = 0; fixture < TEST_BORDER_FIXTURES; fixture++) {
        unsigned char cells[TEST_BORDER_SIDE * TEST_BORDER_SIDE];
        test_border_fixture(fixture, cells);
        material_mask mask = material_mask_from_cells(TEST_BORDER_SIDE, TEST_BORDER_SIDE, 1, cells);
        for (int i = 0; i < 4; i++) {
            REQUIRE(builds_border(&border, material_mask_occupancy(&mask), reaches[i]));
            REQUIRE(caches_border(cache, &mask, reaches[i]));
        }
        material_mask_free(&mask);
    }
    /* A full mask's border two cells deep is its four sides. */
    unsigned char full[TEST_BORDER_SIDE * TEST_BORDER_SIDE];
    test_border_fixture(0, full);
    material_occupancy_view view = { .cells = full,
                                     .width = TEST_BORDER_SIDE,
                                     .height = TEST_BORDER_SIDE,
                                     .cell_size = 1 };
    REQUIRE(builds_border(&border, view, 2) && acount(border.rects) == 4);
    render_material_border_cache_free(cache);
    render_material_border_free(&border);
    return true;
}

/* One border scratch serves masks of changing sizes, empty, full and thin,
 * and allocates nothing once it has seen the largest. */
TEST_CASE(border_scratch_allocates_nothing_once_warm)
{
    static const int widths[] = { 32, 1, 100, 8, 10 },
                     heights[] = { 3, 100, 1, 12, 10 };
    render_material_border border = { 0 };
    xalloc_stats before = { 0 };
    for (int round = 0; round < 4; round++) {
        if (round == 1) {
            xalloc_stats_get(&before);
        }
        for (int size = 0; size < 5; size++) {
            unsigned char cells[100];
            for (int pattern = 0; pattern < 4; pattern++) {
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
                REQUIRE(builds_border(&border, mask, 1) && builds_border(&border, mask, 3));
                REQUIRE(pattern || !acount(border.rects));
            }
        }
    }
    REQUIRE(allocated_nothing(before));
    render_material_border_free(&border);
    return true;
}

/* A cached border hits without allocating, each cache keeps its own copy,
 * and carving a mask keeps its identity while changing its border. */
TEST_CASE(cached_borders_hit_without_allocating)
{
    render_material_border_cache *cache = render_material_border_cache_new(2, 4096),
                                 *other = render_material_border_cache_new(2, 4096);
    material_mask mask = material_mask_new(10, 10, 1, false);
    uint64_t original = mask.incarnation;
    REQUIRE(original && caches_border(cache, &mask, 1));
    render_material_border_cache_stats before = render_material_border_cache_get_stats(cache);
    xalloc_stats allocations;
    xalloc_stats_get(&allocations);
    int sides = 0;
    for (int i = 0; i < 100; i++) {
        sides += render_material_border_cache_get(cache, material_mask_occupancy(&mask), 1)
                     .count;
    }
    REQUIRE(allocated_nothing(allocations) && sides == 400);
    render_material_border_cache_stats after = render_material_border_cache_get_stats(cache);
    REQUIRE(after.builds == before.builds && after.hits == before.hits + 100);
    render_material_border_view
        one = render_material_border_cache_get(cache, material_mask_occupancy(&mask), 1),
        two = render_material_border_cache_get(other, material_mask_occupancy(&mask), 1);
    REQUIRE(one.rects != two.rects && one.count == two.count);
    REQUIRE(!memcmp(one.rects, two.rects, (size_t)one.count * sizeof *one.rects));
    REQUIRE(caches_border(cache, &mask, 3));
    REQUIRE(material_mask_carve(&mask, 0, 0, 2) > 0 && mask.incarnation == original);
    REQUIRE(caches_border(cache, &mask, 3));
    material_mask_free(&mask);
    render_material_border_cache_free(other);
    render_material_border_cache_free(cache);
    return true;
}

/* A new mask at the freed one's address, with its size and first revision,
 * and a mask whose revision wraps never get the old border. */
TEST_CASE(replaced_masks_never_reuse_old_borders)
{
    render_material_border_cache *cache = render_material_border_cache_new(2, 4096);
    material_mask mask = material_mask_new(10, 10, 1, false);
    uint64_t original = mask.incarnation;
    REQUIRE(caches_border(cache, &mask, 3));
    material_mask_free(&mask);
    mask = material_mask_new(10, 10, 1, true);
    REQUIRE(mask.incarnation && mask.incarnation != original);
    REQUIRE(caches_border(cache, &mask, 3));
    uint64_t replacement = mask.incarnation;
    mask.revision = UINT_MAX;
    material_mask_rebuild(&mask);
    REQUIRE(mask.revision == 1 && mask.incarnation != replacement);
    REQUIRE(caches_border(cache, &mask, 3));
    material_mask_free(&mask);
    render_material_border_cache_free(cache);
    return true;
}

/* A full cache evicts its least recent border within its entry and byte
 * limits, and clearing it empties it. */
TEST_CASE(border_cache_evicts_the_least_recent)
{
    render_material_border_cache *cache = render_material_border_cache_new(2, 4096);
    material_mask a = material_mask_new(10, 10, 1, false),
                  b = material_mask_new(10, 10, 1, true),
                  c = material_mask_new(10, 10, 1, true);
    REQUIRE(
        caches_border(cache, &a, 1) && caches_border(cache, &b, 3) &&
        caches_border(cache, &c, 1)
    );
    render_material_border_cache_stats before = render_material_border_cache_get_stats(cache);
    REQUIRE(before.entries == 2 && before.bytes <= 4096);
    REQUIRE(caches_border(cache, &a, 1));
    render_material_border_cache_stats after = render_material_border_cache_get_stats(cache);
    REQUIRE(after.builds == before.builds + 1 && after.entries == 2 && after.bytes <= 4096);
    render_material_border_cache_clear(cache);
    after = render_material_border_cache_get_stats(cache);
    REQUIRE(after.entries == 0 && after.bytes == 0 && caches_border(cache, &a, 1));
    material_mask_free(&c);
    material_mask_free(&b);
    material_mask_free(&a);
    render_material_border_cache_free(cache);
    return true;
}

/* A working set beyond the limit keeps what it cached first, and the rest
 * reuse the build scratch without allocating. */
TEST_CASE(border_cache_keeps_its_first_entries_under_pressure)
{
    render_material_border_cache *cache = render_material_border_cache_new(1, 1024);
    material_mask b = material_mask_new(10, 10, 1, false),
                  c = material_mask_new(10, 10, 1, true);
    render_material_border_cache_begin(cache);
    REQUIRE(caches_border(cache, &b, 1) && caches_border(cache, &c, 1));
    render_material_border_cache_stats before = render_material_border_cache_get_stats(cache);
    xalloc_stats allocations;
    xalloc_stats_get(&allocations);
    bool covered = true;
    for (int i = 0; i < 20; i++) {
        render_material_border_cache_begin(cache);
        material_occupancy_view hot = material_mask_occupancy(&b),
                                cold = material_mask_occupancy(&c);
        covered &= covers_border(render_material_border_cache_get(cache, hot, 1), hot, 1);
        covered &= covers_border(render_material_border_cache_get(cache, cold, 1), cold, 1);
    }
    REQUIRE(covered && allocated_nothing(allocations));
    render_material_border_cache_stats after = render_material_border_cache_get_stats(cache);
    REQUIRE(after.entries == 1 && after.bytes == before.bytes);
    REQUIRE(after.hits == before.hits + 20 && after.builds == before.builds + 20);
    /* A collection without the first mask lets the other take its place. */
    render_material_border_cache_begin(cache);
    REQUIRE(caches_border(cache, &c, 1));
    before = render_material_border_cache_get_stats(cache);
    REQUIRE(caches_border(cache, &c, 1));
    REQUIRE(render_material_border_cache_get_stats(cache).builds == before.builds);
    material_mask_free(&c);
    material_mask_free(&b);
    render_material_border_cache_free(cache);
    return true;
}

/* A border larger than the byte limit and a mask without an owner are built
 * in scratch and never retained. */
TEST_CASE(oversized_and_unowned_borders_skip_the_cache)
{
    render_material_border_cache *cache = render_material_border_cache_new(8, 1024);
    unsigned char cells[400];
    for (int i = 0; i < 400; i++) {
        cells[i] = (i / 20 + i % 20) % 2;
    }
    material_mask small = material_mask_new(10, 10, 1, false),
                  large = material_mask_from_cells(20, 20, 1, cells);
    REQUIRE(caches_border(cache, &small, 1));
    render_material_border_cache_stats before = render_material_border_cache_get_stats(cache);
    REQUIRE(caches_border(cache, &large, 1));
    render_material_border_cache_stats after = render_material_border_cache_get_stats(cache);
    REQUIRE(after.entries == before.entries && after.bytes == before.bytes && after.bytes <= 1024);
    cells[0] = 1;
    material_occupancy_view unowned = { .cells = cells,
                                        .width = 20,
                                        .height = 20,
                                        .cell_size = 1 };
    REQUIRE(covers_border(render_material_border_cache_get(cache, unowned, 1), unowned, 1));
    REQUIRE(render_material_border_cache_get_stats(cache).entries == before.entries);
    material_mask_free(&large);
    material_mask_free(&small);
    render_material_border_cache_free(cache);
    return true;
}

/* Every way of making a mask, fragments included, gives it a new identity. */
TEST_CASE(new_masks_get_their_own_identity)
{
    unsigned char cells[4] = { 1, 0, 0, 1 };
    float distance[4] = { -1, 1, 1, -1 };
    material_mask from = material_mask_from_cells(2, 2, 1, cells),
                  copy = material_mask_copy_cells(2, 2, 1, cells),
                  field = material_mask_from_field(2, 2, 1, distance);
    REQUIRE(from.incarnation && copy.incarnation && field.incarnation);
    REQUIRE(copy.incarnation != from.incarnation && field.incarnation != copy.incarnation);
    int dust = 0;
    material_mask *pieces = material_mask_extract_fragments(&from, 1, false, &dust);
    REQUIRE(acount(pieces) == 1 && pieces[0].incarnation);
    REQUIRE(pieces[0].incarnation != from.incarnation);
    material_mask_free(&pieces[0]);
    afree(pieces);
    material_mask_free(&field);
    material_mask_free(&copy);
    material_mask_free(&from);
    return true;
}

/* 20,000 masks stay cached within 16 MiB, and two more frames of them hit
 * without building or allocating. */
TEST_CASE(border_cache_holds_a_crowd)
{
    enum
    {
        COUNT = 20000
    };
    const size_t limit = 16u * 1024 * 1024;
    render_material_border_cache *cache = render_material_border_cache_new(65536, limit);
    material_mask *masks = xcalloc(COUNT, sizeof *masks);
    render_material_border_cache_begin(cache);
    bool covered = true;
    for (int i = 0; i < COUNT; i++) {
        masks[i] = material_mask_new(10, 10, 1, false);
        material_occupancy_view view = material_mask_occupancy(&masks[i]);
        render_material_border_view border = render_material_border_cache_get(cache, view, 1);
        covered &= border.count == 4 && covers_border(border, view, 1);
    }
    REQUIRE(covered);
    render_material_border_cache_stats before = render_material_border_cache_get_stats(cache);
    REQUIRE(before.entries == COUNT && before.builds == COUNT && before.bytes <= limit);
    xalloc_stats allocations;
    xalloc_stats_get(&allocations);
    int sides = 0;
    for (int frame = 0; frame < 2; frame++) {
        render_material_border_cache_begin(cache);
        for (int i = 0; i < COUNT; i++) {
            sides += render_material_border_cache_get(cache, material_mask_occupancy(&masks[i]), 1)
                         .count;
        }
    }
    REQUIRE(allocated_nothing(allocations) && sides == 2 * COUNT * 4);
    render_material_border_cache_stats after = render_material_border_cache_get_stats(cache);
    REQUIRE(after.entries == COUNT && after.bytes == before.bytes);
    REQUIRE(after.builds == before.builds && after.hits == before.hits + 2 * COUNT);
    for (int i = 0; i < COUNT; i++) {
        material_mask_free(&masks[i]);
    }
    free(masks);
    render_material_border_cache_free(cache);
    return true;
}

void test_render_material_borders()
{
    RUN_TEST_CASE(borders_cover_exactly_the_cells_near_empty_space);
    RUN_TEST_CASE(border_scratch_allocates_nothing_once_warm);
    RUN_TEST_CASE(cached_borders_hit_without_allocating);
    RUN_TEST_CASE(replaced_masks_never_reuse_old_borders);
    RUN_TEST_CASE(border_cache_evicts_the_least_recent);
    RUN_TEST_CASE(border_cache_keeps_its_first_entries_under_pressure);
    RUN_TEST_CASE(oversized_and_unowned_borders_skip_the_cache);
    RUN_TEST_CASE(new_masks_get_their_own_identity);
    RUN_TEST_CASE(border_cache_holds_a_crowd);
}
