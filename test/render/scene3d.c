#include "render/scene3d.h"

#include "execution/status.h"
#include "render/fog.h"
#include "render/mesh_surface.h"
#include "render/scene3d_post.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "render/viewport.h"
#include "runtime/camera.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/images/sky.h"
#include "runtime/particles.h"
#include "runtime/visibility.h"
#include "runtime/world.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#if CHIBI_TEST_MATERIAL_GRAPHS
#include "material_graphs.h"
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One hidden app renders a unit box through the 3D path and post-processing,
 * then reads back the tone-mapped output. */
enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static ecs_world *world;
static bool backdrop; /* Queue a white 2D fill before each 3D draw. */
static render_scene3d_camera lens; /* What each render sees. */
static render_scene3d *scene3d;
static render_scene3d_post *post;
static game_particles *particles;
static CF_Pixel pixels[PIXELS];
static engine *runtime;

/* Looks down at the origin from 45 degrees. */
static render_scene3d_camera camera()
{
    return (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 0, 4, 4 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 100),
    };
}

static void setup()
{
    world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    scene3d = render_scene3d_new();
    post = render_scene3d_post_new();
    particles = game_particles_new();
    lens = camera();
}

static void teardown()
{
    backdrop = false;
    render_shader_catalog_clear();
    render_mesh_assets_clear();
    engine_free(runtime);
    runtime = NULL;
    render_scene3d_free(scene3d);
    render_scene3d_post_free(post);
    game_particles_free(particles);
    ecs_world_free(world);
}

static ecs_entity add(ecs_component_id component, const void *data)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(ecs_world_record_add(world, entity, component, data, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

/* A solid centered on a world point. */
static ecs_entity add_solid(CF_V3 center, GameGeometry3D shape, unsigned color)
{
    ecs_entity solid = add(
        GAME_COMPONENT_GameTransform.id,
        &(GameTransform){ center.x, center.z }
    );
    shape.elevation = center.y;
    GameMaterial material = { .scene = color };
    assert(
        ecs_world_record_add(world, solid, GAME_COMPONENT_GameGeometry3D.id, &shape, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, solid, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return solid;
}

static void add_solid_at(float x, GameGeometry3D shape, unsigned color)
{
    add_solid((CF_V3){ x, shape.elevation, 0 }, shape, color);
}

static void add_box(unsigned color)
{
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2, 0 }, color);
}

/* A floor whose top face fills the whole view. */
static void add_floor()
{
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 400, 400, .2f, -.1f }, 0xffffffffu);
}

static GameDirectionalLight *add_sun(unsigned color, CF_V3 direction)
{
    GameDirectionalLight sun = { .color = color, .intensity = 3, .direction = direction };
    ecs_entity entity = add(GAME_COMPONENT_GameDirectionalLight.id, &sun);
    return GAME_GameDirectionalLight_get(world, entity);
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

static bool render_at(const GameEnvironment *environment, float time)
{
    GameEnvironment settings = *environment;
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
    bool drawn = render_scene3d_draw(scene3d, world, particles, &lens, 0, runtime, &settings, (render_skip){ 0 });
    cf_render_to(canvas, true);
    return finish(time) && drawn;
}

static bool render(const GameEnvironment *environment)
{
    return render_at(environment, 0);
}

TEST_CASE(scene_draws_without_a_valid_rhythm_clock)
{
    add_box(0xffffffffu);
    GameRhythm rhythm = { 120, .18f };
    add(GAME_COMPONENT_GameRhythm.id, &rhythm);
    add(GAME_COMPONENT_GameRhythm.id, &rhythm);
    GameEnvironment environment = game_environment_defaults();
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(top().colors.r > 0);
    return true;
}

/* Points the renders through the scene's camera, as Play frames it. */
static int look_through_camera()
{
    engine_viewport_view view;
    int found = game_camera_view(world, NULL, &view);
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
    ecs_entity entity = add(GAME_COMPONENT_GameCamera.id, &(GameCamera){ .width = 2, .height = 2 });
    GameTransform3D pose = { .rotation = rotation, .scale = { 1, 1, 1 } };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform3D.id, &pose, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
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
    GameGeometry3D cube = { GAME_PRIMITIVE_BOX, 2, 2, 2, 0 };
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
    GameCamera *authored = GAME_GameCamera_get(world, camera);
    authored->offset = (CF_V2){ 1, 2 };
    REQUIRE(look_through_camera() == 1);
    CF_V3 offset = render_scene3d_project(&lens, (CF_V3){ 6, 0, 2 });
    REQUIRE(near_value(offset.x, 0) && near_value(offset.y, 0));
    authored->offset = (CF_V2){ 0 };
    REQUIRE(look_through_camera() == 1);
    GameEnvironment environment = game_environment_defaults();
    REQUIRE(render(&environment));
    REQUIRE(center().colors.r > 2 * center().colors.b);
    GAME_GameTransform3D_get(world, camera)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, CF_PI / 2);
    REQUIRE(look_through_camera() == 1);
    REQUIRE(render(&environment));
    REQUIRE(center().colors.b > 2 * center().colors.r);
    return true;
}

TEST_CASE(camera_field_of_view_and_clips_shape_the_projection)
{
    ecs_entity framing = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ 3, 2 });
    GameCamera authored = { .width = 4, .height = 2 };
    assert(
        ecs_world_record_add(world, framing, GAME_COMPONENT_GameCamera.id, &authored, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(look_through_camera() == 1);
    CF_V3 target = render_scene3d_project(&lens, (CF_V3){ 3, 0, 2 });
    REQUIRE(near_value(target.x, 0) && near_value(target.y, 0));
    REQUIRE(near_value(lens.projection.elements[5] * tanf(.5f * 35 * CF_PI / 180), 1));
    REQUIRE(near_value(lens.projection.elements[5] / lens.projection.elements[0], 2));
    GAME_GameCamera_get(world, framing)->fov = CF_PI / 2;
    REQUIRE(look_through_camera() == 1);
    target = render_scene3d_project(&lens, (CF_V3){ 3, 0, 2 });
    REQUIRE(near_value(target.x, 0) && near_value(target.y, 0));
    REQUIRE(near_value(lens.projection.elements[5], 1));
    assert(ecs_world_destroy_entity(world, framing) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity posed = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, -CF_PI / 2)
    );
    GameCamera *clipped = GAME_GameCamera_get(world, posed);
    clipped->near_clip = .5f;
    clipped->far_clip = 50;
    REQUIRE(look_through_camera() == 1);
    REQUIRE(near_value(render_scene3d_project(&lens, (CF_V3){ .5f, 0, 0 }).z, 0));
    REQUIRE(near_value(render_scene3d_project(&lens, (CF_V3){ 50, 0, 0 }).z, 1));
    return true;
}

