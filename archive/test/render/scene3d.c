#include "render/scene3d.h"

#include "execution/status.h"
#include "render/fog.h"
#include "render/gpu_timing.h"
#include "render/mesh_surface.h"
#include "render/scene3d_cascades.h"
#include "render/scene3d_mesh_cache.h"
#include "render/scene3d_parts.h"
#include "render/scene3d_post.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "render/viewport.h"
#include "runtime/behaviors/entities.h"
#include "runtime/camera.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/fields/program.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/geometry/mesh_pick.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/images/sky.h"
#include "runtime/material_field.h"
#include "runtime/materials.h"
#include "runtime/particles.h"
#include "runtime/transform_propagation.h"
#include "runtime/visibility.h"
#include "runtime/world.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#if CHIBI_TEST_MATERIAL_GRAPHS
#include "material_graphs.h"
#endif
#if CHIBI_TEST_MATERIAL_GRAPHS && CHIBI_TEST_MODEL_IMPORT
#include "assets/model_import.h"
#include "authoring/image_prepare.h"
#include "authoring/shader_prepare.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "test/support/gltf_fixture.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* One hidden app renders a unit box through the 3D path and post-processing,
 * then reads back the tone-mapped output. */
enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static ecs_world *test_world;
static bool backdrop; /* Queue a white 2D fill before each 3D draw. */
static render_scene3d_camera lens; /* What each render sees. */
static render_scene3d *scene3d;
static render_scene3d_post *post;
static particles *test_particles;
static CF_Pixel pixels[PIXELS];
static engine *runtime;

/* Looks down at the origin from 45 degrees. */
static render_scene3d_camera camera()
{
    return (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 0, 4, 4 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 100),
        .eye = { 0, 4, 4 },
    };
}

/* Turns `lens` to look from `eye` toward `target`. */
static void look(CF_V3 eye, CF_V3 target, CF_V3 up)
{
    lens.view = cf_look_at(eye, target, up);
    lens.eye = eye;
}

static void setup()
{
    test_world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(test_world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    scene3d = render_scene3d_new();
    render_scene3d_set_size(scene3d, SIDE, SIDE);
    post = render_scene3d_post_new();
    test_particles = particles_new();
    lens = camera();
}

#if CHIBI_TEST_MATERIAL_GRAPHS && CHIBI_TEST_MODEL_IMPORT
static void remove_imports();
#endif

static void teardown()
{
#if CHIBI_TEST_MATERIAL_GRAPHS && CHIBI_TEST_MODEL_IMPORT
    remove_imports();
#endif
    backdrop = false;
    render_shader_catalog_clear();
    render_mesh_assets_clear();
    engine_free(runtime);
    runtime = NULL;
    render_scene3d_free(scene3d);
    render_scene3d_post_free(post);
    particles_free(test_particles);
    ecs_world_free(test_world);
}

static ecs_entity add(ecs_component_id component, const void *data)
{
    ecs_entity entity = ecs_world_create_entity(test_world);
    assert(ecs_world_record_add(test_world, entity, component, data, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return entity;
}

/* A solid centered on a world point. */
static ecs_entity add_solid(CF_V3 center, Geometry3D shape, unsigned color)
{
    ecs_entity
        solid = add(COMPONENT_Transform.id, &(Transform){ center.x, center.z });
    shape.elevation = center.y;
    Material material = { .scene = color };
    assert(ecs_world_record_add(test_world, solid, COMPONENT_Geometry3D.id, &shape, 0) == ECS_OK);
    assert(ecs_world_record_add(test_world, solid, COMPONENT_Material.id, &material, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return solid;
}

static void add_solid_at(float x, Geometry3D shape, unsigned color)
{
    add_solid((CF_V3){ x, shape.elevation, 0 }, shape, color);
}

static void add_box(unsigned color)
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 0 }, color);
}

/* A floor whose top face fills the whole view. */
static void add_floor()
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 400, 400, .2f, -.1f }, 0xffffffffu);
}

static DirectionalLight *add_sun(unsigned color, CF_V3 direction)
{
    DirectionalLight sun = { .color = color, .intensity = 3, .direction = direction };
    ecs_entity entity = add(COMPONENT_DirectionalLight.id, &sun);
    return DirectionalLight_get(test_world, entity);
}

/* The box's top face fills the image center; its front face is just below. */
static CF_Pixel top()
{
    return pixels[(SIDE / 2 - 4) * SIDE + SIDE / 2];
}

static CF_Pixel front()
{
    return pixels[(SIDE / 2 + 10) * SIDE + SIDE / 2];
}

/* The pixel showing a world point. */
static int pixel_index(CF_V3 world)
{
    CF_V3 ndc = render_scene3d_project(&lens, world);
    int x = (int)((ndc.x + 1) * SIDE / 2), y = (int)((1 - ndc.y) * SIDE / 2);
    assert(x >= 0 && x < SIDE && y >= 0 && y < SIDE);
    return y * SIDE + x;
}

static CF_Pixel pixel_at(CF_V3 world)
{
    return pixels[pixel_index(world)];
}

/* The floor point at the center of the pixel that shows a floor point. */
static CF_V3 pixel_floor(CF_V3 world)
{
    int i = pixel_index(world);
    CF_V2 ndc = { (i % SIDE + .5f) * 2 / SIDE - 1, 1 - (i / SIDE + .5f) * 2 / SIDE };
    CF_V2 ground;
    bool hit = render_scene3d_ground(&lens, ndc, &ground);
    assert(hit);
    return (CF_V3){ ground.x, 0, ground.y };
}

static bool read_pixels(CF_Canvas canvas)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
        cf_sleep(1);
    }
    int bytes = (int)sizeof pixels;
    bool read = cf_readback_data(readback, pixels, bytes) == bytes;
    cf_destroy_readback(readback);
    return read;
}

/* Post-processes the scene target and reads back the tone-mapped output. */
static bool finish(float time)
{
    bool processed = render_scene3d_post_process(post, time);
    cf_app_draw_onto_screen(false);
    return processed && read_pixels(render_scene3d_post_output(post));
}

static bool render_at(const Environment *environment, float time)
{
    Environment settings = *environment;
    settings.bloom_strength = 0;
    if (!render_scene3d_post_prepare(post, SIDE, SIDE, &settings)) {
        return false;
    }
    CF_Canvas canvas = render_scene3d_post_target(post);
    cf_canvas_set_clear_color(canvas, (CF_Color){ 0, 0, 0, 1 });
    cf_app_update(NULL);
    if (backdrop) {
        cf_draw_quad_fill(cf_make_aabb((CF_V2){ -SIDE, -SIDE }, (CF_V2){ SIDE, SIDE }), 0);
    }
    /* As the app does: the post canvas still holds the last frame. */
    render_scene3d_set_history(scene3d, cf_canvas_get_target(canvas));
    bool drawn = render_scene3d_draw(
        scene3d,
        test_world,
        test_particles,
        &lens,
        0,
        runtime,
        &settings,
        (render_skip){ 0 }
    );
    cf_render_to(canvas, true);
    return finish(time) && drawn;
}

static bool render(const Environment *environment)
{
    return render_at(environment, 0);
}

/* Points the renders through the scene's camera, as Play frames it. */
static int look_through_camera()
{
    engine_viewport_view view;
    int found = camera_view(test_world, NULL, &view);
    if (found == 1) {
        lens = render_scene3d_camera_frame(&(engine_viewport_camera){
            .center_x = view.center_x,
            .center_y = view.center_y,
            .span_width = view.width,
            .span_height = view.height,
            .lens = view.lens });
    }
    return found;
}

/* A camera at a 3D pose, looking along -Z until rotated. */
static ecs_entity add_camera_3d(CF_Quat rotation)
{
    ecs_entity entity = add(COMPONENT_Camera.id, &(Camera){ .width = 2, .height = 2 });
    Transform3D pose = { .rotation = rotation, .scale = { 1, 1, 1 } };
    assert(ecs_world_record_add(test_world, entity, COMPONENT_Transform3D.id, &pose, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return entity;
}

static CF_Pixel center()
{
    return pixels[SIDE / 2 * SIDE + SIDE / 2];
}

static bool near_value(float value, float expected)
{
    return fabsf(value - expected) < 1e-4f;
}

TEST_CASE(cameras_look_along_their_transform3d)
{
    Geometry3D cube = { PRIMITIVE_BOX, 2, 2, 2, 0 };
    add_solid((CF_V3){ 6, 0, 0 }, cube, 0xff0000ffu);
    add_solid((CF_V3){ -6, 0, 0 }, cube, 0x0000ffffu);
    ecs_entity camera = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, -CF_PI / 2)
    );
    REQUIRE(look_through_camera() == 1);
    CF_V3 ahead = render_scene3d_project(&lens, (CF_V3){ 5, 0, 0 });
    CF_V3 above = render_scene3d_project(&lens, (CF_V3){ 5, 1, 0 });
    CF_V3 right = render_scene3d_project(&lens, (CF_V3){ 5, 0, 1 });
    REQUIRE(near_value(ahead.x, 0) && near_value(ahead.y, 0));
    REQUIRE(near_value(above.x, 0) && above.y > .1f);
    REQUIRE(right.x > .1f && near_value(right.y, 0));
    Camera *authored = Camera_get(test_world, camera);
    authored->offset = (CF_V2){ 1, 2 };
    REQUIRE(look_through_camera() == 1);
    CF_V3 offset = render_scene3d_project(&lens, (CF_V3){ 6, 0, 2 });
    REQUIRE(near_value(offset.x, 0) && near_value(offset.y, 0));
    authored->offset = (CF_V2){ 0 };
    REQUIRE(look_through_camera() == 1);
    Environment environment = environment_defaults();
    REQUIRE(render(&environment));
    REQUIRE(center().colors.r > 2 * center().colors.b);
    Transform3D_get(test_world, camera)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, CF_PI / 2);
    REQUIRE(look_through_camera() == 1);
    REQUIRE(render(&environment));
    REQUIRE(center().colors.b > 2 * center().colors.r);
    return true;
}

TEST_CASE(camera_field_of_view_and_clips_shape_the_projection)
{
    ecs_entity framing = add(COMPONENT_Transform.id, &(Transform){ 3, 2 });
    Camera authored = { .width = 4, .height = 2 };
    assert(ecs_world_record_add(test_world, framing, COMPONENT_Camera.id, &authored, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(look_through_camera() == 1);
    CF_V3 target = render_scene3d_project(&lens, (CF_V3){ 3, 0, 2 });
    REQUIRE(near_value(target.x, 0) && near_value(target.y, 0));
    REQUIRE(near_value(lens.projection.elements[5] * tanf(.5f * 35 * CF_PI / 180), 1));
    REQUIRE(near_value(lens.projection.elements[5] / lens.projection.elements[0], 2));
    Camera_get(test_world, framing)->fov = CF_PI / 2;
    REQUIRE(look_through_camera() == 1);
    target = render_scene3d_project(&lens, (CF_V3){ 3, 0, 2 });
    REQUIRE(near_value(target.x, 0) && near_value(target.y, 0));
    REQUIRE(near_value(lens.projection.elements[5], 1));
    assert(ecs_world_destroy_entity(test_world, framing) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    ecs_entity posed = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, -CF_PI / 2)
    );
    Camera *clipped = Camera_get(test_world, posed);
    clipped->near_clip = .5f;
    clipped->far_clip = 50;
    REQUIRE(look_through_camera() == 1);
    REQUIRE(near_value(render_scene3d_project(&lens, (CF_V3){ .5f, 0, 0 }).z, 0));
    REQUIRE(near_value(render_scene3d_project(&lens, (CF_V3){ 50, 0, 0 }).z, 1));
    return true;
}

/* A small-scale camera far from the origin still looks along its -Z, though
 * its scaled direction is below the precision of its position, and its
 * scaled axes are too short to cross. One whose coordinates are each finite
 * keeps its view though their sum overflows. */
TEST_CASE(distant_small_scale_cameras_keep_their_view)
{
    ecs_entity camera = add_camera_3d(cf_quat_identity());
    Transform3D *pose = Transform3D_get(test_world, camera);
    pose->position = (CF_V3){ 0, 0, 1000 };
    pose->scale = (CF_V3){ 1e-6f, 1e-6f, 1e-6f };
    REQUIRE(look_through_camera() == 1);
    CF_V3 ahead = render_scene3d_project(&lens, (CF_V3){ 0, 0, 990 });
    CF_V3 right = render_scene3d_project(&lens, (CF_V3){ 1, 0, 990 });
    REQUIRE(near_value(ahead.x, 0) && near_value(ahead.y, 0));
    REQUIRE(right.x > .01f && near_value(right.y, 0));
    pose->scale = (CF_V3){ 1e-12f, 1e-12f, 1e-12f };
    REQUIRE(look_through_camera() == 1);
    ahead = render_scene3d_project(&lens, (CF_V3){ 0, 0, 990 });
    REQUIRE(near_value(ahead.x, 0) && near_value(ahead.y, 0));
    pose->scale = (CF_V3){ 1, 1, 1 };
    pose->position = (CF_V3){ 3e38f, 3e38f, 0 };
    REQUIRE(look_through_camera() == 1);
    return true;
}

/* Default clips stay in order however close the framing distance gets, and
 * keep the framed target in front of near: from the smallest span, and
 * through a field of view of 179 degrees. */
TEST_CASE(default_clips_stay_ordered_for_close_framings)
{
    render_scene3d_camera close = render_scene3d_camera_make(0, 0, .0001f, .0001f);
    CF_V3 ahead = { 0, -cosf(10 * CF_PI / 180), -sinf(10 * CF_PI / 180) };
    float depth = render_scene3d_project(&close, ahead).z;
    REQUIRE(depth > 0 && depth < 1);
    depth = render_scene3d_project(&close, (CF_V3){ 0 }).z;
    REQUIRE(depth > 0 && depth < 1);
    render_scene3d_camera wide = render_scene3d_camera_frame(&(
        engine_viewport_camera
    ){ .span_width = 2, .span_height = 2, .lens = { .fov = 179 * CF_PI / 180 } });
    depth = render_scene3d_project(&wide, (CF_V3){ 0 }).z;
    REQUIRE(depth > 0 && depth < 1);
    ecs_entity camera = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, -CF_PI / 2)
    );
    Camera_get(test_world, camera)->fov = 3.1415f;
    REQUIRE(look_through_camera() == 1);
    depth = render_scene3d_project(&lens, (CF_V3){ 1, 0, 0 }).z;
    REQUIRE(depth > 0 && depth < 1);
    return true;
}

TEST_CASE(cameras_reject_lenses_the_3d_path_cannot_use)
{
    static const float lenses[][3] = {
        { -.1f, 0, 0 },     { 1e-37f, 0, 0 },  { CF_PI, 0, 0 }, { NAN, 0, 0 },
        { 0, 1, 0 },        { 0, 0, 1 },       { 0, 2, 2 },     { 0, 2, 1 },
        { 0, 1, INFINITY }, { 0, .01f, 1e7f },
    };
    ecs_entity entity = add_camera_3d(cf_quat_identity());
    Camera *camera = Camera_get(test_world, entity);
    for (int i = 0; i < (int)(sizeof lenses / sizeof lenses[0]); i++) {
        camera->fov = lenses[i][0];
        camera->near_clip = lenses[i][1];
        camera->far_clip = lenses[i][2];
        REQUIRE(look_through_camera() == -1);
    }
    *camera = (Camera){ .width = 2, .height = 2, .fov = 1, .near_clip = 1, .far_clip = 2 };
    REQUIRE(look_through_camera() == 1);
    Transform3D_get(test_world, entity)->scale = (CF_V3){ 0 };
    REQUIRE(look_through_camera() == -1);
    return true;
}

/* A camera riding a dynamic body looks from where the body draws between
 * physics steps, not from the pose the last step solved. */
TEST_CASE(cameras_on_bodies_look_from_their_drawn_pose)
{
    char error[256];
    world_options options = { .modules = WORLD_PHYSICS3D };
    world *simulation = world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = world_ecs(simulation);
    engine *runtime = world_engine(simulation);
    ecs_entity body = ecs_world_create_entity(ecs),
               eye = ecs_world_create_entity(ecs);
    REQUIRE(Transform3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(Collider3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(Body3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(Gravity3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(Transform3D_add(ecs, eye, 0) == ECS_OK);
    REQUIRE(Camera_add(ecs, eye, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(ecs, eye, ECS_CHILD_OF, body, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    *Transform3D_get(ecs, body) = (Transform3D){ .scale = { 1, 1, 1 } };
    *Collider3D_get(ecs, body) = (Collider3D){ .shape = COLLIDER3D_SPHERE,
                                               .radius = 1,
                                               .category = 1,
                                               .mask = 1 };
    *Body3D_get(ecs, body) = (Body3D){ .type = BODY_DYNAMIC,
                                       .linear_velocity = { 60, 0, 0 } };
    const CF_V3 behind = { 0, 0, 5 };
    *Transform3D_get(ecs, eye) = (Transform3D){ .position = behind,
                                                .scale = { 1, 1, 1 } };
    *Camera_get(ecs, eye) = (Camera){ .width = 320, .height = 180 };
    REQUIRE(engine_set_playing(runtime, true) == 0);
    REQUIRE(engine_frame(runtime, 0) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1 + ENGINE_FIXED_SECONDS * .5) == FLOW_OK);
    engine_viewport_view view;
    REQUIRE(camera_view(ecs, runtime, &view) == 1);
    CF_V3 drawn = cf_add_v3(engine_render_transform_3d(runtime, ecs, body).position, behind);
    CF_V3 offset = cf_sub_v3(view.lens.eye, drawn);
    REQUIRE(cf_len_v3(offset) < 1e-3f);
    float solved = GlobalTransform3D_get_const(ecs, eye)->position.x;
    REQUIRE(fabsf(view.lens.eye.x - (solved - 60 * (float)ENGINE_FIXED_SECONDS * .5f)) < 1e-3f);
    world_free(simulation);
    return true;
}

/* The widest clip range a camera takes still unprojects its ground rays. */
TEST_CASE(widest_clip_range_keeps_ground_rays)
{
    ecs_entity entity = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 1, 0, 0 }, -CF_PI / 6)
    );
    Transform3D_get(test_world, entity)->position = (CF_V3){ 0, 10, 0 };
    Camera *camera = Camera_get(test_world, entity);
    camera->near_clip = .01f;
    camera->far_clip = 1e4f;
    REQUIRE(look_through_camera() == 1);
    CF_V2 ground;
    REQUIRE(render_scene3d_ground(&lens, (CF_V2){ 0 }, &ground));
    REQUIRE(fabsf(ground.x) < .01f && fabsf(ground.y + 10 / tanf(CF_PI / 6)) < .01f);
    return true;
}

/* Fog conceals ground no vision source sees. A ray that never reaches the
 * ground ahead, such as one above a level camera's horizon, sees past the
 * grid, so fog conceals it too. */
/* Fog over a 16-unit grid around one vision source, drawn through a frame. */
static bool draw_fog(CF_V2 seen, float radius, const engine_viewport_camera *frame)
{
    Fog settings = { .origin = { seen.x - 8, seen.y - 8 },
                     .columns = 16,
                     .rows = 16,
                     .cell_size = 1,
                     .color = 0xff0000ffu };
    add(COMPONENT_Fog.id, &settings);
    ecs_entity eye = add(COMPONENT_Transform.id, &(Transform){ seen.x, seen.y });
    Vision vision = { .radius = radius };
    assert(ecs_world_record_add(test_world, eye, COMPONENT_Vision.id, &vision, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    visibility *visibility = visibility_new(test_world, NULL);
    const char *error = visibility_update(visibility, 0);
    render_scene3d_camera camera = render_scene3d_camera_frame(frame);
    render_fog *fog = render_fog_new();
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    cf_canvas_set_clear_color(canvas, (CF_Color){ 1, 1, 1, 1 });
    cf_app_update(NULL);
    bool drawn = render_fog_draw(fog, visibility, frame, &camera, 0);
    cf_render_to(canvas, true);
    cf_app_draw_onto_screen(false);
    bool read = read_pixels(canvas);
    cf_destroy_canvas(canvas);
    render_fog_free(fog);
    visibility_free(visibility);
    return !error && drawn && read;
}

static bool fogged(CF_Pixel pixel)
{
    return pixel.colors.r > 192 && pixel.colors.g < 64;
}

static bool unfogged(CF_Pixel pixel)
{
    return pixel.colors.r > 192 && pixel.colors.g > 192;
}

/* A frame looking straight down from an eye with a 90 degree field of view,
 * so the image spans the eye's height either way from below it. */
static engine_viewport_camera looking_down(CF_V3 eye)
{
    return (engine_viewport_camera){
        .span_width = 2,
        .span_height = 2,
        .lens = { .eye = eye, .forward = { 0, -1, 0 }, .up = { 0, 0, -1 }, .fov = CF_PI / 2 },
    };
}

TEST_CASE(fog_conceals_rays_that_miss_the_ground)
{
    engine_viewport_camera frame = {
        .span_width = 2,
        .span_height = 2,
        .lens = { .eye = { 0, 1, 0 },
                  .forward = { 0, 0, -1 },
                  .up = { 0, 1, 0 },
                  .fov = CF_PI / 2 },
    };
    REQUIRE(draw_fog((CF_V2){ 0 }, 6, &frame));
    REQUIRE(fogged(pixels[4 * SIDE + SIDE / 2]));
    REQUIRE(unfogged(pixels[(SIDE - 5) * SIDE + SIDE / 2]));
    return true;
}

/* An eye far from the origin, whose near plane is closer to it than its
 * position's precision, still finds the ground it looks at. */
TEST_CASE(distant_eyes_keep_precise_ground_rays)
{
    engine_viewport_camera frame = looking_down((CF_V3){ 1e5f, 4, 1e5f });
    render_scene3d_camera above = render_scene3d_camera_frame(&frame);
    CF_V2 center, corner;
    REQUIRE(render_scene3d_ground(&above, (CF_V2){ 0 }, &center));
    REQUIRE(render_scene3d_ground(&above, (CF_V2){ 1, 1 }, &corner));
    REQUIRE(fabsf(center.x - 1e5f) < .02f && fabsf(center.y - 1e5f) < .02f);
    REQUIRE(fabsf(corner.x - (1e5f + 4)) < .02f && fabsf(corner.y - (1e5f - 4)) < .02f);
    return true;
}

/* A distant eye turned off the axes starts its rays at its own height.
 * Rebuilt from the rounded view matrix, its distance along the ground leaks
 * into that height. */
TEST_CASE(distant_turned_eyes_start_rays_where_they_stand)
{
    CF_V3 eye = { 1e7f, 8, -7e6f };
    CF_V3 forward = cf_norm_v3((CF_V3){ .3f, -.8f, .5f });
    render_scene3d_camera turned = render_scene3d_camera_frame(&(engine_viewport_camera){
        .span_width = 2,
        .span_height = 2,
        .lens = { .eye = eye, .forward = forward, .up = { 0, 1, 0 } } });
    CF_Ray3 ray = render_scene3d_ray(&turned, (CF_V2){ 0 });
    REQUIRE(fabsf(ray.p.y - eye.y) < .02f);
    return true;
}

TEST_CASE(distant_eyes_fog_the_ground_they_look_at)
{
    engine_viewport_camera frame = looking_down((CF_V3){ 1e5f, 4, 1e5f });
    REQUIRE(draw_fog((CF_V2){ 1e5f, 1e5f }, 2, &frame));
    REQUIRE(unfogged(center()));
    REQUIRE(fogged(pixels[2 * SIDE + SIDE / 2]));
    REQUIRE(fogged(pixels[(SIDE / 2) * SIDE + 2]));
    return true;
}

/* Panning or zooming hands the view from an authored lens to the editor's
 * overhead framing, which those gestures move. */
TEST_CASE(navigating_drops_an_authored_lens)
{
    engine_viewport *viewport = engine_viewport_new(SIDE, SIDE);
    REQUIRE(viewport);
    engine_viewport_set_perspective(viewport, true);
    engine_viewport_view view = {
        .width = 2,
        .height = 2,
        .lens = { .eye = { 0, 1, 4 }, .forward = { 0, 0, -1 }, .up = { 0, 1, 0 } },
    };
    engine_viewport_set_view(viewport, &view);
    engine_viewport_set_host_input(
        viewport,
        &(engine_viewport_host_input){ .width = SIDE,
                                       .height = SIDE,
                                       .x = SIDE / 2,
                                       .y = SIDE / 2,
                                       .hovered = true,
                                       .wheel = 1 }
    );
    int drawn = engine_viewport_draw(viewport, &(engine_viewport_controls){ 0 });
    engine_viewport_get_view(viewport, &view);
    engine_viewport_free(viewport);
    REQUIRE(drawn == 0);
    REQUIRE(view.width < 2);
    REQUIRE(cf_dot_v3(view.lens.forward, view.lens.forward) == 0);
    return true;
}

/* Picking and bounds can run before any propagation, such as right after a
 * load or an edit, and still find a child where its parent places it. */
TEST_CASE(picking_and_bounds_place_children_under_their_parent)
{
    ecs_entity parent = add(COMPONENT_Transform.id, &(Transform){ 1.5f, 0 });
    ecs_entity child = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1, 0 }, 0xffffffffu);
    REQUIRE(ecs_world_record_add_pair(test_world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    CF_V3 placed = render_scene3d_project(&lens, (CF_V3){ 1.5f, 0, 0 });
    CF_V3 origin = render_scene3d_project(&lens, (CF_V3){ 0 });
    REQUIRE(
        render_scene3d_pick(test_world, &lens, (CF_V2){ placed.x, placed.y }, (render_skip){ 0 })
            .slot == child.slot
    );
    REQUIRE(!render_scene3d_pick(test_world, &lens, (CF_V2){ origin.x, origin.y }, (render_skip){ 0 })
                 .slot);
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, child, corners));
    REQUIRE(fabsf(corners[0].x - 1) < 1e-4f && fabsf(corners[7].x - 2) < 1e-4f);
    return true;
}

#if CHIBI_TEST_MATERIAL_GRAPHS
/* Model surfaces take the same exact placement before propagation. */
TEST_CASE(picking_and_bounds_place_child_models_under_their_parent)
{
    REQUIRE(test_register_model_sphere("ChildSphere", .5f));
    ecs_entity parent = add(COMPONENT_Transform.id, &(Transform){ 1.5f, 0 });
    ecs_entity child = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1, 0 }, 0xffffffffu);
    SdfSurface3D surface = { .enabled = true };
    Shader binding = { .shape = "ChildSphere" };
    REQUIRE(
        ecs_world_record_add(test_world, child, COMPONENT_SdfSurface3D.id, &surface, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(test_world, child, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(test_world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    CF_V3 placed = render_scene3d_project(&lens, (CF_V3){ 1.5f, 0, 0 });
    CF_V3 origin = render_scene3d_project(&lens, (CF_V3){ 0 });
    REQUIRE(
        render_scene3d_pick(test_world, &lens, (CF_V2){ placed.x, placed.y }, (render_skip){ 0 })
            .slot == child.slot
    );
    REQUIRE(!render_scene3d_pick(test_world, &lens, (CF_V2){ origin.x, origin.y }, (render_skip){ 0 })
                 .slot);
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, child, corners));
    REQUIRE(fabsf(corners[0].x - 1) < 1e-3f && fabsf(corners[7].x - 2) < 1e-3f);
    return true;
}
#endif

TEST_CASE(environment_ambient_lights_unlit_faces)
{
    add_box(0xffffffffu);
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    REQUIRE(top().colors.r == 0 && front().colors.r == 0);
    environment.ambient_sky = 0x00ff00ffu;
    environment.ambient_ground = 0x00ff00ffu;
    environment.ambient_intensity = .5f;
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    REQUIRE(render(&environment));
    REQUIRE(top().colors.g > 100 && top().colors.r == 0 && top().colors.b == 0);
    return true;
}

TEST_CASE(directional_light_shades_faces_it_faces)
{
    add_box(0xffffffffu);
    add_sun(0xff0000ffu, (CF_V3){ 0, -2, 0 });
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    CF_Pixel lit = top(), side = front();
    REQUIRE(
        lit.colors.r > 150 && lit.colors.r > 2 * lit.colors.g &&
        lit.colors.r > 2 * lit.colors.b
    );
    REQUIRE(side.colors.r < lit.colors.r / 4);
    return true;
}

TEST_CASE(scene_rejects_a_second_directional_light)
{
    add_box(0xffffffffu);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    Environment environment = environment_defaults();
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "only one Directional Light"));
    return true;
}

TEST_CASE(hidden_directional_lights_neither_shade_nor_count)
{
    add_box(0xffffffffu);
    add_sun(0xff0000ffu, (CF_V3){ 0, -2, 0 });
    DirectionalLight unseen = { .color = 0x00ff00ffu,
                                .intensity = 3,
                                .direction = { 0, -2, 0 } };
    ecs_entity hidden = add(COMPONENT_DirectionalLight.id, &unseen);
    RenderState state = { .hidden = true };
    assert(ecs_world_record_add(test_world, hidden, COMPONENT_RenderState.id, &state, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    runtime = engine_new(test_world, NULL);
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    CF_Pixel lit = top();
    REQUIRE(lit.colors.r > 150 && lit.colors.r > 2 * lit.colors.g);
    return true;
}

TEST_CASE(neutral_tone_mapping_keeps_saturated_colors)
{
    add_box(0xffffffffu);
    add_sun(0xff0000ffu, (CF_V3){ 0, -1, 0 })->intensity = 1;
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    CF_Pixel agx = top();
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    REQUIRE(render(&environment));
    CF_Pixel neutral = top();
    REQUIRE(agx.colors.r > 0 && agx.colors.g > 0 && agx.colors.b > 0);
    REQUIRE(neutral.colors.r > 100 && neutral.colors.g == 0 && neutral.colors.b == 0);
    environment.tone_mapping = TONE_MAPPING_NEUTRAL + 1;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_post_error(post), "tone mapping"));
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    environment.adaptation_speed = 101;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_post_error(post), "adaptation speed"));
    environment.adaptation_speed = .05f;
    REQUIRE(!render(&environment));
    return true;
}

