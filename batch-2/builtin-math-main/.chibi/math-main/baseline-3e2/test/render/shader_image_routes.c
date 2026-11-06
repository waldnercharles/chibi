#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "memory_images.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "render/shader_images.h"
#include "runtime/images/catalog.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Materials and shapes of image textures, drawn by the material raster and
 * the fragment programs, from images the catalog reads from memory. */

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE,
    IMAGES = 10
};

/* One color image per material, then an alpha image for the shape and an
 * asymmetric one for sampling seams. */
enum
{
    ALPHA = IMAGES,
    SAMPLING,
    IMAGE_COUNT
};

static const char *const keys[IMAGE_COUNT] = {
    "content/routes/0.PNG",     "content/routes/1.PNG",
    "content/routes/2.PNG",     "content/routes/3.PNG",
    "content/routes/4.PNG",     "content/routes/5.PNG",
    "content/routes/6.PNG",     "content/routes/7.PNG",
    "content/routes/8.PNG",     "content/routes/9.PNG",
    "content/routes/Alpha.png", "content/routes/Sampling.png"
};
static CF_Pixel colors[IMAGES];
static const CF_Pixel alpha[] = { { .colors = { 255, 0, 0, 0 } },
                                  { .colors = { 0, 255, 0, 255 } },
                                  { .colors = { 0, 0, 255, 255 } },
                                  { .colors = { 255, 255, 255, 0 } } };
static const CF_Pixel asymmetric[] = { { .colors = { 32, 64, 96, 255 } },
                                       { .colors = { 224, 16, 48, 255 } },
                                       { .colors = { 40, 200, 80, 255 } },
                                       { .colors = { 96, 128, 240, 255 } } };
static test_image images[IMAGE_COUNT];
static int materials[IMAGES];

static node *image_node(graph *g, const char *key)
{
    node *image = graph_add_node(g, registry_node_by_name("Shader Image Texture"));
    pin_value value = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = key, .length = (uint32_t)strlen(key) }
    };
    assert(graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_IMAGE, &value) == 0);
    for (int i = SHADER_IMAGE_INPUT_COLOR_SPACE; i < SHADER_IMAGE_INPUT_COUNT; i++) {
        value = (pin_value){ .type = PIN_I32,
                             .type_name = image->input_pins[i].type_name,
                             .i = i == SHADER_IMAGE_INPUT_FILTER ? SHADER_IMAGE_NEAREST
                                                                 : 1 };
        assert(graph_set_pin_default(g, image, 0, i, &value) == 0);
    }
    return image;
}

static int publish(behavior_asset *asset)
{
    shader_error error = { 0 };
    if (!render_shader_upsert(asset, &error)) {
        fprintf(stderr, "Image route graph %s: %s\n", asset->name, error.message);
        assert(false);
    }
    int id = render_shader_find(asset->name);
    assert(id);
    return id;
}

static int material(const char *name, const char *key)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(asset->graph, false);
    node *image = image_node(asset->graph, key);
    assert(graph_connect_data(asset->graph, image, 0, out, 0) == 0);
    assert(graph_connect_data(asset->graph, image, 1, out, 5) == 0);
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(asset->graph, out, 0, 6, &unlit) == 0);
    int id = publish(asset);
    behavior_asset_free(asset);
    return id;
}

static int image_shape(const char *key)
{
    behavior_asset *asset = behavior_asset_new("Image route shape");
    asset->domain = BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 4;
    node *out = shader_graph_add_output(asset->graph, true);
    node *image = image_node(asset->graph, key);
    node *offset = graph_add_node(asset->graph, registry_node_by_name("Shader Subtract"));
    pin_value half = { .type = PIN_F32, .f = .5f };
    assert(graph_set_pin_default(asset->graph, offset, 0, 1, &half) == 0);
    assert(graph_connect_data(asset->graph, image, 1, offset, 0) == 0);
    assert(graph_connect_data(asset->graph, offset, 0, out, 0) == 0);
    int id = publish(asset);
    behavior_asset_free(asset);
    return id;
}

static int eight_image_material()
{
    behavior_asset *asset = behavior_asset_new("Image route eight inputs");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(asset->graph, false);
    node *sum = image_node(asset->graph, keys[0]);
    for (int i = 1; i < RENDER_SHADER_IMAGE_MAX; i++) {
        node *image = image_node(asset->graph, keys[i]);
        node *add = graph_add_node(asset->graph, registry_node_by_name("Shader Add"));
        assert(graph_set_pin_type(asset->graph, add, 0, 0, PIN_STRUCT, "ShaderVec3") == 0);
        assert(graph_set_pin_type(asset->graph, add, 0, 1, PIN_STRUCT, "ShaderVec3") == 0);
        assert(graph_set_pin_type(asset->graph, add, 1, 0, PIN_STRUCT, "ShaderVec3") == 0);
        assert(graph_connect_data(asset->graph, sum, 0, add, 0) == 0);
        assert(graph_connect_data(asset->graph, image, 0, add, 1) == 0);
        sum = add;
    }
    assert(graph_connect_data(asset->graph, sum, 0, out, 0) == 0);
    int id = publish(asset);
    behavior_asset_free(asset);
    return id;
}

