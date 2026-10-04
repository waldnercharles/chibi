#include "platform/cute.h"
#include "render/radiance.h"
#include "render/scene_renderer.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SIDE = 32,
    PIXELS = SIDE * SIDE,
    INPUTS = 6
};
enum
{
    SCENE,
    SOURCE,
    ALBEDO,
    NORMAL,
    COVER,
    VISIBILITY
};
static float half(uint16_t v)
{
    int e = (v >> 10) & 31, m = v & 1023;
    float f = e == 31 ? (m ? NAN : INFINITY)
            : e       ? ldexpf(1 + m / 1024.0f, e - 15)
                      : ldexpf((float)m, -24);
    return v & 0x8000 ? -f : f;
}
static bool read_center(CF_Canvas output, float color[4])
{
    CF_Readback read = cf_canvas_readback(output);
    uint64_t start = cf_get_ticks();
    while (read.id && !cf_readback_ready(read) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    uint16_t pixels[PIXELS][4];
    bool ok = read.id && cf_readback_ready(read) &&
              cf_readback_size(read) == sizeof pixels &&
              cf_readback_data(read, pixels, sizeof pixels) == sizeof pixels;
    for (int p = 0; p < PIXELS && ok; p++) {
        for (int c = 0; c < 4; c++) {
            ok &= isfinite(half(pixels[p][c]));
        }
    }
    for (int c = 0; c < 4 && ok; c++) {
        color[c] = half(pixels[(SIDE / 2) * SIDE + SIDE / 2][c]);
    }
    if (read.id) {
        cf_destroy_readback(read);
    }
    return ok;
}
/* A fully opaque receiver has no planar open-air field to blur into its
 * interior. An emitter above it must travel through depth to illuminate it. */
static void inputs(float data[INPUTS][PIXELS][4], int scenario)
{
    memset(data, 0, sizeof(float) * INPUTS * PIXELS * 4);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int p = y * SIDE + x;
            data[SCENE][p][3] = 1;
            data[SOURCE][p][3] = 20;
            data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = 1;
            data[NORMAL][p][2] = 1;
            data[NORMAL][p][3] = .5f;
            data[COVER][p][0] = data[COVER][p][1] = data[COVER][p][2] = 1;
            if (x >= 24 && y >= 8 && y <= 24) {
                data[ALBEDO][p][3] = 12;
                // Match the nonabsorbing production sphere emitters. Geometry
                // coverage and transport absorption are independent inputs.
                data[SOURCE][p][3] = 0;
                if (scenario != 4) {
                    data[SOURCE][p][0] = 4;
                }
            }
            if (x >= 20 && x <= 23) {
                // Thickness is twelve: [0,12] crosses the ascending light path;
                // [-16,-4] lies below it, and [12,24] lies entirely above it.
                if (scenario == 1 || scenario == 6) {
                    data[ALBEDO][p][3] = 12;
                }
                if (scenario == 6) {
                    data[SOURCE][p][3] = 0;
                }
                if (scenario == 2) {
                    data[ALBEDO][p][3] = -4;
                }
                if (scenario == 3) {
                    data[ALBEDO][p][3] = 24;
                }
            }
            // Keep orientation uniform over the whole receiver, so spatial
            // filtering cannot mix an arbitrary tilted/flat fixture boundary.
            if (scenario == 5 && !(x >= 24 && y >= 8 && y <= 24)) {
                data[NORMAL][p][0] = .6f;
            }
        }
    }
}
/* Equal integrated emission at fractional positions avoids tying a small
 * source to the gather's sample centers or changing its power with its size. */