/* A small-scale camera far from the origin still looks along its -Z, though
 * its scaled direction is below the precision of its position, and its
 * scaled axes are too short to cross. */
TEST_CASE(distant_small_scale_cameras_keep_their_view)
{
    ecs_entity camera = add_camera_3d(cf_quat_identity());
    GameTransform3D *pose = GAME_GameTransform3D_get(world, camera);
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
    return true;
}

/* Default clips stay in order however close the framing distance gets. */
TEST_CASE(default_clips_stay_ordered_for_close_framings)
{
    render_scene3d_camera close = render_scene3d_camera_make(0, 0, .0001f, .0001f);
    CF_V3 ahead = { 0, -cosf(10 * CF_PI / 180), -sinf(10 * CF_PI / 180) };
    float depth = render_scene3d_project(&close, ahead).z;
    REQUIRE(depth > 0 && depth < 1);
    ecs_entity camera = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, -CF_PI / 2)
    );
    GAME_GameCamera_get(world, camera)->fov = 3.1415f;
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
    GameCamera *camera = GAME_GameCamera_get(world, entity);
    for (int i = 0; i < (int)(sizeof lenses / sizeof lenses[0]); i++) {
        camera->fov = lenses[i][0];
        camera->near_clip = lenses[i][1];
        camera->far_clip = lenses[i][2];
        REQUIRE(look_through_camera() == -1);
    }
    *camera = (GameCamera){ .width = 2, .height = 2, .fov = 1, .near_clip = 1, .far_clip = 2 };
    REQUIRE(look_through_camera() == 1);
    GAME_GameTransform3D_get(world, entity)->scale = (CF_V3){ 0 };
    REQUIRE(look_through_camera() == -1);
    return true;
}

/* A camera riding a dynamic body looks from where the body draws between
 * physics steps, not from the pose the last step solved. */