static void read_scene(render_material_raster *raster, uint16_t pixels[PIXELS * 4])
{
    assert(test_read_texture(render_material_raster_textures(raster).scene, pixels, PIXELS));
}

static render_material_entity entity(int material, float x, float y)
{
    return (render_material_entity){ .inv_rot = { 1, 0, 0, 1 },
                                     .pos = { x, y },
                                     .extent = { 20, 20 },
                                     .shape_type = RENDER_SHAPE_CIRCLE,
                                     .radius = 4,
                                     .aa = .5f,
                                     .reserved = { (float)material } };
}

static bool raster_draw(render_material_raster *raster, const render_material_entity *entities, int count)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(
        raster,
        entities,
        count,
        (render_material_view){ -64, 64, 128, 128 }
    );
    cf_app_draw_onto_screen(false);
    return ok;
}

/* Ten image materials draw in one raster pass, smooth unions of different
 * materials match flat colors, and a rejected batch keeps the last frame. */
TEST_CASE(rasters_draw_more_images_than_a_program_binds)
{
    int shape = image_shape(keys[ALPHA]);
    int eight = eight_image_material();
    render_material_raster *raster = render_material_raster_new();
    REQUIRE(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_entity actors[IMAGES + 2];
    for (int i = 0; i < IMAGES; i++) {
        actors[i] = entity(materials[i], -54 + i * 12, 35);
    }
    REQUIRE(raster_draw(raster, actors, IMAGES));
    uint16_t before[PIXELS * 4], after[PIXELS * 4];
    read_scene(raster, before);
    for (int i = 0; i < IMAGES; i++) {
        int at = ((64 - 35) * SIDE + 64 - 54 + i * 12) * 4;
        const unsigned char *color = (const unsigned char *)&colors[i];
        for (int c = 0; c < 4; c++) {
            REQUIRE(fabsf(test_half(before[at + c]) - color[c] / 255.0f) < .003f);
        }
    }
    memmove(actors + 1, actors, IMAGES * sizeof *actors);
    actors[0] = entity(materials[0], -6, 0);
    actors[IMAGES + 1] = entity(materials[IMAGES - 1], 6, 0);
    actors[0].metaball_group = actors[IMAGES + 1].metaball_group = 1;
    actors[0].metaball_blend = actors[IMAGES + 1].metaball_blend = 12;
    actors[0].radius = actors[IMAGES + 1].radius = 7;
    REQUIRE(raster_draw(raster, actors, IMAGES + 2));
    read_scene(raster, before);
    /* This bridge is outside either circle alone, and inside their smooth union. */
    int bridge = ((64 - 7) * SIDE + 64) * 4;
    REQUIRE(test_half(before[bridge + 3]) > .9f);
    render_material_entity flat[IMAGES + 2];
    memcpy(flat, actors, sizeof flat);
    for (int i = 0; i < IMAGES + 2; i++) {
        int color = i == 0 ? 0 : i == IMAGES + 1 ? IMAGES - 1 : i - 1;
        const unsigned char *pixel = (const unsigned char *)&colors[color];
        flat[i].reserved[0] = 0;
        for (int c = 0; c < 4; c++) {
            flat[i].scene[c] = pixel[c] / 255.0f;
        }
    }
    REQUIRE(raster_draw(raster, flat, IMAGES + 2));
    read_scene(raster, after);
    for (int i = 0; i < PIXELS * 4; i++) {
        REQUIRE(fabsf(test_half(before[i]) - test_half(after[i])) < .003f);
    }
    for (int i = 0; i < IMAGES + 2; i++) {
        actors[i].metaball_group = 1;
    }
    REQUIRE(!raster_draw(raster, actors, IMAGES + 2));
    REQUIRE(strstr(render_material_raster_error(raster), "Metaball group 1"));
    read_scene(raster, before);
    REQUIRE(!memcmp(before, after, sizeof before));
    render_material_entity too_many = entity(eight, 0, 0);
    too_many.reserved[1] = (float)shape;
    REQUIRE(!raster_draw(raster, &too_many, 1));
    REQUIRE(strstr(render_material_raster_error(raster), "material/shape"));
    read_scene(raster, before);
    REQUIRE(!memcmp(before, after, sizeof before));
    render_material_raster_free(raster);
    return true;
}

TEST_CASE(fragments_draw_each_material_image)
{
    render_programs *programs = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    REQUIRE(canvas.id);
    render_material_entity actors[IMAGES];
    int programs_for_actor[IMAGES];
    shader_error error = { 0 };
    for (int i = 0; i < IMAGES; i++) {
        actors[i] = entity(materials[i], -54 + i * 12, 0);
        programs_for_actor[i] = render_shader_fragment_prepare(programs, &actors[i], &error);
        REQUIRE(programs_for_actor[i]);
    }
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    for (int i = 0; i < IMAGES; i++) {
        render_shader_fragment_draw(programs, programs_for_actor[i], &actors[i], (CF_V2){ 1, 1 });
    }
    cf_render_to(canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    CF_Pixel pixels[PIXELS];
    REQUIRE(test_read_canvas(canvas, pixels, sizeof pixels));
    for (int i = 0; i < IMAGES; i++) {
        CF_Pixel pixel = pixels[64 * SIDE + 64 - 54 + i * 12];
        REQUIRE(abs(pixel.colors.r - colors[i].colors.r) <= 2);
        REQUIRE(abs(pixel.colors.g - colors[i].colors.g) <= 2);
        REQUIRE(abs(pixel.colors.b - colors[i].colors.b) <= 2);
    }
    cf_destroy_canvas(canvas);
    render_programs_free(programs);
    return true;
}

static int sampling_material(const char *key, int settings)
{
    char name[64];
    snprintf(name, sizeof name, "Image fragment sampling %d", settings);
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, false);
    node *image = image_node(g, key);
    node *mapping = graph_add_node(g, registry_node_by_name("Shader Mapping"));
    assert(shader_graph_init_node(g, mapping));
    CF_V2 scale = { 4, 4 }, offset = { -1.015625f, -1.015625f };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = &scale };
    assert(graph_set_pin_default(g, mapping, 0, 3, &value) == 0);
    value.ptr = &offset;
    assert(graph_set_pin_default(g, mapping, 0, 1, &value) == 0);
    for (int i = 0; i < 3; i++) {
        value = (pin_value){
            .type = PIN_I32,
            .type_name = image->input_pins[SHADER_IMAGE_INPUT_COLOR_SPACE + i].type_name,
            .i = (settings >> i) & 1
        };
        assert(
            graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_COLOR_SPACE + i, &value) == 0
        );
    }
    value = (pin_value){ .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(g, out, 0, 6, &value) == 0);
    assert(graph_connect_data(g, mapping, 0, image, SHADER_IMAGE_INPUT_UV) == 0);
    assert(graph_connect_data(g, image, 0, out, 0) == 0);
    int id = publish(asset);
    behavior_asset_free(asset);
    return id;
}

