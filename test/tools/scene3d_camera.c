#include "render/scene3d.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>

static engine_viewport_camera authored(bool orthographic)
{
    return (engine_viewport_camera){ .span_width = 80,
                                     .span_height = 40,
                                     .lens = { .eye = { 0, 100, 0 },
                                               .forward = { 0, -1, 0 },
                                               .up = { 0, 0, -1 },
                                               .near_clip = 1,
                                               .far_clip = 200,
                                               .orthographic = orthographic } };
}
static bool orthographic_preserves_span_at_different_depths()
{
    engine_viewport_camera frame = authored(true);
    render_scene3d_camera camera = render_scene3d_camera_frame(&frame);
    REQUIRE(camera.orthographic);
    CF_V3 a = render_scene3d_project(&camera, (CF_V3){ 12, 0, -8 });
    CF_V3 b = render_scene3d_project(&camera, (CF_V3){ 12, 30, -8 });
    REQUIRE(fabsf(a.x - .3f) < 1e-5f);
    REQUIRE(fabsf(a.y - .4f) < 1e-5f);
    REQUIRE(fabsf(a.x - b.x) < 1e-5f && fabsf(a.y - b.y) < 1e-5f);
    CF_V3 closest = render_scene3d_project(&camera, (CF_V3){ 0, 99, 0 });
    CF_V3 farthest = render_scene3d_project(&camera, (CF_V3){ 0, -100, 0 });
    REQUIRE(fabsf(closest.z) < 1e-5f);
    REQUIRE(fabsf(farthest.z - 1) < 1e-5f);
    CF_V4 viewer = render_scene3d_viewer(&camera);
    REQUIRE(viewer.w == 0 && fabsf(viewer.y - 1) < 1e-5f);
    return true;
}
static bool orthographic_rays_are_parallel_and_pick_ground()
{
    engine_viewport_camera frame = authored(true);
    render_scene3d_camera camera = render_scene3d_camera_frame(&frame);
    CF_Ray3 left = render_scene3d_ray(&camera, (CF_V2){ -.5f, 0 });
    CF_Ray3 right = render_scene3d_ray(&camera, (CF_V2){ .5f, 0 });
    REQUIRE(fabsf(left.p.x + 20) < 1e-4f && fabsf(right.p.x - 20) < 1e-4f);
    REQUIRE(fabsf(left.p.y - 99) < 1e-4f);
    REQUIRE(cf_len_v3(cf_sub_v3(left.d, right.d)) < 1e-5f);
    REQUIRE(fabsf(left.d.y + 1) < 1e-5f);
    CF_V2 game;
    REQUIRE(render_scene3d_ground(&camera, (CF_V2){ .25f, -.5f }, &game));
    REQUIRE(fabsf(game.x - 10) < 1e-4f && fabsf(game.y - 10) < 1e-4f);
    CF_V3 projected = render_scene3d_project(&camera, (CF_V3){ game.x, 0, game.y });
    REQUIRE(fabsf(projected.x - .25f) < 1e-5f && fabsf(projected.y + .5f) < 1e-5f);
    return true;
}
static bool default_perspective_keeps_diverging_rays()
{
    engine_viewport_camera frame = authored(false);
    render_scene3d_camera camera = render_scene3d_camera_frame(&frame);
    REQUIRE(!camera.orthographic);
    CF_Ray3 left = render_scene3d_ray(&camera, (CF_V2){ -.5f, 0 });
    CF_Ray3 right = render_scene3d_ray(&camera, (CF_V2){ .5f, 0 });
    REQUIRE(right.d.x - left.d.x > .1f);
    CF_V3 a = render_scene3d_project(&camera, (CF_V3){ 12, 0, -8 });
    CF_V3 b = render_scene3d_project(&camera, (CF_V3){ 12, 30, -8 });
    REQUIRE(b.x > a.x && b.y > a.y);
    CF_V4 viewer = render_scene3d_viewer(&camera);
    REQUIRE(viewer.w == 1 && viewer.y == 100);
    render_scene3d_camera fallback = render_scene3d_camera_make(0, 0, 80, 40);
    REQUIRE(!fallback.orthographic && fallback.projection.elements[15] == 0);
    return true;
}
void test_tool_scene3d_camera()
{
    RUN_TEST_CASE(orthographic_preserves_span_at_different_depths);
    RUN_TEST_CASE(orthographic_rays_are_parallel_and_pick_ground);
    RUN_TEST_CASE(default_perspective_keeps_diverging_rays);
}