static void small_source(float data[INPUTS][PIXELS][4], float x, float y, float width)
{
    inputs(data, 4);
    for (int p = 0; p < PIXELS; p++) {
        data[ALBEDO][p][3] = 0;
        data[SOURCE][p][3] = 20;
    }
    for (int py = 0; py < SIDE; py++) {
        float overlap_y = fmaxf(0, fminf(py + 1, y + width * .5f) - fmaxf(py, y - width * .5f));
        for (int px = 0; px < SIDE; px++) {
            float overlap_x = fmaxf(0, fminf(px + 1, x + width * .5f) - fmaxf(px, x - width * .5f));
            float area = overlap_x * overlap_y;
            if (area <= 0) {
                continue;
            }
            int p = py * SIDE + px;
            data[SOURCE][p][0] = 16 * area / (width * width);
            data[SOURCE][p][3] = 0;
            data[ALBEDO][p][3] = 12;
        }
    }
}
static bool draw(
    render_radiance *r,
    CF_Canvas output,
    CF_Texture textures[INPUTS],
    float data[INPUTS][PIXELS][4],
    GameHRC2D lighting,
    render_view_2d view,
    float color[4],
    render_surface_work *work
)
{
    if (!render_radiance_set_lighting(r, lighting)) {
        return false;
    }
    render_radiance_inputs input = { .material = { .scene = textures[SCENE],
                                                   .source = textures[SOURCE],
                                                   .albedo = textures[ALBEDO],
                                                   .normal = textures[NORMAL],
                                                   .cover = textures[COVER],
                                                   .visibility = textures[VISIBILITY] } };
    cf_app_update(NULL);
    for (int i = 0; i < INPUTS; i++) {
        cf_texture_update(textures[i], data[i], sizeof data[i]);
    }
    bool ok = render_radiance_draw(r, input, view, output);
    cf_app_draw_onto_screen(false);
    *work = render_radiance_surface_work(r);
    return ok && read_center(output, color);
}
bool test_surface_depth()
{
    float (*data)[PIXELS][4] = xmalloc(sizeof(float) * INPUTS * PIXELS * 4);
    CF_Texture textures[INPUTS] = { 0 };
    for (int i = 0; i < INPUTS; i++) {
        CF_TextureParams p = cf_texture_defaults(SIDE, SIDE);
        p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        p.filter = CF_FILTER_NEAREST;
        p.wrap_u = p.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        textures[i] = cf_make_texture(p);
    }
    CF_CanvasParams p = cf_canvas_defaults(SIDE, SIDE);
    p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas output = cf_make_canvas(p);
    render_radiance *r = render_radiance_new();
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.resolution = lighting.color_resolution = 1;
    lighting.radiance_hysteresis = 1;
    lighting.ambient = lighting.fog = lighting.shadow_floor = 0;
    lighting.surface_radius = 16;
    lighting.surface_relief = 1;
    lighting.surface_depth_radius = 16;
    lighting.surface_thickness = 12;
    lighting.debug_view = GAME_HRC_DEBUG_SURFACE_LIGHT;
    lighting.surface_mode = GAME_HRC_SURFACE_BLUR;
    render_view_2d view = { -.5f * SIDE, .5f * SIDE,      SIDE,
                            SIDE,        .crop_max_u = 1, .crop_max_v = 1 };
    bool ok = r && output.id && render_radiance_resize(r, SIDE, SIDE);
    for (int i = 0; i < INPUTS; i++) {
        ok &= textures[i].id != 0;
    }
    float off[4] = { 0 }, open[4] = { 0 }, restored[4] = { 0 }, cases[7][4] = { 0 };
    render_surface_work work = { 0 };
    inputs(data, 0);
    ok = draw(r, output, textures, data, lighting, view, off, &work) && ok;
    lighting.surface_mode = GAME_HRC_SURFACE_DEPTH;
    ok = draw(r, output, textures, data, lighting, view, open, &work) && ok;
    bool budget = work.mode == GAME_HRC_SURFACE_DEPTH && work.block == 4 &&
                  work.cells == 64 && work.candidates == (size_t)work.cells * 81 &&
                  work.visibility_taps == (size_t)work.cells * 96 &&
                  !work.blur_quadrant_fetches;
    printf(
        "Surface depth sample budget: %s; cells %d block %d candidates %zu "
        "visibility bound %zu\n",
        budget ? "PASS" : "FAIL",
        work.cells,
        work.block,
        work.candidates,
        work.visibility_taps
    );
    ok &= budget && fabsf(off[0]) < .001f && open[0] > .001f;
    for (int scenario = 0; scenario < 7; scenario++) {
        inputs(data, scenario);
        render_radiance_reset(r);
        ok = draw(r, output, textures, data, lighting, view, cases[scenario], &work) &&
             ok;
    }
    bool slab = cases[0][0] > .001f && cases[1][0] < cases[0][0] * .35f &&
                cases[2][0] > cases[0][0] * .8f &&
                cases[3][0] > cases[0][0] * .8f && fabsf(cases[4][0]) < .00001f;
    bool normal = cases[5][0] > cases[0][0] * 1.05f;
    bool transparent = cases[6][0] > cases[0][0] * .8f &&
                       cases[6][0] < cases[0][0] * 1.2f;
    printf(
        "Surface depth finite slabs: %s; open %g crossing %g below %g above %g "
        "no emitter %g\n",
        slab ? "PASS" : "FAIL",
        cases[0][0],
        cases[1][0],
        cases[2][0],
        cases[3][0],
        cases[4][0]
    );
    printf(
        "Surface depth receiver normal: %s; flat %g tilted toward emitter %g\n",
        normal ? "PASS" : "FAIL",
        cases[0][0],
        cases[5][0]
    );
    printf(
        "Surface depth transparent slab: %s; open %g transparent crossing %g\n",
        transparent ? "PASS" : "FAIL",
        cases[0][0],
        cases[6][0]
    );
    ok &= slab && normal && transparent;
    enum
    {
        ANGLES = 24,
        SIZES = 3
    };
    float mean[SIZES] = { 0 }, lowest[SIZES], highest[SIZES] = { 0 };
    bool coverage = true;
    for (int size = 0; size < SIZES; size++) {
        lowest[size] = INFINITY;
        for (int angle = 0; angle < ANGLES; angle++) {
            float theta = angle * (6.28318530718f / ANGLES);
            small_source(
                data,
                SIDE * .5f + .5f + 8 * cosf(theta),
                SIDE * .5f + .5f + 8 * sinf(theta),
                size + 1
            );
            render_radiance_reset(r);
            float color[4] = { 0 };
            bool sampled = draw(r, output, textures, data, lighting, view, color, &work);
            coverage &= sampled && color[0] > .00001f;
            mean[size] += color[0] / ANGLES;
            lowest[size] = fminf(lowest[size], color[0]);
            highest[size] = fmaxf(highest[size], color[0]);
        }
        // Approximate integration may vary, but should not leave dark wedges.
        coverage &= lowest[size] >= highest[size] * .2f;
        printf(
            "Surface depth azimuth coverage, width %d: min %g max %g mean %g\n",
            size + 1,
            lowest[size],
            highest[size],
            mean[size]
        );
    }
    bool energy = mean[0] > .00001f;
    for (int size = 1; size < SIZES; size++) {
        energy &= mean[size] > mean[0] * .5f && mean[size] < mean[0] * 2;
    }
    printf(
        "Surface depth small-source coverage: %s; equal-power sizes: %s\n",
        coverage ? "PASS" : "FAIL",
        energy ? "PASS" : "FAIL"
    );
    ok &= coverage && energy;
    float farther = 0;
    bool falloff = true;
    for (int angle = 0; angle < ANGLES; angle++) {
        float theta = angle * (6.28318530718f / ANGLES);
        small_source(data, SIDE * .5f + .5f + 12 * cosf(theta), SIDE * .5f + .5f + 12 * sinf(theta), 1);
        render_radiance_reset(r);
        float color[4] = { 0 };
        falloff &= draw(r, output, textures, data, lighting, view, color, &work);
        farther += color[0] / ANGLES;
    }
    falloff &= farther > .00001f && farther < mean[0] * .95f;
    printf(
        "Surface depth distance falloff: %s; radius 8 mean %g radius 12 mean "
        "%g\n",
        falloff ? "PASS" : "FAIL",
        mean[0],
        farther
    );
    ok &= falloff;
    float placement_min = INFINITY, placement_max = 0;
    bool placement = true;
    for (int shift = 0; shift < 4; shift++) {
        small_source(data, 24.5f + .5f * (shift & 1), 16.5f + .5f * (shift >> 1), 1);
        render_radiance_reset(r);
        float color[4] = { 0 };
        placement &= draw(r, output, textures, data, lighting, view, color, &work);
        placement_min = fminf(placement_min, color[0]);
        placement_max = fmaxf(placement_max, color[0]);
    }
    placement &= placement_min > .00001f && placement_min >= placement_max * .4f;
    printf(
        "Surface depth fractional source placement: %s; min %g max %g\n",
        placement ? "PASS" : "FAIL",
        placement_min,
        placement_max
    );
    ok &= placement;
    // Move a constant-power source smoothly across the x=24 pyramid boundary.
    // Correct for the expected smooth change in Lambertian distance falloff;
    // large adjacent changes then reveal gather-cell popping rather than motion.
    bool motion = true;
    float previous = 0, motion_min = INFINITY, motion_max = 0, jump_max = 0;
    for (int frame = 0; frame <= 16; frame++) {
        float x = 23 + frame * .125f;
        small_source(data, x, 24.5f, 1);
        render_radiance_reset(r);
        float color[4] = { 0 };
        motion &= draw(r, output, textures, data, lighting, view, color, &work);
        float dx = x - 16.5f;
        float distance2 = dx * dx + 8 * 8 + 12 * 12;
        float normalized = color[0] * (distance2 + 3 * 3) * sqrtf(distance2) / 12;
        motion &= normalized > .00001f;
        motion_min = fminf(motion_min, normalized);
        motion_max = fmaxf(motion_max, normalized);
        if (frame > 0) {
            float jump = fabsf(normalized - previous) /
                         fmaxf((normalized + previous) * .5f, .00001f);
            jump_max = fmaxf(jump_max, jump);
        }
        previous = normalized;
    }
    motion &= jump_max <= .15f && motion_min >= motion_max * .7f;
    printf(
        "Surface depth moving source: %s; normalized min %g max %g adjacent "
        "jump %g\n",
        motion ? "PASS" : "FAIL",
        motion_min,
        motion_max,
        jump_max
    );
    ok &= motion;
    // Isolate receivers so spatial filtering cannot borrow light from a
    // different surface location that remains inside the gather radius.
    lighting.surface_depth_radius = 8;
    bool cutoff = true;
    float cutoff_first = 0, cutoff_last = 0, cutoff_previous = 0, cutoff_jump = 0;
    for (int frame = 0; frame <= 32; frame++) {
        small_source(data, 20.5f + frame * .25f, 16.5f, 1);
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE; x++) {
                int p = y * SIDE + x;
                bool receiver = x >= 12 && x < 20 && y >= 12 && y < 20;
                if (!receiver && data[SOURCE][p][0] == 0) {
                    data[COVER][p][1] = data[COVER][p][2] = 0;
                }
            }
        }
        render_radiance_reset(r);
        float color[4] = { 0 };
        cutoff &= draw(r, output, textures, data, lighting, view, color, &work);
        if (frame == 0) {
            cutoff_first = color[0];
        } else {
            cutoff_jump = fmaxf(cutoff_jump, fabsf(color[0] - cutoff_previous));
        }
        cutoff_previous = cutoff_last = color[0];
    }
    cutoff &= cutoff_first > .00001f && cutoff_last < .00001f &&
              cutoff_jump < cutoff_first * .15f;
    printf(
        "Surface depth radial cutoff: %s; inside %g outside %g largest step "
        "%g\n",
        cutoff ? "PASS" : "FAIL",
        cutoff_first,
        cutoff_last,
        cutoff_jump
    );
    ok &= cutoff;
    // At the viewport edge the surviving emission must decrease with its
    // clipped footprint; averaging partial tiles must not amplify it.
    lighting.surface_depth_radius = 32;
    bool edge = true;
    float edge_reference = 0, edge_last = 0;
    for (int frame = 0; frame <= 8; frame++) {
        float x = 30.5f + frame * .25f;
        small_source(data, x, 16.5f, 1);
        render_radiance_reset(r);
        float color[4] = { 0 };
        edge &= draw(r, output, textures, data, lighting, view, color, &work);
        float dx = x - 16.5f;
        float distance2 = dx * dx + 12 * 12;
        float normalized = color[0] * (distance2 + 3 * 3) * sqrtf(distance2) / 12;
        float retained = fminf(1, fmaxf(0, SIDE - (x - .5f)));
        if (frame == 0) {
            edge_reference = normalized;
        }
        float expected = edge_reference * retained;
        edge &= fabsf(normalized - expected) <= edge_reference * .2f;
        edge_last = color[0];
    }
    edge &= edge_reference > .00001f && edge_last < .00001f;
    printf(
        "Surface depth viewport edge: %s; normalized reference %g outside %g\n",
        edge ? "PASS" : "FAIL",
        edge_reference,
        edge_last
    );
    ok &= edge;
    lighting.surface_depth_radius = 16;
    inputs(data, 0);
    view.focal_distance = 64;
    render_radiance_reset(r);
    float perspective[4] = { 0 };
    bool projected = draw(r, output, textures, data, lighting, view, perspective, &work) &&
                     perspective[0] > .001f;
    printf("Surface depth perspective: %s; receiver %g\n", projected ? "PASS" : "FAIL", perspective[0]);
    ok &= projected;
    view.focal_distance = 0;
    lighting.surface_mode = GAME_HRC_SURFACE_BLUR;
    ok = draw(r, output, textures, data, lighting, view, restored, &work) && ok;
    bool reset = fabsf(restored[0] - off[0]) < .001f &&
                 fabsf(restored[1] - off[1]) < .001f &&
                 fabsf(restored[2] - off[2]) < .001f;
    printf(
        "Surface depth off-on-off: %s; initial %g depth %g restored %g\n",
        reset ? "PASS" : "FAIL",
        off[0],
        open[0],
        restored[0]
    );
    ok &= reset;
    for (int i = 0; i < INPUTS; i++) {
        if (textures[i].id) {
            cf_destroy_texture(textures[i]);
        }
    }
    if (output.id) {
        cf_destroy_canvas(output);
    }
    render_radiance_free(r);
    free(data);
    return ok;
}
int surface_depth_run(const char *program, bool gles)
{
    CF_Result r = cf_make_app(
        "Surface depth regression",
        0,
        0,
        0,
        SIDE,
        SIDE,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT |
            (gles ? CF_APP_OPTIONS_GFX_OPENGL_BIT : 0),
        program
    );
    if (cf_is_error(r)) {
        fprintf(stderr, "%s\n", r.details);
        return 1;
    }
    app_scene_renderer_initialize();
    bool ok = test_surface_depth();
    cf_destroy_app();
    return !ok;
}
