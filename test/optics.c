#include "render/optics.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define W 256
#define H 144

static bool read_field(CF_Texture texture, float scale, float *pixels)
{
    CF_CanvasParams p = cf_canvas_defaults(0, 0);
    p.attach_target = texture;
    CF_Canvas c = cf_make_canvas(p);
    if (!c.id) {
        return false;
    }
    CF_Readback r = cf_canvas_readback(c);
    bool ok = r.id != 0;
    if (ok) {
        while (!cf_readback_ready(r)) {
        }
        int bytes = W * H * 4 * (int)sizeof(float);
        if (cf_readback_size(r) == bytes / 2) {
            uint16_t *half = malloc(bytes / 2);
            ok = cf_readback_data(r, half, bytes / 2) == bytes / 2;
            for (int i = 0; ok && i < W * H * 4; i++) {
                int e = (half[i] >> 10) & 31, m = half[i] & 1023;
                ok &= !(half[i] & 0x8000) && e != 31;
                float value = e ? ldexpf(1 + m / 1024.0f, e - 15)
                                : ldexpf((float)m, -24);
                pixels[i] = value * (i % 4 == 3 ? 1 : scale);
            }
            free(half);
        } else {
            ok = cf_readback_size(r) == bytes &&
                 cf_readback_data(r, pixels, bytes) == bytes;
        }
        cf_destroy_readback(r);
    }
    cf_destroy_canvas(c);
    return ok;
}

static double column(const float *pixels, int x)
{
    double sum = 0;
    for (int y = 0; y < H; y++) {
        sum += pixels[(y * W + x) * 4];
    }
    return sum * 1.125 / H;
}

