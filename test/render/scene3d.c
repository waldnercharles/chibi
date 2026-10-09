#include "render/scene3d.h"

#include "render/scene3d_post.h"
#include "render/scene_renderer.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/particles.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
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

static void add_solid_at(float x, GameGeometry3D shape, unsigned color)
{
    ecs_entity solid = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ x, 0 });
    GameMaterial material = { .scene = color };
    assert(
        ecs_world_record_add(world, solid, GAME_COMPONENT_GameGeometry3D.id, &shape, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, solid, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
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

static bool render_at(const GameEnvironment *environment, float time)
{
    GameEnvironment settings = *environment;
    settings.bloom_strength = 0;
    if (!render_scene3d_post_prepare(post, SIDE, SIDE, &settings)) {
        return false;
    }
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
    bool processed = render_scene3d_post_process(post, time);
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
    RUN_TEST_CASE(neutral_tone_mapping_keeps_saturated_colors);
    RUN_TEST_CASE(auto_exposure_meters_the_frame_to_middle_gray);
    RUN_TEST_CASE(auto_exposure_meters_the_geometric_mean);
    RUN_TEST_CASE(auto_exposure_ignores_an_empty_background);
    RUN_TEST_CASE(auto_exposure_adapts_over_scene_time);
    pu_clear_setup();
    cf_destroy_app();
}
