#include "runtime/tilemap.h"

#include "third_party/pico_headers/pico_unit.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_tool_tilemap_native();
#endif

static tilemap_grid grid;

static void setup()
{
    memset(&grid, 0, sizeof grid);
}

static void teardown()
{
    tilemap_grid_free(&grid);
}

TEST_CASE(parse_and_cache)
{
    char cells[] = "0 1\n-1\t3";
    REQUIRE(tilemap_grid_update(&grid, cells, 2, 2, 3, 5));
    REQUIRE(tilemap_grid_frame(&grid, 0, 0) == 0);
    REQUIRE(tilemap_grid_frame(&grid, 1, 0) == 1);
    REQUIRE(tilemap_grid_frame(&grid, 0, 1) == -1);
    REQUIRE(tilemap_grid_frame(&grid, 1, 1) == 3);
    REQUIRE(tilemap_grid_frame(&grid, 2, 1) == -1);
    REQUIRE(tilemap_grid_frame(&grid, -1, 0) == -1);
    int *frames = grid.frames;
    REQUIRE(tilemap_grid_update(&grid, "0 1\n-1\t3", 2, 2, 3, 5));
    REQUIRE(grid.frames == frames);
    cells[0] = '2';
    REQUIRE(grid.cells[0] == '0');
    REQUIRE(tilemap_grid_update(&grid, cells, 2, 2, 3, 5));
    REQUIRE(tilemap_grid_frame(&grid, 0, 0) == 2);
    REQUIRE(tilemap_grid_update(&grid, grid.cells, 2, 2, 4, 6));
    REQUIRE(grid.frame_width == 4 && grid.frame_height == 6);
    REQUIRE(grid.error == NULL);
    return true;
}

TEST_CASE(invalid_input)
{
    REQUIRE(tilemap_grid_update(&grid, "7", 1, 1, 8, 8));
    const char *bad[] = { "", "-2", "1 2", "4x", "2147483648", "1.5", "0x1" };
    for (int i = 0; i < (int)(sizeof bad / sizeof *bad); i++) {
        REQUIRE(!tilemap_grid_update(&grid, bad[i], 1, 1, 8, 8));
        REQUIRE(grid.frames == NULL && grid.error != NULL);
        REQUIRE(tilemap_grid_point(&grid, (CF_V2){ 0, 0 }, 0, (CF_V2){ 0, 0 }) == -1);
    }
    REQUIRE(!tilemap_grid_update(&grid, "0", 0, 1, 8, 8));
    REQUIRE(!tilemap_grid_update(&grid, "0", 1, -1, 8, 8));
    REQUIRE(!tilemap_grid_update(&grid, "0", INT_MAX, INT_MAX, 8, 8));
    REQUIRE(!tilemap_grid_update(&grid, "0", 1, 1, 0, 8));
    REQUIRE(!tilemap_grid_update(&grid, NULL, 1, 1, 8, 8));
    REQUIRE(tilemap_grid_update(&grid, "2147483647", 1, 1, 1, 1));
    REQUIRE(tilemap_grid_frame(&grid, 0, 0) == INT_MAX);
    return true;
}

TEST_CASE(point_half_open)
{
    REQUIRE(tilemap_grid_update(&grid, "0 1 -1 3", 2, 2, 3, 5));
    CF_V2 origin = { .25f, -.5f };
    REQUIRE(tilemap_grid_point(&grid, origin, 0, origin) == 0);
    REQUIRE(tilemap_grid_point(&grid, origin, 0, (CF_V2){ 3.25f, -.5f }) == 1);
    REQUIRE(tilemap_grid_point(&grid, origin, 0, (CF_V2){ 6.25f, 0 }) == -1);
    REQUIRE(tilemap_grid_point(&grid, origin, 0, (CF_V2){ .25f, 4.5f }) == -1);
    REQUIRE(tilemap_grid_point(&grid, origin, 0, (CF_V2){ 3.25f, 4.5f }) == 3);
    REQUIRE(tilemap_grid_point(&grid, origin, 0, (CF_V2){ .249f, -.5f }) == -1);
    REQUIRE(tilemap_grid_point(&grid, origin, 0, (CF_V2){ NAN, 0 }) == -1);
    REQUIRE(tilemap_grid_point(&grid, origin, 1.57079632679f, (CF_V2){ -5.75f, 3.5f }) == 3);
    return true;
}