TEST_CASE(cameras_on_bodies_look_from_their_drawn_pose)
{
    char error[256];
    game_world_options options = { .modules = GAME_WORLD_PHYSICS3D };
    game_world *simulation = game_world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = game_world_ecs(simulation);
    engine *runtime = game_world_engine(simulation);
    ecs_entity body = ecs_world_create_entity(ecs),
               eye = ecs_world_create_entity(ecs);
    REQUIRE(GAME_GameTransform3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(GAME_GameCollider3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(GAME_GameBody3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(GAME_GameGravity3D_add(ecs, body, 0) == ECS_OK);
    REQUIRE(GAME_GameTransform3D_add(ecs, eye, 0) == ECS_OK);
    REQUIRE(GAME_GameCamera_add(ecs, eye, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add_pair(ecs, eye, ECS_CHILD_OF, body, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    *GAME_GameTransform3D_get(ecs, body) = (GameTransform3D){ .scale = { 1, 1, 1 } };
    *GAME_GameCollider3D_get(ecs, body) = (GameCollider3D){ .shape = GAME_COLLIDER3D_SPHERE,
                                                            .radius = 1,
                                                            .category = 1,
                                                            .mask = 1 };
    *GAME_GameBody3D_get(ecs, body) = (GameBody3D){ .type = GAME_BODY_DYNAMIC,
                                                    .linear_velocity = { 60, 0, 0 } };
    const CF_V3 behind = { 0, 0, 5 };
    *GAME_GameTransform3D_get(ecs, eye) = (GameTransform3D){ .position = behind,
                                                             .scale = { 1, 1, 1 } };
    *GAME_GameCamera_get(ecs, eye) = (GameCamera){ .width = 320, .height = 180 };
    REQUIRE(engine_set_playing(runtime, true) == 0);
    REQUIRE(engine_frame(runtime, 0) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1 + ENGINE_FIXED_SECONDS * .5) == FLOW_OK);
    engine_viewport_view view;
    REQUIRE(game_camera_view(ecs, runtime, &view) == 1);
    CF_V3 drawn = cf_add_v3(engine_render_transform_3d(runtime, ecs, body).position, behind);
    CF_V3 offset = cf_sub_v3(view.lens.eye, drawn);
    REQUIRE(cf_len_v3(offset) < 1e-3f);
    float solved = GAME_GameGlobalTransform3D_get_const(ecs, eye)->position.x;
    REQUIRE(fabsf(view.lens.eye.x - (solved - 60 * (float)ENGINE_FIXED_SECONDS * .5f)) < 1e-3f);
    game_world_free(simulation);
    return true;
}

/* The widest clip range a camera takes still unprojects its ground rays. */
TEST_CASE(widest_clip_range_keeps_ground_rays)
{
    ecs_entity entity = add_camera_3d(
        cf_quat_from_axis_angle((CF_V3){ 1, 0, 0 }, -CF_PI / 6)
    );
    GAME_GameTransform3D_get(world, entity)->position = (CF_V3){ 0, 10, 0 };
    GameCamera *camera = GAME_GameCamera_get(world, entity);
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
    GameFog settings = { .origin = { seen.x - 8, seen.y - 8 },
                         .columns = 16,
                         .rows = 16,
                         .cell_size = 1,
                         .color = 0xff0000ffu };
    add(GAME_COMPONENT_GameFog.id, &settings);
    ecs_entity eye = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ seen.x, seen.y });
    GameVision vision = { .radius = radius };
    assert(ecs_world_record_add(world, eye, GAME_COMPONENT_GameVision.id, &vision, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_visibility *visibility = game_visibility_new(world, NULL);
    const char *error = game_visibility_update(visibility, 0);
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
    game_visibility_free(visibility);
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
    ecs_entity parent = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ 1.5f, 0 });
    ecs_entity child = add_solid(
        (CF_V3){ 0 },
        (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1, 0 },
        0xffffffffu
    );
    REQUIRE(ecs_world_record_add_pair(world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    CF_V3 placed = render_scene3d_project(&lens, (CF_V3){ 1.5f, 0, 0 });
    CF_V3 origin = render_scene3d_project(&lens, (CF_V3){ 0 });
    REQUIRE(
        render_scene3d_pick(world, &lens, (CF_V2){ placed.x, placed.y }, (render_skip){ 0 })
            .slot == child.slot
    );
    REQUIRE(!render_scene3d_pick(world, &lens, (CF_V2){ origin.x, origin.y }, (render_skip){ 0 })
                 .slot);
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(world, child, corners));
    REQUIRE(fabsf(corners[0].x - 1) < 1e-4f && fabsf(corners[7].x - 2) < 1e-4f);
    return true;
}

#if CHIBI_TEST_MATERIAL_GRAPHS
/* Model surfaces take the same exact placement before propagation. */
TEST_CASE(picking_and_bounds_place_child_models_under_their_parent)
{
    REQUIRE(test_register_model_sphere("ChildSphere", .5f));
    ecs_entity parent = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ 1.5f, 0 });
    ecs_entity child = add_solid(
        (CF_V3){ 0 },
        (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1, 0 },
        0xffffffffu
    );
    GameSdfSurface3D surface = { .enabled = true };
    GameShader binding = { .shape = "ChildSphere" };
    REQUIRE(
        ecs_world_record_add(world, child, GAME_COMPONENT_GameSdfSurface3D.id, &surface, 0) == ECS_OK
    );
    REQUIRE(
        ecs_world_record_add(world, child, GAME_COMPONENT_GameShader.id, &binding, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add_pair(world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    CF_V3 placed = render_scene3d_project(&lens, (CF_V3){ 1.5f, 0, 0 });
    CF_V3 origin = render_scene3d_project(&lens, (CF_V3){ 0 });
    REQUIRE(
        render_scene3d_pick(world, &lens, (CF_V2){ placed.x, placed.y }, (render_skip){ 0 })
            .slot == child.slot
    );
    REQUIRE(!render_scene3d_pick(world, &lens, (CF_V2){ origin.x, origin.y }, (render_skip){ 0 })
                 .slot);
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(world, child, corners));
    REQUIRE(fabsf(corners[0].x - 1) < 1e-3f && fabsf(corners[7].x - 2) < 1e-3f);
    return true;
}
#endif

TEST_CASE(environment_ambient_lights_unlit_faces)
{
    add_box(0xffffffffu);
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    REQUIRE(top().colors.r == 0 && front().colors.r == 0);
    environment.ambient_sky = 0x00ff00ffu;
    environment.ambient_ground = 0x00ff00ffu;
    environment.ambient_intensity = .5f;
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
    REQUIRE(render(&environment));
    REQUIRE(top().colors.g > 100 && top().colors.r == 0 && top().colors.b == 0);
    return true;
}

TEST_CASE(directional_light_shades_faces_it_faces)
{
    add_box(0xffffffffu);
    add_sun(0xff0000ffu, (CF_V3){ 0, -2, 0 });
    GameEnvironment environment = game_environment_defaults();
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
    GameEnvironment environment = game_environment_defaults();
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_error(scene3d), "only one Directional Light"));
    return true;
}

TEST_CASE(hidden_directional_lights_neither_shade_nor_count)
{
    add_box(0xffffffffu);
    add_sun(0xff0000ffu, (CF_V3){ 0, -2, 0 });
    GameDirectionalLight unseen = { .color = 0x00ff00ffu,
                                    .intensity = 3,
                                    .direction = { 0, -2, 0 } };
    ecs_entity hidden = add(GAME_COMPONENT_GameDirectionalLight.id, &unseen);
    GameRenderState state = { .hidden = true };
    assert(
        ecs_world_record_add(world, hidden, GAME_COMPONENT_GameRenderState.id, &state, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    runtime = engine_new(world, NULL);
    GameEnvironment environment = game_environment_defaults();
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
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    CF_Pixel agx = top();
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
    REQUIRE(render(&environment));
    CF_Pixel neutral = top();
    REQUIRE(agx.colors.r > 0 && agx.colors.g > 0 && agx.colors.b > 0);
    REQUIRE(neutral.colors.r > 100 && neutral.colors.g == 0 && neutral.colors.b == 0);
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL + 1;
    REQUIRE(!render(&environment));
    REQUIRE(strstr(render_scene3d_post_error(post), "tone mapping"));
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    sun->intensity = .25f;
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    add_solid_at(-100, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 200, 400, .2f, -.1f }, 0xffffffffu);
    add_solid_at(100, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 200, 400, .2f, -.1f }, 0x404040ffu);
    add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
    environment.auto_exposure = true;
    REQUIRE(render(&environment));
    /* Only the sunlit top face is metered, so it lands on middle gray. */
    REQUIRE(abs(top().colors.g - 105) <= 10);
    REQUIRE(pixels[0].colors.g == 0);
    return true;
}

TEST_CASE(auto_exposure_meters_around_non_finite_texels)
{
    GameEnvironment environment = game_environment_defaults();
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    GameEnvironment environment = game_environment_defaults();
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
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, 0 });
    sun->intensity = 2048;
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
}

static GameDirectionalLight *add_shadow_scene()
{
    add_box_on_floor();
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    return sun;
}

static GameEnvironment unlit_environment()
{
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    GameDirectionalLight *sun = add_shadow_scene();
    GameEnvironment environment = unlit_environment();
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
    GameDirectionalLight *sun = add_shadow_scene();
    sun->shadows = true;
    /* A pillar well beyond the left edge of the view, and nearer the sun than
     * the floor, shades the floor up to x = 1. */
    add_solid_at(-8, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 8, 4 }, 0xffffffffu);
    /* A crowd far away splits collection across workers where the host has
     * them, so the pillar arrives through a worker's buffer. */
    for (int i = 0; i < 1100; i++) {
        add_solid_at(60, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1, 1 }, 0xffffffffu);
    }
    GameEnvironment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(sunlit[0]).colors.g == 0);
    REQUIRE(pixel_at(sunlit[3]).colors.g > 60);
    return true;
}

