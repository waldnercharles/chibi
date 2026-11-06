#include "render/appearance_geometry.h"

#include "render/geometry_2d.h"
#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <math.h>
#include <string.h>

static bool expect_bounds(Appearance appearance, float left, float bottom, float right, float top)
{
    CF_V2 min, max;
    return appearance_bounds(&appearance, &min, &max) &&
           fabsf(min.x - left) < 0.0001f && fabsf(min.y - bottom) < 0.0001f &&
           fabsf(max.x - right) < 0.0001f && fabsf(max.y - top) < 0.0001f;
}

TEST_CASE(stroked_player)
{
    Appearance appearance = { .width = 20,
                              .height = 18,
                              .shape = SHAPE_STROKED_CHEVRON,
                              .stroke_width = 1 };
    REQUIRE(expect_bounds(appearance, -4.5f, -9.5f, 16.5f, 9.5f));
    CF_V2 points[4];
    REQUIRE(appearance_points(&appearance, points) == 4);
    const CF_V2 expected[] = { { 16, 0 }, { -4, -9 }, { 0, 0 }, { -4, 9 } };
    for (int i = 0; i < 4; i++) {
        REQUIRE(points[i].x == expected[i].x && points[i].y == expected[i].y);
        REQUIRE(appearance_contains(&appearance, points[i]));
    }
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 4, 0 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 6, 4.5f }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 6, 6 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 16.49f, 0 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 16.51f, 0 }));
    appearance.border_width = 100;
    appearance.glow_radius = 200;
    REQUIRE(expect_bounds(appearance, -4.5f, -9.5f, 16.5f, 9.5f));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 30, 0 }));
    return true;
}

TEST_CASE(capsules)
{
    Appearance appearance = { .width = 18, .height = 6, .shape = SHAPE_CAPSULE };
    REQUIRE(expect_bounds(appearance, -9, -3, 9, 3));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 9, 0 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ -9, 0 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 0, 3 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 7.8f, 2.3f }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 9, 3 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 9.01f, 0 }));
    CF_V2 points[4];
    REQUIRE(appearance_points(&appearance, points) == 0);
    appearance.width = 6;
    appearance.height = 18;
    REQUIRE(expect_bounds(appearance, -3, -9, 3, 9));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 0, 9 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 2.3f, 7.8f }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 3, 9 }));
    appearance.height = 6;
    REQUIRE(expect_bounds(appearance, -3, -3, 3, 3));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 3, 0 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 3, 3 }));
    return true;
}

TEST_CASE(legacy_shapes)
{
    Appearance appearance = { .width = 20, .height = 10, .shape = SHAPE_BOX };
    REQUIRE(expect_bounds(appearance, -10, -5, 10, 5));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 10, 5 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 10.01f, 0 }));
    appearance.shape = SHAPE_DIAMOND;
    REQUIRE(expect_bounds(appearance, -10, -5, 10, 5));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 10, 0 }));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 5, 2.5f }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 8, 4 }));
    appearance.shape = SHAPE_CHEVRON;
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 0, 0 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ -9, 0 }));
    appearance.shape = SHAPE_CIRCLE;
    REQUIRE(expect_bounds(appearance, -5, -5, 5, 5));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 5, 0 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 6, 0 }));
    REQUIRE(!appearance_contains(&appearance, (CF_V2){ 4, 4 }));
    appearance.shape = 12345;
    REQUIRE(expect_bounds(appearance, -10, -5, 10, 5));
    REQUIRE(appearance_contains(&appearance, (CF_V2){ 10, 5 }));
    return true;
}

TEST_CASE(invalid_geometry)
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
        REQUIRE(!appearance_bounds(&invalid[i], &min, &max));
        REQUIRE(!appearance_contains(&invalid[i], (CF_V2){ 0 }));
        REQUIRE(appearance_points(&invalid[i], points) == 0);
    }
    REQUIRE(!appearance_contains(&base, (CF_V2){ NAN, 0 }));
    REQUIRE(!appearance_contains(&base, (CF_V2){ 0, INFINITY }));
    return true;
}

