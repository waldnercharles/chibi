#include "render/scene3d.h"
#include "runtime/camera.h"
#include "runtime/component_catalog.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>

static const float yaw = .7853981634f;
static const float pitch = .8726646260f; /* 50 degrees from the default 80-degree elevation. */

static engine_viewport_camera frame(engine_viewport_view view)
{
    return (engine_viewport_camera){ .center_x = view.center_x,
                                     .center_y = view.center_y,
                                     .span_width = view.width,
                                     .span_height = view.height,
                                     .yaw = view.yaw,
                                     .pitch = view.pitch,
                                     .target_elevation = view.target_elevation,
                                     .lens = view.lens };
}

static bool near(float a, float b)
{
    return fabsf(a - b) < .0001f;
}

static CF_V3 toward_eye(const render_scene3d_camera *camera, CF_V3 target)
{
    return cf_norm_v3(cf_sub_v3(camera->eye, target));
}

TEST_CASE(zero_angles_keep_the_runtime_overhead_camera)
{
    render_scene3d_camera camera = render_scene3d_camera_make(23, -12, 100, 80);
    CF_V3 direction = toward_eye(&camera, (CF_V3){ 23, 0, 12 });
    REQUIRE(near(direction.x, 0));
    REQUIRE(near(direction.y, cosf(.1745329252f)));
    REQUIRE(near(direction.z, sinf(.1745329252f)));
    CF_V3 projected = render_scene3d_project(&camera, (CF_V3){ 23, 0, 12 });
    REQUIRE(near(projected.x, 0) && near(projected.y, 0));
    return true;
}

TEST_CASE(oblique_angles_and_elevation_frame_the_target)
{
    engine_viewport_camera view = { .center_x = 40,
                                    .center_y = 20,
                                    .span_width = 100,
                                    .span_height = 80,
                                    .yaw = yaw,
                                    .pitch = pitch,
                                    .target_elevation = 60 };
    render_scene3d_camera camera = render_scene3d_camera_frame(&view);
    CF_V3 target = { 40, 60, -20 };
    CF_V3 direction = toward_eye(&camera, target);
    REQUIRE(near(direction.x, .6123724357f));
    REQUIRE(near(direction.y, .5f));
    REQUIRE(near(direction.z, .6123724357f));
    CF_V3 projected = render_scene3d_project(&camera, target);
    REQUIRE(near(projected.x, 0) && near(projected.y, 0));
    view.center_x += 17;
    view.center_y -= 31;
    view.span_width *= .5f;
    view.span_height *= .5f;
    camera = render_scene3d_camera_frame(&view);
    CF_V3 moved = toward_eye(&camera, (CF_V3){ 57, 60, 11 });
    REQUIRE(
        near(moved.x, direction.x) && near(moved.y, direction.y) &&
        near(moved.z, direction.z)
    );
    return true;
}

TEST_CASE(fitting_translated_solids_retains_angle_and_contains_every_corner)
{
    CF_V3 corners[8];
    for (int i = 0; i < 8; i++) {
        corners[i] = (CF_V3){ 70 + (i & 1 ? 14 : -14),
                              130 + (i & 4 ? 30 : -30),
                              -50 + (i & 2 ? 8 : -8) };
    }
    const float aspects[] = { .5f, 1, 3.5f };
    for (int aspect = 0; aspect < 3; aspect++) {
        engine_viewport_view view = { .yaw = yaw, .pitch = pitch };
        render_scene3d_fit_view(corners, 8, aspects[aspect], &view);
        REQUIRE(view.center_x == 70 && view.center_y == 50 && view.target_elevation == 130);
        REQUIRE(view.yaw == yaw && view.pitch == pitch);
        engine_viewport_camera fitted = frame(view);
        render_scene3d_camera camera = render_scene3d_camera_frame(&fitted);
        CF_V3 center = render_scene3d_project(&camera, (CF_V3){ 70, 130, -50 });
        REQUIRE(near(center.x, 0) && near(center.y, 0));
        float extent = 0;
        for (int i = 0; i < 8; i++) {
            CF_V3 projected = render_scene3d_project(&camera, corners[i]);
            REQUIRE(projected.z > 0 && projected.z < 1);
            extent = fmaxf(extent, fmaxf(fabsf(projected.x), fabsf(projected.y)));
        }
        REQUIRE(extent > .7f && extent < .834f);
        /* A subsequent Frame Selection preserves a camera turned by the user. */
        view.yaw = -.4f;
        view.pitch = .5f;
        render_scene3d_fit_view(corners, 8, aspects[aspect], &view);
        REQUIRE(view.yaw == -.4f && view.pitch == .5f);
        fitted = frame(view);
        camera = render_scene3d_camera_frame(&fitted);
        for (int i = 0; i < 8; i++) {
            CF_V3 projected = render_scene3d_project(&camera, corners[i]);
            REQUIRE(fabsf(projected.x) < .834f && fabsf(projected.y) < .834f);
        }
    }
    return true;
}