static int distance(CF_Pixel a, CF_Pixel b)
{
    return abs(a.colors.r - b.colors.r) + abs(a.colors.g - b.colors.g) +
           abs(a.colors.b - b.colors.b);
}

TEST_CASE(auto_exposure_meters_the_frame_to_middle_gray)
{
    add_floor();
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    sun->intensity = .25f;
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    REQUIRE(render(&environment));
    CF_Pixel fixed_dim = top();
    sun->intensity *= 8;
    REQUIRE(render(&environment));
    CF_Pixel fixed_bright = top();
    REQUIRE(distance(fixed_dim, fixed_bright) > 60);

    /* Zero speed adapts fully every frame, even while scene time stands still. */
    environment.auto_exposure = true;
    environment.adaptation_speed = 0;
    REQUIRE(render(&environment));
    CF_Pixel bright = top();
    sun->intensity /= 8;
    REQUIRE(render(&environment));
    CF_Pixel dim = top();
    REQUIRE(distance(dim, bright) <= 6);
    /* sRGB of middle gray after Neutral's toe, 0.14, is about 105. */
    REQUIRE(abs(dim.colors.g - 105) <= 10);
    return true;
}

/* Neutral leaves values from 0.08 to its 0.76 knee at c - 0.04. */
static int neutral_srgb(float linear)
{
    float c = linear - .04f;
    return (int)lroundf((1.055f * powf(c, 1 / 2.4f) - .055f) * 255);
}

TEST_CASE(auto_exposure_meters_the_geometric_mean)
{
    /* Each half of the symmetric view sees one floor, four times apart in albedo. */
    add_solid_at(-100, (Geometry3D){ PRIMITIVE_BOX, 200, 400, .2f, -.1f }, 0xffffffffu);
    add_solid_at(100, (Geometry3D){ PRIMITIVE_BOX, 200, 400, .2f, -.1f }, 0x404040ffu);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    environment.auto_exposure = true;
    REQUIRE(render(&environment));
    float ratio = 255 / 64.0f;
    CF_Pixel bright = pixels[(SIDE / 2) * SIDE + SIDE / 4];
    CF_Pixel dark = pixels[(SIDE / 2) * SIDE + SIDE * 3 / 4];
    REQUIRE(abs(bright.colors.g - neutral_srgb(.18f * sqrtf(ratio))) <= 6);
    REQUIRE(abs(dark.colors.g - neutral_srgb(.18f / sqrtf(ratio))) <= 6);
    return true;
}

TEST_CASE(auto_exposure_ignores_an_empty_background)
{
    add_box(0xffffffffu);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 })->intensity = .25f;
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    environment.auto_exposure = true;
    REQUIRE(render(&environment));
    /* Only the sunlit top face is metered, so it lands on middle gray. */
    REQUIRE(abs(top().colors.g - 105) <= 10);
    REQUIRE(pixels[0].colors.g == 0);
    return true;
}

TEST_CASE(auto_exposure_meters_around_non_finite_texels)
{
    Environment environment = environment_defaults();
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    environment.auto_exposure = true;
    environment.adaptation_speed = 0;
    environment.bloom_strength = 0;
    REQUIRE(render_scene3d_post_prepare(post, SIDE, SIDE, &environment));
    /* A material can write NaN. Here every odd column is NaN and the rest 0.5,
     * as half floats, so filtering raw texels would spoil every metered tap. */
    enum
    {
        HALF_NAN = 0x7e00,
        HALF_0_5 = 0x3800,
        HALF_1 = 0x3c00
    };
    static unsigned short texels[PIXELS][4];
    for (int i = 0; i < PIXELS; i++) {
        unsigned short value = i % SIDE % 2 ? HALF_NAN : HALF_0_5;
        memcpy(texels[i], (unsigned short[]){ value, value, value, HALF_1 }, sizeof texels[i]);
    }
    cf_app_update(NULL);
    CF_Texture scene = cf_canvas_get_target(render_scene3d_post_target(post));
    cf_texture_update(scene, texels, (int)sizeof texels);
    REQUIRE(finish(0));
    /* The finite columns meter to middle gray, not the empty frame's fallback. */
    REQUIRE(abs(pixels[(SIDE / 2) * SIDE + SIDE / 2].colors.g - neutral_srgb(.18f)) <= 6);
    return true;
}

TEST_CASE(auto_exposure_adapts_over_scene_time)
{
    add_floor();
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    environment.auto_exposure = true;
    environment.adaptation_speed = 2;
    REQUIRE(render_at(&environment, 1));
    CF_Pixel settled = top();
    sun->intensity *= 8;
    REQUIRE(render_at(&environment, 1.1f));
    CF_Pixel adapting = top();
    REQUIRE(render_at(&environment, 20));
    CF_Pixel adapted = top();
    REQUIRE(adapting.colors.g > settled.colors.g + 30);
    REQUIRE(distance(adapted, settled) <= 6);
    /* Time running backward, as on a restart, snaps instead of easing. */
    sun->intensity /= 8;
    REQUIRE(render_at(&environment, 0));
    REQUIRE(distance(top(), settled) <= 6);
    /* Turning auto exposure back on snaps, even with no time passing. */
    environment.auto_exposure = false;
    REQUIRE(render_at(&environment, 0));
    sun->intensity *= 8;
    environment.auto_exposure = true;
    REQUIRE(render_at(&environment, 0));
    REQUIRE(distance(top(), settled) <= 6);
    /* A reset, as for a new scene, snaps even with time moving forward. */
    sun->intensity /= 8;
    render_scene3d_post_reset(post);
    REQUIRE(render_at(&environment, .1f));
    REQUIRE(distance(top(), settled) <= 6);
    return true;
}

TEST_CASE(slow_auto_exposure_eases_past_half_float_steps)
{
    add_floor();
    /* Bright enough that half floats hold the metered log2 luminance in coarse
     * steps; near the target this easing moves it by less than one a frame. */
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    sun->intensity = 2048;
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    environment.auto_exposure = true;
    environment.adaptation_speed = 0;
    REQUIRE(render(&environment));
    CF_Pixel target = top();
    sun->intensity /= 1.414f;
    REQUIRE(render(&environment));
    sun->intensity *= 1.414f;
    environment.adaptation_speed = 1.8f;
    bool drawn = true;
    for (int frame = 1; frame <= 110; frame++) {
        drawn &= render_at(&environment, frame / 60.0f);
    }
    REQUIRE(drawn && distance(top(), target) <= 6);
    return true;
}

/* A 2-unit box rests on a floor at the origin. Sunlight travels along +x and
 * down at 45 degrees, so the box shades the floor where 1 < x < 3. The floor
 * spans just the view, so no other solid reaches as close to the sun. */
static void add_box_on_floor()
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
}

static DirectionalLight *add_shadow_scene()
{
    add_box_on_floor();
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    return sun;
}

static Environment unlit_environment()
{
    Environment environment = environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    return environment;
}

static const CF_V3 shaded = { 1.6f, 0, .5f };
/* Sunlit floor at grazing and open spots, and the box's roof. */
static const CF_V3 sunlit[] = {
    { -1.6f, 0, .5f }, { -2.2f, 0, -.5f }, { -1.2f, 0, 1.5f },
    { 1.6f, 0, 1.6f }, { 0, 2, 0 },
};
enum
{
    SUNLIT = sizeof sunlit / sizeof *sunlit
};

TEST_CASE(directional_light_casts_shadows)
{
    DirectionalLight *sun = add_shadow_scene();
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    CF_Pixel open[SUNLIT];
    for (int i = 0; i < SUNLIT; i++) {
        open[i] = pixel_at(sunlit[i]);
    }
    /* Without shadows the floor is lit evenly on both sides of the box. */
    REQUIRE(open[0].colors.g > 60 && distance(pixel_at(shaded), open[0]) <= 3);
    sun->shadows = true;
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(pixel_at(shaded).colors.g == 0);
    /* Lit surfaces keep all their light: nothing shadows itself. */
    for (int i = 0; i < SUNLIT; i++) {
        REQUIRE(distance(pixel_at(sunlit[i]), open[i]) <= 3);
    }
    return true;
}

TEST_CASE(solids_outside_the_view_cast_shadows_into_it)
{
    DirectionalLight *sun = add_shadow_scene();
    sun->shadows = true;
    /* A pillar well beyond the left edge of the view, and nearer the sun than
     * the floor, shades the floor up to x = 1. */
    add_solid_at(-8, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 8, 4 }, 0xffffffffu);
    /* A crowd far away splits collection across workers where the host has
     * them, so the pillar arrives through a worker's buffer. */
    for (int i = 0; i < 1100; i++) {
        add_solid_at(60, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1, 1 }, 0xffffffffu);
    }
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(sunlit[0]).colors.g == 0);
    REQUIRE(pixel_at(sunlit[3]).colors.g > 60);
    return true;
}

TEST_CASE(shadow_pass_leaves_earlier_draws_queued)
{
    add_box(0xffffffffu);
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->shadows = true;
    Environment environment = unlit_environment();
    backdrop = true;
    REQUIRE(render(&environment));
    REQUIRE(pixels[0].colors.g > 100);
    return true;
}

/* Four walls face the camera across a deep view, each a fifth as far as the
 * next and filling one column of the image. Sunlight travels away from the
 * camera and down at 45 degrees, so a cube in front of each wall shades the
 * wall's middle. The cubes alternate between just above the view and in it,
 * so both culled casters and solids in view cast past the first cascade. */
static const float wall_depths[] = { 2, 10, 50, 250 };
enum
{
    WALLS = sizeof wall_depths / sizeof *wall_depths
};

/* A point on a wall's face, centered in its column, `height` wall depths above
 * the line of sight. 0.414 is the tangent of half the field of view. */
static CF_V3 on_wall(int wall, float height)
{
    float depth = wall_depths[wall];
    return (CF_V3){ (-.75f + .5f * wall) * .414f * depth, height * depth, -.99f * depth };
}

/* Adds the walls whose bits are set in `walls`. */
static DirectionalLight *add_walls(unsigned walls)
{
    lens = (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 0 }, (CF_V3){ 0, 0, -1 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 1000),
        .eye = { 0 },
    };
    for (int i = 0; i < WALLS; i++) {
        if (!(walls >> i & 1)) {
            continue;
        }
        float depth = wall_depths[i];
        CF_V3 wall = on_wall(i, 0);
        wall.z = -depth;
        add_solid(wall, (Geometry3D){ PRIMITIVE_BOX, .2f * depth, .02f * depth, .5f * depth }, 0xffffffffu);
        float gap = i % 2 ? .25f : .4f;
        CF_V3 cube = { wall.x, gap * depth, -(1 - gap) * depth };
        add_solid(cube, (Geometry3D){ PRIMITIVE_BOX, .1f * depth, .1f * depth, .1f * depth }, 0xffffffffu);
    }
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 });
    sun->intensity = 1;
    return sun;
}

/* How much green a wall's shadow gained or lost since `before`, summed over
 * a grid across the shadow and its edges. */
static int softened(const CF_Pixel *before, int wall)
{
    float depth = wall_depths[wall];
    int change = 0;
    for (int y = -4; y <= 4; y++) {
        for (int x = -4; x <= 4; x++) {
            CF_V3 at = on_wall(wall, .02f * y);
            at.x += .02f * x * depth;
            int i = pixel_index(at);
            change += abs(pixels[i].colors.g - before[i].colors.g);
        }
    }
    return change;
}

TEST_CASE(deep_views_split_sun_shadows_into_cascades)
{
    DirectionalLight *sun = add_walls(0xf);
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    CF_Pixel open[WALLS][2];
    for (int i = 0; i < WALLS; i++) {
        open[i][0] = pixel_at(on_wall(i, -.15f));
        open[i][1] = pixel_at(on_wall(i, .15f));
    }
    sun->shadows = true;
    /* A single map across the whole view would blur the nearest shadow away.
     * A 5-degree sun leaves each shadow's core dark and its surroundings lit. */
    CF_Pixel hard[PIXELS];
    for (int soft = 0; soft < 2; soft++) {
        sun->angular_diameter = 5.0f * soft;
        REQUIRE(render(&environment));
        REQUIRE(render_scene3d_last_work(scene3d).cascades == WALLS);
        for (int i = 0; i < WALLS; i++) {
            REQUIRE(pixel_at(on_wall(i, 0)).colors.g == 0);
            REQUIRE(
                open[i][0].colors.g > 60 &&
                distance(pixel_at(on_wall(i, -.15f)), open[i][0]) <= 3
            );
            REQUIRE(distance(pixel_at(on_wall(i, .15f)), open[i][1]) <= 3);
        }
        if (!soft) {
            memcpy(hard, pixels, sizeof hard);
        }
    }
    /* It softens the edges in every cascade. */
    for (int i = 0; i < WALLS; i++) {
        REQUIRE(softened(hard, i) > 200);
    }
    return true;
}

/* With nothing between the nearest and farthest walls, the middle depth ranges
 * take no cascade, so two squares sit side by side in the atlas. */
TEST_CASE(empty_depth_ranges_take_no_cascade)
{
    DirectionalLight *sun = add_walls(0x9);
    sun->shadows = true;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).cascades == 2);
    for (int i = 0; i < WALLS; i += WALLS - 1) {
        REQUIRE(pixel_at(on_wall(i, 0)).colors.g == 0);
        REQUIRE(pixel_at(on_wall(i, -.15f)).colors.g > 60);
    }
    /* Turning shadows off lights the walls again. */
    sun->shadows = false;
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).cascades == 0);
    REQUIRE(distance(pixel_at(on_wall(0, 0)), pixel_at(on_wall(0, -.15f))) <= 3);
    return true;
}

/* The floor reaches far past the view, but only the depths the view shows
 * count, so one cascade covers them. */
TEST_CASE(shadow_cascades_cover_only_what_the_view_shows)
{
    add_floor();
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).cascades == 1);
    REQUIRE(pixel_at(shaded).colors.g == 0);
    REQUIRE(pixel_at(sunlit[0]).colors.g > 60);
    return true;
}

/* A thin wall 4 units tall runs along z from -3 to 3 on a floor. Sunlight
 * travels along +x and down at 45 degrees, so the wall shades the floor where
 * 0 < x < 4, and the floor at x lies under the wall's end at height x. The
 * camera looks straight down at that end, z = 3. */
static DirectionalLight *add_wall_scene()
{
    lens = (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 2, 10, 3 }, (CF_V3){ 2, 0, 3 }, (CF_V3){ 0, 0, -1 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 100),
        .eye = { 2, 10, 3 },
    };
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 40, 40, .2f, -.1f }, 0xffffffffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, .2f, 6, 4, 2 }, 0xffffffffu);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    return sun;
}

/* Floor just inside and just outside the shadow's side, near the wall's foot
 * and near its top, then deep inside the shadow. */
static const CF_V3 wall_edges[] = {
    { .5f, 0, 2.85f },  { .5f, 0, 3.15f }, { 3.5f, 0, 2.85f },
    { 3.5f, 0, 3.15f }, { 2, 0, 1 },
};
enum
{
    WALL_EDGES = sizeof wall_edges / sizeof *wall_edges
};

TEST_CASE(sun_size_softens_shadows_away_from_casters)
{
    DirectionalLight *sun = add_wall_scene();
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    CF_Pixel open[WALL_EDGES];
    for (int i = 0; i < WALL_EDGES; i++) {
        open[i] = pixel_at(wall_edges[i]);
        REQUIRE(open[i].colors.g > 60);
    }
    /* A point-sized sun casts hard edges everywhere. */
    sun->shadows = true;
    REQUIRE(render(&environment));
    for (int i = 0; i < WALL_EDGES; i++) {
        bool lit = i % 2 && i < 4;
        REQUIRE(
            lit ? distance(pixel_at(wall_edges[i]), open[i]) <= 3
                : pixel_at(wall_edges[i]).colors.g == 0
        );
    }
    /* A 10-degree sun spreads each edge by 0.25 of the caster's height: the
     * edge stays hard at the wall's foot and blurs past 0.15 near its top,
     * while the shadow's core stays dark. */
    sun->angular_diameter = 10;
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(pixel_at(wall_edges[0]).colors.g == 0);
    REQUIRE(distance(pixel_at(wall_edges[1]), open[1]) <= 3);
    REQUIRE(pixel_at(wall_edges[2]).colors.g > 10);
    REQUIRE(pixel_at(wall_edges[3]).colors.g < open[3].colors.g - 10);
    REQUIRE(pixel_at(wall_edges[4]).colors.g == 0);
    /* A short wall just past the view's near edge, which the camera culls,
     * still shades the floor inside the view that its penumbra reaches. The
     * edge is the cascade's low side, which no rounding widens. */
    CF_V3 fringe = { 5.5f, 0, -1.1f };
    CF_Pixel lit = pixel_at(fringe);
    add_solid((CF_V3){ 2, 2, -1.5f }, (Geometry3D){ PRIMITIVE_BOX, .2f, .4f, 4 }, 0xffffffffu);
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(fringe).colors.g < lit.colors.g - 10);
    sun->angular_diameter = 21;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "angular diameter"));
    return true;
}