TEST_CASE(rectangle_overlap)
{
    REQUIRE(tilemap_grid_update(&grid, "0 1 -1 3", 2, 2, 3, 5));
    CF_V2 origin = { .25f, -.5f };
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, 0, (CF_Aabb){ { 0, -1 }, { 7, 10 } }) == 3
    );
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, 0, (CF_Aabb){ { 3.25f, 0 }, { 6.25f, 4.5f } }) == 1
    );
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, 0, (CF_Aabb){ { .25f, 4.5f }, { 3.25f, 9.5f } }) == 0
    );
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, 0, (CF_Aabb){ { 6.25f, 0 }, { 7, 4 } }) == 0
    );
    REQUIRE(tilemap_grid_overlap_count(&grid, origin, 0, (CF_Aabb){ { 1, 1 }, { 1, 3 } }) == 0);
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, 0, (CF_Aabb){ { -INFINITY, 0 }, { 7, 4 } }) == 0
    );
    /* A decorative square rotated 45 degrees: the corner of its world AABB
     * lies outside the actual diamond. No physics or game rules are involved. */
    REQUIRE(tilemap_grid_update(&grid, "2", 1, 1, 2, 2));
    origin = (CF_V2){ 0, 0 };
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, .78539816339f, (CF_Aabb){ { 1.1f, .1f }, { 1.3f, .3f } }) == 0
    );
    REQUIRE(
        tilemap_grid_overlap_count(
            &grid,
            origin,
            .78539816339f,
            (CF_Aabb){ { -.1f, 1.3f }, { .1f, 1.5f } }
        ) == 1
    );
    /* Local projection intervals intersect, but the world X axis separates. */
    REQUIRE(
        tilemap_grid_overlap_count(&grid, origin, .78539816339f, (CF_Aabb){ { 1.5f, 1 }, { 1.6f, 2 } }) == 0
    );
    return true;
}

TEST_CASE(transformed_bounds)
{
    CF_V2 min = { 0, 0 }, max = { 0, 0 };
    REQUIRE(tilemap_bounds(2, 3, 4, 5, (CF_V2){ .25f, -.5f }, 0, &min, &max));
    REQUIRE(min.x == .25f && min.y == -.5f);
    REQUIRE(max.x == 8.25f && max.y == 14.5f);
    REQUIRE(tilemap_bounds(2, 3, 4, 5, (CF_V2){ .25f, -.5f }, 1.57079632679f, &min, &max));
    REQUIRE(fabsf(min.x + 14.75f) < .00001f && fabsf(min.y + .5f) < .00001f);
    REQUIRE(fabsf(max.x - .25f) < .00001f && fabsf(max.y - 7.5f) < .00001f);
    REQUIRE(tilemap_bounds(1, 1, 2, 2, (CF_V2){ 0, 0 }, .78539816339f, &min, &max));
    REQUIRE(fabsf(min.x + 1.41421356f) < .00001f && fabsf(min.y) < .00001f);
    REQUIRE(fabsf(max.x - 1.41421356f) < .00001f && fabsf(max.y - 2.82842712f) < .00001f);
    REQUIRE(!tilemap_bounds(INT_MAX, INT_MAX, 1, 1, (CF_V2){ 0, 0 }, 0, &min, &max));
    REQUIRE(!tilemap_bounds(0, 3, 4, 5, (CF_V2){ 0, 0 }, 0, &min, &max));
    REQUIRE(!tilemap_bounds(2, 3, 4, -1, (CF_V2){ 0, 0 }, 0, &min, &max));
    REQUIRE(!tilemap_bounds(2, 3, 4, 5, (CF_V2){ INFINITY, 0 }, 0, &min, &max));
    REQUIRE(!tilemap_bounds(2, 3, 4, 5, (CF_V2){ 0, 0 }, NAN, &min, &max));
    return true;
}

void test_tool_tilemap()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(parse_and_cache);
    RUN_TEST_CASE(invalid_input);
    RUN_TEST_CASE(point_half_open);
    RUN_TEST_CASE(rectangle_overlap);
    RUN_TEST_CASE(transformed_bounds);
    pu_setup(NULL, NULL);
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    test_tool_tilemap_native();
#endif
}