TEST_CASE(explicit_lenses_override_framing_angles)
{
    engine_viewport_camera view = {
        .center_x = 500,
        .center_y = -200,
        .span_width = 100,
        .span_height = 80,
        .yaw = yaw,
        .pitch = pitch,
        .target_elevation = 1000,
        .lens = { .eye = { 4, 5, 6 }, .forward = { 0, 0, -1 }, .up = { 0, 1, 0 } }
    };
    render_scene3d_camera camera = render_scene3d_camera_frame(&view);
    REQUIRE(camera.eye.x == 4 && camera.eye.y == 5 && camera.eye.z == 6);
    CF_V3 projected = render_scene3d_project(&camera, (CF_V3){ 4, 5, 0 });
    REQUIRE(near(projected.x, 0) && near(projected.y, 0));
    return true;
}

static ecs_world *world;

static void setup()
{
    world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Transform,         &COMPONENT_Orientation,
        &COMPONENT_GlobalTransform2D, &COMPONENT_Transform3D,
        &COMPONENT_GlobalTransform3D, &COMPONENT_Camera
    };
    for (int i = 0; i < (int)(sizeof components / sizeof *components); i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
}

static void teardown()
{
    ecs_world_free(world);
}

static ecs_entity add_camera()
{
    ecs_entity entity = ecs_world_create_entity(world);
    Transform3D pose = { .position = { 4, 5, 6 }, .scale = { 1, 1, 1 } };
    Camera camera = { .width = 320,
                      .height = 180,
                      .offset = { 7, 11 },
                      .fov = 1,
                      .near_clip = .1f,
                      .far_clip = 100 };
    assert(ecs_world_record_add(world, entity, COMPONENT_Transform3D.id, &pose, 0) == ECS_OK);
    assert(ecs_world_record_add(world, entity, COMPONENT_Camera.id, &camera, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

TEST_CASE(editor_can_frame_a_transform3d_only_or_disabled_selected_camera)
{
    ecs_entity selected = add_camera();
    REQUIRE(!Transform_has(world, selected));
    engine_viewport_view view;
    REQUIRE(camera_entity_view(world, NULL, selected, &view));
    REQUIRE(view.center_x == 11 && view.center_y == -17);
    REQUIRE(view.width == 320 && view.height == 180);
    REQUIRE(view.lens.eye.x == 11 && view.lens.eye.y == 5 && view.lens.eye.z == 17);
    REQUIRE(
        view.lens.forward.x == 0 && view.lens.forward.y == 0 &&
        view.lens.forward.z == -1
    );
    REQUIRE(view.lens.up.x == 0 && view.lens.up.y == 1 && view.lens.up.z == 0);
    REQUIRE(view.lens.fov == 1 && view.lens.near_clip == .1f && view.lens.far_clip == 100);
    engine_viewport_view active;
    REQUIRE(camera_view(world, NULL, &active) == 1);
    REQUIRE(
        active.lens.eye.x == view.lens.eye.x &&
        active.lens.eye.z == view.lens.eye.z
    );
    /* Selection can preview an inactive camera even when Play rejects two active cameras. */
    Camera_get(world, selected)->disabled = true;
    add_camera();
    add_camera();
    REQUIRE(camera_view(world, NULL, &active) == -1);
    Transform3D_get(world, selected)->position.y = 27;
    REQUIRE(camera_entity_view(world, NULL, selected, &view));
    REQUIRE(view.lens.eye.y == 27);
    Camera_get(world, selected)->width = 0;
    REQUIRE(!camera_entity_view(world, NULL, selected, &view));
    return true;
}

void test_scene3d_camera()
{
    RUN_TEST_CASE(zero_angles_keep_the_runtime_overhead_camera);
    RUN_TEST_CASE(oblique_angles_and_elevation_frame_the_target);
    RUN_TEST_CASE(fitting_translated_solids_retains_angle_and_contains_every_corner);
    RUN_TEST_CASE(explicit_lenses_override_framing_angles);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(editor_can_frame_a_transform3d_only_or_disabled_selected_camera);
    pu_setup(NULL, NULL);
}