/* A white point light. */
static ecs_entity add_lamp(CF_V3 at, float intensity, float radius)
{
    ecs_entity lamp = add(COMPONENT_Transform.id, &(Transform){ at.x, at.z });
    PointLight light = { .color = 0xffffffffu,
                         .intensity = intensity,
                         .radius = radius,
                         .elevation = at.y };
    assert(ecs_world_record_add(test_world, lamp, COMPONENT_PointLight.id, &light, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return lamp;
}

static void place_lamp(ecs_entity lamp, CF_V3 at)
{
    Transform *transform = Transform_get(test_world, lamp);
    transform->x = at.x;
    transform->y = at.z;
    PointLight_get(test_world, lamp)->elevation = at.y;
    Geometry3D_get(test_world, lamp)->elevation = at.y;
}

/* Whether the segment from a to b passes through the box on the floor,
 * grown on every side by `grow`. */
static bool crosses_box(CF_V3 a, CF_V3 b, float grow)
{
    float from[] = { a.x, a.y, a.z }, to[] = { b.x, b.y, b.z };
    float low[] = { -1 - grow, -grow, -1 - grow },
          high[] = { 1 + grow, 2 + grow, 1 + grow };
    float enter = 0, leave = 1;
    for (int k = 0; k < 3; k++) {
        float step = to[k] - from[k];
        if (step == 0 && (from[k] < low[k] || from[k] > high[k])) {
            return false;
        }
        if (step == 0) {
            continue;
        }
        float a_t = (low[k] - from[k]) / step, b_t = (high[k] - from[k]) / step;
        enter = fmaxf(enter, fminf(a_t, b_t));
        leave = fminf(leave, fmaxf(a_t, b_t));
    }
    return enter < leave;
}

/* How far a shadow's edge may lie from where the box puts it: the lookup's
 * filter and bias, and a pixel's footprint on the floor. */
static const float edge_slack = .1f;

/* Checks a grid of the floor around the box that the camera sees. Each point
 * shows the frame in `shown` for the set of casting lamps, one bit each,
 * whose light the box blocks from it; a null frame is black. Points within
 * edge_slack of a shadow's edge go unchecked. Counts the points each frame
 * shows lit, or black. */
static bool floor_matches(const CF_V3 *casting, int count, CF_Pixel *const *shown, int *checked)
{
    CF_V3 eye = lens.eye;
    for (float x = -3; x <= 3; x += .25f) {
        for (float z = -3; z <= 3; z += .25f) {
            CF_V3 point = { x, 0, z };
            CF_V3 ndc = render_scene3d_project(&lens, point);
            if (fabsf(ndc.x) > .95f || fabsf(ndc.y) > .95f ||
                crosses_box(eye, point, edge_slack)) {
                continue;
            }
            int blocked = 0;
            bool edge = false;
            for (int i = 0; i < count; i++) {
                bool inside = crosses_box(point, casting[i], -edge_slack);
                edge = edge || inside != crosses_box(point, casting[i], edge_slack);
                blocked |= inside << i;
            }
            if (edge) {
                continue;
            }
            int i = pixel_index(point);
            CF_Pixel expected = shown[blocked] ? shown[blocked][i]
                                               : (CF_Pixel){ .colors.a = 255 };
            if (distance(pixels[i], expected) > 3) {
                return false;
            }
            checked[blocked] += !shown[blocked] || expected.colors.g > 10;
        }
    }
    return true;
}

/* A point or spot lamp's intensity, and whether it casts shadows. */
static float *lamp_intensity(ecs_entity lamp)
{
    if (PointLight_has(test_world, lamp)) {
        return &PointLight_get(test_world, lamp)->intensity;
    }
    return &SpotLight_get(test_world, lamp)->intensity;
}

static bool *lamp_shadows(ecs_entity lamp)
{
    if (PointLight_has(test_world, lamp)) {
        return &PointLight_get(test_world, lamp)->shadows;
    }
    return &SpotLight_get(test_world, lamp)->shadows;
}

/* Renders each set of lamps without shadows into frames[mask], with the
 * lamps whose bits the mask sets turned off, then every lamp with shadows.
 * With every lamp off the frame is black, so it is left out. */
static bool render_lamps(const ecs_entity *lamps, int count, CF_Pixel (*frames)[PIXELS])
{
    Environment environment = unlit_environment();
    float intensities[2];
    assert(count <= 2);
    for (int i = 0; i < count; i++) {
        intensities[i] = *lamp_intensity(lamps[i]);
    }
    for (int mask = 0; mask < (1 << count) - 1; mask++) {
        for (int i = 0; i < count; i++) {
            *lamp_intensity(lamps[i]) = mask >> i & 1 ? 0 : intensities[i];
        }
        if (!render(&environment)) {
            return false;
        }
        memcpy(frames[mask], pixels, sizeof pixels);
    }
    for (int i = 0; i < count; i++) {
        *lamp_intensity(lamps[i]) = intensities[i];
        *lamp_shadows(lamps[i]) = true;
    }
    return render(&environment);
}

/* Spots for one lamp: high to one side of the box, above it, and low beside
 * two of its corners, so shadows cross from one face to the next. */
static const CF_V3 lamp_spots[] = {
    { -3, 4, -1 },
    { 0, 4, 0 },
    { 2.2f, 1, 2.2f },
    { -2.4f, .8f, 1.6f },
};
enum
{
    LAMP_SPOTS = sizeof lamp_spots / sizeof *lamp_spots
};

/* The box's roof and front face, which nothing shades. */
static const CF_V3 box_faces[] = { { 0, 2, 0 }, { 0, 1, 1 } };

TEST_CASE(point_lights_cast_shadows_across_faces)
{
    add_box_on_floor();
    ecs_entity lamp = add_lamp(lamp_spots[0], 2, 20);
    /* The lamp's own bulb casts nothing for it. */
    Geometry3D bulb = { PRIMITIVE_BOX, .6f, .6f, .6f };
    assert(ecs_world_record_add(test_world, lamp, COMPONENT_Geometry3D.id, &bulb, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    static CF_Pixel open[1][PIXELS];
    CF_Pixel *shown[] = { open[0], NULL };
    for (int spot = 0; spot < LAMP_SPOTS; spot++) {
        place_lamp(lamp, lamp_spots[spot]);
        PointLight_get(test_world, lamp)->shadows = false;
        REQUIRE(render_lamps(&lamp, 1, open));
        REQUIRE(!render_scene3d_error(scene3d)[0]);
        REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 1);
        int checked[2] = { 0 };
        REQUIRE(floor_matches(&lamp_spots[spot], 1, shown, checked));
        REQUIRE(checked[0] > 10 && checked[1] > 10);
        /* Lit surfaces keep all their light: nothing shadows itself. */
        for (int i = 0; i < 2; i++) {
            int at = pixel_index(box_faces[i]);
            REQUIRE(distance(pixels[at], open[0][at]) <= 3);
        }
    }
    return true;
}

/* A sphere above the view, level with the lamp, shares a face with the floor
 * and the box, so that face draws its boxes and then the sphere as ranges of
 * one batch. The sphere must stay in its face: the floor around the box shows
 * only the box's shadow. */
TEST_CASE(point_lights_cast_from_mixed_primitives)
{
    add_box_on_floor();
    add_solid((CF_V3){ 0, 4, -.5f }, (Geometry3D){ PRIMITIVE_SPHERE, 1, 1, 1 }, 0xffffffffu);
    ecs_entity lamp = add_lamp(lamp_spots[0], 2, 20);
    static CF_Pixel open[1][PIXELS];
    CF_Pixel *shown[] = { open[0], NULL };
    REQUIRE(render_lamps(&lamp, 1, open));
    int checked[2] = { 0 };
    REQUIRE(floor_matches(lamp_spots, 1, shown, checked));
    REQUIRE(checked[0] > 10 && checked[1] > 10);
    return true;
}

/* A sphere lit from between three of the lamp's faces keeps all its light:
 * its surface turns away from each face's axis, and nothing shadows itself. */
TEST_CASE(point_lights_leave_curved_surfaces_lit)
{
    add_solid((CF_V3){ 0, 1, 0 }, (Geometry3D){ PRIMITIVE_SPHERE, 2, 2, 2 }, 0xffffffffu);
    ecs_entity lamp = add_lamp((CF_V3){ 1.5f, 2.5f, 2.5f }, 2, 20);
    static CF_Pixel open[1][PIXELS];
    REQUIRE(render_lamps(&lamp, 1, open));
    /* Within two levels a channel: the sphere's flat facets stray a little
     * from the smooth normals the lift follows. */
    int lit = 0, darker = 0;
    for (int i = 0; i < PIXELS; i++) {
        lit += open[0][i].colors.g > 10;
        darker += distance(pixels[i], open[0][i]) > 6;
    }
    REQUIRE(lit > 300 && darker == 0);
    return true;
}

/* Two lamps each cast from their own maps, though a brighter light near the
 * camera that casts nothing, and reaches nothing in view, outranks both. */
TEST_CASE(point_lights_cast_shadows_from_their_own_maps)
{
    add_box_on_floor();
    add_lamp((CF_V3){ 0, 4, 4 }, 10, 1);
    CF_V3 spots[] = { lamp_spots[0], { -2.5f, 3, 2 } };
    ecs_entity lamps[] = { add_lamp(spots[0], 2, 20), add_lamp(spots[1], 2, 20) };
    static CF_Pixel frames[3][PIXELS];
    CF_Pixel *shown[] = { frames[0], frames[1], frames[2], NULL };
    REQUIRE(render_lamps(lamps, 2, frames));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 2);
    int checked[4] = { 0 };
    REQUIRE(floor_matches(spots, 2, shown, checked));
    for (int i = 0; i < 4; i++) {
        REQUIRE(checked[i] > 5);
    }
    return true;
}

/* Five lamps cast, but only the four a view ranks highest get maps. The four
 * share a spot; the dimmest, added first, lights the floor around the box
 * unblocked, and alone where the box blocks the four. */
TEST_CASE(point_light_shadows_stop_at_four_lights)
{
    add_box_on_floor();
    ecs_entity lamps[5];
    for (int i = 0; i < 5; i++) {
        lamps[i] = add_lamp(lamp_spots[i ? 0 : 1], i ? .5f : .4f, 20);
    }
    Environment environment = unlit_environment();
    static CF_Pixel frames[2][PIXELS];
    for (int frame = 1; frame >= 0; frame--) {
        for (int i = 1; i < 5; i++) {
            PointLight_get(test_world, lamps[i])->intensity = frame ? 0 : .5f;
        }
        REQUIRE(render(&environment));
        memcpy(frames[frame], pixels, sizeof pixels);
    }
    for (int i = 0; i < 5; i++) {
        PointLight_get(test_world, lamps[i])->shadows = true;
    }
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 4);
    CF_Pixel *shown[] = { frames[0], frames[1] };
    int checked[2] = { 0 };
    REQUIRE(floor_matches(lamp_spots, 1, shown, checked));
    REQUIRE(checked[0] > 10 && checked[1] > 10);
    /* Floor the box would block from the dim lamp. */
    CF_V3 past = { -1.5f, 0, .25f };
    REQUIRE(crosses_box(past, lamp_spots[1], -edge_slack));
    REQUIRE(distance(pixel_at(past), frames[0][pixel_index(past)]) <= 3);
    return true;
}

/* A long wall casts for a lamp whose radius reaches only one end of it. */
TEST_CASE(long_solids_cast_for_point_lights_near_one_end)
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    add_solid((CF_V3){ 0, .5f, -14 }, (Geometry3D){ PRIMITIVE_BOX, .2f, 30, 1 }, 0xffffffffu);
    ecs_entity lamp = add_lamp((CF_V3){ -1.5f, 1.5f, .5f }, 2, 4);
    static CF_Pixel open[1][PIXELS];
    REQUIRE(render_lamps(&lamp, 1, open));
    CF_V3 lit = { -1, 0, 0 }, shaded = { 1, 0, 0 };
    REQUIRE(
        open[0][pixel_index(shaded)].colors.g > 10 && pixel_at(shaded).colors.g == 0
    );
    REQUIRE(distance(pixel_at(lit), open[0][pixel_index(lit)]) <= 3);
    return true;
}

/* A small slab above the view, which the camera culls, still blocks a lamp
 * above it. */
TEST_CASE(solids_outside_the_view_cast_for_point_lights)
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    add_solid((CF_V3){ 0, 6, 0 }, (Geometry3D){ PRIMITIVE_BOX, .5f, .5f, .2f }, 0xffffffffu);
    ecs_entity lamp = add_lamp((CF_V3){ 0, 8, 0 }, 3, 12);
    static CF_Pixel open[1][PIXELS];
    REQUIRE(render_lamps(&lamp, 1, open));
    CF_V3 lit = { -2, 0, 1 }, shaded = { 0, 0, .5f };
    REQUIRE(
        open[0][pixel_index(shaded)].colors.g > 10 && pixel_at(shaded).colors.g == 0
    );
    REQUIRE(distance(pixel_at(lit), open[0][pixel_index(lit)]) <= 3);
    return true;
}

/* A white spot light pointing from `at` toward `target`. */
static ecs_entity add_spot(CF_V3 at, CF_V3 target, float inner, float outer)
{
    ecs_entity spot = add(COMPONENT_Transform.id, &(Transform){ at.x, at.z });
    SpotLight light = { .color = 0xffffffffu,
                        .intensity = 2,
                        .radius = 20,
                        .elevation = at.y,
                        .direction = cf_sub_v3(target, at),
                        .inner_angle = inner,
                        .outer_angle = outer };
    assert(ecs_world_record_add(test_world, spot, COMPONENT_SpotLight.id, &light, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return spot;
}

/* A spot light pointing down lights the floor inside its inner angle like a
 * point light in its place, fades across its edge, and leaves the floor past
 * its outer angle dark. */
TEST_CASE(spot_lights_light_their_cone)
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    CF_V3 at = { 0, 2, 0 };
    ecs_entity lamp = add_lamp(at, 2, 20);
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel point[PIXELS];
    memcpy(point, pixels, sizeof pixels);
    PointLight_get(test_world, lamp)->intensity = 0;
    /* 30 and 60 degrees across: full strength out to 15 degrees off its
     * axis, and none past 30. */
    ecs_entity spot = add_spot(at, (CF_V3){ 0 }, 30, 60);
    REQUIRE(render(&environment));
    CF_V3 inside[] = { { 0, 0, 0 }, { .4f, 0, 0 }, { -.4f, 0, 0 } };
    for (int i = 0; i < 3; i++) {
        REQUIRE(point[pixel_index(inside[i])].colors.g > 10);
        REQUIRE(distance(pixel_at(inside[i]), point[pixel_index(inside[i])]) <= 3);
    }
    /* 39 degrees off: dark. */
    CF_V3 outside = { -1.6f, 0, 0 };
    REQUIRE(
        point[pixel_index(outside)].colors.g > 10 && pixel_at(outside).colors.g == 0
    );
    /* About 23 degrees off, the light falls off with the square of how far
     * the point lies into the edge: a point lamp dimmed by that much matches. */
    CF_V3 edge = pixel_floor((CF_V3){ .85f, 0, 0 });
    float cosine = -cf_norm_v3(cf_sub_v3(edge, at)).y, inner = cosf(CF_PI / 12),
          outer = cosf(CF_PI / 6);
    float share = (cosine - outer) / (inner - outer);
    REQUIRE(share > .2f && share < .8f);
    CF_Pixel faded = pixel_at(edge);
    SpotLight_get(test_world, spot)->intensity = 0;
    PointLight_get(test_world, lamp)->intensity = 2 * share * share;
    REQUIRE(render(&environment));
    REQUIRE(faded.colors.g > 10 && distance(pixel_at(edge), faded) <= 3);
    return true;
}

/* A spot light aimed across the box casts its shadow on the floor, from a cone
 * at a slant and from one pointing straight down. */
TEST_CASE(spot_lights_cast_shadows)
{
    add_box_on_floor();
    CF_V3 spots[][2] = { { { -3, 4, -1 }, { 1, 0, .5f } },
                         { { -1.6f, 5, .4f }, { -1.6f, 0, .4f } } };
    static CF_Pixel open[1][PIXELS];
    CF_Pixel *shown[] = { open[0], NULL };
    for (int i = 0; i < 2; i++) {
        ecs_entity lamp = add_spot(spots[i][0], spots[i][1], 90, 120);
        REQUIRE(render_lamps(&lamp, 1, open));
        REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 1);
        int checked[2] = { 0 };
        REQUIRE(floor_matches(spots[i], 1, shown, checked));
        REQUIRE(checked[0] > 10 && checked[1] > 10);
        assert(ecs_world_destroy_entity(test_world, lamp) == ECS_OK);
        assert(ecs_world_apply_commands(test_world) == ECS_OK);
    }
    return true;
}

/* A spot light and a point light each cast from their own atlas rows. */
TEST_CASE(spot_and_point_lights_share_shadow_maps)
{
    add_box_on_floor();
    CF_V3 spots[] = { lamp_spots[0], { -2.5f, 3, 2 } };
    ecs_entity lamps[] = { add_spot(spots[0], (CF_V3){ 1, 0, .5f }, 90, 120),
                           add_lamp(spots[1], 2, 20) };
    static CF_Pixel frames[3][PIXELS];
    CF_Pixel *shown[] = { frames[0], frames[1], frames[2], NULL };
    REQUIRE(render_lamps(lamps, 2, frames));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 2);
    int checked[4] = { 0 };
    REQUIRE(floor_matches(spots, 2, shown, checked));
    for (int i = 0; i < 4; i++) {
        REQUIRE(checked[i] > 5);
    }
    return true;
}

/* Floor straight below a spot light high above it, with full strength at
 * every outer angle the light accepts, even at its narrowest with a hard edge;
 * angles out of range are errors. */
TEST_CASE(spot_light_angles_must_be_in_range)
{
    add_floor();
    CF_V3 at = { 0, 50, 0 }, below = pixel_floor((CF_V3){ 0 });
    ecs_entity lamp = add_lamp(at, 2, 100);
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    CF_Pixel point = pixel_at(below);
    REQUIRE(point.colors.g > 10);
    PointLight_get(test_world, lamp)->intensity = 0;
    SpotLight *spot = SpotLight_get(test_world, add_spot(at, below, 2, 2));
    spot->radius = 100;
    float accepted[][2] = { { 2, 2 }, { 0, 2 }, { 160, 160 } };
    for (int i = 0; i < 3; i++) {
        spot->inner_angle = accepted[i][0];
        spot->outer_angle = accepted[i][1];
        REQUIRE(render(&environment));
        REQUIRE(distance(pixel_at(below), point) <= 3);
    }
    spot->inner_angle = 0;
    float outer[] = { 1.9f, 161, NAN };
    for (int i = 0; i < 3; i++) {
        spot->outer_angle = outer[i];
        REQUIRE(!render(&environment));
        REQUIRE(strstr(render_scene3d_error(scene3d), "Spot Light outer angle"));
    }
    spot->outer_angle = 160;
    float inner[] = { -1, 161, NAN };
    for (int i = 0; i < 3; i++) {
        spot->inner_angle = inner[i];
        REQUIRE(!render(&environment));
        REQUIRE(strstr(render_scene3d_error(scene3d), "Spot Light inner angle"));
    }
    return true;
}

/* Near the edge of a hard-edged spot light 160 degrees across, the bias lifts
 * lookups past its face. They read that face's edge, not the row of the point
 * light before it, whose faces a box beside the lamp fills with near depths. */
TEST_CASE(wide_spot_lights_read_only_their_face)
{
    add_floor();
    add_solid((CF_V3){ 7.5f, 1, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 6, 6 }, 0xffffffffu);
    ecs_entity lamp = add_lamp((CF_V3){ 6, 1, 0 }, 2, 20);
    PointLight_get(test_world, lamp)->shadows = true;
    CF_V3 at = { 0, .6f, -2.5f };
    SpotLight *spot = SpotLight_get(test_world, add_spot(at, (CF_V3){ at.x, 0, at.z }, 160, 160));
    spot->radius = 4;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel open[PIXELS];
    memcpy(open, pixels, sizeof pixels);
    spot->shadows = true;
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 2);
    /* The cone's edge toward the camera lies 3.4 past the light along z, and
     * lookups from 3.25 on lift past the face. */
    for (float x = -.6f; x <= .6f; x += .1f) {
        for (float z = .78f; z <= .9f; z += .04f) {
            CF_V3 point = { x, 0, z };
            REQUIRE(open[pixel_index(point)].colors.g > 10);
            REQUIRE(distance(pixel_at(point), open[pixel_index(point)]) <= 3);
        }
    }
    return true;
}

enum
{
    SKY_WIDTH = 64,
    SKY_HEIGHT = SKY_WIDTH / 2
};
static const char *const sky_key = "content/skies/test.hdr";

/* Publishes the RGBE texels `rgbe` as the sky, row 0 looking up. Tests keep
 * values under Neutral's knee, which leaves hues as they are. */
static void publish_sky(const unsigned char (*rgbe)[SKY_WIDTH][4])
{
    render_sky sky = { .width = SKY_WIDTH,
                       .height = SKY_HEIGHT,
                       .texels = xmalloc(SKY_WIDTH * SKY_HEIGHT * 4) };
    memcpy(sky.texels, rgbe, SKY_WIDTH * SKY_HEIGHT * 4);
    render_sky_publish(sky_key, &sky);
}

static void publish_uniform_sky(const unsigned char rgbe[4])
{
    static unsigned char texels[SKY_HEIGHT][SKY_WIDTH][4];
    for (int y = 0; y < SKY_HEIGHT; y++) {
        for (int x = 0; x < SKY_WIDTH; x++) {
            memcpy(texels[y][x], rgbe, 4);
        }
    }
    publish_sky(texels);
}

/* Looks level along -Z from the origin: +X lies to the right and up on top. */
static void look_level()
{
    look((CF_V3){ 0 }, (CF_V3){ 0, 0, -1 }, (CF_V3){ 0, 1, 0 });
}

static Environment sky_environment()
{
    Environment environment = environment_defaults();
    environment.tone_mapping = TONE_MAPPING_NEUTRAL;
    environment.sky = sky_key;
    return environment;
}

static CF_Pixel pixel(int x, int y)
{
    return pixels[y * SIDE + x];
}

/* The direction sky texel (x, y) looks along. Row 0 looks up, the middle
 * column along -Z and the column three quarters across along +X. */
static CF_V3 sky_ray(int x, int y)
{
    float elevation = (.5f - (y + .5f) / SKY_HEIGHT) * 3.14159265f;
    float azimuth = ((x + .5f) / SKY_WIDTH - .5f) * 2 * 3.14159265f;
    return (CF_V3){ cosf(elevation) * sinf(azimuth),
                    sinf(elevation),
                    -cosf(elevation) * cosf(azimuth) };
}

/* A sky colored by the octant each texel looks into: red where it looks
 * toward +X, green toward +Y and blue toward +Z. */
static void publish_octant_sky()
{
    static unsigned char texels[SKY_HEIGHT][SKY_WIDTH][4];
    for (int y = 0; y < SKY_HEIGHT; y++) {
        for (int x = 0; x < SKY_WIDTH; x++) {
            CF_V3 ray = sky_ray(x, y);
            unsigned char rgbe[4] = { ray.x > 0 ? 128 : 0,
                                      ray.y > 0 ? 128 : 0,
                                      ray.z > 0 ? 128 : 0,
                                      128 };
            memcpy(texels[y][x], rgbe, 4);
        }
    }
    publish_sky(texels);
}

/* The axes the quadratic sky brightens toward, one per channel: +X, +Y and
 * the diagonal between +X and +Z. */
static const CF_V3 quadratic_axes[] = { { 1, 0, 0 },
                                        { 0, 1, 0 },
                                        { .70710678f, 0, .70710678f } };

/* A sky whose channel c holds (a_c . d)^2 toward direction d. */
static void publish_quadratic_sky()
{
    static unsigned char texels[SKY_HEIGHT][SKY_WIDTH][4];
    for (int y = 0; y < SKY_HEIGHT; y++) {
        for (int x = 0; x < SKY_WIDTH; x++) {
            CF_V3 ray = sky_ray(x, y);
            for (int c = 0; c < 3; c++) {
                float cosine = cf_dot_v3(quadratic_axes[c], ray);
                /* Mantissa over 256 under exponent 128, capped at 255. */
                texels[y][x][c] = (unsigned char)fminf(roundf(cosine * cosine * 256), 255);
            }
            texels[y][x][3] = 128;
        }
    }
    publish_sky(texels);
}

/* Where world direction d points in the sky turned `quarters` quarter turns
 * counterclockwise from above. */
static CF_V3 sky_frame(CF_V3 d, int quarters)
{
    for (int i = 0; i < quarters; i++) {
        d = (CF_V3){ -d.z, d.y, d.x };
    }
    return d;
}

/* Whether the pixel at (x, y) shows the octant its view ray looks into, with
 * the sky turned `quarters` quarter turns. */
static bool shows_octant(int x, int y, int quarters)
{
    CF_V2 ndc = { (x + .5f) * 2 / SIDE - 1, 1 - (y + .5f) * 2 / SIDE };
    CF_V3 ray = sky_frame(render_scene3d_ray(&lens, ndc).d, quarters);
    CF_Pixel shown = pixel(x, y);
    REQUIRE(ray.x > 0 ? shown.colors.r > 100 : !shown.colors.r);
    REQUIRE(ray.y > 0 ? shown.colors.g > 100 : !shown.colors.g);
    REQUIRE(ray.z > 0 ? shown.colors.b > 100 : !shown.colors.b);
    return true;
}

/* Looking along each axis, unturned and turned a quarter, every corner of
 * the view shows the sky in the direction it looks: each cube face keeps
 * its orientation on the GPU. */
TEST_CASE(sky_fills_the_view_in_its_directions)
{
    publish_octant_sky();
    static const CF_V3 views[][2] = {
        { { 0, 0, -1 }, { 0, 1, 0 } }, { { 0, 0, 1 }, { 0, 1, 0 } },
        { { 1, 0, 0 }, { 0, 1, 0 } },  { { -1, 0, 0 }, { 0, 1, 0 } },
        { { 0, 1, 0 }, { 0, 0, -1 } }, { { 0, -1, 0 }, { 0, 0, 1 } }
    };
    Environment environment = sky_environment();
    for (int quarters = 0; quarters < 2; quarters++) {
        environment.sky_rotation = 90.0f * quarters;
        for (int i = 0; i < 6; i++) {
            look((CF_V3){ 0 }, views[i][0], views[i][1]);
            REQUIRE(render(&environment));
            int corners[] = { 8, SIDE - 8 };
            for (int j = 0; j < 4; j++) {
                REQUIRE(shows_octant(corners[j % 2], corners[j / 2], quarters));
            }
        }
    }
    return true;
}

/* The pixel center's view ray, offset so `center` lies at the origin. */
static CF_Ray3 pixel_ray(int x, int y, CF_V3 center)
{
    CF_V2 ndc = { (x + .5f) * 2 / SIDE - 1, 1 - (y + .5f) * 2 / SIDE };
    CF_Ray3 ray = render_scene3d_ray(&lens, ndc);
    ray.p = cf_sub_v3(ray.p, center);
    return ray;
}

/* Whether the ray meets a cube of half size `half` at the origin, and where
 * it enters which axis's face. */
static bool meets_cube(CF_Ray3 ray, float half, float *enter, int *axis)
{
    float p[3] = { ray.p.x, ray.p.y, ray.p.z }, d[3] = { ray.d.x, ray.d.y, ray.d.z };
    float leave = INFINITY;
    *enter = -INFINITY;
    for (int i = 0; i < 3; i++) {
        float a = (-half - p[i]) / d[i], b = (half - p[i]) / d[i];
        if (fminf(a, b) > *enter) {
            *enter = fminf(a, b);
            *axis = i;
        }
        leave = fminf(leave, fmaxf(a, b));
    }
    return *enter <= leave;
}

/* Which face of the two-unit box at `center` pixel (x, y) shows: its outward
 * normal, or zero where the pixel shows what lies beyond. False within a
 * tenth of a unit of an edge, where the pixel could show either side. */
static bool box_face(int x, int y, CF_V3 center, CF_V3 *normal)
{
    CF_Ray3 ray = pixel_ray(x, y, center);
    float enter;
    int axis;
    *normal = (CF_V3){ 0 };
    if (!meets_cube(ray, 1.1f, &enter, &axis)) {
        return true;
    }
    if (!meets_cube(ray, .9f, &enter, &axis)) {
        return false;
    }
    meets_cube(ray, 1, &enter, &axis);
    CF_V3 hit = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, enter));
    float at[3] = { hit.x, hit.y, hit.z };
    if (fabsf(at[(axis + 1) % 3]) > .9f || fabsf(at[(axis + 2) % 3]) > .9f) {
        return false;
    }
    float n[3] = { 0 };
    n[axis] = at[axis] > 0 ? 1 : -1;
    *normal = (CF_V3){ n[0], n[1], n[2] };
    return true;
}

/* The sky lies on the far plane: it fills only pixels no surface covers. A
 * black box shows only its dim reflection of the sky. */
TEST_CASE(sky_shows_only_where_no_surface_is)
{
    look_level();
    CF_V3 center = { 0, 0, -6 };
    add_solid(center, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0x000000ffu);
    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    Environment environment = sky_environment();
    REQUIRE(render(&environment));
    int covered = 0, open = 0;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            CF_V3 normal;
            if (!box_face(x, y, center, &normal)) {
                continue;
            }
            bool face = cf_len_sq_v3(normal) > 0;
            covered += face;
            open += !face;
            REQUIRE(face ? pixel(x, y).colors.r < 60 : pixel(x, y).colors.r > 160);
        }
    }
    REQUIRE(covered > 100 && open > PIXELS / 2);
    return true;
}

