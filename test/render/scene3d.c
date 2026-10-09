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
static void add_solid(CF_V3 center, GameGeometry3D shape, unsigned color)
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
    RUN_TEST_CASE(slow_auto_exposure_eases_past_half_float_steps);
    RUN_TEST_CASE(directional_light_casts_shadows);
    RUN_TEST_CASE(solids_outside_the_view_cast_shadows_into_it);
    RUN_TEST_CASE(shadow_pass_leaves_earlier_draws_queued);
    RUN_TEST_CASE(deep_views_split_sun_shadows_into_cascades);
    RUN_TEST_CASE(empty_depth_ranges_take_no_cascade);
    RUN_TEST_CASE(shadow_cascades_cover_only_what_the_view_shows);
    RUN_TEST_CASE(sun_size_softens_shadows_away_from_casters);
    RUN_TEST_CASE(point_lights_cast_shadows_across_faces);
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
    pu_clear_setup();
    cf_destroy_app();
}
