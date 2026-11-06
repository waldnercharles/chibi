#include "material_graphs.h"
#include "render/material_raster.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "render/shader_program.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* One hidden app draws a ramp material through the 2D draw shader and the
 * material raster, and compiles it for every kind of program. Ramps of every
 * material in a program share one table, and nine full ramps fill its first
 * 288 entries, so the last material's stops lie past the 4 KB a Vulkan uniform
 * block exposes. */
enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE,
    FILLERS = 9
};

static const float red[4] = { .7f, .1f, .4f, 1 }, green[4] = { .1f, .7f, .4f, 1 };
/* A quarter of the way from red to green. */
static const float mixed[3] = { .55f, .25f, .4f };

static void teardown()
{
    render_shader_catalog_clear();
}

/* Registers the fillers, then the red-to-green ramp, and returns its id, or
 * zero if a registration failed. */
static int register_ramps()
{
    static const float gray[4] = { .3f, .3f, .3f, 1 };
    bool registered = true;
    for (int i = 0; i < FILLERS; i++) {
        char name[16];
        snprintf(name, sizeof name, "Filler %d", i);
        registered = registered &&
                     test_register_ramp(name, SHADER_RAMP_MAX, gray, gray);
    }
    return registered && test_register_ramp("Ramp", 2, red, green)
             ? render_shader_find("Ramp")
             : 0;
}

static bool readback(CF_Canvas canvas, void *data, int bytes)
{
    CF_Readback read = cf_canvas_readback(canvas);
    if (!read.id) {
        return false;
    }
    while (!cf_readback_ready(read)) {
        cf_sleep(1);
    }
    bool read_all = cf_readback_data(read, data, bytes) == bytes;
    cf_destroy_readback(read);
    return read_all;
}

static float half(uint16_t value)
{
    int exponent = (value >> 10) & 31, mantissa = value & 1023;
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

/* A box at the origin filling the view with the material. */
static render_material_entity box(int material)
{
    render_material_entity entity = { .inv_rot = { 1, 0, 0, 1 },
                                      .extent = { SIDE * .5f, SIDE * .5f },
                                      .shape_type = RENDER_SHAPE_BOX,
                                      .aa = .5f,
                                      .reserved = { (float)material } };
    entity.points[0] = entity.points[1] = SIDE * .5f;
    return entity;
}

/* A draw shader colors its box with the ramp. */
TEST_CASE(draw_shader_ramps_read_their_whole_table)
{
    int ramp = register_ramps();
    REQUIRE(ramp);
    render_material_entity entity = box(ramp);
    render_programs *programs = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    shader_error error = { 0 };
    int program = render_shader_fragment_prepare(programs, &entity, &error);
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    if (program) {
        render_shader_fragment_draw(programs, program, &entity, (CF_V2){ 1, 0 });
    }
    cf_render_to(canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    static CF_Pixel pixels[PIXELS];
    bool read = readback(canvas, pixels, (int)sizeof pixels);
    cf_destroy_canvas(canvas);
    render_programs_free(programs);
    REQUIRE(program && read);
    CF_Pixel shown = pixels[SIDE / 2 * SIDE + SIDE / 2];
    int values[] = { shown.colors.r, shown.colors.g, shown.colors.b };
    for (int c = 0; c < 3; c++) {
        REQUIRE(abs(values[c] - (int)lroundf(mixed[c] * 255)) <= 2);
    }
    return true;
}

/* The raster writes the ramp's premultiplied color to its scene target. */
TEST_CASE(raster_ramps_read_their_whole_table)
{
    int ramp = register_ramps();
    REQUIRE(ramp);
    render_material_entity entity = box(ramp);
    render_material_raster *raster = render_material_raster_new();
    bool resized = render_material_raster_resize(raster, SIDE, SIDE);
    cf_app_update(NULL);
    render_material_view view = { .min_x = -SIDE * .5f,
                                  .max_y = SIDE * .5f,
                                  .width = SIDE,
                                  .height = SIDE,
                                  .crop_max_u = 1,
                                  .crop_max_v = 1 };
    bool drawn = resized && render_material_raster_draw(raster, &entity, 1, view);
    cf_app_draw_onto_screen(false);
    static uint16_t pixels[PIXELS * 4];
    bool read = false;
    if (drawn) {
        CF_CanvasParams params = cf_canvas_defaults(0, 0);
        params.attach_target = render_material_raster_textures(raster).scene;
        CF_Canvas canvas = cf_make_canvas(params);
        read = readback(canvas, pixels, (int)sizeof pixels);
        cf_destroy_canvas(canvas);
    }
    render_material_raster_free(raster);
    REQUIRE(read);
    const uint16_t *shown = &pixels[(SIDE / 2 * SIDE + SIDE / 2) * 4];
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(half(shown[c]) - mixed[c]) < .01f);
    }
    return true;
}