/* Translucent surfaces blend over the sky rather than under it. */
TEST_CASE(translucent_surfaces_blend_over_the_sky)
{
    look_level();
    publish_uniform_sky((const unsigned char[4]){ 128, 0, 128, 128 });
    add_solid((CF_V3){ 0, 0, -6 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xff000080u);
    Environment environment = sky_environment();
    REQUIRE(render(&environment));
    CF_Pixel open = pixel(4, 4), behind = pixel(SIDE / 2, SIDE / 2);
    REQUIRE(open.colors.r > 100 && open.colors.b > 100);
    REQUIRE(behind.colors.b > 20 && behind.colors.b + 20 < open.colors.b);
    return true;
}

/* Rotation turns the sky counterclockwise seen from above, so a quarter turn
 * brings what lay along +X ahead, along -Z. */
TEST_CASE(sky_rotation_turns_it_counterclockwise_from_above)
{
    look_level();
    static unsigned char texels[SKY_HEIGHT][SKY_WIDTH][4];
    for (int y = 0; y < SKY_HEIGHT; y++) {
        for (int x = 0; x < SKY_WIDTH; x++) {
            bool east = x >= SKY_WIDTH * 5 / 8 && x < SKY_WIDTH * 7 / 8;
            memcpy(texels[y][x], (unsigned char[4]){ east ? 128 : 0, 0, 0, 128 }, 4);
        }
    }
    publish_sky(texels);
    Environment environment = sky_environment();
    float rotations[] = { 0, 90, -90, 450 };
    for (int i = 0; i < 4; i++) {
        environment.sky_rotation = rotations[i];
        REQUIRE(render(&environment));
        CF_Pixel ahead = pixel(SIDE / 2, SIDE / 2);
        REQUIRE(i % 2 ? ahead.colors.r > 100 : !ahead.colors.r);
    }
    return true;
}

/* Each stop of sky exposure doubles it and the light it casts, exactly as
 * doubled texels would. */
TEST_CASE(sky_exposure_scales_it_in_stops)
{
    look_level();
    add_solid((CF_V3){ 0, 0, -6 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    int box = SIDE / 2 * SIDE + SIDE / 2;
    publish_uniform_sky((const unsigned char[4]){ 128, 64, 32, 127 });
    Environment environment = sky_environment();
    environment.sky_exposure = 1;
    REQUIRE(render(&environment));
    static CF_Pixel exposed[PIXELS];
    memcpy(exposed, pixels, sizeof pixels);
    publish_uniform_sky((const unsigned char[4]){ 128, 64, 32, 128 });
    environment.sky_exposure = 0;
    REQUIRE(render(&environment));
    REQUIRE(pixels[0].colors.r > 100 && pixels[box].colors.r > 100);
    REQUIRE(!memcmp(pixels, exposed, sizeof pixels));
    environment.sky_exposure = -1;
    REQUIRE(render(&environment));
    REQUIRE(pixels[0].colors.r < exposed[0].colors.r);
    REQUIRE(pixels[box].colors.r < exposed[box].colors.r);
    return true;
}

TEST_CASE(sky_settings_must_be_valid)
{
    Environment environment = sky_environment();
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "test.hdr has not been prepared"));
    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    float exposures[] = { 20.5f, -21, NAN };
    for (int i = 0; i < 3; i++) {
        environment.sky_exposure = exposures[i];
        REQUIRE(!render(&environment));
        REQUIRE(strstr(render_scene3d_error(scene3d), "sky exposure must be -20 to 20 stops"));
    }
    environment.sky_exposure = -20;
    float rotations[] = { INFINITY, NAN };
    for (int i = 0; i < 2; i++) {
        environment.sky_rotation = rotations[i];
        REQUIRE(!render(&environment));
        REQUIRE(strstr(render_scene3d_error(scene3d), "sky rotation must be finite"));
    }
    environment.sky_rotation = -1e6f;
    REQUIRE(render(&environment));
    return true;
}

/* Looks at the two-unit box at the origin from above a corner, so its +X, +Y
 * and +Z faces show. */
static void look_at_corner()
{
    look((CF_V3){ 3, 2.5f, 4 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 });
}

static void channels(CF_Pixel shown, int values[3])
{
    values[0] = shown.colors.r;
    values[1] = shown.colors.g;
    values[2] = shown.colors.b;
}

/* A white surface takes each channel of the octant sky from the half of the
 * sky that channel's axis points into: all of it where the surface faces
 * along the axis, half where it faces across, and only a faint reflection
 * where it faces away. The ambient colors give way to the sky. */
TEST_CASE(sky_lights_surfaces_from_its_directions)
{
    look_at_corner();
    publish_octant_sky();
    add_box(0xffffffffu);
    Environment environment = sky_environment();
    environment.ambient_sky = environment.ambient_ground = 0x00ff00ffu;
    static const CF_V3 faces[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (int quarters = 0; quarters < 2; quarters++) {
        environment.sky_rotation = 90.0f * quarters;
        REQUIRE(render(&environment));
        for (int i = 0; i < 3; i++) {
            CF_V3 facing = sky_frame(faces[i], quarters);
            float axes[3] = { facing.x, facing.y, facing.z };
            int shown[3];
            channels(pixel_at(faces[i]), shown);
            for (int c = 0; c < 3; c++) {
                REQUIRE(
                    axes[c] > 0   ? shown[c] > 165
                    : axes[c] < 0 ? shown[c] < 40
                                  : shown[c] > 100 && shown[c] < 150
                );
            }
        }
    }
    return true;
}

/* A sky's broad gradients light surfaces too: radiance (a . d)^2 gives
 * irradiance pi (1 + (a . n)^2) / 4, twice as much facing along a as across
 * it. An eighth of a turn brings the diagonal axis onto +X. */
TEST_CASE(sky_lights_surfaces_by_its_gradients)
{
    look_at_corner();
    publish_quadratic_sky();
    add_box(0xffffffffu);
    Environment environment = sky_environment();
    static const CF_V3 faces[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (int eighths = 0; eighths < 2; eighths++) {
        environment.sky_rotation = 45.0f * eighths;
        REQUIRE(render(&environment));
        float turn = eighths * 3.14159265f / 4, c = cosf(turn), s = sinf(turn);
        for (int i = 0; i < 3; i++) {
            CF_V3 n = faces[i],
                  facing = { c * n.x - s * n.z, n.y, s * n.x + c * n.z };
            int shown[3];
            channels(pixel_at(n), shown);
            for (int k = 0; k < 3; k++) {
                float along = cf_dot_v3(quadratic_axes[k], facing);
                along *= along;
                REQUIRE(
                    along > .75f   ? shown[k] > 168
                    : along > .25f ? shown[k] > 145 && shown[k] < 166
                                   : shown[k] < 143
                );
            }
        }
    }
    return true;
}

static const char *const mesh_key = "content/meshes/test.mesh";

/* Publishes `mesh` under mesh_key, written and read back through the
 * prepared format, with the tangents it has. Takes the mesh's storage. */
static void publish_prepared(render_mesh_asset *mesh)
{
    unsigned char *bytes;
    size_t length, budget = SIZE_MAX;
    char error[256];
    render_mesh_asset prepared = { 0 };
    assert(render_mesh_asset_prepared_write(mesh, &bytes, &length, error, sizeof error));
    assert(
        render_mesh_asset_prepared_read_budget(bytes, length, &budget, &prepared, error, sizeof error)
    );
    free(bytes);
    render_mesh_asset_free(mesh);
    render_mesh_asset_publish(mesh_key, &prepared);
}

/* Publishes `mesh` as preparation does, with tangents from its UVs. */
static void publish_mesh(render_mesh_asset *mesh)
{
    render_mesh_asset_tangents(mesh);
    publish_prepared(mesh);
}

/* The unit box's triangles with their own corners: the -x, +x, bottom, top,
 * back and front faces in turn, six indices each. */
static render_mesh_asset box_mesh()
{
    render_mesh_surface_vertex *box = NULL;
    char error[128];
    assert(render_mesh_surface_vertices(PRIMITIVE_BOX, &box, error, sizeof error));
    render_mesh_asset mesh = { .min = { -.5f, -.5f, -.5f }, .max = { .5f, .5f, .5f } };
    for (int i = 0; i < acount(box); i++) {
        apush(mesh.vertices, box[i]);
        apush(mesh.indices, i);
    }
    afree(box);
    return mesh;
}

/* Draws an entity as the mesh under mesh_key, sized by its Geometry3D. */
static void import_mesh(ecs_entity entity)
{
    MeshAsset imported = { .source = mesh_key };
    assert(
        ecs_world_record_add(test_world, entity, COMPONENT_MeshAsset.id, &imported, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
}

/* Publishes the unit box mesh with its vertices moved by `offset`. */
static void publish_moved_box(CF_V3 offset)
{
    render_mesh_asset box = box_mesh();
    for (int i = 0; i < acount(box.vertices); i++) {
        box.vertices[i].position = cf_add_v3(box.vertices[i].position, offset);
    }
    box.min = cf_add_v3(box.min, offset);
    box.max = cf_add_v3(box.max, offset);
    publish_mesh(&box);
}

/* Every pixel matches the frame `expected` saved. */
static bool matches(const CF_Pixel *expected)
{
    for (int i = 0; i < PIXELS; i++) {
        REQUIRE(distance(pixels[i], expected[i]) <= 3);
    }
    return true;
}

/* Moves a solid that add_solid made to center its shape on `center`. */
static void place_solid(ecs_entity solid, CF_V3 center)
{
    *Transform_get(test_world, solid) = (Transform){ center.x, center.z };
    Geometry3D_get(test_world, solid)->elevation = center.y;
}

/* The shadow scene's box and a pillar beyond the view's left edge draw first
 * as shapes, then as the unit box mesh. Every pixel matches: imported meshes
 * light, shade and cast from outside the view as shapes do. */
TEST_CASE(imported_meshes_draw_like_the_shapes_they_copy)
{
    render_mesh_asset box = box_mesh();
    publish_mesh(&box);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity solids[] = {
        add_solid((CF_V3){ 0, 1, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu),
        add_solid((CF_V3){ -8, 4, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 8 }, 0xffffffffu),
    };
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel shapes[PIXELS];
    memcpy(shapes, pixels, sizeof pixels);
    for (int i = 0; i < 2; i++) {
        import_mesh(solids[i]);
    }
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(pixel_at(shaded).colors.g == 0 && pixel_at(sunlit[0]).colors.g == 0);
    REQUIRE(pixel_at(sunlit[4]).colors.g > 60);
    REQUIRE(matches(shapes));
    return true;
}

/* Draws, at `scale` times its mesh's size, one triangle over half of a square
 * 2 / `scale` units wide on the ground: the triangle covers half of a
 * two-unit square wherever it is drawn. */
static ecs_entity add_imported_triangle(float scale)
{
    float half = 1 / scale;
    CF_V3 corners[] = { { -half, 0, -half }, { -half, 0, half }, { half, 0, -half } };
    render_mesh_asset mesh = { .min = { -half, 0, -half }, .max = { half, 0, half } };
    for (int i = 0; i < 3; i++) {
        render_mesh_surface_vertex vertex = { .position = corners[i],
                                              .normal = { 0, 1, 0 } };
        apush(mesh.vertices, vertex);
        apush(mesh.indices, i);
    }
    publish_mesh(&mesh);
    ecs_entity triangle = add_solid(
        (CF_V3){ 0 },
        (Geometry3D){ PRIMITIVE_BOX, scale, scale, scale },
        0xffffffffu
    );
    import_mesh(triangle);
    return triangle;
}

/* Picks hit add_imported_triangle's triangle beyond the unit box and miss
 * the box's corner that the triangle leaves out. */
static bool picks_follow_triangle(ecs_entity triangle)
{
    CF_V3 inside = render_scene3d_project(&lens, (CF_V3){ -.9f, 0, -.9f });
    CF_V3 outside = render_scene3d_project(&lens, (CF_V3){ .3f, 0, .3f });
    REQUIRE(
        render_scene3d_pick(test_world, &lens, (CF_V2){ inside.x, inside.y }, (render_skip){ 0 })
            .slot == triangle.slot
    );
    REQUIRE(!render_scene3d_pick(test_world, &lens, (CF_V2){ outside.x, outside.y }, (render_skip){ 0 })
                 .slot);
    return true;
}

/* A triangle drawn at unit size: picks follow it, and bounds hold just the
 * triangle. */
TEST_CASE(picking_and_bounds_follow_imported_triangles)
{
    ecs_entity triangle = add_imported_triangle(1);
    REQUIRE(picks_follow_triangle(triangle));
    CF_V3 box[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, triangle, box));
    REQUIRE(near_value(box[0].x, -1) && near_value(box[0].z, -1));
    REQUIRE(near_value(box[7].x, 1) && near_value(box[7].z, 1));
    REQUIRE(fabsf(box[0].y) < 2e-3f && fabsf(box[7].y) < 2e-3f);
    return true;
}

/* The same triangle with a mesh a hundred-thousandth the size, which its
 * Geometry3D scales back up: picks still follow it, because the test for a
 * ray parallel to a triangle scales with the triangle's edges. */
TEST_CASE(picks_follow_tiny_triangles_scaled_up)
{
    REQUIRE(picks_follow_triangle(add_imported_triangle(1e5f)));
    return true;
}

/* A lit box drawn as a shape, then as the unit box mesh made 2e37 wide around
 * 1.85e38 on each axis, which its Geometry3D scales and its Transform moves
 * back over the shape. Its bounds lie past half the float limit, where adding
 * them overflows, yet it covers the pixels, bounds and picks as the shape
 * did. */
TEST_CASE(meshes_near_the_float_limit_draw_bound_and_pick_in_place)
{
    add_sun(0xffffffffu, (CF_V3){ 1, -1, -.5f })->intensity = 1;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel empty[PIXELS], shape[PIXELS];
    memcpy(empty, pixels, sizeof pixels);
    ecs_entity box = add_solid((CF_V3){ 0, 1, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    REQUIRE(render(&environment));
    memcpy(shape, pixels, sizeof pixels);
    CF_V3 expected[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, box, expected));
    const float size = 2e37f, middle = 1.85e38f, scale = 1e-37f, back = middle * scale;
    render_mesh_asset far = box_mesh();
    for (int i = 0; i < acount(far.vertices); i++) {
        CF_V3 *position = &far.vertices[i].position;
        *position = cf_add_v3(cf_mul_v3_f(*position, size), (CF_V3){ middle, middle, middle });
    }
    far.min = (CF_V3){ middle - size / 2, middle - size / 2, middle - size / 2 };
    far.max = (CF_V3){ middle + size / 2, middle + size / 2, middle + size / 2 };
    publish_mesh(&far);
    const Geometry3D shrunk = { PRIMITIVE_BOX, scale, scale, scale };
    *Geometry3D_get(test_world, box) = shrunk;
    place_solid(box, (CF_V3){ -back, 1 - back, -back });
    import_mesh(box);
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    int covered = 0;
    for (int i = 0; i < PIXELS; i++) {
        bool shows = distance(pixels[i], empty[i]) > 3;
        REQUIRE(shows == (distance(shape[i], empty[i]) > 3));
        covered += shows;
    }
    REQUIRE(covered > PIXELS / 8);
    CF_V3 bounds[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, box, bounds));
    for (int i = 0; i < 8; i++) {
        REQUIRE(near_value(bounds[i].x, expected[i].x));
        REQUIRE(near_value(bounds[i].y, expected[i].y));
        REQUIRE(near_value(bounds[i].z, expected[i].z));
    }
    CF_V3 top = render_scene3d_project(&lens, (CF_V3){ 0, 2, 0 });
    CF_V3 beside = render_scene3d_project(&lens, (CF_V3){ 2, 0, 0 });
    REQUIRE(
        render_scene3d_pick(test_world, &lens, (CF_V2){ top.x, top.y }, (render_skip){ 0 })
            .slot == box.slot
    );
    REQUIRE(!render_scene3d_pick(test_world, &lens, (CF_V2){ beside.x, beside.y }, (render_skip){ 0 })
                 .slot);
    return true;
}

/* Picks across the view, PICK_GRID by PICK_GRID, that meet `solid`. */
enum
{
    PICK_GRID = 9
};

static void picks_of(ecs_entity solid, bool picked[PICK_GRID][PICK_GRID])
{
    for (int y = 0; y < PICK_GRID; y++) {
        for (int x = 0; x < PICK_GRID; x++) {
            CF_V2 ndc = { -.8f + 1.6f * x / (PICK_GRID - 1),
                          -.8f + 1.6f * y / (PICK_GRID - 1) };
            picked[y][x] = render_scene3d_pick(test_world, &lens, ndc, (render_skip){ 0 })
                               .slot == solid.slot;
        }
    }
}

/* A lit box drawn as a shape 8 wide, then as the unit box mesh made 6e38 wide
 * around the origin, which its Geometry3D scales back to 8. Its bounds span
 * more than a float holds, yet it covers the pixels, bounds and picks as the
 * shape did. */
TEST_CASE(meshes_wider_than_the_float_limit_draw_bound_and_pick_in_place)
{
    look((CF_V3){ 0, 16, 16 }, (CF_V3){ 0, 4, 0 }, (CF_V3){ 0, 1, 0 });
    add_sun(0xffffffffu, (CF_V3){ 1, -1, -.5f })->intensity = 1;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel empty[PIXELS], shape[PIXELS];
    memcpy(empty, pixels, sizeof pixels);
    ecs_entity box = add_solid((CF_V3){ 0, 4, 0 }, (Geometry3D){ PRIMITIVE_BOX, 8, 8, 8 }, 0xffffffffu);
    REQUIRE(render(&environment));
    memcpy(shape, pixels, sizeof pixels);
    CF_V3 expected[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, box, expected));
    bool expected_picks[PICK_GRID][PICK_GRID];
    picks_of(box, expected_picks);
    const float reach = 3e38f, scale = 4 / reach;
    render_mesh_asset wide = box_mesh();
    for (int i = 0; i < acount(wide.vertices); i++) {
        CF_V3 *position = &wide.vertices[i].position;
        *position = cf_mul_v3_f(cf_mul_v3_f(*position, reach), 2);
    }
    wide.min = (CF_V3){ -reach, -reach, -reach };
    wide.max = (CF_V3){ reach, reach, reach };
    publish_mesh(&wide);
    Geometry3D *geometry = Geometry3D_get(test_world, box);
    geometry->width = geometry->height = geometry->depth = scale;
    import_mesh(box);
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    int covered = 0;
    for (int i = 0; i < PIXELS; i++) {
        bool shows = distance(pixels[i], empty[i]) > 3;
        REQUIRE(shows == (distance(shape[i], empty[i]) > 3));
        covered += shows;
    }
    REQUIRE(covered > PIXELS / 8);
    CF_V3 bounds[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, box, bounds));
    for (int i = 0; i < 8; i++) {
        REQUIRE(near_value(bounds[i].x, expected[i].x));
        REQUIRE(near_value(bounds[i].y, expected[i].y));
        REQUIRE(near_value(bounds[i].z, expected[i].z));
    }
    bool picks[PICK_GRID][PICK_GRID];
    picks_of(box, picks);
    int hits = 0;
    for (int y = 0; y < PICK_GRID; y++) {
        for (int x = 0; x < PICK_GRID; x++) {
            REQUIRE(picks[y][x] == expected_picks[y][x]);
            hits += picks[y][x];
        }
    }
    REQUIRE(hits > 0 && hits < PICK_GRID * PICK_GRID);
    return true;
}

enum
{
    GRID_CELLS = 64
};

/* The height of the grid's square at column x and row z, from 0 to 1. */
static float grid_height(int x, int z)
{
    return (float)((x * 7 + z * 3) % 5) / 4;
}

/* Publishes a ground grid of GRID_CELLS by GRID_CELLS unit squares, each
 * two triangles of its own at grid_height plus `raise`. */
static void publish_grid(float raise)
{
    render_mesh_asset grid = { .min = { 0, raise, 0 },
                               .max = { GRID_CELLS, raise + 1, GRID_CELLS } };
    static const int corners[][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
    static const int indices[] = { 0, 1, 2, 0, 2, 3 };
    for (int z = 0; z < GRID_CELLS; z++) {
        for (int x = 0; x < GRID_CELLS; x++) {
            int first = acount(grid.vertices);
            for (int i = 0; i < 4; i++) {
                render_mesh_surface_vertex vertex = {
                    .position = { (float)(x + corners[i][0]),
                                  grid_height(x, z) + raise,
                                  (float)(z + corners[i][1]) },
                    .normal = { 0, 1, 0 },
                };
                apush(grid.vertices, vertex);
            }
            for (int i = 0; i < 6; i++) {
                apush(grid.indices, first + indices[i]);
            }
        }
    }
    publish_mesh(&grid);
}

/* A pick straight down through a cell's center meets its square. */
static bool picks_cell(int x, int z, float raise)
{
    char error[128];
    const render_mesh_asset *grid = render_mesh_asset_load(mesh_key, error, sizeof error);
    REQUIRE(grid);
    CF_V3 above = { x + .5f, 10, z + .5f };
    render_mesh_hit hit = render_mesh_pick(
        grid,
        cf_make_ray3(above, (CF_V3){ x + .5f, -10, z + .5f })
    );
    REQUIRE(near_value(hit.t, 10 - grid_height(x, z) - raise));
    REQUIRE(hit.tested <= 16);
    return true;
}

/* A pick through an imported mesh of 8192 triangles tests only the few in
 * boxes its ray crosses and still meets the nearest; one that misses the
 * mesh tests none. A republished mesh is picked as it now stands. */
TEST_CASE(mesh_picks_test_only_the_triangles_near_their_ray)
{
    publish_grid(0);
    REQUIRE(picks_cell(0, 0, 0) && picks_cell(17, 40, 0) && picks_cell(63, 63, 0));
    char error[128];
    const render_mesh_asset *grid = render_mesh_asset_load(mesh_key, error, sizeof error);
    REQUIRE(grid);
    CF_V3 beside = { -5, 10, 5 };
    render_mesh_hit miss = render_mesh_pick(grid, cf_make_ray3(beside, (CF_V3){ -5, -10, 5 }));
    REQUIRE(miss.t == FLT_MAX && !miss.tested);
    publish_grid(2);
    REQUIRE(picks_cell(17, 40, 2));
    return true;
}

/* Sixteen walls facing x, spread from -2.8e38 to 2.9e38, with only the last
 * in the path of a ray from x = -3e38 whose direction is half the span. The
 * boxes around the far walls start more than a float's range from the ray's
 * origin, yet the pick reaches the last wall through them alone. */
TEST_CASE(mesh_picks_cross_boxes_past_the_float_limit)
{
    enum
    {
        WALLS = 16
    };
    /* In double, since the walls' span passes float's range. */
    const double first = -2.8e38, spacing = .38e38;
    const float last = (float)(first + (WALLS - 1) * spacing);
    render_mesh_asset walls = { .min = { (float)first, 0, 0 }, .max = { last, 1, 3 } };
    for (int k = 0; k < WALLS; k++) {
        float x = (float)(first + k * spacing), z = k == WALLS - 1 ? 0 : 2;
        CF_V3 corners[] = { { x, 0, z }, { x, 1, z }, { x, 0, z + 1 } };
        for (int i = 0; i < 3; i++) {
            render_mesh_surface_vertex vertex = { .position = corners[i],
                                                  .normal = { 1, 0, 0 } };
            apush(walls.indices, acount(walls.vertices));
            apush(walls.vertices, vertex);
        }
    }
    publish_mesh(&walls);
    char error[128];
    const render_mesh_asset *mesh = render_mesh_asset_load(mesh_key, error, sizeof error);
    REQUIRE(mesh);
    const float origin = -3e38f, half = 3e38f;
    CF_Ray3 ray = { .p = { origin, .25f, .25f }, .d = { half, 0, 0 }, .t = 2 };
    render_mesh_hit hit = render_mesh_pick(mesh, ray);
    REQUIRE(near_value(hit.t, (float)(((double)last - origin) / half)));
    REQUIRE(hit.tested <= 4);
    return true;
}

/* A republished source draws its new triangles, and the GPU copies of
 * replaced and removed meshes are freed. */
TEST_CASE(republished_meshes_replace_their_gpu_copies)
{
    render_mesh_asset box = box_mesh();
    publish_mesh(&box);
    ecs_entity solid = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    import_mesh(solid);
    Environment environment = environment_defaults();
    REQUIRE(render(&environment));
    CF_Pixel drawn = top();
    REQUIRE(drawn.colors.r > 60 && render_scene3d_last_work(scene3d).meshes == 1);
    /* Moved a box width to the right, the mesh leaves the center empty. */
    publish_moved_box((CF_V3){ 1, 0, 0 });
    REQUIRE(render(&environment));
    REQUIRE(top().colors.r < 10 && render_scene3d_last_work(scene3d).meshes == 1);
    render_mesh_assets_clear();
    REQUIRE(!render(&environment));
    REQUIRE(
        strstr(render_scene3d_error(scene3d), "Mesh content/meshes/test.mesh has not been prepared.")
    );
    MeshAsset_get(test_world, solid)->source = "";
    REQUIRE(render(&environment));
    REQUIRE(distance(top(), drawn) <= 3 && render_scene3d_last_work(scene3d).meshes == 0);
    return true;
}

/* Copies uploaded out of identity order are each found again: uploading
 * every mesh a second time makes no new copy. */
TEST_CASE(mesh_copies_are_found_in_any_upload_order)
{
    static const uint64_t identities[] = { 20, 10, 30 };
    render_scene3d_mesh_cache *cache = render_scene3d_mesh_cache_new();
    render_mesh_asset meshes[3];
    bool uploaded = true;
    for (int i = 0; i < 3; i++) {
        meshes[i] = box_mesh();
        meshes[i].identity = identities[i];
        uploaded = uploaded && render_scene3d_mesh_cache_upload(cache, &meshes[i]);
    }
    for (int i = 0; i < 3; i++) {
        uploaded = uploaded &&
                   render_scene3d_mesh_cache_upload(cache, &meshes[2 - i]);
    }
    int copies = render_scene3d_mesh_cache_count(cache);
    render_scene3d_mesh_cache_free(cache);
    for (int i = 0; i < 3; i++) {
        render_mesh_asset_free(&meshes[i]);
    }
    REQUIRE(uploaded && copies == 3);
    return true;
}

static const CF_V3 eyeward = { 0, 2, 2 }; /* Up and toward the eye. */
static const Geometry3D unit_box = { PRIMITIVE_BOX, 1, 1, 1 };

/* A translucent blue unit box nearer the eye than a translucent red one
 * blends over it, drawn first as a shape and then as a mesh whose vertices
 * sit at the shape's place while its origin lies behind the red box. Every
 * pixel matches: the mesh sorts by its bounds, not its origin. */
TEST_CASE(translucent_meshes_sort_by_their_bounds)
{
    add_solid((CF_V3){ 0, 1, 1 }, unit_box, 0xff000080u);
    ecs_entity blue = add_solid(eyeward, unit_box, 0x0000ff80u);
    Environment environment = environment_defaults();
    REQUIRE(render(&environment));
    static CF_Pixel shapes[PIXELS];
    memcpy(shapes, pixels, sizeof pixels);
    publish_moved_box(eyeward);
    place_solid(blue, (CF_V3){ 0 });
    import_mesh(blue);
    REQUIRE(render(&environment));
    REQUIRE(matches(shapes));
    return true;
}

/* Two translucent copies of that mesh draw in one batch, in the order the
 * scene submits them: a blue copy at the origin, and a red one turned half
 * around whose box sits behind the blue one, though its origin is nearer
 * the eye. Every pixel matches two shape boxes in their places. */
TEST_CASE(translucent_copies_of_a_mesh_sort_by_their_bounds)
{
    ecs_entity red = add_solid((CF_V3){ 0, 1, 1 }, unit_box, 0xff000080u);
    ecs_entity blue = add_solid(eyeward, unit_box, 0x0000ff80u);
    Environment environment = environment_defaults();
    REQUIRE(render(&environment));
    static CF_Pixel shapes[PIXELS];
    memcpy(shapes, pixels, sizeof pixels);
    publish_moved_box(eyeward);
    place_solid(red, (CF_V3){ 0, -1, 3 });
    VisualTransform turned = { .yaw = CF_PI, .scale = 1 };
    assert(
        ecs_world_record_add(test_world, red, COMPONENT_VisualTransform.id, &turned, 0) == ECS_OK
    );
    place_solid(blue, (CF_V3){ 0 });
    import_mesh(red);
    import_mesh(blue);
    REQUIRE(render(&environment));
    REQUIRE(matches(shapes));
    return true;
}

/* Places an entity by a Transform3D, which outranks its Transform. */
static void place_3d(ecs_entity entity, CF_V3 position, CF_Quat rotation, CF_V3 scale)
{
    Transform3D transform = { position, rotation, scale };
    assert(
        ecs_world_record_add(test_world, entity, COMPONENT_Transform3D.id, &transform, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
}

/* The shadow scene's box and a pillar beyond the view's left edge draw first
 * in 2D placement, then at Transform3D poses with their Transforms and
 * elevations moved away. Every pixel matches, with each solid drawn once: a
 * Transform3D places a solid's center and scales its shape, and its solids
 * shade and cast as before. A box turned a quarter about x then shows as one
 * with its height and depth swapped. */
TEST_CASE(transform3d_places_solids_in_their_pose)
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_solid((CF_V3){ 0, 1, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    ecs_entity pillar = add_solid((CF_V3){ -8, 4, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 8 }, 0xffffffffu);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel placed[PIXELS];
    memcpy(placed, pixels, sizeof placed);
    place_solid(box, (CF_V3){ 5, 7, 5 });
    place_3d(box, (CF_V3){ 0, 1, 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    place_solid(pillar, (CF_V3){ 5, -7, 5 });
    *Geometry3D_get(test_world, pillar) = (Geometry3D){ PRIMITIVE_BOX, 4, 1, 4 };
    place_3d(pillar, (CF_V3){ -8, 4, 0 }, cf_quat_identity(), (CF_V3){ .5f, 2, 2 });
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(render_scene3d_last_work(scene3d).entities == 3);
    REQUIRE(pixel_at(shaded).colors.g == 0 && pixel_at(sunlit[0]).colors.g == 0);
    REQUIRE(pixel_at(sunlit[4]).colors.g > 60);
    REQUIRE(matches(placed));
    /* A box two wide, one high and 2.4 deep, upright, then turned a quarter
     * about x from 2.4 high and one deep, shows the same. */
    *Geometry3D_get(test_world, box) = (Geometry3D){ PRIMITIVE_BOX, 2, 2.4f, 1 };
    *Transform3D_get(test_world, box) = (Transform3D){ { 0, .5f, 0 },
                                                       cf_quat_identity(),
                                                       { 1, 1, 1 } };
    REQUIRE(render(&environment));
    REQUIRE(
        pixel_at((CF_V3){ 0, 1, 0 }).colors.g > 30 && pixel_at(shaded).colors.g == 0
    );
    REQUIRE(pixel_at(sunlit[3]).colors.g > 60);
    static CF_Pixel upright[PIXELS];
    memcpy(upright, pixels, sizeof upright);
    *Geometry3D_get(test_world, box) = (Geometry3D){ PRIMITIVE_BOX, 2, 1, 2.4f };
    Transform3D_get(test_world, box)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 1, 0, 0 }, CF_PI / 2);
    REQUIRE(render(&environment));
    REQUIRE(matches(upright));
    /* A Visual Transform turns the box within its Transform3D: a quarter
     * about y after a quarter roll about x takes 2.4 wide, one deep and two
     * high to the upright box. */
    *Geometry3D_get(test_world, box) = (Geometry3D){ PRIMITIVE_BOX, 2.4f, 1, 2 };
    Transform3D_get(test_world, box)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, CF_PI / 2);
    VisualTransform rolled = { .roll = CF_PI / 2, .scale = 1 };
    assert(
        ecs_world_record_add(test_world, box, COMPONENT_VisualTransform.id, &rolled, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(render(&environment));
    REQUIRE(matches(upright));
    return true;
}

/* A box under a ChildOf parent with a Transform3D, both without a Transform,
 * takes the parent's turn: three units deep, it lies along x. Picks and
 * bounds follow it before any propagation. */
TEST_CASE(transform3d_children_pick_and_bound_under_their_parent)
{
    ecs_entity parent = ecs_world_create_entity(test_world);
    place_3d(
        parent,
        (CF_V3){ 1, 0, 0 },
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, CF_PI / 2),
        (CF_V3){ 1, 1, 1 }
    );
    ecs_entity child = add(COMPONENT_Geometry3D.id, &(Geometry3D){ PRIMITIVE_BOX, 1, 3, 1 });
    place_3d(child, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    REQUIRE(ecs_world_record_add_pair(test_world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    const CF_V3 on[] = { { -.2f, 0, 0 }, { 2.2f, 0, 0 } },
                off[] = { { -1.2f, 0, 0 }, { 1, 0, -1.5f } };
    for (int i = 0; i < 2; i++) {
        CF_V3 hit = render_scene3d_project(&lens, on[i]),
              miss = render_scene3d_project(&lens, off[i]);
        REQUIRE(
            render_scene3d_pick(test_world, &lens, (CF_V2){ hit.x, hit.y }, (render_skip){ 0 })
                .slot == child.slot
        );
        REQUIRE(!render_scene3d_pick(test_world, &lens, (CF_V2){ miss.x, miss.y }, (render_skip){ 0 })
                     .slot);
    }
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(test_world, child, corners));
    CF_V3 low = corners[0], high = corners[0];
    for (int i = 1; i < 8; i++) {
        low = cf_min_v3(low, corners[i]);
        high = cf_max_v3(high, corners[i]);
    }
    REQUIRE(fabsf(low.x + .5f) < 1e-4f && fabsf(high.x - 2.5f) < 1e-4f);
    REQUIRE(fabsf(low.z + .5f) < 1e-4f && fabsf(high.z - .5f) < 1e-4f);
    return true;
}

#if CHIBI_TEST_MATERIAL_GRAPHS
/* A model surface picks and bounds where its Transform3D places it, with or
 * without a Transform it outranks. */
TEST_CASE(transform3d_models_pick_and_bound_at_their_pose)
{
    REQUIRE(test_register_model_sphere("PlacedSphere", .5f));
    ecs_entity model = add_solid(
        (CF_V3){ -1.5f, 0, 0 },
        (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1 },
        0xffffffffu
    );
    SdfSurface3D surface = { .enabled = true };
    Shader binding = { .shape = "PlacedSphere" };
    REQUIRE(
        ecs_world_record_add(test_world, model, COMPONENT_SdfSurface3D.id, &surface, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(test_world, model, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    place_3d(model, (CF_V3){ 1.5f, 0, 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    CF_V3 placed = render_scene3d_project(&lens, (CF_V3){ 1.5f, 0, 0 });
    CF_V3 flat = render_scene3d_project(&lens, (CF_V3){ -1.5f, 0, 0 });
    for (int alone = 0; alone < 2; alone++) {
        if (alone) {
            REQUIRE(
                ecs_world_record_remove(test_world, model, COMPONENT_Transform.id, 0) == ECS_OK
            );
            REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
        }
        REQUIRE(
            render_scene3d_pick(test_world, &lens, (CF_V2){ placed.x, placed.y }, (render_skip){ 0 })
                .slot == model.slot
        );
        REQUIRE(!render_scene3d_pick(test_world, &lens, (CF_V2){ flat.x, flat.y }, (render_skip){ 0 })
                     .slot);
        CF_V3 corners[8];
        REQUIRE(render_scene3d_entity_bounds(test_world, model, corners));
        REQUIRE(fabsf(corners[0].x - 1) < 1e-3f && fabsf(corners[7].x - 2) < 1e-3f);
    }
    return true;
}
#endif

/* A Transform3D that mirrors a box across x shows it as the unmirrored box
 * does, as a shape and as the unit box mesh: the lit pass culls the faces
 * turned away, not the ones in view, and their normals point out. So does
 * a mirroring scale too tiny for its determinant, under a Geometry3D as
 * large. A Transform3D that flattens a solid hides it. */
TEST_CASE(mirroring_transform3d_keeps_faces_outward)
{
    ecs_entity box = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    place_3d(box, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    add_sun(0xffffffffu, (CF_V3){ -1, -2, -3 })->intensity = 1;
    Environment environment = unlit_environment();
    render_mesh_asset mesh = box_mesh();
    publish_mesh(&mesh);
    for (int imported = 0; imported < 2; imported++) {
        if (imported) {
            import_mesh(box);
        }
        Transform3D_get(test_world, box)->scale = (CF_V3){ 1, 1, 1 };
        REQUIRE(render(&environment));
        REQUIRE(top().colors.g > 30 && front().colors.g > 30);
        static CF_Pixel upright[PIXELS];
        memcpy(upright, pixels, sizeof upright);
        Transform3D_get(test_world, box)->scale = (CF_V3){ -1, 1, 1 };
        REQUIRE(render(&environment));
        REQUIRE(!render_scene3d_error(scene3d)[0]);
        REQUIRE(matches(upright));
        Geometry3D shape = *Geometry3D_get(test_world, box);
        Geometry3D huge = { PRIMITIVE_BOX, 2e16f, 2e16f, 2e16f };
        Transform3D_get(test_world, box)->scale = (CF_V3){ -1e-16f, 1e-16f, 1e-16f };
        *Geometry3D_get(test_world, box) = huge;
        REQUIRE(render(&environment));
        REQUIRE(!render_scene3d_error(scene3d)[0]);
        REQUIRE(matches(upright));
        if (imported) {
            /* So does a mesh 2e16 across in its own units, whose model's
             * determinant underflows float at that scale. */
            render_mesh_asset vast = box_mesh();
            for (int i = 0; i < acount(vast.vertices); i++) {
                vast.vertices[i].position = cf_mul_v3_f(vast.vertices[i].position, 2e16f);
            }
            vast.min = cf_mul_v3_f(vast.min, 2e16f);
            vast.max = cf_mul_v3_f(vast.max, 2e16f);
            publish_mesh(&vast);
            *Geometry3D_get(test_world, box) = (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1 };
            REQUIRE(render(&environment));
            REQUIRE(!render_scene3d_error(scene3d)[0]);
            REQUIRE(matches(upright));
            mesh = box_mesh();
            publish_mesh(&mesh);
        }
        *Geometry3D_get(test_world, box) = shape;
        Transform3D_get(test_world, box)->scale = (CF_V3){ 1, 0, 1 };
        REQUIRE(render(&environment));
        REQUIRE(!render_scene3d_error(scene3d)[0] && !top().colors.g);
    }
    return true;
}

/* A box mesh 1e30 long in x in its own units, drawn at a 1e-30 x scale,
 * matches the unit box mesh: the cofactors that give its top and front
 * normals are 1e-30 long, too short to normalize directly. */
TEST_CASE(lopsided_mesh_models_keep_their_normals)
{
    ecs_entity box = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    place_3d(box, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    add_sun(0xffffffffu, (CF_V3){ -1, -2, -3 })->intensity = 1;
    Environment environment = unlit_environment();
    render_mesh_asset mesh = box_mesh();
    publish_mesh(&mesh);
    import_mesh(box);
    REQUIRE(render(&environment));
    REQUIRE(top().colors.g > 30 && front().colors.g > 30);
    static CF_Pixel upright[PIXELS];
    memcpy(upright, pixels, sizeof upright);
    mesh = box_mesh();
    for (int i = 0; i < acount(mesh.vertices); i++) {
        mesh.vertices[i].position.x *= 1e30f;
    }
    mesh.min.x *= 1e30f;
    mesh.max.x *= 1e30f;
    publish_mesh(&mesh);
    Transform3D_get(test_world, box)->scale = (CF_V3){ 1e-30f, 1, 1 };
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(matches(upright));
    return true;
}

/* A box whose parent's Transform3D flattens it onto a plane, both turned so
 * that rounding leaves its frame a sliver of volume, hides and has no
 * bounds. A box scaled tiny but whole keeps them, as does one turned within
 * a parent squashed thin, whose axes nearly meet. One whose Geometry3D
 * width underflows its whole frame's x axis to zero has none. */
TEST_CASE(flattened_transform3d_frames_hide_at_any_scale)
{
    ecs_entity parent = ecs_world_create_entity(test_world);
    place_3d(
        parent,
        (CF_V3){ 0 },
        cf_quat_from_axis_angle(cf_norm_v3((CF_V3){ 1, 2, 3 }), 1.14f),
        (CF_V3){ 1, 0, 1 }
    );
    ecs_entity child = add(COMPONENT_Geometry3D.id, &(Geometry3D){ PRIMITIVE_BOX, 1, 1, 1 });
    place_3d(
        child,
        (CF_V3){ 0 },
        cf_quat_from_axis_angle(cf_norm_v3((CF_V3){ 3, -1, 2 }), 2.62f),
        (CF_V3){ 1, 1, 1 }
    );
    REQUIRE(ecs_world_record_add_pair(test_world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    CF_V3 corners[8];
    REQUIRE(!render_scene3d_entity_bounds(test_world, child, corners));
    *Transform3D_get(test_world, parent) = (Transform3D){ { 0 },
                                                          cf_quat_identity(),
                                                          { 1e-16f, 1e-16f, 1e-16f } };
    REQUIRE(render_scene3d_entity_bounds(test_world, child, corners));
    CF_Quat turn = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, CF_PI / 4);
    Transform3D_get(test_world, parent)->scale = (CF_V3){ 1e-6f, 1, 1 };
    Transform3D_get(test_world, child)->rotation = turn;
    REQUIRE(render_scene3d_entity_bounds(test_world, child, corners));
    Transform3D_get(test_world, parent)->scale = (CF_V3){ 1e-25f, 1, 1 };
    Transform3D_get(test_world, child)->rotation = cf_quat_identity();
    REQUIRE(render_scene3d_entity_bounds(test_world, child, corners));
    Geometry3D_get(test_world, child)->width = 1e-25f;
    REQUIRE(!render_scene3d_entity_bounds(test_world, child, corners));
    return true;
}

/* A frame whose values are each finite but whose product overflows float is
 * an authoring error, as a value that is not finite is, and never draws. */
TEST_CASE(overflowing_transform3d_frames_are_authoring_errors)
{
    ecs_entity box = add(COMPONENT_Geometry3D.id, &(Geometry3D){ PRIMITIVE_BOX, 1, 1, 1 });
    place_3d(box, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1e20f, 1e20f, 1e20f });
    VisualTransform grown = { .scale = 1e20f };
    REQUIRE(
        ecs_world_record_add(test_world, box, COMPONENT_VisualTransform.id, &grown, 0) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    GlobalTransform3D transform = transform_world_3d(test_world, box);
    scene3d_visual visuals[SCENE3D_PART_LIMIT];
    const render_mesh_asset *mesh;
    char error[128];
    int parts = render_scene3d_visuals(test_world, box, NULL, &transform, visuals, &mesh, error, sizeof error);
    REQUIRE(parts == -1);
    REQUIRE(!strcmp(error, "Visual transform needs finite values and nonnegative scale."));
    VisualTransform_get(test_world, box)->scale = 1;
    parts = render_scene3d_visuals(test_world, box, NULL, &transform, visuals, &mesh, error, sizeof error);
    REQUIRE(parts == 1);
    /* So is a Geometry3D, or a part's position, that carries the frame past
     * it. */
    const char *beyond = "3D Geometry needs a placement within float range.";
    Geometry3D_get(test_world, box)->width = 1e20f;
    parts = render_scene3d_visuals(test_world, box, NULL, &transform, visuals, &mesh, error, sizeof error);
    REQUIRE(parts == -1 && !strcmp(error, beyond));
    Geometry3D_get(test_world, box)->width = 1;
    scene3d_parts far = { .count = 1 };
    far.parts[0] = (scene3d_part){ .primitive = PRIMITIVE_BOX,
                                   .dimensions = { 1, 1, 1 },
                                   .position = { 1e20f, 0, 0 } };
    REQUIRE(render_scene3d_parts_publish("far", &far, error, sizeof error));
    MeshParts authored = { .parts = "far" };
    REQUIRE(
        ecs_world_record_add(test_world, box, COMPONENT_MeshParts.id, &authored, 0) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    parts = render_scene3d_visuals(test_world, box, NULL, &transform, visuals, &mesh, error, sizeof error);
    render_scene3d_parts_clear();
    REQUIRE(parts == -1 && !strcmp(error, beyond));
    return true;
}

/* Fog culls a solid with a Transform3D by the ground under its 3D pose,
 * where it draws, and not by the 2D Transform it also carries: the box
 * shows while its 3D pose is in sight and its 2D position isn't, and hides
 * once the eye turns to its 2D position. */
TEST_CASE(fog_culls_transform3d_solids_where_they_draw)
{
    Fog settings = { .origin = { -8, -8 },
                     .columns = 16,
                     .rows = 16,
                     .cell_size = 1,
                     .color = 0xff0000ffu };
    add(COMPONENT_Fog.id, &settings);
    ecs_entity eye = add(COMPONENT_Transform.id, &(Transform){ 0, 0 });
    Vision vision = { .radius = 3 };
    REQUIRE(ecs_world_record_add(test_world, eye, COMPONENT_Vision.id, &vision, 0) == ECS_OK);
    ecs_entity box = add_solid((CF_V3){ 6, 0, 6 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    RenderState state = { .visibility_culled = true };
    REQUIRE(ecs_world_record_add(test_world, box, COMPONENT_RenderState.id, &state, 0) == ECS_OK);
    place_3d(box, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    add_sun(0xffffffffu, (CF_V3){ -1, -2, -3 })->intensity = 1;
    visibility *visibility = visibility_new(test_world, NULL);
    runtime = engine_new(test_world, &(engine_modules){ .visibility = visibility });
    Environment environment = unlit_environment();
    bool seen = !visibility_update(visibility, 0) && render(&environment) &&
                top().colors.g > 30;
    *Transform_get(test_world, eye) = (Transform){ 6, 6 };
    bool hidden = !visibility_update(visibility, 0) && render(&environment) &&
                  !top().colors.g;
    engine_free(runtime);
    runtime = NULL;
    visibility_free(visibility);
    REQUIRE(seen && hidden);
    return true;
}

/* Uniform white ambient light alone, which occlusion dims. */
static Environment ambient_environment(Occlusion occlusion)
{
    Environment environment = unlit_environment();
    environment.ambient_sky = environment.ambient_ground = 0xffffffffu;
    environment.ambient_intensity = .5f;
    environment.occlusion = occlusion;
    return environment;
}

/* Around add_box_on_floor's box: floor just in front of it, its front face
 * just above the floor, floor beyond the radius, and its roof. */
static const CF_V3 crease_floor = { 0, 0, 1.15f }, crease_face = { 0, .15f, 1 },
                   open_floor = { -2.2f, 0, -1.5f }, roof = { 0, 2, 0 };

/* Occlusion darkens the ambient light where the box meets the floor at every
 * quality, and leaves open floor and the roof alone. */
TEST_CASE(ambient_occlusion_darkens_where_surfaces_meet)
{
    add_box_on_floor();
    Environment environment = ambient_environment(OCCLUSION_OFF);
    REQUIRE(render(&environment));
    CF_Pixel floor = pixel_at(crease_floor), face = pixel_at(crease_face),
             open = pixel_at(open_floor), top = pixel_at(roof);
    /* Without occlusion the floor is lit evenly. */
    REQUIRE(floor.colors.g > 60 && distance(floor, open) <= 3);
    for (int quality = OCCLUSION_LOW; quality <= OCCLUSION_HIGH; quality++) {
        environment.occlusion = quality;
        REQUIRE(render(&environment));
        REQUIRE(!render_scene3d_error(scene3d)[0]);
        REQUIRE(pixel_at(crease_floor).colors.g + 8 < floor.colors.g);
        REQUIRE(pixel_at(crease_face).colors.g + 8 < face.colors.g);
        REQUIRE(distance(pixel_at(open_floor), open) <= 6);
        REQUIRE(distance(pixel_at(roof), top) <= 6);
    }
    return true;
}

/* Occlusion leaves the sun's light alone. */
TEST_CASE(ambient_occlusion_leaves_direct_light_alone)
{
    add_shadow_scene()->shadows = true;
    Environment environment = unlit_environment();
    environment.occlusion = OCCLUSION_OFF;
    REQUIRE(render(&environment));
    static CF_Pixel open[PIXELS];
    memcpy(open, pixels, sizeof pixels);
    REQUIRE(pixel_at(crease_floor).colors.g > 60);
    environment.occlusion = OCCLUSION_HIGH;
    REQUIRE(render(&environment));
    REQUIRE(!memcmp(pixels, open, sizeof pixels));
    return true;
}

/* A translucent panel in front of the crease neither takes the occlusion of
 * the crease behind it nor occludes the floor it stands on. */
TEST_CASE(ambient_occlusion_passes_translucent_solids)
{
    add_box_on_floor();
    add_solid((CF_V3){ 0, .5f, 1.6f }, (Geometry3D){ PRIMITIVE_BOX, 2, 1, .05f }, 0xfffffffeu);
    CF_V3 before = { 0, 0, 1.75f };
    Environment environment = ambient_environment(OCCLUSION_OFF);
    REQUIRE(render(&environment));
    CF_Pixel panel = pixel_at(crease_floor), floor = pixel_at(before);
    REQUIRE(panel.colors.g > 60);
    environment.occlusion = OCCLUSION_HIGH;
    REQUIRE(render(&environment));
    REQUIRE(distance(pixel_at(crease_floor), panel) <= 3);
    REQUIRE(distance(pixel_at(before), floor) <= 3);
    return true;
}

/* Occlusion needs a known quality and, while it is on, a positive finite
 * radius; projects saved before it had one leave it off. */
TEST_CASE(ambient_occlusion_settings_must_be_valid)
{
    add_box_on_floor();
    Environment environment = ambient_environment(OCCLUSION_MEDIUM);
    const float radii[] = { 0, -1, NAN, INFINITY };
    for (int i = 0; i < 4; i++) {
        environment.occlusion_radius = radii[i];
        REQUIRE(!render(&environment));
        REQUIRE(strstr(render_scene3d_error(scene3d), "occlusion radius"));
    }
    environment.occlusion = OCCLUSION_OFF;
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    environment.occlusion = OCCLUSION_HIGH + 1;
    environment.occlusion_radius = 1;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "Off, Low, Medium or High"));
    return true;
}

/* No light but what a test adds, bounced at the given quality. */
static Environment bounced_environment(GlobalIllumination quality)
{
    Environment environment = unlit_environment();
    environment.global_illumination = quality;
    return environment;
}

/* The floor add_box_on_floor's box stands on. */
static ecs_entity add_ground(unsigned color)
{
    return add_solid((CF_V3){ 0, -.1f, 0 }, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f }, color);
}

static bool render_bounced(Environment environment, int quality)
{
    environment.global_illumination = quality;
    return render(&environment) && !render_scene3d_error(scene3d)[0];
}

/* The middle of add_box_on_floor's box's face toward the eye, which the sun
 * along x never reaches. */
static const CF_V3 box_front = { 0, 1, 1 };

/* The sunlit red floor lights the white box's face that the sun misses, in
 * the floor's color. */
TEST_CASE(global_illumination_bounces_light_between_solids)
{
    add_ground(0xff0000ffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_OFF);
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(box_front).colors.r == 0);
    REQUIRE(render_bounced(environment, GLOBAL_ILLUMINATION_LOW));
    CF_Pixel face = pixel_at(box_front);
    REQUIRE(face.colors.r > 20 && face.colors.r > face.colors.g + 15);
    return true;
}

/* A sunlit red block 1e13 units wide under the white box lights the box's
 * face, though its volume and its frame's inverse pass float's range. One
 * 1e20 units wide, past what the shaders' float math holds, leaves the face
 * the light it takes without global illumination. */
TEST_CASE(global_illumination_reaches_across_vast_solids)
{
    ecs_entity block = add_solid(
        (CF_V3){ 0, -5e12f, 0 },
        (Geometry3D){ PRIMITIVE_BOX, 1e13f, 1e13f, 1e13f },
        0xff0000ffu
    );
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->intensity = 1;
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_LOW);
    REQUIRE(render(&environment));
    CF_Pixel face = pixel_at(box_front);
    REQUIRE(face.colors.r > 20 && face.colors.r > face.colors.g + 15);
    *Geometry3D_get(test_world, block) = (Geometry3D){ PRIMITIVE_BOX,
                                                       1e20f,
                                                       1e20f,
                                                       1e20f,
                                                       -5e19f };
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(pixel_at(box_front).colors.r == 0);
    return true;
}

/* An emissive solid below the view lights the floor near it, where no light
 * reaches. */
TEST_CASE(global_illumination_carries_emitted_light)
{
    add_ground(0xffffffffu);
    ecs_entity glow = add_solid((CF_V3){ 0, .5f, 4 }, (Geometry3D){ PRIMITIVE_BOX, 4, 1, 1 }, 0xffffffffu);
    Material *material = Material_get(test_world, glow);
    material->emission_color = 0xff8000ffu;
    material->emission_intensity = 4;
    material->emission_multiplier = 1;
    const CF_V3 near = { 0, 0, 1.5f };
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_OFF);
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(near).colors.r == 0);
    REQUIRE(render_bounced(environment, GLOBAL_ILLUMINATION_LOW));
    CF_Pixel floor = pixel_at(near);
    REQUIRE(floor.colors.r > 10 && floor.colors.r > floor.colors.b + 5);
    return true;
}

/* Screen light carries the light of an emissive solid the last frame showed
 * to the floor beside it, where no other light reaches, and needs the
 * occlusion's depth. */
TEST_CASE(screen_light_carries_emitted_light_it_sees)
{
    add_ground(0xffffffffu);
    ecs_entity glow = add_solid((CF_V3){ 0, .5f, 0 }, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1 }, 0xffffffffu);
    Material *material = Material_get(test_world, glow);
    material->emission_color = 0xff8000ffu;
    material->emission_intensity = 4;
    material->emission_multiplier = 1;
    const CF_V3 near = { 1.5f, 0, 0 };
    Environment environment = unlit_environment();
    environment.occlusion = OCCLUSION_LOW;
    environment.occlusion_radius = 1;
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(near).colors.r == 0);
    environment.screen_light = SCREEN_LIGHT_SCREEN;
    /* The first frame's light comes from what the frame before it showed. */
    for (int i = 0; i < 3; i++) {
        REQUIRE(render(&environment));
    }
    CF_Pixel floor = pixel_at(near);
    REQUIRE(floor.colors.r > 10 && floor.colors.r > floor.colors.b + 5);
    /* Accumulated over jittered frames, light falls off with distance and
     * leaves the far floor well below the near. */
    for (int i = 0; i < 40; i++) {
        REQUIRE(render(&environment));
    }
    CF_Pixel close = pixel_at(near), far = pixels[SIDE / 2];
    REQUIRE(close.colors.r > far.colors.r + 60 && far.colors.r < 128);
    environment.occlusion = OCCLUSION_OFF;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "screen light needs occlusion"));
    return true;
}

/* On open ground the probes give back the light the ambient colors give
 * without them, the sky's above the horizon and the ground's below, at every
 * quality. */
TEST_CASE(global_illumination_keeps_open_ground_lit_as_the_ambient_colors)
{
    add_ground(0xffffffffu);
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_OFF);
    environment.ambient_sky = 0x80c0ffffu;
    environment.ambient_ground = 0x402000ffu;
    environment.ambient_intensity = .5f;
    const CF_V3 spots[] = { { 0, 0, 0 }, { -2.2f, 0, -1.5f }, { 1.5f, 0, 1.2f } };
    REQUIRE(render(&environment));
    CF_Pixel ambient[3];
    for (int i = 0; i < 3; i++) {
        ambient[i] = pixel_at(spots[i]);
    }
    REQUIRE(ambient[0].colors.b > 60);
    for (int quality = GLOBAL_ILLUMINATION_LOW; quality <= GLOBAL_ILLUMINATION_HIGH;
         quality++) {
        REQUIRE(render_bounced(environment, quality));
        for (int i = 0; i < 3; i++) {
            REQUIRE(distance(pixel_at(spots[i]), ambient[i]) <= 4);
        }
    }
    return true;
}

/* Global illumination needs a known quality, and traces a bounded number of
 * solids: here 342 Box Frames' 12 beams each. */
TEST_CASE(global_illumination_settings_must_be_valid)
{
    add_box_on_floor();
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_HIGH + 1);
    REQUIRE(!render(&environment));
    REQUIRE(
        strstr(render_scene3d_error(scene3d), "global illumination must be Off, Low, Medium or High")
    );
    for (int i = 0; i < 342; i++) {
        add_solid_at(
            (float)(i % 18),
            (Geometry3D){ PRIMITIVE_BOX_FRAME, .5f, .5f, .5f, (float)(i / 18) },
            0xffffffffu
        );
    }
    environment.global_illumination = GLOBAL_ILLUMINATION_LOW;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "at most 4096 solids; the scene has 4106"));
    return true;
}

/* Starts the renders over, as a new renderer. */
static void renew()
{
    render_scene3d_free(scene3d);
    scene3d = render_scene3d_new();
    render_scene3d_set_size(scene3d, SIDE, SIDE);
}

/* A shadowed light inside its bulb still lights the red floor, which
 * bounces it onto the white box's face that the light misses. */
TEST_CASE(global_illumination_lights_pass_their_bulbs)
{
    add_ground(0xff0000ffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    const CF_V3 bulb = { 0, 3, .5f };
    add_solid(bulb, (Geometry3D){ PRIMITIVE_SPHERE, .4f, .4f, .4f }, 0xffffffffu);
    ecs_entity lamp = add_lamp(bulb, 4, 8);
    PointLight_get(test_world, lamp)->shadows = true;
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_OFF);
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(box_front).colors.r == 0);
    REQUIRE(render_bounced(environment, GLOBAL_ILLUMINATION_LOW));
    CF_Pixel face = pixel_at(box_front);
    REQUIRE(face.colors.r > 20 && face.colors.r > face.colors.g + 15);
    return true;
}

/* The light on the white box's face after eight frames. */
static int box_front_after_frames(Environment *environment)
{
    for (int i = 0; i < 8; i++) {
        bool drawn = render(environment);
        assert(drawn);
    }
    return pixel_at(box_front).colors.r;
}

/* A shadowed light inside a Box Frame lights the red floor through it, as in
 * the lit pass: the frame's box holds the light, so its beams cast no shadow
 * from it, and the floor bounces about as much light onto the white box's
 * face as without the frame. Shadowing beams would take nearly half. */
TEST_CASE(global_illumination_lights_pass_frames_around_them)
{
    add_ground(0xff0000ffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    const CF_V3 bulb = { 0, 3, .5f };
    ecs_entity lamp = add_lamp(bulb, 4, 8);
    PointLight_get(test_world, lamp)->shadows = true;
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_LOW);
    int open = box_front_after_frames(&environment);
    REQUIRE(open > 20);
    add_solid(bulb, (Geometry3D){ PRIMITIVE_BOX_FRAME, 1.2f, 1.2f, 1.2f }, 0xffffffffu);
    renew();
    int framed = box_front_after_frames(&environment);
    REQUIRE(framed * 10 > open * 9);
    return true;
}

/* Two small triangles at opposite corners of the unit box, whose bounds are
 * the whole box. */
static render_mesh_asset corner_triangles()
{
    const CF_V3 corners[] = { { -.5f, -.5f, -.5f }, { -.5f, -.5f, -.4f },
                              { -.4f, -.5f, -.5f }, { .5f, .5f, .5f },
                              { .4f, .5f, .5f },    { .5f, .5f, .4f } };
    render_mesh_asset mesh = { .min = { -.5f, -.5f, -.5f }, .max = { .5f, .5f, .5f } };
    for (int i = 0; i < 6; i++) {
        render_mesh_surface_vertex vertex = { .position = corners[i],
                                              .normal = { 0, 1, 0 } };
        apush(mesh.vertices, vertex);
        apush(mesh.indices, i);
    }
    return mesh;
}

/* Inside a hollow mesh's box, the sunlit red floor still lights the white
 * box's face that the sun misses: rays leave the mesh's box they start in,
 * as the mesh within it may be open. */
TEST_CASE(global_illumination_sees_out_of_mesh_boxes)
{
    render_mesh_asset mesh = corner_triangles();
    publish_mesh(&mesh);
    add_ground(0xff0000ffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    import_mesh(
        add_solid((CF_V3){ 0, 2, 0 }, (Geometry3D){ PRIMITIVE_BOX, 12, 12, 4 }, 0xffffffffu)
    );
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    REQUIRE(render_bounced(bounced_environment(GLOBAL_ILLUMINATION_OFF), GLOBAL_ILLUMINATION_LOW));
    CF_Pixel face = pixel_at(box_front);
    REQUIRE(face.colors.r > 20 && face.colors.r > face.colors.g + 15);
    return true;
}

/* Where no probe holds light, as when they all lie inside a solid around
 * the view, surfaces take the ambient light they have without them, from
 * the frame a solid moves around the probes as from the first. The pebble
 * keeps the probes' grid around both places the solid stands. */
TEST_CASE(global_illumination_falls_back_where_no_probe_holds_light)
{
    add_ground(0xffffffffu);
    ecs_entity around = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 40, 40, 40 }, 0xffffffffu);
    add_solid((CF_V3){ -25, 0, 0 }, (Geometry3D){ PRIMITIVE_BOX, .2f, .2f, .2f }, 0xffffffffu);
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_OFF);
    environment.ambient_sky = 0x80c0ffffu;
    environment.ambient_ground = 0x402000ffu;
    environment.ambient_intensity = .5f;
    REQUIRE(render(&environment));
    CF_Pixel ambient = pixel_at((CF_V3){ 0 });
    REQUIRE(ambient.colors.b > 60);
    REQUIRE(render_bounced(environment, GLOBAL_ILLUMINATION_LOW));
    REQUIRE(distance(pixel_at((CF_V3){ 0 }), ambient) <= 1);
    Transform_get(test_world, around)->x = 30;
    for (int i = 0; i < 4; i++) {
        REQUIRE(render_bounced(environment, GLOBAL_ILLUMINATION_LOW));
    }
    REQUIRE(distance(pixel_at((CF_V3){ 0 }), ambient) > 1);
    Transform_get(test_world, around)->x = 0;
    REQUIRE(render_bounced(environment, GLOBAL_ILLUMINATION_LOW));
    REQUIRE(distance(pixel_at((CF_V3){ 0 }), ambient) <= 1);
    return true;
}

/* A floor of 144 tiles bounces the light one floor box does: rays find each
 * tile through the BVH, whose nodes take more than a row of their texture. */
TEST_CASE(global_illumination_finds_every_solid_through_its_bvh)
{
    ecs_entity ground = add_ground(0xff0000ffu);
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_LOW);
    REQUIRE(render(&environment));
    CF_Pixel whole = pixel_at(box_front);
    REQUIRE(whole.colors.r > 20);
    assert(ecs_world_destroy_entity(test_world, ground) == ECS_OK);
    const float tile = 16.0f / 12;
    for (int i = 0; i < 144; i++) {
        CF_V3 center = { ((float)(i % 12) + .5f) * tile - 8,
                         -.1f,
                         ((float)(i / 12) + .5f) * tile - 8 };
        add_solid(center, (Geometry3D){ PRIMITIVE_BOX, tile, tile, .2f }, 0xff0000ffu);
    }
    renew();
    REQUIRE(render(&environment));
    REQUIRE(distance(pixel_at(box_front), whole) <= 2);
    return true;
}

/* A white roof over white ground, with the sun along x: the ground under
 * the roof takes light mostly from the roof's underside, which takes it from
 * the sunlit ground, so it needs two bounces. Looks under the roof. */
static void add_roof()
{
    add_ground(0xffffffffu);
    add_solid((CF_V3){ 0, 1.6f, 0 }, (Geometry3D){ PRIMITIVE_BOX, 4, 4, .2f }, 0xffffffffu);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    look((CF_V3){ 0, .5f, 6 }, (CF_V3){ 0, .3f, 0 }, (CF_V3){ 0, 1, 0 });
}

/* The ground's light under the roof after `frames` more frames. */
static int under_roof(Environment *environment, int frames)
{
    for (int i = 0; i < frames; i++) {
        bool drawn = render(environment);
        assert(drawn);
    }
    return pixel_at((CF_V3){ 0 }).colors.r;
}

/* Light gathers over frames, a bounce further each frame, and stays when a
 * new solid refits the probes. Turning it off forgets it. */
TEST_CASE(global_illumination_gathers_light_over_frames)
{
    add_roof();
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_LOW);
    REQUIRE(under_roof(&environment, 1) < 5);
    int gathered = under_roof(&environment, 7);
    REQUIRE(gathered > 30);
    add_solid((CF_V3){ 12, 0, 0 }, (Geometry3D){ PRIMITIVE_BOX, .2f, .2f, .2f }, 0xffffffffu);
    REQUIRE(under_roof(&environment, 1) > gathered / 2);
    environment.global_illumination = GLOBAL_ILLUMINATION_OFF;
    under_roof(&environment, 1);
    environment.global_illumination = GLOBAL_ILLUMINATION_LOW;
    REQUIRE(under_roof(&environment, 1) < 5);
    return true;
}

static bool timed(const char *name)
{
    int count;
    const render_gpu_pass *passes = render_gpu_timing_passes(&count);
    for (int i = 0; i < count; i++) {
        if (!strcmp(passes[i].name, name)) {
            return passes[i].count == 1;
        }
    }
    return false;
}

/* Pass timing charges the scene's uploads, the shadow maps, ambient
 * occlusion and global illumination each frame to passes of their own. */
TEST_CASE(gpu_timing_names_the_3d_passes)
{
    add_shadow_scene()->shadows = true;
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_LOW);
    environment.occlusion = OCCLUSION_LOW;
    render_gpu_timing_enable(RENDER_GPU_TIMING_SYNC, false);
    render_gpu_timing_start();
    bool drawn = render(&environment);
    bool named = timed("3D setup") && timed("3D shadow maps") &&
                 timed("3D ambient occlusion") && timed("3D global illumination");
    render_gpu_timing_enable(RENDER_GPU_TIMING_OFF, false);
    REQUIRE(drawn && named);
    return true;
}