TEST_CASE(shadow_pass_leaves_earlier_draws_queued)
{
    add_box(0xffffffffu);
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->shadows = true;
    GameEnvironment environment = unlit_environment();
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
static GameDirectionalLight *add_walls(unsigned walls)
{
    lens = (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 0 }, (CF_V3){ 0, 0, -1 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 1000),
    };
    for (int i = 0; i < WALLS; i++) {
        if (!(walls >> i & 1)) {
            continue;
        }
        float depth = wall_depths[i];
        CF_V3 wall = on_wall(i, 0);
        wall.z = -depth;
        add_solid(
            wall,
            (GameGeometry3D){ GAME_PRIMITIVE_BOX, .2f * depth, .02f * depth, .5f * depth },
            0xffffffffu
        );
        float gap = i % 2 ? .25f : .4f;
        CF_V3 cube = { wall.x, gap * depth, -(1 - gap) * depth };
        add_solid(
            cube,
            (GameGeometry3D){ GAME_PRIMITIVE_BOX, .1f * depth, .1f * depth, .1f * depth },
            0xffffffffu
        );
    }
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 });
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
    GameDirectionalLight *sun = add_walls(0xf);
    GameEnvironment environment = unlit_environment();
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
    GameDirectionalLight *sun = add_walls(0x9);
    sun->shadows = true;
    GameEnvironment environment = unlit_environment();
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
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2, 1 }, 0xffffffffu);
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    GameEnvironment environment = unlit_environment();
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
static GameDirectionalLight *add_wall_scene()
{
    lens = (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 2, 10, 3 }, (CF_V3){ 2, 0, 3 }, (CF_V3){ 0, 0, -1 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 100),
    };
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 40, 40, .2f, -.1f }, 0xffffffffu);
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, .2f, 6, 4, 2 }, 0xffffffffu);
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
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
    GameDirectionalLight *sun = add_wall_scene();
    GameEnvironment environment = unlit_environment();
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
    add_solid((CF_V3){ 2, 2, -1.5f }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, .2f, .4f, 4 }, 0xffffffffu);
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
    ecs_entity lamp = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ at.x, at.z });
    GamePointLight light = { .color = 0xffffffffu,
                             .intensity = intensity,
                             .radius = radius,
                             .elevation = at.y };
    assert(
        ecs_world_record_add(world, lamp, GAME_COMPONENT_GamePointLight.id, &light, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return lamp;
}

static void place_lamp(ecs_entity lamp, CF_V3 at)
{
    GameTransform *transform = GAME_GameTransform_get(world, lamp);
    transform->x = at.x;
    transform->y = at.z;
    GAME_GamePointLight_get(world, lamp)->elevation = at.y;
    GAME_GameGeometry3D_get(world, lamp)->elevation = at.y;
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
    CF_M4x4 inverse = cf_m4_invert(lens.view);
    CF_V3 eye = { inverse.elements[12], inverse.elements[13], inverse.elements[14] };
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
    if (GAME_GamePointLight_has(world, lamp)) {
        return &GAME_GamePointLight_get(world, lamp)->intensity;
    }
    return &GAME_GameSpotLight_get(world, lamp)->intensity;
}

static bool *lamp_shadows(ecs_entity lamp)
{
    if (GAME_GamePointLight_has(world, lamp)) {
        return &GAME_GamePointLight_get(world, lamp)->shadows;
    }
    return &GAME_GameSpotLight_get(world, lamp)->shadows;
}

/* Renders each set of lamps without shadows into frames[mask], with the
 * lamps whose bits the mask sets turned off, then every lamp with shadows.
 * With every lamp off the frame is black, so it is left out. */
static bool render_lamps(const ecs_entity *lamps, int count, CF_Pixel (*frames)[PIXELS])
{
    GameEnvironment environment = unlit_environment();
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
    GameGeometry3D bulb = { GAME_PRIMITIVE_BOX, .6f, .6f, .6f };
    assert(ecs_world_record_add(world, lamp, GAME_COMPONENT_GameGeometry3D.id, &bulb, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    static CF_Pixel open[1][PIXELS];
    CF_Pixel *shown[] = { open[0], NULL };
    for (int spot = 0; spot < LAMP_SPOTS; spot++) {
        place_lamp(lamp, lamp_spots[spot]);
        GAME_GamePointLight_get(world, lamp)->shadows = false;
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
    add_solid((CF_V3){ 0, 4, -.5f }, (GameGeometry3D){ GAME_PRIMITIVE_SPHERE, 1, 1, 1 }, 0xffffffffu);
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
    add_solid((CF_V3){ 0, 1, 0 }, (GameGeometry3D){ GAME_PRIMITIVE_SPHERE, 2, 2, 2 }, 0xffffffffu);
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
    GameEnvironment environment = unlit_environment();
    static CF_Pixel frames[2][PIXELS];
    for (int frame = 1; frame >= 0; frame--) {
        for (int i = 1; i < 5; i++) {
            GAME_GamePointLight_get(world, lamps[i])->intensity = frame ? 0 : .5f;
        }
        REQUIRE(render(&environment));
        memcpy(frames[frame], pixels, sizeof pixels);
    }
    for (int i = 0; i < 5; i++) {
        GAME_GamePointLight_get(world, lamps[i])->shadows = true;
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
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    add_solid((CF_V3){ 0, .5f, -14 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, .2f, 30, 1 }, 0xffffffffu);
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
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    add_solid((CF_V3){ 0, 6, 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, .5f, .5f, .2f }, 0xffffffffu);
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
    ecs_entity spot = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ at.x, at.z });
    GameSpotLight light = { .color = 0xffffffffu,
                            .intensity = 2,
                            .radius = 20,
                            .elevation = at.y,
                            .direction = cf_sub_v3(target, at),
                            .inner_angle = inner,
                            .outer_angle = outer };
    assert(ecs_world_record_add(world, spot, GAME_COMPONENT_GameSpotLight.id, &light, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return spot;
}

/* A spot light pointing down lights the floor inside its inner angle like a
 * point light in its place, fades across its edge, and leaves the floor past
 * its outer angle dark. */
TEST_CASE(spot_lights_light_their_cone)
{
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    CF_V3 at = { 0, 2, 0 };
    ecs_entity lamp = add_lamp(at, 2, 20);
    GameEnvironment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel point[PIXELS];
    memcpy(point, pixels, sizeof pixels);
    GAME_GamePointLight_get(world, lamp)->intensity = 0;
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
    GAME_GameSpotLight_get(world, spot)->intensity = 0;
    GAME_GamePointLight_get(world, lamp)->intensity = 2 * share * share;
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
        assert(ecs_world_destroy_entity(world, lamp) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
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
    GameEnvironment environment = unlit_environment();
    REQUIRE(render(&environment));
    CF_Pixel point = pixel_at(below);
    REQUIRE(point.colors.g > 10);
    GAME_GamePointLight_get(world, lamp)->intensity = 0;
    GameSpotLight *spot = GAME_GameSpotLight_get(world, add_spot(at, below, 2, 2));
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
    add_solid((CF_V3){ 7.5f, 1, 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 6, 6 }, 0xffffffffu);
    ecs_entity lamp = add_lamp((CF_V3){ 6, 1, 0 }, 2, 20);
    GAME_GamePointLight_get(world, lamp)->shadows = true;
    CF_V3 at = { 0, .6f, -2.5f };
    GameSpotLight *spot = GAME_GameSpotLight_get(
        world,
        add_spot(at, (CF_V3){ at.x, 0, at.z }, 160, 160)
    );
    spot->radius = 4;
    GameEnvironment environment = unlit_environment();
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
    lens.view = cf_look_at((CF_V3){ 0 }, (CF_V3){ 0, 0, -1 }, (CF_V3){ 0, 1, 0 });
}

static GameEnvironment sky_environment()
{
    GameEnvironment environment = game_environment_defaults();
    environment.tone_mapping = GAME_TONE_MAPPING_NEUTRAL;
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
    GameEnvironment environment = sky_environment();
    for (int quarters = 0; quarters < 2; quarters++) {
        environment.sky_rotation = 90.0f * quarters;
        for (int i = 0; i < 6; i++) {
            lens.view = cf_look_at((CF_V3){ 0 }, views[i][0], views[i][1]);
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
    add_solid(center, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0x000000ffu);
    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    GameEnvironment environment = sky_environment();
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
    add_solid((CF_V3){ 0, 0, -6 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0xff000080u);
    GameEnvironment environment = sky_environment();
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
    GameEnvironment environment = sky_environment();
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
    add_solid((CF_V3){ 0, 0, -6 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    int box = SIDE / 2 * SIDE + SIDE / 2;
    publish_uniform_sky((const unsigned char[4]){ 128, 64, 32, 127 });
    GameEnvironment environment = sky_environment();
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
    GameEnvironment environment = sky_environment();
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
    lens.view = cf_look_at((CF_V3){ 3, 2.5f, 4 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 });
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
    GameEnvironment environment = sky_environment();
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
    GameEnvironment environment = sky_environment();
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
    assert(render_mesh_surface_vertices(GAME_PRIMITIVE_BOX, &box, error, sizeof error));
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
    GameMeshAsset imported = { .source = mesh_key };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMeshAsset.id, &imported, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
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
    *GAME_GameTransform_get(world, solid) = (GameTransform){ center.x, center.z };
    GAME_GameGeometry3D_get(world, solid)->elevation = center.y;
}

/* The shadow scene's box and a pillar beyond the view's left edge draw first
 * as shapes, then as the unit box mesh. Every pixel matches: imported meshes
 * light, shade and cast from outside the view as shapes do. */
TEST_CASE(imported_meshes_draw_like_the_shapes_they_copy)
{
    render_mesh_asset box = box_mesh();
    publish_mesh(&box);
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity solids[] = {
        add_solid((CF_V3){ 0, 1, 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu),
        add_solid((CF_V3){ -8, 4, 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 8 }, 0xffffffffu),
    };
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    GameEnvironment environment = unlit_environment();
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

/* One triangle over half of a two-unit square on the ground, drawn at unit
 * size: picks hit the triangle beyond the unit box and miss the box's corner
 * that the triangle leaves out, and bounds hold just the triangle. */
TEST_CASE(picking_and_bounds_follow_imported_triangles)
{
    static const CF_V3 corners[] = { { -1, 0, -1 }, { -1, 0, 1 }, { 1, 0, -1 } };
    render_mesh_asset mesh = { .min = { -1, 0, -1 }, .max = { 1, 0, 1 } };
    for (int i = 0; i < 3; i++) {
        render_mesh_surface_vertex vertex = { .position = corners[i],
                                              .normal = { 0, 1, 0 } };
        apush(mesh.vertices, vertex);
        apush(mesh.indices, i);
    }
    publish_mesh(&mesh);
    ecs_entity triangle = add_solid(
        (CF_V3){ 0 },
        (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1 },
        0xffffffffu
    );
    import_mesh(triangle);
    CF_V3 inside = render_scene3d_project(&lens, (CF_V3){ -.9f, 0, -.9f });
    CF_V3 outside = render_scene3d_project(&lens, (CF_V3){ .3f, 0, .3f });
    REQUIRE(
        render_scene3d_pick(world, &lens, (CF_V2){ inside.x, inside.y }, (render_skip){ 0 })
            .slot == triangle.slot
    );
    REQUIRE(!render_scene3d_pick(world, &lens, (CF_V2){ outside.x, outside.y }, (render_skip){ 0 })
                 .slot);
    CF_V3 box[8];
    REQUIRE(render_scene3d_entity_bounds(world, triangle, box));
    REQUIRE(near_value(box[0].x, -1) && near_value(box[0].z, -1));
    REQUIRE(near_value(box[7].x, 1) && near_value(box[7].z, 1));
    REQUIRE(fabsf(box[0].y) < 2e-3f && fabsf(box[7].y) < 2e-3f);
    return true;
}

/* A republished source draws its new triangles, and the GPU copies of
 * replaced and removed meshes are freed. */
TEST_CASE(republished_meshes_replace_their_gpu_copies)
{
    render_mesh_asset box = box_mesh();
    publish_mesh(&box);
    ecs_entity solid = add_solid((CF_V3){ 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    import_mesh(solid);
    GameEnvironment environment = game_environment_defaults();
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
    GAME_GameMeshAsset_get(world, solid)->source = "";
    REQUIRE(render(&environment));
    REQUIRE(distance(top(), drawn) <= 3 && render_scene3d_last_work(scene3d).meshes == 0);
    return true;
}

static const CF_V3 eyeward = { 0, 2, 2 }; /* Up and toward the eye. */
static const GameGeometry3D unit_box = { GAME_PRIMITIVE_BOX, 1, 1, 1 };

/* A translucent blue unit box nearer the eye than a translucent red one
 * blends over it, drawn first as a shape and then as a mesh whose vertices
 * sit at the shape's place while its origin lies behind the red box. Every
 * pixel matches: the mesh sorts by its bounds, not its origin. */
TEST_CASE(translucent_meshes_sort_by_their_bounds)
{
    add_solid((CF_V3){ 0, 1, 1 }, unit_box, 0xff000080u);
    ecs_entity blue = add_solid(eyeward, unit_box, 0x0000ff80u);
    GameEnvironment environment = game_environment_defaults();
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
    GameEnvironment environment = game_environment_defaults();
    REQUIRE(render(&environment));
    static CF_Pixel shapes[PIXELS];
    memcpy(shapes, pixels, sizeof pixels);
    publish_moved_box(eyeward);
    place_solid(red, (CF_V3){ 0, -1, 3 });
    GameVisualTransform turned = { .yaw = CF_PI, .scale = 1 };
    assert(
        ecs_world_record_add(world, red, GAME_COMPONENT_GameVisualTransform.id, &turned, 0) == ECS_OK
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
    GameTransform3D transform = { position, rotation, scale };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform3D.id, &transform, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

/* The shadow scene's box and a pillar beyond the view's left edge draw first
 * in 2D placement, then at Transform3D poses with their Transforms and
 * elevations moved away. Every pixel matches, with each solid drawn once: a
 * Transform3D places a solid's center and scales its shape, and its solids
 * shade and cast as before. A box turned a quarter about x then shows as one
 * with its height and depth swapped. */
TEST_CASE(transform3d_places_solids_in_their_pose)
{
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_solid(
        (CF_V3){ 0, 1, 0 },
        (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 },
        0xffffffffu
    );
    ecs_entity pillar = add_solid(
        (CF_V3){ -8, 4, 0 },
        (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 8 },
        0xffffffffu
    );
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->intensity = 1;
    sun->shadows = true;
    GameEnvironment environment = unlit_environment();
    REQUIRE(render(&environment));
    static CF_Pixel placed[PIXELS];
    memcpy(placed, pixels, sizeof placed);
    place_solid(box, (CF_V3){ 5, 7, 5 });
    place_3d(box, (CF_V3){ 0, 1, 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    place_solid(pillar, (CF_V3){ 5, -7, 5 });
    *GAME_GameGeometry3D_get(world, pillar) = (GameGeometry3D){ GAME_PRIMITIVE_BOX, 4, 1, 4 };
    place_3d(pillar, (CF_V3){ -8, 4, 0 }, cf_quat_identity(), (CF_V3){ .5f, 2, 2 });
    REQUIRE(render(&environment));
    REQUIRE(!render_scene3d_error(scene3d)[0]);
    REQUIRE(render_scene3d_last_work(scene3d).entities == 3);
    REQUIRE(pixel_at(shaded).colors.g == 0 && pixel_at(sunlit[0]).colors.g == 0);
    REQUIRE(pixel_at(sunlit[4]).colors.g > 60);
    REQUIRE(matches(placed));
    /* A box two wide, one high and 2.4 deep, upright, then turned a quarter
     * about x from 2.4 high and one deep, shows the same. */
    *GAME_GameGeometry3D_get(world, box) = (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2.4f, 1 };
    *GAME_GameTransform3D_get(world, box) = (GameTransform3D){ { 0, .5f, 0 },
                                                               cf_quat_identity(),
                                                               { 1, 1, 1 } };
    REQUIRE(render(&environment));
    REQUIRE(
        pixel_at((CF_V3){ 0, 1, 0 }).colors.g > 30 && pixel_at(shaded).colors.g == 0
    );
    REQUIRE(pixel_at(sunlit[3]).colors.g > 60);
    static CF_Pixel upright[PIXELS];
    memcpy(upright, pixels, sizeof upright);
    *GAME_GameGeometry3D_get(world, box) = (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 1, 2.4f };
    GAME_GameTransform3D_get(world, box)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 1, 0, 0 }, CF_PI / 2);
    REQUIRE(render(&environment));
    REQUIRE(matches(upright));
    /* A Visual Transform turns the box within its Transform3D: a quarter
     * about y after a quarter roll about x takes 2.4 wide, one deep and two
     * high to the upright box. */
    *GAME_GameGeometry3D_get(world, box) = (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2.4f, 1, 2 };
    GAME_GameTransform3D_get(world, box)
        ->rotation = cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, CF_PI / 2);
    GameVisualTransform rolled = { .roll = CF_PI / 2, .scale = 1 };
    assert(
        ecs_world_record_add(world, box, GAME_COMPONENT_GameVisualTransform.id, &rolled, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(render(&environment));
    REQUIRE(matches(upright));
    return true;
}

/* A box under a ChildOf parent with a Transform3D, both without a Transform,
 * takes the parent's turn: three units deep, it lies along x. Picks and
 * bounds follow it before any propagation. */
TEST_CASE(transform3d_children_pick_and_bound_under_their_parent)
{
    ecs_entity parent = ecs_world_create_entity(world);
    place_3d(
        parent,
        (CF_V3){ 1, 0, 0 },
        cf_quat_from_axis_angle((CF_V3){ 0, 1, 0 }, CF_PI / 2),
        (CF_V3){ 1, 1, 1 }
    );
    ecs_entity child = add(
        GAME_COMPONENT_GameGeometry3D.id,
        &(GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 3, 1 }
    );
    place_3d(child, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    REQUIRE(ecs_world_record_add_pair(world, child, ECS_CHILD_OF, parent, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    const CF_V3 on[] = { { -.2f, 0, 0 }, { 2.2f, 0, 0 } },
                off[] = { { -1.2f, 0, 0 }, { 1, 0, -1.5f } };
    for (int i = 0; i < 2; i++) {
        CF_V3 hit = render_scene3d_project(&lens, on[i]),
              miss = render_scene3d_project(&lens, off[i]);
        REQUIRE(
            render_scene3d_pick(world, &lens, (CF_V2){ hit.x, hit.y }, (render_skip){ 0 })
                .slot == child.slot
        );
        REQUIRE(!render_scene3d_pick(world, &lens, (CF_V2){ miss.x, miss.y }, (render_skip){ 0 })
                     .slot);
    }
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(world, child, corners));
    CF_V3 low = corners[0], high = corners[0];
    for (int i = 1; i < 8; i++) {
        low = cf_min_v3(low, corners[i]);
        high = cf_max_v3(high, corners[i]);
    }
    REQUIRE(fabsf(low.x + .5f) < 1e-4f && fabsf(high.x - 2.5f) < 1e-4f);
    REQUIRE(fabsf(low.z + .5f) < 1e-4f && fabsf(high.z - .5f) < 1e-4f);
    return true;
}

/* A Transform3D that mirrors a box across x shows it as the unmirrored box
 * does, as a shape and as the unit box mesh: the lit pass culls the faces
 * turned away, not the ones in view, and their normals point out. A
 * Transform3D that flattens a solid hides it. */
TEST_CASE(mirroring_transform3d_keeps_faces_outward)
{
    ecs_entity box = add_solid((CF_V3){ 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    place_3d(box, (CF_V3){ 0 }, cf_quat_identity(), (CF_V3){ 1, 1, 1 });
    add_sun(0xffffffffu, (CF_V3){ -1, -2, -3 })->intensity = 1;
    GameEnvironment environment = unlit_environment();
    render_mesh_asset mesh = box_mesh();
    publish_mesh(&mesh);
    for (int imported = 0; imported < 2; imported++) {
        if (imported) {
            import_mesh(box);
        }
        GAME_GameTransform3D_get(world, box)->scale = (CF_V3){ 1, 1, 1 };
        REQUIRE(render(&environment));
        REQUIRE(top().colors.g > 30 && front().colors.g > 30);
        static CF_Pixel upright[PIXELS];
        memcpy(upright, pixels, sizeof upright);
        GAME_GameTransform3D_get(world, box)->scale = (CF_V3){ -1, 1, 1 };
        REQUIRE(render(&environment));
        REQUIRE(!render_scene3d_error(scene3d)[0]);
        REQUIRE(matches(upright));
        GAME_GameTransform3D_get(world, box)->scale = (CF_V3){ 1, 0, 1 };
        REQUIRE(render(&environment));
        REQUIRE(!render_scene3d_error(scene3d)[0] && !top().colors.g);
    }
    return true;
}

#if CHIBI_TEST_MATERIAL_GRAPHS
/* The two-unit box at the origin, drawn with surface material `name`. */
static ecs_entity add_surface_box(const char *name)
{
    ecs_entity box = add_solid((CF_V3){ 0 }, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2 }, 0xffffffffu);
    GameShader binding = { .material = name };
    assert(ecs_world_record_add(world, box, GAME_COMPONENT_GameShader.id, &binding, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
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
    GameEnvironment environment = sky_environment();
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
    GameEnvironment environment = sky_environment();
    int mean[2][3] = { 0 };
    for (int i = 0; i < 2; i++) {
        GAME_GameShader_get(world, box)->material = i ? "Rough" : "Mirror";
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
    GameEnvironment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), gray));
    GAME_GameShader_get(world, box)->material = "Ramp";
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
    GameEnvironment environment = unlit_environment();
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
    GameEnvironment environment = unlit_environment();
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
    GAME_GameShader_get(world, box)->material = "Part X";
    REQUIRE(render(&environment));
    REQUIRE(shows_ramp(top(), (const float[]){ .4f, .4f, .4f }));
    mesh = box_mesh();
    publish_mesh(&mesh);
    REQUIRE(render(&environment));
    static CF_Pixel part[PIXELS];
    memcpy(part, pixels, sizeof pixels);
    GAME_GameShader_get(world, box)->material = "Object X";
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
    GAME_GameShader_get(world, square)->material = name;
    GameEnvironment environment = unlit_environment();
    if (!render(&environment)) {
        return -1;
    }
    return pixel_at((CF_V3){ 0 }).colors.g;
}

/* Normal maps turn with a mesh's tangents. A sun shines down and toward -z.
 * A map tilted along u lights a square whose u runs along +z more than a
 * flat map does, and a square whose u runs along +x but whose authored
 * tangents point along +z shades the same. A map tilted along v follows the
 * tangents' handedness: it faces the sun when v runs along +z and turns away
 * when v is mirrored. Box-projected UVs keep their own frame, along +x, and
 * authored tangents turn with the square, across the sun. */
TEST_CASE(normal_maps_follow_mesh_tangents)
{
    static const float flat[] = { .5f, .5f, 1 }, along_u[] = { .85f, .5f, .85f },
                       along_v[] = { .5f, .85f, .85f };
    REQUIRE(test_register_normal_map("Flat", flat));
    REQUIRE(test_register_normal_map("AlongU", along_u));
    REQUIRE(test_register_normal_map("AlongV", along_v));
    static const CF_V3 x = { 1, 0, 0 }, z = { 0, 0, 1 }, back = { 0, 0, -1 };
    publish_square(z, x, (CF_V4){ 0 }, false);
    ecs_entity square = add_surface_box("Flat");
    import_mesh(square);
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 0, -1, -1 });
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
    GameVisualTransform turned = { .yaw = CF_PI / 2, .scale = 1 };
    assert(
        ecs_world_record_add(world, square, GAME_COMPONENT_GameVisualTransform.id, &turned, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(abs(square_light(square, "AlongU") - along) <= 3);
    return true;
}

/* A Transform3D that mirrors the ground square along z keeps it lit from
 * above, and turns its bitangent with its surface: a map tilted along v,
 * which faces the sun while v runs along +z, then shades as it does with v
 * along -z. */
TEST_CASE(mirroring_transform3d_turns_mesh_bitangents)
{
    static const float flat[] = { .5f, .5f, 1 }, along_v[] = { .5f, .85f, .85f };
    REQUIRE(test_register_normal_map("Flat", flat));
    REQUIRE(test_register_normal_map("AlongV", along_v));
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
    GameEnvironment environment = unlit_environment();
    environment.ambient_sky = 0x00ff00ffu;
    environment.ambient_ground = 0x00ff00ffu;
    environment.ambient_intensity = .5f;
    int green[3];
    for (int i = 0; i < 3; i++) {
        GAME_GameShader_get(world, box)->material = names[i];
        REQUIRE(render(&environment));
        green[i] = top().colors.g;
    }
    REQUIRE(green[0] > 100 && green[2] == 0);
    REQUIRE(green[2] + 20 < green[1] && green[1] + 20 < green[0]);

    publish_uniform_sky((const unsigned char[4]){ 128, 128, 128, 128 });
    environment = sky_environment();
    GAME_GameShader_get(world, box)->material = names[0];
    REQUIRE(render(&environment));
    REQUIRE(top().colors.g > 60);
    GAME_GameShader_get(world, box)->material = names[2];
    REQUIRE(render(&environment));
    REQUIRE(!top().colors.r && !top().colors.g && !top().colors.b);

    add_sun(0xff0000ffu, (CF_V3){ 0, -2, 0 });
    environment = unlit_environment();
    GAME_GameShader_get(world, box)->material = names[0];
    REQUIRE(render(&environment));
    CF_Pixel open = top();
    GAME_GameShader_get(world, box)->material = names[2];
    REQUIRE(render(&environment));
    REQUIRE(open.colors.r > 150 && distance(top(), open) <= 1);
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
    GAME_GameTransform_get(world, box)->y = -6;
    GameEnvironment environment = sky_environment();
    CF_Pixel shown[4];
    for (int i = 0; i < 4; i++) {
        REQUIRE(test_register_alpha(modes[i].name, modes[i].mode, .3f, modes[i].cutoff));
        int id = render_shader_find(modes[i].name);
        REQUIRE(render_shader_may_be_transparent(id) == (modes[i].mode == SHADER_ALPHA_BLEND));
        GAME_GameShader_get(world, box)->material = modes[i].name;
        REQUIRE(render(&environment));
        shown[i] = pixel(SIDE / 2, SIDE / 2);
    }
    CF_Pixel open = pixel(4, 4);
    REQUIRE(shown[1].colors.g > 200 && distance(shown[2], shown[1]) <= 1);
    REQUIRE(shown[0].colors.g > 60 && shown[0].colors.g + 60 < shown[1].colors.g);
    REQUIRE(distance(shown[3], open) <= 1);
    return true;
}

/* Opaque and masked surfaces cast shadows like any solid; a blended one lets
 * the sun through. */
TEST_CASE(opaque_and_masked_surfaces_cast_shadows)
{
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_surface_box("Blended");
    GAME_GameGeometry3D_get(world, box)->elevation = 1;
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->shadows = true;
    REQUIRE(test_register_alpha("Blended", SHADER_ALPHA_BLEND, .5f, .5f));
    REQUIRE(test_register_alpha("Opaque", SHADER_ALPHA_OPAQUE, .5f, .5f));
    REQUIRE(test_register_alpha("Kept", SHADER_ALPHA_MASK, .5f, .4f));
    GameEnvironment environment = unlit_environment();
    REQUIRE(render(&environment));
    REQUIRE(pixel_at(shaded).colors.g > 60);
    const char *casters[] = { "Opaque", "Kept" };
    for (int i = 0; i < 2; i++) {
        GAME_GameShader_get(world, box)->material = casters[i];
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
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    ecs_entity box = add_surface_box("Half");
    GAME_GameGeometry3D_get(world, box)->elevation = 1;
    GameDirectionalLight *sun = add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 });
    sun->shadows = true;
    GameEnvironment environment = unlit_environment();
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
    GAME_GamePointLight_get(world, lamp)->shadows = true;
    REQUIRE(render(&environment));
    REQUIRE(render_scene3d_last_work(scene3d).light_shadows == 1);
    REQUIRE(open.colors.g > 30 && distance(pixel_at(cut), open) <= 3);
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
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 16, 16, .2f, -.1f }, 0xffffffffu);
    for (int i = 0; i < 600; i++) {
        add_solid_at(60, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1, 1 }, 0xffffffffu);
    }
    /* Beyond the left edge of the view and spanning z = -2 to 0, it keeps
     * z = -1 to 0, which shades the floor there up to x = 1. */
    ecs_entity pillar = add_solid(
        (CF_V3){ -8, 4, -1 },
        (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 8 },
        0xffffffffu
    );
    GameShader binding = { .material = "Half" };
    assert(
        ecs_world_record_add(world, pillar, GAME_COMPONENT_GameShader.id, &binding, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    for (int i = 0; i < 600; i++) {
        add_solid_at(60, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1, 1 }, 0xffffffffu);
    }
    add_solid_at(0, (GameGeometry3D){ GAME_PRIMITIVE_BOX, 1, 1, 1, .5f }, 0xff0000ffu);
    add_sun(0xffffffffu, (CF_V3){ 1, -1, 0 })->shadows = true;
    GameEnvironment environment = unlit_environment();
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
#endif

void test_render_scene3d(bool gles)
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
    RUN_TEST_CASE(scene_draws_without_a_valid_rhythm_clock);
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
    RUN_TEST_CASE(republished_meshes_replace_their_gpu_copies);
    RUN_TEST_CASE(translucent_meshes_sort_by_their_bounds);
    RUN_TEST_CASE(translucent_copies_of_a_mesh_sort_by_their_bounds);
    RUN_TEST_CASE(transform3d_places_solids_in_their_pose);
    RUN_TEST_CASE(transform3d_children_pick_and_bound_under_their_parent);
    RUN_TEST_CASE(mirroring_transform3d_keeps_faces_outward);
#if CHIBI_TEST_MATERIAL_GRAPHS
    RUN_TEST_CASE(mirrors_reflect_the_sky_in_its_directions);
    RUN_TEST_CASE(rough_surfaces_blur_the_sky_they_reflect);
    RUN_TEST_CASE(color_ramps_color_surfaces_from_any_table_offset);
    RUN_TEST_CASE(imported_mesh_groups_take_their_own_materials);
    RUN_TEST_CASE(imported_meshes_carry_texture_and_part_coordinates);
    RUN_TEST_CASE(normal_maps_follow_mesh_tangents);
    RUN_TEST_CASE(mirroring_transform3d_turns_mesh_bitangents);
    RUN_TEST_CASE(occlusion_dims_only_ambient_light);
    RUN_TEST_CASE(alpha_modes_decide_how_opacity_draws);
    RUN_TEST_CASE(opaque_and_masked_surfaces_cast_shadows);
    RUN_TEST_CASE(masked_surfaces_cast_cut_out_shadows);
    RUN_TEST_CASE(masked_solids_outside_the_view_cut_their_shadows);
#endif
    pu_clear_setup();
    cf_destroy_app();
}