/* Every filter, wrap and color space draws what the CPU samples on both sides
 * of the image's seams, through either draw filter. */
TEST_CASE(fragments_sample_like_the_cpu_across_seams)
{
    render_programs *programs = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    REQUIRE(canvas.id);
    /* Mapping puts exact image seams at pixels 32/64/96, with samples on both
     * sides, at texel centers, and outside the original UV range. */
    int coordinates[] = { 1,  15, 16, 31, 32, 33, 47, 48,
                          63, 64, 65, 79, 80, 95, 96, 126 };
    for (int settings = 0; settings < 8; settings++) {
        int material = sampling_material(keys[SAMPLING], settings);
        render_material_entity actor = entity(material, 0, 0);
        actor.shape_type = RENDER_SHAPE_BOX;
        actor.extent[0] = actor.extent[1] = SIDE * .5f;
        actor.points[0] = actor.points[1] = SIDE * .5f;
        shader_error error = { 0 };
        int program = render_shader_fragment_prepare(programs, &actor, &error);
        REQUIRE(program);
        for (int draw_filter = 0; draw_filter < 2; draw_filter++) {
            cf_app_update(NULL);
            cf_draw_push();
            cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
            cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
            cf_draw_push_filter(draw_filter ? CF_DRAW_FILTER_LINEAR : CF_DRAW_FILTER_NEAREST);
            render_shader_fragment_draw(programs, program, &actor, (CF_V2){ 1, 1 });
            cf_render_to(canvas, true);
            cf_draw_pop_filter();
            cf_draw_pop();
            cf_app_draw_onto_screen(false);
            CF_Pixel pixels[PIXELS];
            REQUIRE(test_read_canvas(canvas, pixels, sizeof pixels));
            for (int yi = 0; yi < 16; yi++) {
                for (int xi = 0; xi < 16; xi++) {
                    int x = coordinates[xi], y = coordinates[yi];
                    shader_sample_context sample = {
                        .uv = { (x + .5f) / SIDE, (y + .5f) / SIDE },
                    };
                    float expected[SHADER_OUTPUT_MAX][4];
                    REQUIRE(render_shader_sample(material, &sample, NULL, expected, &error));
                    const unsigned char *actual = (const unsigned char *)&pixels[y * SIDE + x];
                    for (int channel = 0; channel < 3; channel++) {
                        int reference = (int)lroundf(expected[0][channel] * 255);
                        if (abs(actual[channel] - reference) > 2) {
                            fprintf(
                                stderr,
                                "Image fragment settings %d draw filter %d "
                                "pixel %d,%d channel %d: %d != %d\n",
                                settings,
                                draw_filter,
                                x,
                                y,
                                channel,
                                actual[channel],
                                reference
                            );
                            return false;
                        }
                    }
                }
            }
        }
    }
    cf_destroy_canvas(canvas);
    render_programs_free(programs);
    return true;
}

