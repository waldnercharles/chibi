#include "../render/radiance.h"

#include "../render/material_raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

static bool read_pixels(CF_Canvas canvas, CF_Pixel *pixels, int count)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return false;
    }
    while (!cf_readback_ready(readback)) {
    }
    int size = count * (int)sizeof *pixels;
    bool ok = cf_readback_size(readback) == size &&
              cf_readback_data(readback, pixels, size) == size;
    cf_destroy_readback(readback);
    return ok;
}

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    int mantissa = value & 1023;
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

static bool material_pixels(render_material_textures textures)
{
    CF_Texture inputs[] = { textures.emission, textures.transmittance };
    uint16_t pixels[PIXELS * 4];
    for (int i = 0; i < 2; i++) {
        CF_CanvasParams params = cf_canvas_defaults(0, 0);
        params.attach_target = inputs[i];
        CF_Canvas canvas = cf_make_canvas(params);
        if (!canvas.id) {
            return false;
        }
        CF_Readback readback = cf_canvas_readback(canvas);
        bool ok = readback.id != 0;
        if (ok) {
            while (!cf_readback_ready(readback)) {
            }
            int size = (int)sizeof pixels;
            ok = cf_readback_size(readback) == size &&
                 cf_readback_data(readback, pixels, size) == size;
            cf_destroy_readback(readback);
        }
        cf_destroy_canvas(canvas);
        if (!ok) {
            return false;
        }
        int sample = 4 * (64 * SIDE + (i ? 64 : 36));
        if (i) {
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(half_value(pixels[sample + channel]) - 1) > 0.002f) {
                    return false;
                }
            }
        } else {
            if (fabsf(half_value(pixels[sample + 1]) - 0.08f) > 0.002f) {
                return false;
            }
            sample = 4 * (64 * SIDE + 43);
            if (fabsf(half_value(pixels[sample]) - 0.2f) > 0.002f) {
                return false;
            }
        }
    }
    return true;
}

static render_material_entity emitter()
{
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .pos = { -28, 0 },
        .extent = { 10, 10 },
        .shape_type = RENDER_SHAPE_CIRCLE,
        .radius = 8,
        .aa = 1,
        .border_thickness = 2,
        .scene = { 0, 1, 0, 1 },
        .emission = { 0, 0.08f, 0, 1 },
        .border_scene = { 1, 0, 0, 1 },
        .border_emission = { 0.2f, 0, 0, 1 },
    };
    return entity;
}

static render_material_entity wall()
{
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 5, 66 },
        .shape_type = RENDER_SHAPE_BOX,
        .aa = 1,
        .points = { 4, 64 },
        .scene = { 0, 0, 0, 1 },
        .transmittance = { 1, 1, 1, 1 },
    };
    return entity;
}

static bool solve(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    int size,
    CF_Pixel *pixels
)
{
    render_radiance_reset(radiance);
    for (int frame = 0; frame < 4; frame++) {
        cf_app_update(NULL);
        if (!render_material_raster_draw(
                raster,
                entities,
                count,
                (render_material_view){ -size * 0.5f, size * 0.5f, size, size }
            )) {
            cf_app_draw_onto_screen(false);
            return false;
        }
        render_material_textures textures = render_material_raster_textures(raster);
        render_radiance_inputs inputs = { textures.scene,
                                          textures.emission,
                                          textures.transmittance,
                                          textures.albedo };
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, size, size));
        bool ok = render_radiance_draw(radiance, inputs, canvas);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        if (!ok) {
            return false;
        }
    }
    return read_pixels(canvas, pixels, size * size);
}

static double receiver_light(const CF_Pixel *pixels)
{
    double sum = 0;
    for (int y = 52; y < 76; y++) {
        for (int x = 84; x < 100; x++) {
            CF_Pixel pixel = pixels[y * SIDE + x];
            sum += pixel.colors.r + pixel.colors.g + pixel.colors.b;
        }
    }
    return sum / (24 * 16);
}

static bool save_capture(const CF_Pixel *pixels)
{
    const char *path = getenv("CBLUEPRINT_RADIANCE_SCREENSHOT");
    if (!path) {
        return true;
    }
    CF_Image image = { SIDE, SIDE, (CF_Pixel *)pixels };
    void *data = NULL;
    int size = 0;
    CF_Result result = cf_image_save_png_to_memory(&image, &data, &size);
    if (cf_is_error(result)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    bool ok = file && fwrite(data, 1, (size_t)size, file) == (size_t)size;
    if (file) {
        fclose(file);
    }
    cf_free(data);
    return ok;
}

/* The host initializes Cute and mounts/configures render/shaders before this call. */
int test_radiance()
{
    render_material_raster *raster = render_material_raster_new();
    render_radiance *radiance = render_radiance_new();
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    bool ok = raster && radiance && canvas.id;
    CF_Pixel open[PIXELS], blocked[PIXELS], reset[PIXELS];
    render_material_entity entities[] = { emitter(), wall() };
    if (ok) {
        ok = render_material_raster_resize(raster, SIDE, SIDE) &&
             render_radiance_resize(radiance, SIDE, SIDE);
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 1, SIDE, open);
    }
    if (ok) {
        CF_Pixel center = open[64 * SIDE + 36];
        CF_Pixel border = open[64 * SIDE + 43];
        ok = center.colors.g > center.colors.r && center.colors.g > 100 &&
             border.colors.r > border.colors.g;
        if (!ok) {
            fprintf(
                stderr,
                "radiance emitter/border: center %u,%u border %u,%u\n",
                center.colors.r,
                center.colors.g,
                border.colors.r,
                border.colors.g
            );
        }
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 2, SIDE, blocked);
    }
    if (ok) {
        ok = material_pixels(render_material_raster_textures(raster));
        if (!ok) {
            fprintf(stderr, "radiance source material emission/border/absorption mismatch\n");
        }
    }
    if (ok) {
        double before = receiver_light(open), after = receiver_light(blocked);
        printf("radiance receiver: open %.3f, occluded %.3f\n", before, after);
        ok = before > 3 && after < before * 0.8;
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 1, SIDE, reset);
    }
    if (ok) {
        for (int i = 0; i < PIXELS; i++) {
            const unsigned char *a = (const unsigned char *)&open[i];
            const unsigned char *b = (const unsigned char *)&reset[i];
            for (int c = 0; c < 4; c++) {
                if (abs(a[c] - b[c]) > 2) {
                    ok = false;
                }
            }
        }
        if (!ok) {
            fprintf(stderr, "radiance reset retained occluder history\n");
        }
    }
    if (ok) {
        ok = save_capture(blocked);
    }
    if (ok) {
        cf_destroy_canvas(canvas);
        canvas = cf_make_canvas(cf_canvas_defaults(64, 64));
        ok = canvas.id && render_material_raster_resize(raster, 64, 64) &&
             render_radiance_resize(radiance, 64, 64) &&
             solve(raster, radiance, canvas, NULL, 0, 64, reset);
        if (ok) {
            for (int i = 0; i < 64 * 64; i++) {
                if (reset[i].colors.r > 2 || reset[i].colors.g > 2 ||
                    reset[i].colors.b > 2) {
                    ok = false;
                }
            }
        }
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    if (raster) {
        render_material_raster_free(raster);
    }
    printf("radiance %s: emissive fill/border, light transport, occlusion, reset and resize\n", ok ? "PASS" : "FAIL");
    return !ok;
}
