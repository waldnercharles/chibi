#include "render/scene3d.h"

#include "render/scene3d_post.h"
#include "render/scene_renderer.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/particles.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
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
static render_scene3d *scene3d;
static render_scene3d_post *post;
static game_particles *particles;
static CF_Pixel pixels[PIXELS];
static engine *runtime;

static void setup()
{
    world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    scene3d = render_scene3d_new();
    post = render_scene3d_post_new();
    particles = game_particles_new();
}

static void teardown()
{
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

static void add_box(unsigned color)
{
    ecs_entity box = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ 0 });
    GameGeometry3D shape = { GAME_PRIMITIVE_BOX, 2, 2, 2, 0 };
    GameMaterial material = { .scene = color };
    assert(ecs_world_record_add(world, box, GAME_COMPONENT_GameGeometry3D.id, &shape, 0) == ECS_OK);
    assert(
        ecs_world_record_add(world, box, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

static ecs_entity add_sun(unsigned color, CF_V3 direction)
{
    GameDirectionalLight sun = { .color = color, .intensity = 3, .direction = direction };
    return add(GAME_COMPONENT_GameDirectionalLight.id, &sun);
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

static bool render(const GameEnvironment *environment)
{
    GameEnvironment settings = *environment;
    settings.bloom_strength = 0;
    assert(render_scene3d_post_prepare(post, SIDE, SIDE, &settings));
    CF_Canvas canvas = render_scene3d_post_target(post);
    cf_canvas_set_clear_color(canvas, (CF_Color){ 0, 0, 0, 1 });
    render_scene3d_camera camera = {
        .view = cf_look_at((CF_V3){ 0, 4, 4 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 100),
    };
    cf_app_update(NULL);
    bool drawn = render_scene3d_draw(
        scene3d,
        world,
        particles,
        &camera,
        0,
        runtime,
        &settings,
        (render_skip){ 0 }
    );
    cf_render_to(canvas, true);
    bool processed = render_scene3d_post_process(post);
    cf_app_draw_onto_screen(false);
    if (!drawn || !processed) {
        return false;
    }
    CF_Readback readback = cf_canvas_readback(render_scene3d_post_output(post));
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
        cf_sleep(1);
    }
    int bytes = (int)sizeof pixels;
    bool read = cf_readback_data(readback, pixels, bytes) == bytes;
    cf_destroy_readback(readback);
    return read;
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

TEST_CASE(environment_ambient_lights_unlit_faces)
{
    add_box(0xffffffffu);
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    REQUIRE(top().colors.r == 0 && front().colors.r == 0);
    environment.ambient_sky = 0x00ff00ffu;
    environment.ambient_ground = 0x00ff00ffu;
    environment.ambient_intensity = 1;
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
    REQUIRE(lit.colors.r > 150 && lit.colors.g == 0 && lit.colors.b == 0);
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
    ecs_entity hidden = add_sun(0x00ff00ffu, (CF_V3){ 0, -2, 0 });
    GameRenderState state = { .hidden = true };
    assert(
        ecs_world_record_add(world, hidden, GAME_COMPONENT_GameRenderState.id, &state, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    runtime = engine_new(world, NULL);
    GameEnvironment environment = game_environment_defaults();
    environment.ambient_intensity = 0;
    REQUIRE(render(&environment));
    REQUIRE(top().colors.r > 150 && top().colors.g == 0);
    return true;
}

void test_render_scene3d()
{
    setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_Result made = cf_make_app("scene3d", 0, 0, 0, SIDE, SIDE, CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT, NULL);
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(scene_draws_without_a_valid_rhythm_clock);
    RUN_TEST_CASE(environment_ambient_lights_unlit_faces);
    RUN_TEST_CASE(directional_light_shades_faces_it_faces);
    RUN_TEST_CASE(scene_rejects_a_second_directional_light);
    RUN_TEST_CASE(hidden_directional_lights_neither_shade_nor_count);
    pu_clear_setup();
    cf_destroy_app();
}