#if CHIBI_TEST_MATERIAL_GRAPHS
/* The two-unit box at the origin, drawn with surface material `name`. */
static ecs_entity add_surface_box(const char *name)
{
    ecs_entity box = add_solid((CF_V3){ 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    Shader binding = { .material = name };
    assert(ecs_world_record_add(test_world, box, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return box;
}

/* The direction the view ray through pixel (x, y) leaves a face with outward
 * `normal`, in the sky turned `quarters` quarter turns. */
static CF_V3 reflection(int x, int y, CF_V3 normal, int quarters)
{
    CF_V3 d = cf_norm_v3(pixel_ray(x, y, (CF_V3){ 0 }).d);
    return sky_frame(cf_sub_v3(d, cf_mul_v3_f(normal, 2 * cf_dot_v3(d, normal))), quarters);
}

/* A white mirror shows on each face the octant its reflected view rays look
 * into, unturned and turned a quarter. Reflections near an octant's edge are
 * left out, where the cube's filtering blends neighbors. */
TEST_CASE(mirrors_reflect_the_sky_in_its_directions)
{
    look_at_corner();
    publish_octant_sky();
    REQUIRE(test_register_surface("Mirror", 0, 1));
    add_surface_box("Mirror");
    Environment environment = sky_environment();
    for (int quarters = 0; quarters < 2; quarters++) {
        environment.sky_rotation = 90.0f * quarters;
        REQUIRE(render(&environment));
        int checked[3] = { 0 };
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE; x++) {
                CF_V3 normal;
                if (!box_face(x, y, (CF_V3){ 0 }, &normal) || !cf_len_sq_v3(normal)) {
                    continue;
                }
                CF_V3 r = reflection(x, y, normal, quarters);
                float axes[3] = { r.x, r.y, r.z };
                if (fabsf(r.x) < .3f || fabsf(r.y) < .3f || fabsf(r.z) < .3f) {
                    continue;
                }
                int shown[3];
                channels(pixel(x, y), shown);
                for (int c = 0; c < 3; c++) {
                    REQUIRE(axes[c] > 0 ? shown[c] > 160 : shown[c] < 10);
                }
                checked[normal.x ? 0 : normal.y ? 1 : 2]++;
            }
        }
        REQUIRE(checked[0] > 20 && checked[1] > 20 && checked[2] > 20);
    }
    return true;
}

/* A materials world whose 16 x 16 terrain at the origin is a TerrainVolume
 * over field `name`, run by the test's runtime until volume_scene_free. */
typedef struct
{
    entities *actors;
    materials *rock;
    ecs_entity source;
} volume_scene;

static volume_scene volume_scene_new(const char *name, const field_ir *code, int count, TerrainVolume volume)
{
    volume_scene s = { entities_new(test_world, NULL) };
    s.rock = materials_new(test_world, s.actors, test_particles);
    entities_bind_materials(s.actors, s.rock);
    runtime = engine_new(
        test_world,
        &(engine_modules){ .entities = s.actors, .materials = s.rock, .particles = test_particles }
    );
    char error[256];
    field_program *program = field_program_new(code, count, count - 1, NULL, 0, error, sizeof error);
    assert(program && material_field_register(materials_fields(s.rock), name, program));
    s.source = add(COMPONENT_Transform.id, &(Transform){ 0 });
    MaterialTerrain terrain = { .width = 16,
                                .height = 16,
                                .cell_size = 1,
                                .field = name,
                                .min_fragment_cells = 1,
                                .category = 1,
                                .mask = 1 };
    assert(
        ecs_world_record_add(test_world, s.source, COMPONENT_MaterialTerrain.id, &terrain, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(test_world, s.source, COMPONENT_TerrainVolume.id, &volume, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    assert(!materials_prepare(s.rock));
    return s;
}

static void volume_scene_free(volume_scene *s)
{
    engine_free(runtime);
    runtime = NULL;
    materials_free(s->rock);
    entities_free(s->actors);
}

/* Rock ahead of a terrain volume's play plane only casts shadows until the
 * volume names a front material, which then draws it. */
TEST_CASE(terrain_volume_front_rock_draws_with_its_front_material)
{
    look((CF_V3){ 0, 12, 0 }, (CF_V3){ 0, -1, 0 }, (CF_V3){ 0, 0, -1 });
    REQUIRE(test_register_tint("Red", (const float[3]){ 1, 0, 0 }));
    /* Solid from four units ahead of the plane back through it. */
    const field_ir code[] = { { .op = FIELD_Z },
                              { .op = FIELD_CONSTANT, .value = -4 },
                              { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 1, 0 } } };
    TerrainVolume volume = { .enabled = true,
                             .voxel = 1,
                             .depth = 4,
                             .refine = 1,
                             .front = 8,
                             .slab = 2 };
    volume_scene s = volume_scene_new("Front", code, 3, volume);
    materials *rock = s.rock;
    ecs_entity source = s.source;
    Environment environment = environment_defaults();
    REQUIRE(render(&environment));
    CF_Pixel hidden = pixels[SIDE / 2 * SIDE + SIDE / 2];
    REQUIRE(hidden.colors.r < 30);
    volume.front_material = "Red";
    REQUIRE(ecs_world_record_remove(test_world, source, COMPONENT_TerrainVolume.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(
        ecs_world_record_add(test_world, source, COMPONENT_TerrainVolume.id, &volume, 0) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(!materials_prepare(rock));
    REQUIRE(render(&environment));
    CF_Pixel drawn = pixels[SIDE / 2 * SIDE + SIDE / 2];
    REQUIRE(drawn.colors.r > 150 && drawn.colors.r > 3 * drawn.colors.g);
    volume_scene_free(&s);
    return true;
}

/* World screen light meets an emissive solid in its grid and takes the light
 * the last frame showed there to the floor beside it, with global
 * illumination off and the scene collected in parallel chunks. A terrain
 * volume of open air gives the rays their world. */
TEST_CASE(world_screen_light_meets_the_solids_it_traces)
{
    add_ground(0xffffffffu);
    /* Tall and wide enough to hold a two-unit grid cell's center. */
    ecs_entity glow = add_solid((CF_V3){ 0, 1.5f, 0 }, (Geometry3D){ PRIMITIVE_BOX, 2, 3, 2 }, 0xffffffffu);
    Material *material = Material_get(test_world, glow);
    material->emission_color = 0xff8000ffu;
    material->emission_intensity = 4;
    material->emission_multiplier = 1;
    /* Far solids, out of view, make the scene two collection chunks. */
    for (int i = 0; i < 1100; i++) {
        add_solid(
            (CF_V3){ 200.0f + i % 40, 0, 200.0f + i / 40 },
            (Geometry3D){ PRIMITIVE_BOX, .1f, .1f, .1f },
            0xffffffffu
        );
    }
    const field_ir air[] = { { .op = FIELD_CONSTANT, .value = 1 } };
    volume_scene s = volume_scene_new(
        "Air",
        air,
        1,
        (TerrainVolume){ .enabled = true, .voxel = 1, .depth = 4, .refine = 1, .slab = 2 }
    );
    const CF_V3 near = { 2, 0, 0 };
    Environment environment = unlit_environment();
    environment.occlusion = OCCLUSION_LOW;
    environment.occlusion_radius = 1;
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(near).colors.r == 0);
    environment.screen_light = SCREEN_LIGHT_WORLD;
    for (int i = 0; i < 20; i++) {
        REQUIRE(render(&environment));
    }
    REQUIRE(render_scene3d_last_work(scene3d).tasks > 1);
    CF_Pixel floor = pixel_at(near);
    REQUIRE(floor.colors.r > 10 && floor.colors.r > floor.colors.b + 5);
    volume_scene_free(&s);
    return true;
}
/* A sky material of constant radiance draws and lights exactly as a uniform
 * sky image of that radiance does. */
TEST_CASE(sky_materials_draw_and_light_like_sky_images)
{
    look_level();
    add_solid((CF_V3){ 0, 0, -6 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    int box = SIDE / 2 * SIDE + SIDE / 2;
    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    Environment environment = sky_environment();
    REQUIRE(render(&environment));
    static CF_Pixel imaged[PIXELS];
    memcpy(imaged, pixels, sizeof pixels);
    REQUIRE(imaged[0].colors.r > 100 && imaged[box].colors.r > 100);
    REQUIRE(test_register_tint("Gray", (const float[3]){ .5f, .5f, .5f }));
    environment.sky = NULL;
    environment.sky_material = "Gray";
    REQUIRE(render(&environment));
    REQUIRE(abs(pixels[0].colors.r - imaged[0].colors.r) <= 2);
    REQUIRE(abs(pixels[box].colors.r - imaged[box].colors.r) <= 3);
    return true;
}

/* Another sky material bakes again: the box it lights and the sky behind it
 * turn red, then gray again. */
TEST_CASE(sky_materials_bake_again_when_they_change)
{
    look_level();
    add_solid((CF_V3){ 0, 0, -6 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    int box = SIDE / 2 * SIDE + SIDE / 2;
    REQUIRE(test_register_tint("Gray", (const float[3]){ .5f, .5f, .5f }));
    REQUIRE(test_register_tint("Red", (const float[3]){ 1, 0, 0 }));
    Environment environment = sky_environment();
    environment.sky = NULL;
    environment.sky_material = "Gray";
    REQUIRE(render(&environment));
    CF_Pixel gray = pixels[box];
    REQUIRE(abs(gray.colors.r - gray.colors.g) <= 3 && gray.colors.r > 30);
    environment.sky_material = "Red";
    REQUIRE(render(&environment));
    REQUIRE(
        pixels[0].colors.r > 100 && pixels[0].colors.r > 3 * pixels[0].colors.g
    );
    REQUIRE(
        pixels[box].colors.r > 100 &&
        pixels[box].colors.r > 3 * pixels[box].colors.g
    );
    environment.sky_material = "Gray";
    REQUIRE(render(&environment));
    REQUIRE(abs(pixels[box].colors.r - gray.colors.r) <= 2);
    REQUIRE(abs(pixels[box].colors.g - gray.colors.g) <= 2);
    return true;
}

/* A sky material sees each direction it is drawn along: red only where
 * the view ray leans far enough toward +X. */
TEST_CASE(sky_materials_follow_view_directions)
{
    look((CF_V3){ 0 }, (CF_V3){ 1, 0, -1 }, (CF_V3){ 0, 1, 0 });
    REQUIRE(test_register_world_step("Step"));
    Environment environment = sky_environment();
    environment.sky = NULL;
    environment.sky_material = "Step";
    REQUIRE(render(&environment));
    int red = 0, black = 0;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            CF_V3 d = cf_norm_v3(pixel_ray(x, y, (CF_V3){ 0 }).d);
            if (fabsf(d.x - .5f) < .03f) {
                continue;
            }
            bool lit = pixel(x, y).colors.r > 100;
            REQUIRE(lit == (d.x > .5f));
            red += lit;
            black += !lit;
        }
    }
    REQUIRE(red > 100 && black > 100);
    return true;
}

/* An Environment draws a sky image or a sky material, and the material must
 * be a surface material that reads no images. */
TEST_CASE(sky_material_settings_must_be_valid)
{
    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    REQUIRE(test_register_tint("Gray", (const float[3]){ .5f, .5f, .5f }));
    REQUIRE(test_register_images("Pictured", 1, false));
    Environment environment = sky_environment();
    environment.sky_material = "Gray";
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "a sky image or a sky material, not both"));
    environment.sky = NULL;
    environment.sky_material = "Missing";
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "Missing is not a surface material"));
    environment.sky_material = "Pictured";
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "reads images"));
    environment.sky_material = "Gray";
    REQUIRE(render(&environment));
    return true;
}

/* Rougher surfaces reflect the sky over a wider lobe. Every view ray that
 * meets a mirror's +Z face reflects into the octant toward -X, -Y and +Z, so
 * the face shows only blue; a fully rough face gathers red and green from the
 * neighboring octants. */
TEST_CASE(rough_surfaces_blur_the_sky_they_reflect)
{
    look_at_corner();
    publish_octant_sky();
    REQUIRE(test_register_surface("Mirror", 0, 1));
    REQUIRE(test_register_surface("Rough", 1, 1));
    ecs_entity box = add_surface_box("Mirror");
    Environment environment = sky_environment();
    int mean[2][3] = { 0 };
    for (int i = 0; i < 2; i++) {
        Shader_get(test_world, box)->material = i ? "Rough" : "Mirror";
        REQUIRE(render(&environment));
        int count = 0, sums[3] = { 0 };
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE; x++) {
                CF_V3 normal;
                if (!box_face(x, y, (CF_V3){ 0 }, &normal) || normal.z <= 0) {
                    continue;
                }
                int shown[3];
                channels(pixel(x, y), shown);
                for (int c = 0; c < 3; c++) {
                    sums[c] += shown[c];
                }
                count++;
            }
        }
        REQUIRE(count > 100);
        for (int c = 0; c < 3; c++) {
            mean[i][c] = sums[c] / count;
        }
    }
    REQUIRE(mean[0][0] < 10 && mean[0][1] < 10 && mean[0][2] > 160);
    REQUIRE(mean[1][0] > 25 && mean[1][1] > 25 && mean[1][2] + 40 < mean[0][2]);
    return true;
}

