#include "platform/cute.h"
#include "render/material_raster.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    int mantissa = value & 1023;
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

/* Reads one RGBA16F material target's pixel as floats. */
static bool texel(CF_Texture texture, int x, int y, float out[4])
{
    static uint16_t pixels[PIXELS * 4];
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = canvas.id ? cf_canvas_readback(canvas)
                                     : (CF_Readback){ 0 };
    bool ok = readback.id != 0;
    if (ok) {
        while (!cf_readback_ready(readback)) {
        }
        int bytes = (int)sizeof pixels;
        ok = cf_readback_size(readback) == bytes &&
             cf_readback_data(readback, pixels, bytes) == bytes;
        cf_destroy_readback(readback);
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    for (int c = 0; ok && c < 4; c++) {
        out[c] = half_value(pixels[4 * (y * SIDE + x) + c]);
    }
    return ok;
}

static render_material_entity block()
{
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 18, 18 },
        .shape_type = RENDER_SHAPE_BOX,
        .aa = 1,
        .points = { 16, 16 },
        .scene = { .1f, .2f, .6f, 1 },
        .albedo = { .1f, .2f, .6f, 1 },
        .absorption = { 1, 1, 1, 1 },
    };
    return entity;
}

static bool raster(render_material_raster *r, const render_material_entity *entities, int count)
{
    render_material_view view = { .min_x = -32, .max_y = 32, .width = SIDE, .height = SIDE };
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(r, entities, count, view);
    cf_app_draw_onto_screen(false);
    return ok;
}

/* A flashed solid turns toward the flash color and emits it. */
static bool hit_flash(render_material_raster *r)
{
    render_material_entity plain = block(), flashed = block();
    flashed.flash[0] = 1;
    flashed.flash[1] = flashed.flash[2] = .9f;
    flashed.flash[3] = .75f;
    float scene[4], source[4], lit[4], glow[4];
    render_material_textures textures = render_material_raster_textures(r);
    bool ok = raster(r, &plain, 1) && texel(textures.scene, 32, 32, scene) &&
              texel(textures.source, 32, 32, source);
    ok = ok && raster(r, &flashed, 1) && texel(textures.scene, 32, 32, lit) &&
         texel(textures.source, 32, 32, glow);
    printf(
        "material hit flash: scene %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f; emission "
        "%.3f -> %.3f\n",
        scene[0],
        scene[1],
        scene[2],
        lit[0],
        lit[1],
        lit[2],
        source[0],
        glow[0]
    );
    return ok && fabsf(lit[0] - .775f) < .01f && fabsf(lit[2] - .825f) < .01f &&
           source[0] == 0 && glow[0] > .1f;
}

bool test_material_effects()
{
    render_material_raster *r = render_material_raster_new();
    bool ok = r && render_material_raster_resize(r, SIDE, SIDE) && hit_flash(r);
    render_material_raster_free(r);
    printf("material effects %s: hit flash\n", ok ? "PASS" : "FAIL");
    return ok;
}
