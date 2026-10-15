#include "render/appearance_geometry.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void expect_bounds(Appearance appearance, float left, float bottom, float right, float top)
{
    CF_V2 min, max;
    assert(appearance_bounds(&appearance, &min, &max));
    assert(fabsf(min.x - left) < 0.0001f && fabsf(min.y - bottom) < 0.0001f);
    assert(fabsf(max.x - right) < 0.0001f && fabsf(max.y - top) < 0.0001f);
}

static void stroked_player()
{
    Appearance appearance = { .width = 20,
                              .height = 18,
                              .shape = SHAPE_STROKED_CHEVRON,
                              .stroke_width = 1 };
    expect_bounds(appearance, -4.5f, -9.5f, 16.5f, 9.5f);
    CF_V2 points[4];
    assert(appearance_points(&appearance, points) == 4);
    const CF_V2 expected[] = { { 16, 0 }, { -4, -9 }, { 0, 0 }, { -4, 9 } };
    for (int i = 0; i < 4; i++) {
        assert(points[i].x == expected[i].x && points[i].y == expected[i].y);
        assert(appearance_contains(&appearance, points[i]));
    }
    assert(!appearance_contains(&appearance, (CF_V2){ 4, 0 }));
    assert(appearance_contains(&appearance, (CF_V2){ 6, 4.5f }));
    assert(!appearance_contains(&appearance, (CF_V2){ 6, 6 }));
    assert(appearance_contains(&appearance, (CF_V2){ 16.49f, 0 }));
    assert(!appearance_contains(&appearance, (CF_V2){ 16.51f, 0 }));
    appearance.border_width = 100;
    appearance.glow_radius = 200;
    expect_bounds(appearance, -4.5f, -9.5f, 16.5f, 9.5f);
    assert(!appearance_contains(&appearance, (CF_V2){ 30, 0 }));
}

static void capsules()
{
    Appearance appearance = { .width = 18, .height = 6, .shape = SHAPE_CAPSULE };
    expect_bounds(appearance, -9, -3, 9, 3);
    assert(appearance_contains(&appearance, (CF_V2){ 9, 0 }));
    assert(appearance_contains(&appearance, (CF_V2){ -9, 0 }));
    assert(appearance_contains(&appearance, (CF_V2){ 0, 3 }));
    assert(appearance_contains(&appearance, (CF_V2){ 7.8f, 2.3f }));
    assert(!appearance_contains(&appearance, (CF_V2){ 9, 3 }));
    assert(!appearance_contains(&appearance, (CF_V2){ 9.01f, 0 }));
    CF_V2 points[4];
    assert(appearance_points(&appearance, points) == 0);
    appearance.width = 6;
    appearance.height = 18;
    expect_bounds(appearance, -3, -9, 3, 9);
    assert(appearance_contains(&appearance, (CF_V2){ 0, 9 }));
    assert(appearance_contains(&appearance, (CF_V2){ 2.3f, 7.8f }));
    assert(!appearance_contains(&appearance, (CF_V2){ 3, 9 }));
    appearance.height = 6;
    expect_bounds(appearance, -3, -3, 3, 3);
    assert(appearance_contains(&appearance, (CF_V2){ 3, 0 }));
    assert(!appearance_contains(&appearance, (CF_V2){ 3, 3 }));
}

static void legacy_shapes()
{
    Appearance appearance = { .width = 20, .height = 10, .shape = SHAPE_BOX };
    expect_bounds(appearance, -10, -5, 10, 5);
    assert(appearance_contains(&appearance, (CF_V2){ 10, 5 }));
    assert(!appearance_contains(&appearance, (CF_V2){ 10.01f, 0 }));
    appearance.shape = SHAPE_DIAMOND;
    expect_bounds(appearance, -10, -5, 10, 5);
    assert(appearance_contains(&appearance, (CF_V2){ 10, 0 }));
    assert(appearance_contains(&appearance, (CF_V2){ 5, 2.5f }));
    assert(!appearance_contains(&appearance, (CF_V2){ 8, 4 }));
    appearance.shape = SHAPE_CHEVRON;
    assert(appearance_contains(&appearance, (CF_V2){ 0, 0 }));
    assert(!appearance_contains(&appearance, (CF_V2){ -9, 0 }));
    appearance.shape = SHAPE_CIRCLE;
    expect_bounds(appearance, -5, -5, 5, 5);
    assert(appearance_contains(&appearance, (CF_V2){ 5, 0 }));
    assert(!appearance_contains(&appearance, (CF_V2){ 6, 0 }));
    assert(!appearance_contains(&appearance, (CF_V2){ 4, 4 }));
    appearance.shape = 12345;
    expect_bounds(appearance, -10, -5, 10, 5);
    assert(appearance_contains(&appearance, (CF_V2){ 10, 5 }));
}