/* Whether a pixel shows linear color `rgb` through neutral tone mapping,
 * whose offset applies with every channel at least 0.08. */
static bool shows_ramp(CF_Pixel shown, const float rgb[3])
{
    int values[3];
    channels(shown, values);
    for (int c = 0; c < 3; c++) {
        if (abs(values[c] - neutral_srgb(rgb[c])) > 2) {
            return false;
        }
    }
    return true;
}

/* Ramps of every material in a program share one table. SDL's Vulkan backend
 * binds the first 4 KB of a uniform block, which the scene's lights fill, so a
 * table in that block left every ramp black. Nine full ramps fill its first 288
 * entries; the first and the last material each read their own stops, and a
 * numeric stop edit reaches the GPU without a recompile. */
TEST_CASE(color_ramps_color_surfaces_from_any_table_offset)
{
    static const float gray[4] = { .3f, .3f, .3f, 1 },
                       red[4] = { .7f, .1f, .4f, 1 },
                       green[4] = { .1f, .7f, .4f, 1 },
                       blue[4] = { .1f, .4f, .7f, 1 };
    for (int i = 0; i < 9; i++) {
        char name[16];
        snprintf(name, sizeof name, "Filler %d", i);
        REQUIRE(test_register_ramp(name, SHADER_RAMP_MAX, gray, gray));
    }
    REQUIRE(test_register_ramp("Ramp", 2, red, green));
    ecs_entity box = add_surface_box("Filler 0");
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), gray));
    Shader_get(test_world, box)->material = "Ramp";
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), (const float[]){ .55f, .25f, .4f }));
    unsigned compiles = render_shader_compile_count();
    REQUIRE(test_register_ramp("Ramp", 2, red, blue));
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), (const float[]){ .55f, .175f, .475f }));
    REQUIRE(render_shader_compile_count() == compiles);
    return true;
}