/* Every kind of program compiles with the ramp table. A compile cannot tell
 * whether the table's binding collides with another, which only drawing shows. */
TEST_CASE(every_program_kind_compiles_ramps)
{
    int ramp = register_ramps();
    REQUIRE(ramp);
    render_shader_set set = { 0 };
    shader_error error = { 0 };
    REQUIRE(render_shader_set_add(&set, ramp, &error));
    for (int kind = RENDER_PROGRAM_SCENE3D; kind <= RENDER_PROGRAM_MESH_MATERIAL;
         kind++) {
        render_programs *programs = render_programs_new(kind);
        render_programs_set_model_preview(programs, 0, -1);
        bool compiled = render_programs_prepare(programs, &set) &&
                        !render_programs_error(programs)[0];
        if (!compiled) {
            fprintf(stderr, "Program kind %d: %s\n", kind, render_programs_error(programs));
        }
        render_programs_free(programs);
        REQUIRE(compiled);
    }
    return true;
}

/* A later raster batch samples its images, the previous batch's five targets
 * and the ramp table. GLES3 binds the raster's four storage buffers among a
 * fragment shader's 16 samplers, so seven images and a ramp need 17 there and
 * the raster splits the batch. */
TEST_CASE(ramp_tables_count_among_program_samplers)
{
    render_shader_set set = { 0 };
    for (int image = 1; image <= 7; image++) {
        REQUIRE(render_shader_set_add_image(&set, image, NULL));
    }
    REQUIRE(render_program_samplers(RENDER_PROGRAM_RASTER_ACCUMULATE, &set) == 12);
    REQUIRE(register_ramps());
    REQUIRE(render_program_samplers(RENDER_PROGRAM_RASTER_ACCUMULATE, &set) == 13);
    REQUIRE(render_program_samplers(RENDER_PROGRAM_RASTER, &set) == 8);
    REQUIRE(render_program_samplers(RENDER_PROGRAM_SCENE3D, &set) == 15);
    /* While global illumination is on, the lit pass also reads the probes'
     * atlas: 16, the most SDL binds. */
    REQUIRE(render_program_samplers(RENDER_PROGRAM_SCENE3D_GI, &set) == 16);
    /* Shadow programs read the parameters, images and ramps, but no maps. */
    REQUIRE(render_program_samplers(RENDER_PROGRAM_SCENE3D_SHADOW, &set) == 9);
    /* The lit pass's widest set fills GLES3's 16 samplers exactly, leaving
     * no room for the probes. */
    REQUIRE(render_shader_set_add_image(&set, 8, NULL));
    REQUIRE(set.count == RENDER_SHADER_IMAGE_MAX);
    REQUIRE(render_program_samplers(RENDER_PROGRAM_SCENE3D, &set) == 16);
    REQUIRE(render_program_samplers(RENDER_PROGRAM_SCENE3D_GI, &set) == 16);
    return true;
}

void test_render_material_ramps()
{
    setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_Result made = cf_make_app(
        "material-ramps",
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
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(draw_shader_ramps_read_their_whole_table);
    RUN_TEST_CASE(raster_ramps_read_their_whole_table);
    RUN_TEST_CASE(every_program_kind_compiles_ramps);
    RUN_TEST_CASE(ramp_tables_count_among_program_samplers);
    pu_clear_setup();
    cf_destroy_app();
}