TEST_CASE(rotated_geometry)
{
    const float quarter_turn = 1.5707963267948966f;
    const float diagonal = 0.7853981633974483f;
    Appearance appearance = { .width = 20,
                              .height = 18,
                              .shape = SHAPE_STROKED_CHEVRON,
                              .stroke_width = 1 };
    CF_V2 min, max;
    REQUIRE(appearance_rotated_bounds(&appearance, quarter_turn, &min, &max));
    REQUIRE(fabsf(min.x + 9.5f) < 0.0001f && fabsf(min.y + 4.5f) < 0.0001f);
    REQUIRE(fabsf(max.x - 9.5f) < 0.0001f && fabsf(max.y - 16.5f) < 0.0001f);
    CF_V2 rotated;
    REQUIRE(appearance_rotate_point((CF_V2){ 16, 0 }, quarter_turn, &rotated));
    REQUIRE(fabsf(rotated.x) < 0.0001f && fabsf(rotated.y - 16) < 0.0001f);
    appearance = (Appearance){ .width = 18, .height = 6, .shape = SHAPE_CAPSULE };
    REQUIRE(appearance_rotated_bounds(&appearance, diagonal, &min, &max));
    float extent = 6 / sqrtf(2) + 3;
    REQUIRE(fabsf(min.x + extent) < 0.0001f && fabsf(min.y + extent) < 0.0001f);
    REQUIRE(fabsf(max.x - extent) < 0.0001f && fabsf(max.y - extent) < 0.0001f);

    min = (CF_V2){ 11, 12 };
    max = (CF_V2){ 13, 14 };
    REQUIRE(!appearance_rotated_bounds(&appearance, NAN, &min, &max));
    REQUIRE(min.x == 11 && min.y == 12 && max.x == 13 && max.y == 14);
    rotated = (CF_V2){ 15, 16 };
    REQUIRE(!appearance_rotate_point((CF_V2){ 1, 2 }, NAN, &rotated));
    REQUIRE(rotated.x == 15 && rotated.y == 16);
    REQUIRE(!appearance_rotate_point((CF_V2){ NAN, 2 }, diagonal, &rotated));
    REQUIRE(!appearance_rotate_point((CF_V2){ FLT_MAX, -FLT_MAX }, diagonal, &rotated));
    REQUIRE(rotated.x == 15 && rotated.y == 16);
    return true;
}

TEST_CASE(projected_relief_rejects_nonfinite_values)
{
    Appearance authored = { .width = 8, .height = 8, .relief = 3 };
    VisualTransform visual = { .scale = 2 };
    Appearance projected = { 0 };
    float angle = 0;
    REQUIRE(render_geometry_2d_project(NULL, &authored, &visual, 0, &projected, &angle));
    REQUIRE(projected.relief == 6);
    unsigned char saved[sizeof projected];
    memcpy(saved, &projected, sizeof saved);
    const float invalid[] = { NAN, INFINITY, -INFINITY, FLT_MAX };
    for (int i = 0; i < 4; i++) {
        authored.relief = invalid[i];
        angle = 7;
        REQUIRE(!render_geometry_2d_project(NULL, &authored, &visual, 0, &projected, &angle));
        REQUIRE(memcmp(saved, &projected, sizeof saved) == 0 && angle == 7);
    }
    return true;
}

void test_appearance_geometry()
{
    RUN_TEST_CASE(stroked_player);
    RUN_TEST_CASE(capsules);
    RUN_TEST_CASE(legacy_shapes);
    RUN_TEST_CASE(invalid_geometry);
    RUN_TEST_CASE(rotated_geometry);
    RUN_TEST_CASE(projected_relief_rejects_nonfinite_values);
}