/* The unit box mesh whose top face draws with `material` and whose other
 * faces inherit the entity's. */
static void publish_box_top(const char *material)
{
    render_mesh_asset box = box_mesh();
    static const int runs[][2] = { { 0, 18 }, { 18, 6 }, { 24, 12 } };
    for (int i = 0; i < 3; i++) {
        render_mesh_group group = { .first = runs[i][0],
                                    .count = runs[i][1],
                                    .part = -1,
                                    .asset = xstrdup("Box"),
                                    .name = xstrdup("Faces"),
                                    .path = xstrdup("Box/Faces"),
                                    .material = xstrdup(i == 1 ? material : ""),
                                    .material_scope = xstrdup("") };
        apush(box.groups, group);
    }
    publish_mesh(&box);
}

/* A mesh's top face has its own material and its other faces inherit the
 * entity's. A group naming no surface material fails the frame. */
TEST_CASE(imported_mesh_groups_take_their_own_materials)
{
    static const float red[4] = { .7f, .1f, .1f, 1 }, blue[4] = { .1f, .1f, .7f, 1 };
    REQUIRE(test_register_ramp("Red", 2, red, red));
    REQUIRE(test_register_ramp("Blue", 2, blue, blue));
    publish_box_top("Red");
    import_mesh(add_surface_box("Blue"));
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), red) && shows_ramp(front(), blue));
    publish_box_top("Missing");
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "Unknown surface material: Missing"));
    return true;
}

/* Surface graphs read a mesh's vertex UVs, or with source bounds their box
 * projection, and its part positions, or without them its own positions. */
TEST_CASE(imported_meshes_carry_texture_and_part_coordinates)
{
    REQUIRE(test_register_coordinate("U", "Shader UV"));
    REQUIRE(test_register_coordinate("Part X", "Shader PartPosition"));
    REQUIRE(test_register_coordinate("Object X", "Shader ObjectPosition"));
    ecs_entity box = add_surface_box("U");
    import_mesh(box);
    Environment environment = unlit_environment();
    render_mesh_asset mesh = box_mesh();
    for (int i = 0; i < acount(mesh.vertices); i++) {
        mesh.vertices[i].uv = (CF_V2){ .25f, .5f };
    }
    publish_mesh(&mesh);
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), (const float[]){ .25f, .25f, .25f }));
    /* Bounds 100 units wide from x = -60 put the top's center at u = 0.6. */
    mesh = box_mesh();
    mesh.has_uv_bounds = true;
    mesh.uv_min = (CF_V3){ -60, -1, -1 };
    mesh.uv_max = (CF_V3){ 40, 1, 1 };
    publish_mesh(&mesh);
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), (const float[]){ .6f, .6f, .6f }));
    mesh = box_mesh();
    mesh.has_part_coordinates = true;
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex *vertex = &mesh.vertices[i];
        vertex->part_position = (CF_V3){ .4f, 0, 0 };
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                vertex->part_from_object[j][k] = j == k;
            }
        }
    }
    publish_mesh(&mesh);
    Shader_get(test_world, box)->material = "Part X";
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), (const float[]){ .4f, .4f, .4f }));
    mesh = box_mesh();
    publish_mesh(&mesh);
    REQUIRE(render(&environment));
    static CF_Pixel part[PIXELS];
    memcpy(part, pixels, sizeof pixels);
    Shader_get(test_world, box)->material = "Object X";
    REQUIRE(render(&environment));
    /* The top's x = 0.6 is the mesh's x = 0.3. */
    REQUIRE(pixel_at((CF_V3){ .6f, 1, 0 }).colors.g > 60);
    for (int i = 0; i < PIXELS; i++) {
        REQUIRE(distance(pixels[i], part[i]) <= 1);
    }
    return true;
}

/* A square on the ground facing up, scaled by the two-unit surface box to
 * four units, with its texture's u along `u` and v along `v`, and every
 * vertex's tangent `tangent`, or with no tangent, tangents from its UVs.
 * With `projected`, source bounds project its UVs from its positions. */
static void publish_square(CF_V3 u, CF_V3 v, CF_V4 tangent, bool projected)
{
    static const CF_V3 corners[] = { { -1, 0, -1 },
                                     { -1, 0, 1 },
                                     { 1, 0, 1 },
                                     { 1, 0, -1 } };
    static const int indices[] = { 0, 1, 2, 0, 2, 3 };
    render_mesh_asset square = { .min = { -1, 0, -1 },
                                 .max = { 1, 0, 1 },
                                 .has_uv_bounds = projected,
                                 .uv_min = { -1, -1, -1 },
                                 .uv_max = { 1, 1, 1 } };
    for (int i = 0; i < 4; i++) {
        CF_V3 p = corners[i];
        render_mesh_surface_vertex vertex = {
            .position = p,
            .normal = { 0, 1, 0 },
            .uv = { (cf_dot_v3(p, u) + 1) / 2, (cf_dot_v3(p, v) + 1) / 2 },
            .tangent = tangent,
        };
        apush(square.vertices, vertex);
    }
    for (int i = 0; i < 6; i++) {
        apush(square.indices, indices[i]);
    }
    if (tangent.w) {
        publish_prepared(&square);
        return;
    }
    publish_mesh(&square);
}

/* How bright the ground square's center shows with material `name`. */
static int square_light(ecs_entity square, const char *name)
{
    Shader_get(test_world, square)->material = name;
    Environment environment = unlit_environment();
    if (!render(&environment)) {
        return -1;
    }
    return pixel_at((CF_V3){ 0 }).colors.g;
}

/* Normal maps turn with a mesh's tangents. A sun shines down and toward -z.
 * A map tilted along u lights a square whose u runs along +z more than a
 * flat map does, and a square whose u runs along +x but whose authored
 * tangents point along +z shades the same. A DirectX map tilted along v
 * follows the tangents' handedness: it faces the sun when v runs along +z
 * and turns away when v is mirrored. Box-projected UVs keep their own frame,
 * along +x, and authored tangents turn with the square, across the sun. */
TEST_CASE(normal_maps_follow_mesh_tangents)
{
    static const float flat[] = { .5f, .5f, 1 }, along_u[] = { .85f, .5f, .85f },
                       along_v[] = { .5f, .85f, .85f };
    REQUIRE(test_register_normal_map("Flat", flat, SHADER_NORMAL_DIRECTX));
    REQUIRE(test_register_normal_map("AlongU", along_u, SHADER_NORMAL_DIRECTX));
    REQUIRE(test_register_normal_map("AlongV", along_v, SHADER_NORMAL_DIRECTX));
    static const CF_V3 x = { 1, 0, 0 }, z = { 0, 0, 1 }, back = { 0, 0, -1 };
    publish_square(z, x, (CF_V4){ 0 }, false);
    ecs_entity square = add_surface_box("Flat");
    import_mesh(square);
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 });
    sun->intensity = 1;
    int level = square_light(square, "Flat"), facing = square_light(square, "AlongU");
    REQUIRE(level > 0 && facing > level + 10);
    publish_square(x, z, (CF_V4){ 0, 0, 1, 1 }, false);
    REQUIRE(abs(square_light(square, "AlongU") - facing) <= 3);
    publish_square(x, z, (CF_V4){ 0 }, false);
    int along = square_light(square, "AlongU");
    REQUIRE(along >= 0 && along + 10 < level);
    REQUIRE(abs(square_light(square, "AlongV") - facing) <= 3);
    publish_square(x, back, (CF_V4){ 0 }, false);
    int away = square_light(square, "AlongV");
    publish_square(x, z, (CF_V4){ 0, 0, 1, 1 }, true);
    REQUIRE(away >= 0 && away + 10 < along);
    REQUIRE(abs(square_light(square, "AlongU") - along) <= 3);
    publish_square(x, z, (CF_V4){ 0, 0, 1, 1 }, false);
    VisualTransform turned = { .yaw = CF_PI / 2, .scale = 1 };
    assert(
        ecs_world_record_add(test_world, square, COMPONENT_VisualTransform.id, &turned, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(abs(square_light(square, "AlongU") - along) <= 3);
    return true;
}

/* A Transform3D that mirrors the ground square along z keeps it lit from
 * above, and turns its bitangent with its surface: a DirectX map tilted
 * along v, which faces the sun while v runs along +z, then shades as it does
 * with v along -z. */
TEST_CASE(mirroring_transform3d_turns_mesh_bitangents)
{
    static const float flat[] = { .5f, .5f, 1 }, along_v[] = { .5f, .85f, .85f };
    REQUIRE(test_register_normal_map("Flat", flat, SHADER_NORMAL_DIRECTX));
    REQUIRE(test_register_normal_map("AlongV", along_v, SHADER_NORMAL_DIRECTX));
    static const CF_V3 x = { 1, 0, 0 }, z = { 0, 0, 1 }, back = { 0, 0, -1 };
    publish_square(x, back, (CF_V4){ 0 }, false);
    ecs_entity square = add_surface_box("Flat");
    import_mesh(square);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 })->intensity = 1;
    int away = square_light(square, "AlongV");
    publish_square(x, z, (CF_V4){ 0 }, false);
    int level = square_light(square, "Flat"), facing = square_light(square, "AlongV");
    REQUIRE(away >= 0 && away + 10 < level && level + 10 < facing);
    place_3d(square, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, -1 });
    REQUIRE(abs(square_light(square, "Flat") - level) <= 3);
    REQUIRE(abs(square_light(square, "AlongV") - away) <= 3);
    return true;
}

/* Images put (0, 0) at their top-left. A sun shines down and toward -z on a
 * square whose image runs up toward +z, so a map Studio labels OpenGL, whose
 * green tilts up the image, faces the sun, and one labelled DirectX, whose
 * green tilts down it, turns away. */
TEST_CASE(normal_map_green_tilts_up_the_image_in_opengl)
{
    static const float flat[] = { .5f, .5f, 1 }, green[] = { .5f, .85f, .85f };
    shader_normal_convention opengl = test_normal_convention("OpenGL (+Y)"),
                             directx = test_normal_convention("DirectX (-Y)");
    REQUIRE(test_register_normal_map("Flat", flat, opengl));
    REQUIRE(test_register_normal_map("OpenGL", green, opengl));
    REQUIRE(test_register_normal_map("DirectX", green, directx));
    publish_square((CF_V3){ 1, 0, 0 }, (CF_V3){ 0, 0, -1 }, (CF_V4){ 0 }, false);
    ecs_entity square = add_surface_box("Flat");
    import_mesh(square);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 })->intensity = 1;
    int level = square_light(square, "Flat");
    REQUIRE(level > 0 && square_light(square, "OpenGL") > level + 10);
    REQUIRE(square_light(square, "DirectX") + 10 < level);
    return true;
}

/* A Transform3D that stretches the ground square ten million times along
 * its normal leaves it as it was, tangents included: with authored tangents
 * along +z, a map tilted along u still faces the sun, though the frame's
 * other axes are ten million times shorter than its y. */
TEST_CASE(stretched_transform3d_keeps_mesh_tangents)
{
    static const float along_u[] = { .85f, .5f, .85f };
    REQUIRE(test_register_normal_map("AlongU", along_u, SHADER_NORMAL_DIRECTX));
    static const CF_V3 x = { 1, 0, 0 }, z = { 0, 0, 1 };
    publish_square(x, z, (CF_V4){ 0, 0, 1, 1 }, false);
    ecs_entity square = add_surface_box("AlongU");
    import_mesh(square);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 })->intensity = 1;
    place_3d(square, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    int facing = square_light(square, "AlongU");
    REQUIRE(facing > 0);
    Transform3D_get(test_world, square)->scale = (CF_V3){ 1, 1e7f, 1 };
    REQUIRE(abs(square_light(square, "AlongU") - facing) <= 3);
    return true;
}

/* Occlusion scales the light that arrives from every side, ambient and sky
 * alike, and leaves the sun's alone. */
TEST_CASE(occlusion_dims_only_ambient_light)
{
    const char *names[] = { "Open", "Half", "Occluded" };
    const float occlusion[] = { 1, .5f, 0 };
    for (int i = 0; i < 3; i++) {
        REQUIRE(test_register_occlusion(names[i], occlusion[i]));
    }
    ecs_entity box = add_surface_box(names[0]);
    Environment environment = unlit_environment();
    environment.ambient_sky = 0x00ff00ffu;
    environment.ambient_ground = 0x00ff00ffu;
    environment.ambient_intensity = .5f;
    int green[3];
    for (int i = 0; i < 3; i++) {
        Shader_get(test_world, box)->material = names[i];
        REQUIRE(render(&environment));
        green[i] = top().colors.g;
    }
    REQUIRE(green[0] > 100 && green[2] == 0);
    REQUIRE(green[2] + 20 < green[1] && green[1] + 20 < green[0]);

    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    environment = sky_environment();
    Shader_get(test_world, box)->material = names[0];
    REQUIRE(render(&environment));
    REQUIRE(top().colors.g > 60);
    Shader_get(test_world, box)->material = names[2];
    REQUIRE(render(&environment));
    REQUIRE(!top().colors.r && !top().colors.g && !top().colors.b);

    add_sun(0xff0000ffu, (CF_V3){ 0, -2, 0 });
    environment = unlit_environment();
    Shader_get(test_world, box)->material = names[0];
    REQUIRE(render(&environment));
    CF_Pixel open = top();
    Shader_get(test_world, box)->material = names[2];
    REQUIRE(render(&environment));
    REQUIRE(open.colors.r > 150 && distance(top(), open) <= 1);
    return true;
}

/* The probes' atlas takes the place of a 3D surface's eighth image, so with
 * global illumination on a material reading eight is an error. One reading
 * seven and a ramp table fills all 16 samplers and draws. */
TEST_CASE(global_illumination_leaves_surfaces_seven_images)
{
    REQUIRE(test_register_images("Seven images", 7, true));
    REQUIRE(test_register_images("Eight images", 8, false));
    ecs_entity box = add_surface_box("Eight images");
    add_sun(0xffffffffu, (CF_V3){ 0, -2, 0 });
    Environment environment = bounced_environment(GLOBAL_ILLUMINATION_OFF);
    REQUIRE(render(&environment));
    environment.global_illumination = GLOBAL_ILLUMINATION_LOW;
    REQUIRE(!render(&environment));
    REQUIRE(
        strstr(render_scene3d_error(scene3d), "Eight images reads 8 images; with global illumination on, a 3D surface reads at most 7.")
    );
    Shader_get(test_world, box)->material = "Seven images";
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(top().colors.r > 150);
    return true;
}

/* A white box at Opacity 0.3 over a purple sky. Blend mixes it over the sky.
 * Opaque draws it whole, and so does Mask while Opacity reaches the cutoff;
 * below the cutoff Mask leaves only the sky. Only Blend is translucent. */
TEST_CASE(alpha_modes_decide_how_opacity_draws)
{
    static const struct
    {
        const char *name;
        shader_alpha_mode mode;
        float cutoff;
    } modes[] = { { "Blended", SHADER_ALPHA_BLEND, .5f },
                  { "Opaque", SHADER_ALPHA_OPAQUE, .5f },
                  { "Kept", SHADER_ALPHA_MASK, .2f },
                  { "Cut", SHADER_ALPHA_MASK, .4f } };
    look_level();
    publish_uniform_sky((const unsigned char[4]){ 128, 0, 128, 128 });
    ecs_entity box = add_surface_box(modes[0].name);
    Transform_get(test_world, box)->y = -6;
    Environment environment = sky_environment();
    CF_Pixel shown[4];
    for (int i = 0; i < 4; i++) {
        REQUIRE(test_register_alpha(modes[i].name, modes[i].mode, .3f, modes[i].cutoff));
        int id = render_shader_find(modes[i].name);
        REQUIRE(render_shader_may_be_transparent(id) == (modes[i].mode == SHADER_ALPHA_BLEND));
        Shader_get(test_world, box)->material = modes[i].name;
        REQUIRE(render(&environment));
        shown[i] = pixel(SIDE / 2, SIDE / 2);
    }
    CF_Pixel open = pixel(4, 4);
    REQUIRE(shown[1].colors.g > 200 && distance(shown[2], shown[1]) <= 1);
    REQUIRE(shown[0].colors.g > 60 && shown[0].colors.g + 60 < shown[1].colors.g);
    REQUIRE(distance(shown[3], open) <= 1);
    return true;
}

/* A masked box whose Opacity works out to NaN, as the fraction of an
 * overflowed product does, drops its pixels on the GPU as the CPU evaluator
 * does, leaving the sky where a kept box stood. */
TEST_CASE(masks_drop_pixels_whose_opacity_is_not_a_number)
{
    look_level();
    publish_uniform_sky((const unsigned char[4]){ 128, 0, 128, 128 });
    ecs_entity box = add_surface_box("Kept");
    Transform_get(test_world, box)->y = -6;
    Environment environment = sky_environment();
    REQUIRE(test_register_alpha("Kept", SHADER_ALPHA_MASK, .5f, .4f));
    REQUIRE(render(&environment));
    CF_Pixel open = pixel(4, 4);
    REQUIRE(pixel(SIDE / 2, SIDE / 2).colors.g > 200);
    REQUIRE(test_register_unknown_opacity("Unknown"));
    Shader_get(test_world, box)->material = "Unknown";
    REQUIRE(render(&environment));
    REQUIRE(distance(pixel(SIDE / 2, SIDE / 2), open) <= 1);
    return true;
}

/* Opaque and masked surfaces cast shadows like any solid; a blended one lets
 * the sun through. */
TEST_CASE(opaque_and_masked_surfaces_cast_shadows)
{
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_surface_box("Blended");
    Geometry3D_get(test_world, box)->elevation = 1;
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->shadows = true;
    REQUIRE(test_register_alpha("Blended", SHADER_ALPHA_BLEND, .5f, .5f));
    REQUIRE(test_register_alpha("Opaque", SHADER_ALPHA_OPAQUE, .5f, .5f));
    REQUIRE(test_register_alpha("Kept", SHADER_ALPHA_MASK, .5f, .4f));
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(shaded).colors.g > 60);
    const char *casters[] = { "Opaque", "Kept" };
    for (int i = 0; i < 2; i++) {
        Shader_get(test_world, box)->material = casters[i];
        REQUIRE(render(&environment));
        REQUIRE(pixel_at(shaded).colors.g == 0);
    }
    return true;
}

/* A box masked to its half at z >= 0 shades the floor behind that half alone,
 * from the sun, as the unit box mesh too, and from a point light. */
TEST_CASE(masked_surfaces_cast_cut_out_shadows)
{
    REQUIRE(test_register_half_mask("Half"));
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_surface_box("Half");
    Geometry3D_get(test_world, box)->elevation = 1;
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->shadows = true;
    Environment environment = unlit_environment();
    CF_V3 cut = { shaded.x, 0, -shaded.z };
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(pixel_at(shaded).colors.g == 0 && pixel_at(cut).colors.g > 60);
    static CF_Pixel shape[PIXELS];
    memcpy(shape, pixels, sizeof pixels);
    render_mesh_asset mesh = box_mesh();
    publish_mesh(&mesh);
    import_mesh(box);
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(matches(shape));
    sun->intensity = 0;
    ecs_entity lamp = add_lamp((CF_V3){ -3, 4, 0 }, 2, 20);
    REQUIRE(render(&environment));
    CF_Pixel open = pixel_at(cut);
    PointLight_get(test_world, lamp)->shadows = true;
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 1);
    REQUIRE(open.colors.g > 30 && distance(pixel_at(cut), open) <= 3);
    REQUIRE(pixel_at(shaded).colors.g == 0);
    return true;
}

/* A box masked by View Direction to what is seen from toward -X shades the
 * floor from the sun and from a point light there, which see it kept, though
 * the camera above sees it cut. */
TEST_CASE(view_dependent_masks_cast_shadows_seen_from_the_light)
{
    REQUIRE(test_register_view_mask("Facing"));
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_surface_box("Facing");
    Geometry3D_get(test_world, box)->elevation = 1;
    DirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(pixel_at(shaded).colors.g > 60);
    sun->shadows = true;
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(shaded).colors.g == 0);
    sun->intensity = 0;
    ecs_entity lamp = add_lamp((CF_V3){ -3, 4, 0 }, 2, 20);
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(shaded).colors.g > 30);
    PointLight_get(test_world, lamp)->shadows = true;
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 1);
    REQUIRE(pixel_at(shaded).colors.g == 0);
    return true;
}

/* A masked pillar outside the view casts only its kept half into it. Crowds
 * far away split collection across workers where the host has them: the
 * pillar's material row arrives through a later buffer than the floor's, and
 * a red box's through a later buffer than the pillar's. */
TEST_CASE(masked_solids_outside_the_view_cut_their_shadows)
{
    REQUIRE(test_register_half_mask("Half"));
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    for (int i = 0; i < 600; i++) {
        add_solid_at(60, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1, 1 }, 0xffffffffu);
    }
    /* Beyond the left edge of the view and spanning z = -2 to 0, it keeps
     * z = -1 to 0, which shades the floor there up to x = 1. */
    ecs_entity pillar = add_solid((CF_V3){ -8, 4, -1 }, (Geometry3D){ PRIMITIVE_BOX, 2, 2, 8 }, 0xffffffffu);
    Shader binding = { .material = "Half" };
    assert(ecs_world_record_add(test_world, pillar, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    for (int i = 0; i < 600; i++) {
        add_solid_at(60, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1, 1 }, 0xffffffffu);
    }
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 1, 1, 1, .5f }, 0xff0000ffu);
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->shadows = true;
    Environment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    CF_V3 kept = { -1.6f, 0, -.5f }, cut = { -1.6f, 0, -1.5f },
          open = { -1.6f, 0, .5f };
    REQUIRE(pixel_at(kept).colors.g == 0);
    REQUIRE(pixel_at(cut).colors.g > 60 && pixel_at(open).colors.g > 60);
    CF_Pixel red = pixel_at((CF_V3){ 0, 1, 0 });
    REQUIRE(red.colors.r > 60 && red.colors.g == 0);
    return true;
}

/* A masked box occludes the floor with the pixels it keeps alone: cut out
 * entirely, it leaves the crease open. No light casts shadows, so only the
 * occlusion's depth pass needs its masked program. */
