#include "memory_images.h"
#include "render/scene_renderer.h"
#include "render/viewport.h"
#include "runtime/component_catalog.h"
#include "runtime/particles.h"
#include "runtime/world.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* One hidden app draws 2D scenes through the scene renderer and the viewport,
 * then reads back the app canvas. */
enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE,
    /* The side of the sprite images, in texels and world units. */
    SPRITE = 16,
    SPRITE_TEXELS = SPRITE * SPRITE
};

static const unsigned red = 0xff0000ffu;

static ecs_world *test_world;
static particles *test_particles;
static app_scene_renderer *renderer;
static engine_viewport *viewport;
static bool drawn;
static CF_Pixel pixels[PIXELS];

static void draw_scene(void *)
{
    drawn = app_scene_renderer_draw(renderer, test_world, test_particles, viewport, NULL, (render_skip){ 0 });
}

static void setup()
{
    test_world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(test_world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    test_particles = particles_new();
    renderer = app_scene_renderer_new();
    viewport = engine_viewport_new(SIDE, SIDE);
    engine_viewport_set_view(viewport, &(engine_viewport_view){ .width = SIDE, .height = SIDE });
    engine_viewport_set_render_callback(viewport, draw_scene, NULL);
}

static void teardown()
{
    engine_viewport_free(viewport);
    app_scene_renderer_free(renderer);
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

/* A box covering the middle half of the view. */
static void add_box(unsigned color)
{
    ecs_entity box = add(COMPONENT_Transform.id, &(Transform){ 0 });
    Appearance look = { .width = SIDE / 2, .height = SIDE / 2, .color = color, .shape = SHAPE_BOX };
    assert(ecs_world_record_add(test_world, box, COMPONENT_Appearance.id, &look, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
}

static bool read_pixels()
{
    CF_Readback readback = cf_canvas_readback(cf_app_get_canvas());
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
        cf_sleep(1);
    }
    int bytes = (int)sizeof pixels;
    bool read = cf_readback_size(readback) == bytes &&
                cf_readback_data(readback, pixels, bytes) == bytes;
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
    HRC2D defaults = hrc2d_defaults();
    ecs_entity settings = add(COMPONENT_HRC2D.id, &defaults);
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
    REQUIRE(ecs_world_destroy_entity(test_world, settings) == ECS_OK);
    REQUIRE(draw());
    REQUIRE(!lit());
    REQUIRE(shows_red(center()));
    return true;
}

/* White texels, normals tilted toward -x, flat normals, and a map too small
 * for them. */
static unsigned char white[SPRITE_TEXELS * 4], facing_left[SPRITE_TEXELS * 4],
    flat[SPRITE_TEXELS * 4], small[4 * 4];

static void fill(unsigned char *pixels, int count, const unsigned char rgba[4])
{
    for (int i = 0; i < count; i++) {
        memcpy(pixels + 4 * i, rgba, 4);
    }
}

static const test_image SPRITE_IMAGES[] = {
    { "white.png", SPRITE, SPRITE, white },
    { "facing_left.png", SPRITE, SPRITE, facing_left },
    { "flat.png", SPRITE, SPRITE, flat },
    { "small.png", 2, 2, small },
};

/* Loads the sprite images as preparing a scene does. */
static void prepare_sprite_images()
{
    fill(white, SPRITE_TEXELS, (unsigned char[4]){ 255, 255, 255, 255 });
    fill(facing_left, SPRITE_TEXELS, (unsigned char[4]){ 26, 128, 204, 255 });
    fill(flat, SPRITE_TEXELS, (unsigned char[4]){ 128, 128, 255, 255 });
    fill(small, 4, (unsigned char[4]){ 128, 128, 255, 255 });
    test_mount_images(SPRITE_IMAGES, 4);
    test_sprite_variant("white.png", false);
    for (int i = 1; i < 4; i++) {
        test_sprite_variant(SPRITE_IMAGES[i].key, true);
    }
}

/* A white sprite at x 12 shaded by `normal_map`, with no Material. */
static ecs_entity add_sprite(const char *normal_map, bool flip_x)
{
    ecs_entity sprite = add(COMPONENT_Transform.id, &(Transform){ 12, 0 });
    Sprite look = { .image = "white.png", .flip_x = flip_x, .normal_map = normal_map };
    assert(ecs_world_record_add(test_world, sprite, COMPONENT_Sprite.id, &look, 0) == ECS_OK);
    assert(ecs_world_apply_commands(test_world) == ECS_OK);
    return sprite;
}

/* Lighting without unlit color or history. */
static HRC2D plain_lighting()
{
    HRC2D lighting = hrc2d_defaults();
    lighting.ambient = 0;
    lighting.adaptive = false;
    lighting.radiance_hysteresis = lighting.bounce_hysteresis = 1;
    return lighting;
}

/* A white light at x -20, lighting without unlit color or history. */
static void add_light()
{
    ecs_entity light = add(COMPONENT_Transform.id, &(Transform){ -20, 0 });
    Appearance disc = { .width = 8, .height = 8, .shape = SHAPE_CIRCLE };
    Material glow = { .emission_color = 0xffffffffu,
                      .emission_intensity = .1f,
                      .emission_multiplier = 1 };
    assert(ecs_world_record_add(test_world, light, COMPONENT_Appearance.id, &disc, 0) == ECS_OK);
    assert(ecs_world_record_add(test_world, light, COMPONENT_Material.id, &glow, 0) == ECS_OK);
    HRC2D lighting = plain_lighting();
    add(COMPONENT_HRC2D.id, &lighting);
}

/* The mean green across the sprite's middle row. */
static int sprite_green()
{
    int sum = 0, first = SIDE / 2 + 12 - SPRITE / 2;
    for (int x = first; x < first + SPRITE; x++) {
        sum += pixels[SIDE / 2 * SIDE + x].colors.g;
    }
    return sum / SPRITE;
}

/* Without a Material, a normal-mapped sprite takes surface light: the side
 * its normals face is lit more than flat normals, and mirrored they face
 * away and are lit less. */
TEST_CASE(normal_maps_turn_sprites_toward_the_light)
{
    prepare_sprite_images();
    add_light();
    const char *maps[] = { "facing_left.png", "flat.png", "facing_left.png" };
    int green[3];
    for (int i = 0; i < 3; i++) {
        ecs_entity sprite = add_sprite(maps[i], i == 2);
        REQUIRE(draw());
        green[i] = sprite_green();
        REQUIRE(ecs_world_destroy_entity(test_world, sprite) == ECS_OK);
    }
    REQUIRE(green[0] > green[1] + 4 && green[1] > green[2] + 4);
    /* A Material's edge_light decides instead, and at zero the map shades
     * nothing. */
    Material matte = { .scene = 0xffffffffu, .albedo = 0xffffffffu };
    for (int i = 0; i < 2; i++) {
        ecs_entity sprite = add_sprite("facing_left.png", i == 1);
        REQUIRE(
            ecs_world_record_add(test_world, sprite, COMPONENT_Material.id, &matte, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
        REQUIRE(draw());
        green[i] = sprite_green();
        REQUIRE(ecs_world_destroy_entity(test_world, sprite) == ECS_OK);
    }
    REQUIRE(abs(green[0] - green[1]) <= 1);
    return true;
}

/* A sprite map must be loaded and laid out like its image, and its relief
 * must be finite. */
TEST_CASE(sprites_reject_bad_maps_and_relief)
{
    prepare_sprite_images();
    ecs_entity sprite = add_sprite("small.png", false);
    REQUIRE(!draw());
    REQUIRE(strstr(app_scene_renderer_error(renderer), "small.png is 2x2, not the 16x16 of its image"));
    REQUIRE(ecs_world_destroy_entity(test_world, sprite) == ECS_OK);
    sprite = add_sprite("unloaded.png", false);
    REQUIRE(!draw());
    REQUIRE(
        strstr(app_scene_renderer_error(renderer), "Sprite map: Image resource or sampler is not resident: unloaded.png")
    );
    REQUIRE(ecs_world_destroy_entity(test_world, sprite) == ECS_OK);
    sprite = add(COMPONENT_Transform.id, &(Transform){ 0 });
    Sprite raised = { .image = "white.png", .relief = NAN };
    REQUIRE(ecs_world_record_add(test_world, sprite, COMPONENT_Sprite.id, &raised, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(!draw());
    REQUIRE(strstr(app_scene_renderer_error(renderer), "Sprite white.png has no finite relief"));
    return true;
}

/* A white floor over the view's right half that absorbs light, lit by Depth
 * thickness from as far as the view reaches. */
static void add_floor()
{
    ecs_entity floor = add(COMPONENT_Transform.id, &(Transform){ SIDE / 4, 0 });
    Appearance look = { .width = SIDE / 2,
                        .height = SIDE,
                        .color = 0xffffffffu,
                        .shape = SHAPE_BOX };
    Material closed = { .scene = 0xffffffffu,
                        .albedo = 0xffffffffu,
                        .absorption = 1,
                        .edge_light = 1 };
    assert(ecs_world_record_add(test_world, floor, COMPONENT_Appearance.id, &look, 0) == ECS_OK);
    assert(ecs_world_record_add(test_world, floor, COMPONENT_Material.id, &closed, 0) == ECS_OK);
    HRC2D lighting = plain_lighting();
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    lighting.surface_depth_radius = SIDE;
    lighting.feedback = false;
    lighting.fog = 0;
    add(COMPONENT_HRC2D.id, &lighting);
}

/* The green of the floor's middle, 36 units from a white lamp at x -20
 * drawn as the disc `look` at `scale`. */
static bool floor_green(Appearance look, float scale, int *green)
{
    ecs_entity lamp = add(COMPONENT_Transform.id, &(Transform){ -20, 0 });
    look.shape = SHAPE_CIRCLE;
    Material glow = { .scene = 0xffffffffu,
                      .emission_color = 0xffffffffu,
                      .emission_intensity = 2,
                      .emission_multiplier = 1 };
    VisualTransform visual = { .scale = scale };
    REQUIRE(ecs_world_record_add(test_world, lamp, COMPONENT_Appearance.id, &look, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(test_world, lamp, COMPONENT_Material.id, &glow, 0) == ECS_OK);
    REQUIRE(
        ecs_world_record_add(test_world, lamp, COMPONENT_VisualTransform.id, &visual, 0) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(test_world) == ECS_OK);
    REQUIRE(draw());
    *green = pixels[SIDE / 2 * SIDE + SIDE / 2 + 16].colors.g;
    REQUIRE(ecs_world_destroy_entity(test_world, lamp) == ECS_OK);
    return true;
}

/* A light drawn as a shape stands its relief above the plane, scaled with
 * its visual, so it lights the closed floor beside it from above. On the
 * plane it lights none of the floor past its rim, and a relief that isn't
 * finite draws nothing. */
TEST_CASE(appearance_relief_raises_lights_over_closed_floors)
{
    add_floor();
    int on_plane, raised, scaled, lower, unfinite;
    CF_Pixel *lamp = &pixels[SIDE / 2 * SIDE + SIDE / 2 - 20];
    REQUIRE(floor_green((Appearance){ .width = 8, .height = 8 }, 1, &on_plane));
    REQUIRE(floor_green((Appearance){ .width = 8, .height = 8, .relief = 24 }, 1, &raised));
    REQUIRE(lamp->colors.g > 200);
    REQUIRE(floor_green((Appearance){ .width = 4, .height = 4, .relief = 12 }, 2, &scaled));
    REQUIRE(floor_green((Appearance){ .width = 8, .height = 8, .relief = 12 }, 1, &lower));
    REQUIRE(floor_green((Appearance){ .width = 8, .height = 8, .relief = NAN }, 1, &unfinite));
    REQUIRE(lamp->colors.g < 3 && unfinite < 3);
    REQUIRE(on_plane < 3 && raised > 60);
    REQUIRE(abs(scaled - raised) <= 3 && lower < raised - 15);
    return true;
}

void test_render_scene2d()
{
#ifdef __linux__
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_AppOptionFlags options = CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT;
    CF_Result made = cf_make_app("scene2d", 0, 0, 0, SIDE, SIDE, options, NULL);
    assert(!cf_is_error(made));
    /* Pixel assertions sample a fixed target, independent of window density. */
    cf_app_set_canvas_size(SIDE, SIDE);
    app_scene_renderer_initialize();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(scenes_without_lighting_draw_their_materials_unlit);
    RUN_TEST_CASE(default_lighting_keeps_materials_visible);
    RUN_TEST_CASE(normal_maps_turn_sprites_toward_the_light);
    RUN_TEST_CASE(sprites_reject_bad_maps_and_relief);
    RUN_TEST_CASE(appearance_relief_raises_lights_over_closed_floors);
    pu_clear_setup();
    cf_destroy_app();
}