static void invalid_geometry()
{
    Appearance base = { .width = 20,
                        .height = 18,
                        .shape = SHAPE_STROKED_CHEVRON,
                        .stroke_width = 1 };
    Appearance invalid[] = { base, base, base, base, base, base };
    invalid[0].width = NAN;
    invalid[1].height = INFINITY;
    invalid[2].width = 0;
    invalid[3].height = -1;
    invalid[4].stroke_width = 0;
    invalid[5].stroke_width = NAN;
    for (int i = 0; i < 6; i++) {
        CF_V2 min, max, points[4];
        assert(!appearance_bounds(&invalid[i], &min, &max));
        assert(!appearance_contains(&invalid[i], (CF_V2){ 0 }));
        assert(appearance_points(&invalid[i], points) == 0);
    }
    assert(!appearance_contains(&base, (CF_V2){ NAN, 0 }));
    assert(!appearance_contains(&base, (CF_V2){ 0, INFINITY }));
}

static void rotated_geometry()
{
    const float quarter_turn = 1.5707963267948966f;
    const float diagonal = 0.7853981633974483f;
    Appearance appearance = { .width = 20,
                              .height = 18,
                              .shape = SHAPE_STROKED_CHEVRON,
                              .stroke_width = 1 };
    CF_V2 min, max;
    assert(appearance_rotated_bounds(&appearance, quarter_turn, &min, &max));
    assert(fabsf(min.x + 9.5f) < 0.0001f && fabsf(min.y + 4.5f) < 0.0001f);
    assert(fabsf(max.x - 9.5f) < 0.0001f && fabsf(max.y - 16.5f) < 0.0001f);
    CF_V2 rotated;
    assert(appearance_rotate_point((CF_V2){ 16, 0 }, quarter_turn, &rotated));
    assert(fabsf(rotated.x) < 0.0001f && fabsf(rotated.y - 16) < 0.0001f);
    appearance = (Appearance){ .width = 18, .height = 6, .shape = SHAPE_CAPSULE };
    assert(appearance_rotated_bounds(&appearance, diagonal, &min, &max));
    float extent = 6 / sqrtf(2) + 3;
    assert(fabsf(min.x + extent) < 0.0001f && fabsf(min.y + extent) < 0.0001f);
    assert(fabsf(max.x - extent) < 0.0001f && fabsf(max.y - extent) < 0.0001f);

    min = (CF_V2){ 11, 12 };
    max = (CF_V2){ 13, 14 };
    assert(!appearance_rotated_bounds(&appearance, NAN, &min, &max));
    assert(min.x == 11 && min.y == 12 && max.x == 13 && max.y == 14);
    rotated = (CF_V2){ 15, 16 };
    assert(!appearance_rotate_point((CF_V2){ 1, 2 }, NAN, &rotated));
    assert(rotated.x == 15 && rotated.y == 16);
    assert(!appearance_rotate_point((CF_V2){ NAN, 2 }, diagonal, &rotated));
    assert(!appearance_rotate_point((CF_V2){ FLT_MAX, -FLT_MAX }, diagonal, &rotated));
    assert(rotated.x == 15 && rotated.y == 16);
}

void test_appearance_geometry()
{
    stroked_player();
    capsules();
    legacy_shapes();
    invalid_geometry();
    rotated_geometry();
    puts("appearance geometry PASS: stroke picking, exact extents, rounded capsules and legacy shapes");
}