TEST_CASE(ambient_occlusion_follows_masks)
{
    REQUIRE(test_register_alpha("Cut", SHADER_ALPHA_MASK, .2f, .5f));
    REQUIRE(test_register_alpha("Kept", SHADER_ALPHA_MASK, .5f, .4f));
    add_solid_at(0, (Geometry3D){ PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_surface_box("Cut");
    Geometry3D_get(test_world, box)->elevation = 1;
    Environment environment = ambient_environment(OCCLUSION_OFF);
    REQUIRE(render(&environment));
    static CF_Pixel open[PIXELS];
    memcpy(open, pixels, sizeof pixels);
    environment.occlusion = OCCLUSION_MEDIUM;
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(matches(open));
    Shader_get(test_world, box)->material = "Kept";
    REQUIRE(render(&environment));
    CF_Pixel floor = open[pixel_index(crease_floor)];
    REQUIRE(pixel_at(crease_floor).colors.g + 8 < floor.colors.g);
    return true;
}

#if CHIBI_TEST_MODEL_IMPORT
/* The folder models import into, removed with the images they made
 * resident. */
static char import_folder[64];

static void remove_imports()
{
    if (!import_folder[0]) {
        return;
    }
    render_image_catalog_clear();
    char command[128];
    snprintf(command, sizeof command, "rm -rf '%s'", import_folder);
    if (system(command) != 0) {
        abort();
    }
    import_folder[0] = '\0';
}

/* The material file `text` loaded with the shader nodes registered, as
 * Studio and chibi_ctl register them. */
static behavior_asset *load_material(const char *text)
{
    const registry_pack **previous = NULL;
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
    behavior_asset *asset = behavior_asset_load(text);
    registry_set_packs(previous, acount(previous));
    afree(previous);
    return asset;
}

/* Makes the imported file `key` drawable: registers a material, or publishes
 * a mesh under mesh_key. Images load from the mounted content folder. */
static bool make_drawable(const char *key)
{
    bool material = !strncmp(key, "content/materials/", 18),
         mesh = !strncmp(key, "content/meshes/", 15);
    if (!material && !mesh) {
        return true;
    }
    char path[256];
    snprintf(path, sizeof path, "%s/%s", import_folder, key);
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    size_t length = (size_t)ftell(file);
    char *bytes = xmalloc(length + 1);
    assert(
        !fseek(file, 0, SEEK_SET) && fread(bytes, 1, length, file) == length &&
        !fclose(file)
    );
    bytes[length] = '\0';
    bool drawable = false;
    if (material) {
        behavior_asset *asset = load_material(bytes);
        shader_error error = { 0 };
        drawable = asset && render_shader_upsert(asset, &error);
        if (!drawable) {
            fprintf(
                stderr,
                "Cannot prepare imported material %s: %s\n",
                key,
                asset ? error.message : "cannot load material graph"
            );
        }
        if (asset) {
            behavior_asset_free(asset);
        }
    } else {
        render_mesh_asset loaded = { 0 };
        size_t budget = SIZE_MAX;
        char error[128];
        drawable = render_mesh_asset_prepared_read_budget(bytes, length, &budget, &loaded, error, sizeof error);
        assert(drawable);
        render_mesh_asset_publish(mesh_key, &loaded);
    }
    free(bytes);
    return drawable;
}

/* Imports model `f`, with the JSON members `members`, as `name`, and makes
 * what it wrote drawable. */
static bool import_model(const gltf_fixture *f, const char *name, const char *members)
{
    if (!import_folder[0]) {
        strcpy(import_folder, "/tmp/chibi-scene3d-XXXXXX");
        assert(mkdtemp(import_folder));
    }
    char source[128], content[128], error[256] = "";
    snprintf(source, sizeof source, "%s/%s.gltf", import_folder, name);
    snprintf(content, sizeof content, "%s/content", import_folder);
    assert(!mkdir(content, 0700) || errno == EEXIST);
    model_import_report report = { 0 };
    bool imported = gltf_fixture_write(f, source, members) &&
                    model_import(content, source, name, &report, error, sizeof error) &&
                    render_image_mount_content(content, error, sizeof error);
    if (!imported) {
        fprintf(stderr, "Cannot import rendering fixture %s: %s\n", name, error);
    }
    for (int i = 0; imported && i < acount(report.written); i++) {
        imported = make_drawable(report.written[i]);
    }
    model_import_report_free(&report);
    return imported;
}

/* The image's up along +z, u along +x. */
static const float upright_uvs[] = { 0, 1, 0, 0, 1, 0, 1, 1 };

/* The 8-bit sRGB `value` as linear light. */
static float linear(int value)
{
    float c = value / 255.0f;
    return c <= .04045f ? c / 12.92f : powf((c + .055f) / 1.055f, 2.4f);
}

/* The Look material's factors as a format: base color, metallic, roughness
 * and emission, and the shut image's occlusion strength, since
 * 1 + strength * (red - 1) matches the open image's occlusion. */
static const char LOOK_FACTORS
    [] = "\"materials\":[{\"name\":\"Look\",\"pbrMetallicRoughness\":"
         "{\"baseColorFactor\":[%.9g,%.9g,%.9g,1],"
         "\"metallicFactor\":%.9g,\"roughnessFactor\":%.9g},"
         "\"emissiveFactor\":[%.9g,%.9g,0],"
         "\"occlusionTexture\":{\"index\":4,\"strength\":%.9g}}]";

/* Standard materials from glTF shade alike whether a factor or an image
 * gives each value: base color and emission images hold sRGB, roughness
 * and metallic come from the green and blue of one image, and occlusion
 * from the red of another scaled by its strength. The view tells a sRGB
 * image read as linear, and swapped roughness and metallic, apart. */
TEST_CASE(imported_standard_materials_shade_as_their_factors)
{
    gltf_fixture f = { 0 };
    gltf_fixture_square(&f, upright_uvs);
    gltf_fixture_png(&f, "base.png", 2, 2, (const uint8_t[]){ 200, 120, 60, 255 });
    gltf_fixture_png(&f, "packed.png", 2, 2, (const uint8_t[]){ 0, 77, 204, 255 });
    gltf_fixture_png(&f, "glow.png", 2, 2, (const uint8_t[]){ 120, 60, 0, 255 });
    gltf_fixture_png(&f, "open.png", 2, 2, (const uint8_t[]){ 128, 0, 255, 255 });
    gltf_fixture_png(&f, "shut.png", 2, 2, (const uint8_t[]){ 0, 255, 0, 255 });
    const char *textures = "\"textures\":[{\"source\":0},{\"source\":1},"
                           "{\"source\":2},{\"source\":3},{\"source\":4}]";
    char members[2048];
    snprintf(
        members,
        sizeof members,
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":["
        "{" GLTF_FIXTURE_SQUARE ",\"material\":0}]}],%s,"
        "\"materials\":[{\"name\":\"Look\",\"pbrMetallicRoughness\":{"
        "\"baseColorTexture\":{\"index\":0},"
        "\"metallicRoughnessTexture\":{\"index\":1}},"
        "\"emissiveFactor\":[1,1,1],\"emissiveTexture\":{\"index\":2},"
        "\"occlusionTexture\":{\"index\":3}}]",
        textures
    );
    REQUIRE(import_model(&f, "Images", members));
    ecs_entity square = add_surface_box("Images_Look");
    import_mesh(square);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 })->intensity = 1;
    Environment environment = unlit_environment();
    environment.ambient_sky = 0xffffffffu;
    environment.ambient_ground = 0xffffffffu;
    environment.ambient_intensity = .5f;
    REQUIRE(render(&environment));
    static CF_Pixel images[PIXELS];
    memcpy(images, pixels, sizeof pixels);

    const char *meshes = "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":"
                         "[{" GLTF_FIXTURE_SQUARE ",\"material\":0}]}]";
    char format[1024];
    snprintf(format, sizeof format, "%s,%s,%s", meshes, textures, LOOK_FACTORS);
    snprintf(
        members,
        sizeof members,
        format,
        linear(200),
        linear(120),
        linear(60),
        204 / 255.0,
        77 / 255.0,
        linear(120),
        linear(60),
        127 / 255.0
    );
    REQUIRE(import_model(&f, "Factors", members));
    REQUIRE(render(&environment));
    REQUIRE(matches(images));

    snprintf(
        members,
        sizeof members,
        format,
        linear(200),
        linear(120),
        linear(60),
        77 / 255.0,
        204 / 255.0,
        linear(120),
        linear(60),
        127 / 255.0
    );
    REQUIRE(import_model(&f, "Swapped", members));
    REQUIRE(render(&environment));
    REQUIRE(distance(pixel_at((CF_V3){ 0 }), images[pixel_index((CF_V3){ 0 })]) > 10);
    snprintf(
        members,
        sizeof members,
        format,
        200 / 255.0,
        120 / 255.0,
        60 / 255.0,
        204 / 255.0,
        77 / 255.0,
        linear(120),
        linear(60),
        127 / 255.0
    );
    REQUIRE(import_model(&f, "Raw", members));
    REQUIRE(render(&environment));
    REQUIRE(distance(pixel_at((CF_V3){ 0 }), images[pixel_index((CF_V3){ 0 })]) > 10);
    gltf_fixture_free(&f);
    return true;
}

/* How bright the square's center shows after importing it as `name`, with
 * the nodes `nodes`, the attributes `attributes`, and with the fixture's
 * image as its normal map unless `flat`. It is a dielectric: with the sun
 * behind the eye, a rough metal's light would not depend on its normal. */
static int imported_light(
    const gltf_fixture *f,
    const char *name,
    const char *nodes,
    const char *attributes,
    bool flat
)
{
    char members[1024];
    snprintf(
        members,
        sizeof members,
        "\"nodes\":[%s],\"meshes\":[{\"primitives\":[{\"attributes\":{%s},"
        "\"indices\":3,\"material\":0}]}],"
        "\"textures\":[{\"source\":0}],\"materials\":[{\"name\":\"Map\","
        "\"pbrMetallicRoughness\":{\"metallicFactor\":0}%s}]",
        nodes,
        attributes,
        flat ? "" : ",\"normalTexture\":{\"index\":0}"
    );
    if (!import_model(f, name, members)) {
        return -1;
    }
    Environment environment = unlit_environment();
    return render(&environment) ? pixel_at((CF_V3){ 0 }).colors.g : -1;
}

/* A glTF normal image's green tilts toward the top of the image. With the
 * image's up along +z, toward a sun that shines down toward -z, an image
 * tilted toward green lights the square more than a flat one does, whether
 * the import makes its tangents or the model authors them, its u runs
 * either way, or a node mirrors it. With the image's up along -z, or
 * authored bitangents along -z, it shades the square. */
TEST_CASE(imported_normal_maps_tilt_toward_the_top_of_their_image)
{
    gltf_fixture f = { 0 };
    gltf_fixture_square(&f, upright_uvs);
    gltf_fixture_png(&f, "up.png", 2, 2, (const uint8_t[]){ 128, 217, 217, 255 });
    int up = gltf_fixture_floats(
        &f,
        (const float[16]){ 1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1 },
        4,
        4
    );
    int down = gltf_fixture_floats(
        &f,
        (const float[16]){ 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1 },
        4,
        4
    );
    int turned = gltf_fixture_floats(&f, (const float[]){ 1, 1, 1, 0, 0, 0, 0, 1 }, 4, 2);
    int flipped = gltf_fixture_floats(&f, (const float[]){ 0, 0, 0, 1, 1, 1, 1, 0 }, 4, 2);
    const char *plain = "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2",
               *single = "{\"mesh\":0}";
    char attributes[4][96];
    snprintf(attributes[0], sizeof attributes[0], "%s,\"TANGENT\":%d", plain, up);
    snprintf(attributes[1], sizeof attributes[1], "%s,\"TANGENT\":%d", plain, down);
    snprintf(attributes[2], sizeof attributes[2], "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":%d", turned);
    snprintf(attributes[3], sizeof attributes[3], "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":%d", flipped);
    REQUIRE(
        import_model(&f, "Square", "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{" GLTF_FIXTURE_SQUARE "}]}]")
    );
    import_mesh(add_surface_box("Square_Default"));
    add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 })->intensity = 1;
    int level = imported_light(&f, "Level", single, plain, true);
    int made = imported_light(&f, "Made", single, plain, false);
    REQUIRE(level > 0 && made > level + 10);
    REQUIRE(abs(imported_light(&f, "Authored", single, attributes[0], false) - made) <= 3);
    REQUIRE(abs(imported_light(&f, "Turned", single, attributes[2], false) - made) <= 3);
    REQUIRE(
        abs(imported_light(&f, "Mirrored", "{\"mesh\":0,\"scale\":[-1,1,1]}", plain, false) -
            made) <= 3
    );
    int away = imported_light(&f, "Flipped", single, attributes[3], false);
    REQUIRE(away >= 0 && away + 10 < level);
    REQUIRE(abs(imported_light(&f, "Inverted", single, attributes[1], false) - away) <= 3);
    gltf_fixture_free(&f);
    return true;
}

/* A varying atlas makes each slot's UV lookup observable. Normal texels
 * remain in the upper hemisphere and tilt toward both tangent axes. */
static bool transform_atlas(gltf_fixture *f, bool normal)
{
    CF_Pixel atlas[16];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            atlas[y * 4 + x] = normal
                                 ? (CF_Pixel){ .colors = { 140 + x * 20, 145 + y * 20, 235, 255 } }
                                 : (CF_Pixel){ .colors = { 30 + x * 65,
                                                           35 + y * 60,
                                                           230 - x * 45,
                                                           255 } };
        }
    }
    CF_Image image = { 4, 4, atlas };
    void *png = NULL;
    int length = 0;
    if (cf_is_error(cf_image_save_png_to_memory(&image, &png, &length))) {
        return false;
    }
    gltf_fixture_image(f, "atlas.png", png, (size_t)length);
    cf_free(png);
    return true;
}

/* One slot transformed in its material, baked into its UV accessor, or
 * left unchanged. Authored tangents deliberately stay fixed in all forms. */
static bool transform_model(
    const gltf_fixture *f,
    const char *name,
    int slot,
    int uv,
    int tangent,
    const char *transform
)
{
    char reference[512], material[1024], attributes[128], members[2048];
    snprintf(reference, sizeof reference, "{\"index\":0%s}", transform);
    static const char *const slots[] = { "baseColorTexture",
                                         "metallicRoughnessTexture",
                                         "occlusionTexture",
                                         "emissiveTexture",
                                         "normalTexture" };
    if (slot < 2) {
        snprintf(
            material,
            sizeof material,
            "\"pbrMetallicRoughness\":{\"%s\":%s,\"metallicFactor\":%g}",
            slots[slot],
            reference,
            slot == 0 ? 0.0 : 1.0
        );
    } else {
        snprintf(
            material,
            sizeof material,
            "\"pbrMetallicRoughness\":{\"metallicFactor\":0},"
            "\"emissiveFactor\":[%g,%g,%g],\"%s\":%s",
            slot == 3 ? 1.0 : 0.0,
            slot == 3 ? 1.0 : 0.0,
            slot == 3 ? 1.0 : 0.0,
            slots[slot],
            reference
        );
    }
    snprintf(attributes, sizeof attributes, "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":%d", uv);
    if (tangent >= 0) {
        size_t used = strlen(attributes);
        snprintf(attributes + used, sizeof attributes - used, ",\"TANGENT\":%d", tangent);
    }
    snprintf(
        members,
        sizeof members,
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{"
        "\"attributes\":{%s},\"indices\":3,\"material\":0}]}],"
        "\"textures\":[{\"source\":0}],\"materials\":[{\"name\":\"Map\",%s}]",
        attributes,
        material
    );
    return import_model(f, name, members);
}

/* Seven independent slot/frame contracts, three draws each: transformed,
 * baked control, and unchanged negative control. No full renderer suite. */
TEST_CASE(imported_texture_transforms_match_baked_uvs)
{
    Environment environment = unlit_environment();
    environment.ambient_sky = 0xffffffffu;
    environment.ambient_ground = 0xffffffffu;
    environment.ambient_intensity = .5f;
    add_sun(0xffffffffu, (CF_V3){ -.5f, -1, -1 })->intensity = 1;
    bool entity = false;
    for (int variant = 0; variant < 7; variant++) {
        int slot = variant < 4 ? variant : 4;
        bool authored = variant == 6, mirrored = variant == 5;
        /* Arbitrary rotation with unequal scales also checks the generated
         * normal tangent's direction, beyond axis-aligned quarter turns. */
        float rotation = variant <= 4 ? .4f : mirrored ? 0 : 1.57079632679f;
        float sx = mirrored ? -.6f : .6f, sy = .35f;
        float ox = variant <= 4 ? .2f : .8f, oy = .1f, baked[8];
        float c = cosf(rotation), s = sinf(rotation);
        for (int i = 0; i < 4; i++) {
            float u = upright_uvs[i * 2], v = upright_uvs[i * 2 + 1];
            baked[i * 2] = ox + c * sx * u - s * sy * v;
            baked[i * 2 + 1] = oy + s * sx * u + c * sy * v;
        }
        gltf_fixture f = { 0 };
        gltf_fixture_square(&f, upright_uvs);
        int uv = gltf_fixture_floats(&f, baked, 4, 2), tangent = -1;
        if (authored) {
            tangent = gltf_fixture_floats(
                &f,
                (const float[]){ 1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1 },
                4,
                4
            );
        }
        REQUIRE(transform_atlas(&f, slot == 4));
        char transform[256], name[64];
        snprintf(
            transform,
            sizeof transform,
            ",\"extensions\":{\"KHR_texture_transform\":{"
            "\"offset\":[%.9g,%.9g],\"scale\":[%.9g,%.9g],\"rotation\":%.9g}}",
            ox,
            oy,
            sx,
            sy,
            rotation
        );
        snprintf(name, sizeof name, "Transform%d", variant);
        REQUIRE(transform_model(&f, name, slot, 2, tangent, transform));
        if (!entity) {
            import_mesh(add_surface_box("Transform0_Map"));
            entity = true;
        }
        REQUIRE(render(&environment));
        CF_Pixel transformed[PIXELS];
        memcpy(transformed, pixels, sizeof pixels);
        snprintf(name, sizeof name, "Baked%d", variant);
        REQUIRE(transform_model(&f, name, slot, uv, tangent, ""));
        REQUIRE(render(&environment));
        int maximum = 0;
        for (int i = 0; i < PIXELS; i++) {
            int difference = distance(pixels[i], transformed[i]);
            maximum = difference > maximum ? difference : maximum;
        }
        REQUIRE(matches(transformed));
        snprintf(name, sizeof name, "Plain%d", variant);
        REQUIRE(transform_model(&f, name, slot, 2, tangent, ""));
        REQUIRE(render(&environment));
        int changed = 0;
        for (int i = 0; i < PIXELS; i++) {
            changed += distance(pixels[i], transformed[i]) > 5;
        }
        REQUIRE(changed > 20);
        printf("texture-transform variant=%d max_rgb_error=%d negative_control_pixels=%d frames=3\n", variant, maximum, changed);
        gltf_fixture_free(&f);
    }
    return true;
}

#endif
#endif

/* Opens the hidden app the cases render in, on GLES or the default
 * backend. */
static void open_app(bool gles)
{
    /* GLES needs a display, such as Xvfb's; SDL's offscreen driver has none. */
    if (!gles) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_AppOptionFlags options = CF_APP_OPTIONS_NO_AUDIO_BIT |
                                CF_APP_OPTIONS_HIDDEN_BIT |
                                (gles ? CF_APP_OPTIONS_GFX_OPENGL_BIT : 0);
    CF_Result made = cf_make_app("scene3d", 0, 0, 0, SIDE, SIDE, options, NULL);
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
    pu_setup(setup, teardown);
}

static void close_app()
{
    pu_clear_setup();
    cf_destroy_app();
}

void test_render_scene3d(bool gles)
{
    open_app(gles);
    RUN_TEST_CASE(cameras_look_along_their_transform3d);
    RUN_TEST_CASE(camera_field_of_view_and_clips_shape_the_projection);
    RUN_TEST_CASE(distant_small_scale_cameras_keep_their_view);
    RUN_TEST_CASE(default_clips_stay_ordered_for_close_framings);
    RUN_TEST_CASE(cameras_reject_lenses_the_3d_path_cannot_use);
    RUN_TEST_CASE(cameras_on_bodies_look_from_their_drawn_pose);
    RUN_TEST_CASE(widest_clip_range_keeps_ground_rays);
    RUN_TEST_CASE(navigating_drops_an_authored_lens);
    RUN_TEST_CASE(fog_conceals_rays_that_miss_the_ground);
    RUN_TEST_CASE(distant_eyes_keep_precise_ground_rays);
    RUN_TEST_CASE(distant_turned_eyes_start_rays_where_they_stand);
    RUN_TEST_CASE(distant_eyes_fog_the_ground_they_look_at);
    RUN_TEST_CASE(picking_and_bounds_place_children_under_their_parent);
#if CHIBI_TEST_MATERIAL_GRAPHS
    RUN_TEST_CASE(picking_and_bounds_place_child_models_under_their_parent);
#endif
    RUN_TEST_CASE(environment_ambient_lights_unlit_faces);
    RUN_TEST_CASE(directional_light_shades_faces_it_faces);
    RUN_TEST_CASE(scene_rejects_a_second_directional_light);
    RUN_TEST_CASE(hidden_directional_lights_neither_shade_nor_count);
    RUN_TEST_CASE(neutral_tone_mapping_keeps_saturated_colors);
    RUN_TEST_CASE(auto_exposure_meters_the_frame_to_middle_gray);
    RUN_TEST_CASE(auto_exposure_meters_the_geometric_mean);
    RUN_TEST_CASE(auto_exposure_ignores_an_empty_background);
    RUN_TEST_CASE(auto_exposure_meters_around_non_finite_texels);
    RUN_TEST_CASE(auto_exposure_adapts_over_scene_time);
    RUN_TEST_CASE(slow_auto_exposure_eases_past_half_float_steps);
    RUN_TEST_CASE(directional_light_casts_shadows);
    RUN_TEST_CASE(solids_outside_the_view_cast_shadows_into_it);
    RUN_TEST_CASE(shadow_pass_leaves_earlier_draws_queued);
    RUN_TEST_CASE(deep_views_split_sun_shadows_into_cascades);
    RUN_TEST_CASE(empty_depth_ranges_take_no_cascade);
    RUN_TEST_CASE(shadow_cascades_cover_only_what_the_view_shows);
    RUN_TEST_CASE(sun_size_softens_shadows_away_from_casters);
    RUN_TEST_CASE(point_lights_cast_shadows_across_faces);
    RUN_TEST_CASE(point_lights_cast_from_mixed_primitives);
    RUN_TEST_CASE(point_lights_leave_curved_surfaces_lit);
    RUN_TEST_CASE(point_lights_cast_shadows_from_their_own_maps);
    RUN_TEST_CASE(point_light_shadows_stop_at_four_lights);
    RUN_TEST_CASE(long_solids_cast_for_point_lights_near_one_end);
    RUN_TEST_CASE(solids_outside_the_view_cast_for_point_lights);
    RUN_TEST_CASE(spot_lights_light_their_cone);
    RUN_TEST_CASE(spot_lights_cast_shadows);
    RUN_TEST_CASE(spot_and_point_lights_share_shadow_maps);
    RUN_TEST_CASE(spot_light_angles_must_be_in_range);
    RUN_TEST_CASE(wide_spot_lights_read_only_their_face);
    RUN_TEST_CASE(sky_fills_the_view_in_its_directions);
    RUN_TEST_CASE(sky_shows_only_where_no_surface_is);
    RUN_TEST_CASE(translucent_surfaces_blend_over_the_sky);
    RUN_TEST_CASE(sky_rotation_turns_it_counterclockwise_from_above);
    RUN_TEST_CASE(sky_exposure_scales_it_in_stops);
    RUN_TEST_CASE(sky_settings_must_be_valid);
    RUN_TEST_CASE(sky_lights_surfaces_from_its_directions);
    RUN_TEST_CASE(sky_lights_surfaces_by_its_gradients);
    RUN_TEST_CASE(imported_meshes_draw_like_the_shapes_they_copy);
    RUN_TEST_CASE(picking_and_bounds_follow_imported_triangles);
    RUN_TEST_CASE(picks_follow_tiny_triangles_scaled_up);
    RUN_TEST_CASE(meshes_near_the_float_limit_draw_bound_and_pick_in_place);
    RUN_TEST_CASE(meshes_wider_than_the_float_limit_draw_bound_and_pick_in_place);
    RUN_TEST_CASE(mesh_picks_test_only_the_triangles_near_their_ray);
    RUN_TEST_CASE(mesh_picks_cross_boxes_past_the_float_limit);
    RUN_TEST_CASE(republished_meshes_replace_their_gpu_copies);
    RUN_TEST_CASE(mesh_copies_are_found_in_any_upload_order);
    RUN_TEST_CASE(translucent_meshes_sort_by_their_bounds);
    RUN_TEST_CASE(translucent_copies_of_a_mesh_sort_by_their_bounds);
    RUN_TEST_CASE(transform3d_places_solids_in_their_pose);
    RUN_TEST_CASE(transform3d_children_pick_and_bound_under_their_parent);
#if CHIBI_TEST_MATERIAL_GRAPHS
    RUN_TEST_CASE(transform3d_models_pick_and_bound_at_their_pose);
#endif
    RUN_TEST_CASE(mirroring_transform3d_keeps_faces_outward);
    RUN_TEST_CASE(lopsided_mesh_models_keep_their_normals);
    RUN_TEST_CASE(flattened_transform3d_frames_hide_at_any_scale);
    RUN_TEST_CASE(overflowing_transform3d_frames_are_authoring_errors);
    RUN_TEST_CASE(fog_culls_transform3d_solids_where_they_draw);
    RUN_TEST_CASE(ambient_occlusion_darkens_where_surfaces_meet);
    RUN_TEST_CASE(ambient_occlusion_leaves_direct_light_alone);
    RUN_TEST_CASE(ambient_occlusion_passes_translucent_solids);
    RUN_TEST_CASE(ambient_occlusion_settings_must_be_valid);
#if CHIBI_TEST_MATERIAL_GRAPHS
    RUN_TEST_CASE(mirrors_reflect_the_sky_in_its_directions);
    RUN_TEST_CASE(rough_surfaces_blur_the_sky_they_reflect);
    RUN_TEST_CASE(sky_materials_draw_and_light_like_sky_images);
    RUN_TEST_CASE(sky_materials_bake_again_when_they_change);
    RUN_TEST_CASE(sky_materials_follow_view_directions);
    RUN_TEST_CASE(sky_material_settings_must_be_valid);
    RUN_TEST_CASE(terrain_volume_front_rock_draws_with_its_front_material);
    RUN_TEST_CASE(color_ramps_color_surfaces_from_any_table_offset);
    RUN_TEST_CASE(imported_mesh_groups_take_their_own_materials);
    RUN_TEST_CASE(imported_meshes_carry_texture_and_part_coordinates);
    RUN_TEST_CASE(normal_maps_follow_mesh_tangents);
    RUN_TEST_CASE(normal_map_green_tilts_up_the_image_in_opengl);
    RUN_TEST_CASE(mirroring_transform3d_turns_mesh_bitangents);
    RUN_TEST_CASE(stretched_transform3d_keeps_mesh_tangents);
    RUN_TEST_CASE(occlusion_dims_only_ambient_light);
    RUN_TEST_CASE(alpha_modes_decide_how_opacity_draws);
    RUN_TEST_CASE(masks_drop_pixels_whose_opacity_is_not_a_number);
    RUN_TEST_CASE(opaque_and_masked_surfaces_cast_shadows);
    RUN_TEST_CASE(masked_surfaces_cast_cut_out_shadows);
    RUN_TEST_CASE(view_dependent_masks_cast_shadows_seen_from_the_light);
    RUN_TEST_CASE(masked_solids_outside_the_view_cut_their_shadows);
    RUN_TEST_CASE(ambient_occlusion_follows_masks);
#if CHIBI_TEST_MODEL_IMPORT
    RUN_TEST_CASE(imported_standard_materials_shade_as_their_factors);
    RUN_TEST_CASE(imported_normal_maps_tilt_toward_the_top_of_their_image);
#endif
#endif
    close_app();
}

/* Global illumination's dedicated selector keeps its cold-shader-cache
 * preparation and execution separate from the other scene cases. */
void test_render_scene3d_gi(bool gles)
{
    open_app(gles);
    RUN_TEST_CASE(global_illumination_bounces_light_between_solids);
    RUN_TEST_CASE(global_illumination_reaches_across_vast_solids);
    RUN_TEST_CASE(global_illumination_carries_emitted_light);
    RUN_TEST_CASE(screen_light_carries_emitted_light_it_sees);
#if CHIBI_TEST_MATERIAL_GRAPHS
    RUN_TEST_CASE(world_screen_light_meets_the_solids_it_traces);
#endif
    RUN_TEST_CASE(global_illumination_keeps_open_ground_lit_as_the_ambient_colors);
    RUN_TEST_CASE(global_illumination_settings_must_be_valid);
    RUN_TEST_CASE(global_illumination_lights_pass_their_bulbs);
    RUN_TEST_CASE(global_illumination_lights_pass_frames_around_them);
    RUN_TEST_CASE(global_illumination_sees_out_of_mesh_boxes);
    RUN_TEST_CASE(global_illumination_falls_back_where_no_probe_holds_light);
    RUN_TEST_CASE(global_illumination_finds_every_solid_through_its_bvh);
    RUN_TEST_CASE(global_illumination_gathers_light_over_frames);
    RUN_TEST_CASE(gpu_timing_names_the_3d_passes);
#if CHIBI_TEST_MATERIAL_GRAPHS
    RUN_TEST_CASE(global_illumination_leaves_surfaces_seven_images);
#endif
    close_app();
}

/* Dedicated bounded GPU assertion, registered separately from scene3d. */
void test_render_texture_transform()
{
#if CHIBI_TEST_MATERIAL_GRAPHS && CHIBI_TEST_MODEL_IMPORT
#ifdef __linux__
    setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
#endif
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_Result made = cf_make_app(
        "texture-transform",
        0,
        0,
        0,
        SIDE,
        SIDE,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        NULL
    );
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(imported_texture_transforms_match_baked_uvs);
    pu_clear_setup();
    cf_destroy_app();
#else
    assert(!"Texture transform checks require material graphs and model import");
#endif
}