bool test_optics()
{
    render_optics *r = render_optics_new();
    float *pixels = malloc(W * H * 4 * sizeof(float));
    render_view_2d view = { .min_x = 0,
                            .max_y = 0,
                            .width = 2,
                            .height = 1.125,
                            .crop_max_u = 1,
                            .crop_max_v = 1 };
    render_optical_budget budget = { W, 2048, 4 };
    render_optical_light light = { .position = { .06f, .5625f },
                                   .direction = { 1, 0 },
                                   .color = { 1, 1, 1 },
                                   .power = 1,
                                   .width = .4f };
    CF_TextureParams tp = cf_texture_defaults(1, 1);
    tp.pixel_format = CF_PIXEL_FORMAT_R32_FLOAT;
    tp.stream = true;
    CF_Texture medium = cf_make_texture(tp);
    float tau = .2f * 2 / W;
    cf_texture_update(medium, &tau, sizeof tau);
    cf_app_update(NULL);
    bool ok = render_optics_draw(
        r,
        NULL,
        0,
        &light,
        1,
        budget,
        medium,
        (CF_Color){ .8f, .8f, .8f, 1 },
        view,
        W,
        H
    );
    cf_app_draw_onto_screen(false);
    if (ok) {
        ok = read_field(render_optics_field(r).direct, render_optics_field(r).display_scale, pixels);
    }
    if (ok) {
        double got = column(pixels, 192),
               expected = exp(-.2 * ((192.5 * 2 / W) - .06)) / (2 * 3.14159265359);
        printf("optics homogeneous transmission: %.6f expected %.6f\n", got, expected);
        ok &= fabs(got / expected - 1) < .005;
    }
    if (ok) {
        ok = read_field(render_optics_field(r).source, 1, pixels);
    }
    if (ok) {
        double got = 0;
        for (int i = 0; i < W * H; i++) {
            got += pixels[i * 4];
        }
        got *= (2.0 / W) * (1.125 / H) * 2 * 3.14159265359;
        double expected = .8 * (1 - exp(-.2 * 1.94));
        printf("optics scattered power: %.6f expected %.6f\n", got, expected);
        ok &= fabs(got / expected - 1) < .005;
    }
    render_material_entity lens = { .inv_rot = { 1, 0, 0, 1 },
                                    .pos = { .8f, -.5625f },
                                    .extent = { .23f, .23f },
                                    .radius = .23f,
                                    .shape_type = RENDER_SHAPE_CIRCLE,
                                    .optical = { 1.5f, 0, .15f, .05f, .01f } };
    cf_app_update(NULL);
    ok &= render_optics_draw(r, &lens, 1, &light, 1, budget, (CF_Texture){ 0 }, (CF_Color){ 0 }, view, W, H);
    cf_app_draw_onto_screen(false);
    if (ok) {
        ok = read_field(render_optics_field(r).direct, render_optics_field(r).display_scale, pixels);
    }
    if (ok) {
        // Independent double-precision reference (2048 paths, four interfaces):
        // 0.589753 concentration and 0.149880 column radiance at x = 1.22.
        double sum = 0, narrow = 0;
        int x = (int)(1.22 / 2 * W);
        for (int y = 0; y < H; y++) {
            double v = pixels[(y * W + x) * 4];
            sum += v;
            if (fabs((y + .5) * 1.125 / H - .5625) < .05) {
                narrow += v;
            }
        }
        printf(
            "optics lens: focal concentration %.4f, transmitted flux %.4f\n",
            narrow / sum,
            column(pixels, x)
        );
        ok &= sum > 0 && fabs(narrow / sum - .589753) < .002 &&
              fabs(column(pixels, x) - .149880) < .0005;
    }
    /* A vertical mirror reverses the beam: no light may survive behind it.
     * Off-path objects exercise the production BVH; moving it invalidates the cache. */
    render_material_entity obstacles[65] = { 0 };
    for (int i = 0; i < 65; i++) {
        obstacles[i] = (render_material_entity){
            .inv_rot = { 1, 0, 0, 1 },
            .pos = { .4f + (i % 16) * .08f, -.03f - (i / 16) * .025f },
            .extent = { .01f, .01f },
            .radius = .01f,
            .absorption = { 8, 8, 8, 1 },
            .shape_type = RENDER_SHAPE_CIRCLE
        };
    }
    obstacles[0] = (render_material_entity){ .inv_rot = { 1, 0, 0, 1 },
                                             .pos = { 1, -.5625f },
                                             .extent = { .02f, .45f },
                                             .points = { .02f, .45f },
                                             .shape_type = RENDER_SHAPE_BOX,
                                             .optical = { 1, 1 },
                                             .albedo = { .8f, .6f, .4f, 1 } };
    cf_app_update(NULL);
    ok &= render_optics_draw(r, obstacles, 65, &light, 1, budget, (CF_Texture){ 0 }, (CF_Color){ 0 }, view, W, H);
    cf_app_draw_onto_screen(false);
    if (ok) {
        ok = read_field(render_optics_field(r).direct, render_optics_field(r).display_scale, pixels);
    }
    if (ok) {
        double before = column(pixels, 80), behind = column(pixels, 160);
        printf("optics mirror: incident plus reflected %.6f, behind %.6f\n", before, behind);
        ok &= fabs(before - 1.8 / (2 * 3.14159265359)) < .001 && behind < 1e-6;
    }
    obstacles[0].pos[1] = -2;
    cf_app_update(NULL);
    ok &= render_optics_draw(r, obstacles, 65, &light, 1, budget, (CF_Texture){ 0 }, (CF_Color){ 0 }, view, W, H);
    cf_app_draw_onto_screen(false);
    if (ok) {
        ok = read_field(render_optics_field(r).direct, render_optics_field(r).display_scale, pixels);
    }
    if (ok) {
        ok &= fabs(column(pixels, 160) - 1 / (2 * 3.14159265359)) < .001;
    }
    /* An opaque diffuse receiver returns its albedo-weighted energy to HRC.
     * The isotropic grid source has twice the flux: the wall absorbs its inward half. */
    obstacles[0].pos[1] = -.5625f;
    obstacles[0].optical[1] = 0;
    cf_app_update(NULL);
    ok &= render_optics_draw(r, obstacles, 1, &light, 1, budget, (CF_Texture){ 0 }, (CF_Color){ 0 }, view, W, H);
    cf_app_draw_onto_screen(false);
    if (ok) {
        ok = read_field(render_optics_field(r).source, 1, pixels);
    }
    if (ok) {
        double energy = 0;
        for (int i = 0; i < W * H; i++) {
            energy += pixels[i * 4];
        }
        energy *= (2.0 / W) * (1.125 / H) * 3.14159265359;
        printf("optics diffuse receiver power %.6f expected .8\n", energy);
        ok &= fabs(energy - .8) < .003;
    }
    /* Display normalization must preserve bright and dim light.
     * Reversing power after the bright case catches stale normalization. */
    float powers[] = { 1, 1e6f, .001f, 1 };
    float *reference = malloc(W * H * 4 * sizeof(float));
    for (int field = 0; field < 2; field++) {
        for (int i = 0; ok && i < 4; i++) {
            light.power = powers[i];
            cf_app_update(NULL);
            ok &= render_optics_draw(
                r,
                obstacles,
                1,
                &light,
                1,
                budget,
                medium,
                (CF_Color){ .8f, .8f, .8f, 1 },
                view,
                W,
                H
            );
            cf_app_draw_onto_screen(false);
            render_optical_field output = render_optics_field(r);
            ok &= read_field(field ? output.surface : output.direct, output.display_scale, pixels);
            double error = 0, energy = 0;
            for (int k = 0; ok && k < W * H * 4; k++) {
                if (k % 4 == 3) {
                    continue;
                }
                if (i == 0) {
                    reference[k] = pixels[k];
                }
                double expected = reference[k] * (double)powers[i];
                error += fabs(pixels[k] - expected);
                energy += expected;
            }
            ok &= energy > 0 && error / energy < .001;
            printf(
                "optics display range field=%d power=%g: relative error %.6f\n",
                field,
                powers[i],
                energy > 0 ? error / energy : 1
            );
        }
    }
    free(reference);
    light.power = 0;
    cf_app_update(NULL);
    ok &= render_optics_draw(r, &lens, 1, &light, 1, budget, medium, (CF_Color){ .8f, .8f, .8f, 1 }, view, W, H);
    cf_app_draw_onto_screen(false);
    if (ok) {
        ok = read_field(render_optics_field(r).direct, render_optics_field(r).display_scale, pixels);
    }
    if (ok) {
        for (int i = 0; i < W * H * 4; i++) {
            if (i % 4 != 3) {
                ok &= pixels[i] == 0;
            }
        }
    }
    if (!ok) {
        fprintf(stderr, "optics failed: %s\n", render_optics_error(r));
    }
    render_optics_free(r);
    cf_destroy_texture(medium);
    free(pixels);
    printf(
        "optics %s: transmission, scattered power, lens focus, mirror, moving "
        "BVH, diffuse source, HDR display range and light removal\n",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}