/* Shapes of an image's alpha contain the points its alpha covers. */
TEST_CASE(image_shapes_contain_what_their_alpha_covers)
{
    image_shape(keys[ALPHA]);
    Shader binding = { .shape = "Image route shape" };
    float none[2] = { 0 };
    REQUIRE(
        render_shader_contains(&binding, NULL, (ecs_entity){ 0 }, (float[2]){ -2, 2 }, none, 0)
    );
    REQUIRE(
        !render_shader_contains(&binding, NULL, (ecs_entity){ 0 }, (float[2]){ 2, 2 }, none, 0)
    );
    return true;
}

/* A reload past the resident side limit keeps the last good image. */
TEST_CASE(oversized_reloads_keep_the_last_good_image)
{
    char message[256];
    REQUIRE(render_image_reload(keys[0], message, sizeof message));
    CF_Pixel *wide = xcalloc(RENDER_IMAGE_SIDE_MAX + 1, sizeof *wide);
    images[0] = (test_image){ keys[0],
                              RENDER_IMAGE_SIDE_MAX + 1,
                              1,
                              (const unsigned char *)wide };
    bool rejected = !render_image_reload(keys[0], message, sizeof message);
    images[0] = (test_image){ keys[0], 1, 1, (const unsigned char *)&colors[0] };
    free(wide);
    REQUIRE(rejected && strstr(message, "resident limit"));
    int width = 0, height = 0;
    REQUIRE(render_image_info(keys[0], &width, &height) && width == 1 && height == 1);
    shader_sample_context sample = { .uv = { .5f, .5f } };
    shader_error error = { 0 };
    float sampled[SHADER_OUTPUT_MAX][4];
    REQUIRE(render_shader_sample(materials[0], &sample, NULL, sampled, &error));
    REQUIRE(fabsf(sampled[0][0] - colors[0].colors.r / 255.0f) < .0001f);
    REQUIRE(render_image_reload(keys[0], message, sizeof message));
    return true;
}

/* Mounts the images and publishes a material of each color image. */
static void setup()
{
    for (int i = 0; i < IMAGES; i++) {
        colors[i] = (CF_Pixel){ .colors = { (uint8_t)(32 + 17 * i),
                                            (uint8_t)(220 - 13 * i),
                                            (uint8_t)(64 + 11 * i),
                                            255 } };
        images[i] = (test_image){ keys[i], 1, 1, (const unsigned char *)&colors[i] };
    }
    images[ALPHA] = (test_image){ keys[ALPHA], 2, 2, (const unsigned char *)alpha };
    images[SAMPLING] = (test_image){ keys[SAMPLING], 2, 2, (const unsigned char *)asymmetric };
    test_mount_images(images, IMAGE_COUNT);
    for (int i = 0; i < IMAGES; i++) {
        char name[64];
        snprintf(name, sizeof name, "Image route %d", i);
        materials[i] = material(name, keys[i]);
    }
}

static void teardown()
{
    authoring_shader_catalog_clear();
    render_images_clear();
}

/* Runs in an open hidden app with the graph packs installed. */
void test_shader_image_routes()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(image_shapes_contain_what_their_alpha_covers);
    RUN_TEST_CASE(rasters_draw_more_images_than_a_program_binds);
    RUN_TEST_CASE(fragments_draw_each_material_image);
    RUN_TEST_CASE(fragments_sample_like_the_cpu_across_seams);
    RUN_TEST_CASE(oversized_reloads_keep_the_last_good_image);
    pu_clear_setup();
}
