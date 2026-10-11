#include "render/scene_renderer.h"
#include "render/viewport.h"
#include "runtime/component_catalog.h"
#include "runtime/particles.h"
#include "runtime/world.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>

/* One hidden app draws 2D scenes through the scene renderer and the viewport,
 * then reads back the app canvas. */
enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static const unsigned red = 0xff0000ffu;

static ecs_world *world;
static game_particles *particles;
static app_scene_renderer *renderer;
static engine_viewport *viewport;
static bool drawn;
static CF_Pixel pixels[PIXELS];

static void draw_scene(void *)
{
    drawn = app_scene_renderer_draw(renderer, world, particles, viewport, NULL, (render_skip){ 0 });
}

static void setup()
{
    world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    particles = game_particles_new();
    renderer = app_scene_renderer_new();
    viewport = engine_viewport_new(SIDE, SIDE);
    engine_viewport_set_view(viewport, &(engine_viewport_view){ .width = SIDE, .height = SIDE });
    engine_viewport_set_render_callback(viewport, draw_scene, NULL);
}

static void teardown()
{
    engine_viewport_free(viewport);
    app_scene_renderer_free(renderer);
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

/* A box covering the middle half of the view. */
static void add_box(unsigned color)
{
    ecs_entity box = add(GAME_COMPONENT_GameTransform.id, &(GameTransform){ 0 });
    GameAppearance look = { .width = SIDE / 2,
                            .height = SIDE / 2,
                            .color = color,
                            .shape = GAME_SHAPE_BOX };
    assert(ecs_world_record_add(world, box, GAME_COMPONENT_GameAppearance.id, &look, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

static bool read_pixels()
{
    CF_Readback readback = cf_canvas_readback(cf_app_get_canvas());
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
        cf_sleep(1);
    }
    int bytes = (int)sizeof pixels;
    bool read = cf_readback_data(readback, pixels, bytes) == bytes;
    cf_destroy_readback(readback);
    return read;
}

/* Draws a frame as a scene preview does and reads it back. */
static bool draw()
{
    cf_app_update(NULL);
    drawn = false;
    if (engine_viewport_render(viewport, SIDE, SIDE) != 0) {
        return false;
    }
    engine_viewport_present(viewport);
    cf_app_draw_onto_screen(true);
    return read_pixels() && drawn;
}

static CF_Pixel center()
{
    return pixels[SIDE / 2 * SIDE + SIDE / 2];
}

static bool shows_red(CF_Pixel pixel)
{
    return pixel.colors.r > 200 && pixel.colors.g < 40 && pixel.colors.b < 40;
}

static bool lit()
{
    render_radiance_status status;
    return app_scene_renderer_lighting_status(renderer, &status);
}

TEST_CASE(scenes_without_lighting_draw_their_materials_unlit)
{
    add_box(red);
    REQUIRE(draw());
    REQUIRE(shows_red(center()));
    REQUIRE(!lit());
    return true;
}

/* Lighting a scene keeps what it drew without lighting and adds light to it.
 * The unlit view, or removing the settings, stops lighting again. */
TEST_CASE(default_lighting_keeps_materials_visible)
{
    add_box(red);
    GameHRC2D defaults = game_hrc2d_defaults();
    ecs_entity settings = add(GAME_COMPONENT_GameHRC2D.id, &defaults);
    REQUIRE(draw());
    REQUIRE(lit());
    REQUIRE(shows_red(center()));
    app_scene_renderer_set_view(renderer, true, false);
    REQUIRE(draw());
    REQUIRE(!lit());
    REQUIRE(shows_red(center()));
    app_scene_renderer_set_view(renderer, false, false);
    REQUIRE(draw());
    REQUIRE(lit());
    REQUIRE(ecs_world_destroy_entity(world, settings) == ECS_OK);
    REQUIRE(draw());
    REQUIRE(!lit());
    REQUIRE(shows_red(center()));
    return true;
}

void test_render_scene2d()
{
    setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_AppOptionFlags options = CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT;
    CF_Result made = cf_make_app("scene2d", 0, 0, 0, SIDE, SIDE, options, NULL);
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(scenes_without_lighting_draw_their_materials_unlit);
    RUN_TEST_CASE(default_lighting_keeps_materials_visible);
    pu_clear_setup();
    cf_destroy_app();
}
