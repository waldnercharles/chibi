#include "platform/cute.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static float half_float(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    float fraction = value & 1023;
    float result = exponent ? ldexpf(1 + fraction / 1024, exponent - 15)
                            : ldexpf(fraction, -24);
    return value & 0x8000 ? -result : result;
}

bool test_radiance_history()
{
    enum
    {
        W = 16,
        H = 8
    };
    float current[W * H][4], history[W * H][4], absorption[W * H][4] = { 0 };
    CF_Texture textures[3];
    for (int i = 0; i < 3; i++) {
        CF_TextureParams params = cf_texture_defaults(W, H);
        params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        params.filter = CF_FILTER_LINEAR;
        params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        textures[i] = cf_make_texture(params);
    }
    CF_CanvasParams params = cf_canvas_defaults(W, H);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Material material = cf_make_material();
    CF_ComputeShader shader = cf_make_compute_shader("hrc/blur.shd");
    bool ok = canvas.id && material.id && shader.id;
    for (int i = 0; i < 3; i++) {
        ok &= textures[i].id != 0;
    }
    for (int scenario = 0; ok && scenario < 3; scenario++) {
        for (int i = 0; i < W * H; i++) {
            for (int c = 0; c < 4; c++) {
                current[i][c] = scenario == 0 ? .1f + .02f * (i % W) : .2f;
                history[i][c] = scenario == 0 ? .1f + .02f * (i % W - .5f) : 10;
            }
        }
        cf_app_update(NULL);
        cf_texture_update(textures[0], current, sizeof current);
        cf_texture_update(textures[1], absorption, sizeof absorption);
        cf_texture_update(textures[2], history, sizeof history);
        cf_material_set_texture_cs(material, "u_in_radiance", textures[0]);
        cf_material_set_texture_cs(material, "u_transmittance", textures[1]);
        cf_material_set_texture_cs(material, "u_prev_radiance", textures[2]);
        float texel[] = { 1.f / W, 1.f / H };
        float offset[] = { scenario == 0 ? .5f / W : 0, 0 };
        float threshold = .1f, weight = .1f;
        int valid = scenario != 2;
        cf_material_set_uniform_cs(material, "u_texel_size", texel, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_cs(material, "u_history_offset", offset, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_cs(material, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
        cf_material_set_uniform_cs(material, "u_threshold", &threshold, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_cs(material, "u_hysteresis", &weight, CF_UNIFORM_TYPE_FLOAT, 1);
        CF_Texture target = cf_canvas_get_target(canvas);
        CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
        dispatch.rw_textures = &target;
        dispatch.rw_texture_count = 1;
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        CF_Readback readback = cf_canvas_readback(canvas);
        uint16_t pixels[W * H][4];
        ok = readback.id != 0;
        if (ok) {
            uint64_t start = cf_get_ticks();
            while (!cf_readback_ready(readback) &&
                   cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
                cf_sleep(1);
            }
            ok = cf_readback_ready(readback) &&
                 cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
            cf_destroy_readback(readback);
        }
        float maximum = 0;
        for (int y = 1; ok && y < H - 1; y++) {
            for (int x = 1; x < W - 1; x++) {
                int i = y * W + x;
                maximum = fmaxf(maximum, fabsf(half_float(pixels[i][0]) - current[i][0]));
            }
        }
        printf("radiance history scenario %d: max error %.6f\n", scenario, maximum);
        ok &= maximum < .0005f;
    }
    for (int i = 0; i < 3; i++) {
        if (textures[i].id) {
            cf_destroy_texture(textures[i]);
        }
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (material.id) {
        cf_destroy_material(material);
    }
    if (shader.id) {
        cf_destroy_compute_shader(shader);
    }
    return ok;
}
