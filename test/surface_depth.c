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
static bool read_center_size(CF_Canvas output, int side, float color[4])
{
    CF_Readback read = cf_canvas_readback(output);
    uint64_t start = cf_get_ticks();
    while (read.id && !cf_readback_ready(read) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    size_t bytes = (size_t)side * side * 4 * sizeof(uint16_t);
    uint16_t (*pixels)[4] = xmalloc(bytes);
    bool ok = read.id && cf_readback_ready(read) &&
              (size_t)cf_readback_size(read) == bytes &&
              (size_t)cf_readback_data(read, pixels, bytes) == bytes;
    for (int p = 0; p < side * side && ok; p++) {
        for (int c = 0; c < 4; c++) {
            ok &= isfinite(half(pixels[p][c]));
        }
    }
    for (int c = 0; c < 4 && ok; c++) {
        color[c] = half(pixels[(side / 2) * side + side / 2][c]);
    }
    if (read.id) {
        cf_destroy_readback(read);
    }
    free(pixels);
    return ok;
}
static bool read_center(CF_Canvas output, float color[4])
{
    return read_center_size(output, SIDE, color);
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
    HRC2D lighting,
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
static bool fill_tests(
    render_radiance *r,
    CF_Canvas output,
    CF_Texture textures[INPUTS],
    float data[INPUTS][PIXELS][4],
    HRC2D lighting,
    render_view_2d view
)
{
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    lighting.debug_view = HRC_DEBUG_NORMAL;
    lighting.tone_mapping = HRC_TONE_AGX;
    lighting.intensity = .5f;
    lighting.specular = 0;
    render_surface_work work = { 0 };
    float base[4] = { 0 }, gains[3][4] = { 0 };
    inputs(data, 0);
    // Open transport keeps a measurable HRC base underneath the depth fill.
    for (int p = 0; p < PIXELS; p++) {
        data[SOURCE][p][3] = 0;
        data[NORMAL][p][3] = 0;
    }
    lighting.surface_radius = 0;
    render_radiance_reset(r);
    bool ok = draw(r, output, textures, data, lighting, view, base, &work);
    lighting.surface_radius = 16;
    bool skipped = false;
    for (int gain = 0; gain < 3; gain++) {
        lighting.surface_depth_gain = gain;
        render_radiance_reset(r);
        ok = draw(r, output, textures, data, lighting, view, gains[gain], &work) && ok;
        if (gain == 0) {
            skipped = !work.cells && !work.candidates && !work.visibility_taps;
        }
    }
    bool preserved = base[0] > .00001f;
    bool additive = gains[1][0] > gains[0][0] + .00001f;
    for (int c = 0; c < 3; c++) {
        preserved &= fabsf(gains[0][c] - base[c]) < .001f;
        float fill = gains[1][c] - gains[0][c];
        additive &= fabsf(gains[2][c] - gains[0][c] - 2 * fill) <
                    fmaxf(.001f, fabsf(fill) * .03f);
    }
    printf(
        "Surface depth HRC preservation: %s; HRC %g gain zero %g\n",
        preserved ? "PASS" : "FAIL",
        base[0],
        gains[0][0]
    );
    printf(
        "Surface depth additive gain: %s; zero %g one %g two %g\n",
        additive ? "PASS" : "FAIL",
        gains[0][0],
        gains[1][0],
        gains[2][0]
    );
    printf("Surface depth zero gain skips passes: %s\n", skipped ? "PASS" : "FAIL");
    ok &= preserved && additive && skipped;
    lighting.surface_depth_gain = 4;
    float open[4] = { 0 }, blocked[4] = { 0 }, empty[4] = { 0 };
    inputs(data, 0);
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, open, &work) && ok;
    inputs(data, 1);
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, blocked, &work) && ok;
    inputs(data, 4);
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, empty, &work) && ok;
    bool visibility = open[0] > .00001f && blocked[0] < open[0] * .35f &&
                      fabsf(empty[0]) < .00001f && fabsf(empty[1]) < .00001f &&
                      fabsf(empty[2]) < .00001f;
    printf(
        "Surface depth gain preserves occlusion: %s; open %g blocked %g no "
        "source %g\n",
        visibility ? "PASS" : "FAIL",
        open[0],
        blocked[0],
        empty[0]
    );
    ok &= visibility;
    // A dark receiver facing the half-vector exposes a glossy contribution
    // independently of diffuse albedo. The emitter stays outside the receiver.
    inputs(data, 0);
    for (int p = 0; p < PIXELS; p++) {
        data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = .01f;
        data[NORMAL][p][0] = .3f;
        data[NORMAL][p][3] = 0;
    }
    lighting.surface_depth_gain = 1;
    lighting.specular = 1;
    float matte[4] = { 0 }, glossy[4] = { 0 }, glossy_base[4] = { 0 },
          glossy_double[4] = { 0 };
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, matte, &work) && ok;
    for (int p = 0; p < PIXELS; p++) {
        data[NORMAL][p][3] = .6f;
    }
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, glossy, &work) && ok;
    bool highlight = glossy[0] > matte[0] * 1.1f + .00001f;
    lighting.surface_depth_gain = 0;
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, glossy_base, &work) && ok;
    lighting.surface_depth_gain = 2;
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, glossy_double, &work) && ok;
    float glossy_fill = glossy[0] - glossy_base[0];
    highlight &= fabsf(glossy_double[0] - glossy_base[0] - 2 * glossy_fill) <
                 fmaxf(.001f, fabsf(glossy_fill) * .03f);
    printf(
        "Surface depth material gloss: %s; matte %g glossy %g double gain %g\n",
        highlight ? "PASS" : "FAIL",
        matte[0],
        glossy[0],
        glossy_double[0]
    );
    // A matte patch shares the exact plane, normal and albedo of its glossy
    // surroundings. Geometry-only reconstruction must not borrow their BRDF.
    for (int y = 12; y < 20; y++) {
        for (int x = 12; x < 20; x++) {
            data[NORMAL][y * SIDE + x][3] = 0;
        }
    }
    lighting.surface_depth_gain = 1;
    lighting.specular = 0;
    float patch_off[4] = { 0 }, patch_on[4] = { 0 };
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, patch_off, &work) && ok;
    lighting.specular = 1;
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, patch_on, &work) && ok;
    bool boundary = true;
    for (int c = 0; c < 3; c++) {
        boundary &= fabsf(patch_on[c] - patch_off[c]) < .00001f;
    }
    printf(
        "Surface depth matte/gloss boundary: %s; specular off %g on %g\n",
        boundary ? "PASS" : "FAIL",
        patch_off[0],
        patch_on[0]
    );
    return ok && highlight && boundary;
}
/* Separate colors share one coarse pyramid tile. Their integrated luma crosses
 * by only two percent, while their distances and normals produce different
 * illumination. Assigning all tile RGB to the brightest source makes the
 * receiver jump at the crossing even though every input varies smoothly. */
static bool source_stability()
{
    enum
    {
        WIDTH = 128,
        COUNT = WIDTH * WIDTH
    };
    float (*data)[COUNT][4] = xmalloc(sizeof(float) * INPUTS * COUNT * 4);
    CF_Texture textures[INPUTS] = { 0 };
    for (int i = 0; i < INPUTS; i++) {
        CF_TextureParams p = cf_texture_defaults(WIDTH, WIDTH);
        p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        p.filter = CF_FILTER_NEAREST;
        p.wrap_u = p.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        textures[i] = cf_make_texture(p);
    }
    CF_CanvasParams p = cf_canvas_defaults(WIDTH, WIDTH);
    p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas output = cf_make_canvas(p);
    render_radiance *r = render_radiance_new();
    HRC2D lighting = hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.resolution = lighting.color_resolution = 1;
    lighting.ambient = lighting.fog = lighting.shadow_floor = 0;
    lighting.specular = 0;
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    lighting.surface_radius = lighting.surface_depth_radius = 128;
    lighting.surface_thickness = 0;
    lighting.debug_view = HRC_DEBUG_SURFACE_LIGHT;
    render_view_2d view = {
        -64, 64, WIDTH, WIDTH, .crop_max_u = 1, .crop_max_v = 1
    };
    bool ok = r && output.id && render_radiance_resize(r, WIDTH, WIDTH);
    render_radiance_inputs input = { .material = { .scene = textures[SCENE],
                                                   .source = textures[SOURCE],
                                                   .albedo = textures[ALBEDO],
                                                   .normal = textures[NORMAL],
                                                   .cover = textures[COVER],
                                                   .visibility = textures[VISIBILITY] } };
    float previous[4] = { 0 }, largest = 0, smallest = INFINITY;
    for (int frame = 0; frame < 9; frame++) {
        memset(data, 0, sizeof(float) * INPUTS * COUNT * 4);
        for (int cell = 0; cell < COUNT; cell++) {
            data[SCENE][cell][3] = 1;
            data[ALBEDO][cell][0] = data[ALBEDO][cell][1] = data[ALBEDO][cell][2] = 1;
            data[NORMAL][cell][0] = .9f;
            data[NORMAL][cell][2] = .4358899f;
            data[COVER][cell][0] = data[COVER][cell][1] = data[COVER][cell][2] = 1;
        }
        for (int y = 64; y < 68; y++) {
            for (int x = 80; x < 96; x++) {
                if (x >= 84 && x < 92) {
                    continue;
                }
                int cell = y * WIDTH + x;
                data[ALBEDO][cell][3] = 12;
                if (x < 84) {
                    data[SOURCE][cell][0] = 4 * (1 + (frame - 4) * .005f);
                } else {
                    data[SOURCE][cell][2] = 4 * (.2126f / .0722f);
                }
            }
        }
        render_radiance_reset(r);
        ok = render_radiance_set_lighting(r, lighting) && ok;
        cf_app_update(NULL);
        for (int i = 0; i < INPUTS; i++) {
            cf_texture_update(textures[i], data[i], sizeof data[i]);
        }
        ok = render_radiance_draw(r, input, view, output) && ok;
        cf_app_draw_onto_screen(false);
        float color[4] = { 0 };
        ok = read_center_size(output, WIDTH, color) && ok;
        smallest = fminf(smallest, fminf(color[0], color[2]));
        if (frame > 0) {
            for (int c = 0; c < 3; c += 2) {
                largest = fmaxf(
                    largest,
                    fabsf(color[c] - previous[c]) /
                        fmaxf((color[c] + previous[c]) * .5f, .00001f)
                );
            }
        }
        memcpy(previous, color, sizeof previous);
    }
    bool continuous = smallest > .00001f && largest < .06f;
    printf(
        "Surface depth mixed-color source stability: %s; smallest channel %g "
        "largest relative step %g\n",
        continuous ? "PASS" : "FAIL",
        smallest,
        largest
    );
    for (int i = 0; i < INPUTS; i++) {
        cf_destroy_texture(textures[i]);
    }
    cf_destroy_canvas(output);
    render_radiance_free(r);
    free(data);
    return ok && continuous;
}
/* A readback patch tests every reconstructed pixel, including pixels between
 * the quarter-resolution receiver centers. Center-only checks miss stripes. */
static bool read_patch(
    CF_Canvas output,
    int side,
    int x0,
    int y0,
    int x1,
    int y1,
    float *mean,
    float *minimum,
    float *maximum
)
{
    CF_Readback read = cf_canvas_readback(output);
    uint64_t start = cf_get_ticks();
    while (read.id && !cf_readback_ready(read) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    size_t bytes = (size_t)side * side * 4 * sizeof(uint16_t);
    uint16_t (*pixels)[4] = xmalloc(bytes);
    bool ok = read.id && cf_readback_ready(read) &&
              (size_t)cf_readback_size(read) == bytes &&
              (size_t)cf_readback_data(read, pixels, bytes) == bytes;
    *mean = 0;
    *minimum = INFINITY;
    *maximum = 0;
    for (int y = y0; y < y1 && ok; y++) {
        for (int x = x0; x < x1; x++) {
            float value = half(pixels[y * side + x][0]);
            ok &= isfinite(value);
            *mean += value / ((x1 - x0) * (y1 - y0));
            *minimum = fminf(*minimum, value);
            *maximum = fmaxf(*maximum, value);
        }
    }
    if (read.id) {
        cf_destroy_readback(read);
    }
    free(pixels);
    return ok;
}
static bool segment_reconstruction_tests()
{
    enum
    {
        WIDTH = 128,
        COUNT = WIDTH * WIDTH
    };
    float (*data)[COUNT][4] = xmalloc(sizeof(float) * INPUTS * COUNT * 4);
    CF_Texture textures[INPUTS] = { 0 };
    for (int i = 0; i < INPUTS; i++) {
        CF_TextureParams p = cf_texture_defaults(WIDTH, WIDTH);
        p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        p.filter = CF_FILTER_NEAREST;
        p.wrap_u = p.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        textures[i] = cf_make_texture(p);
    }
    CF_CanvasParams p = cf_canvas_defaults(WIDTH, WIDTH);
    p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas output = cf_make_canvas(p);
    render_radiance *r = render_radiance_new();
    HRC2D lighting = hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.resolution = lighting.color_resolution = 1;
    lighting.ambient = lighting.fog = lighting.shadow_floor = 0;
    lighting.specular = 0;
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    lighting.surface_radius = lighting.surface_depth_radius = 384;
    lighting.surface_thickness = 12;
    lighting.surface_depth_visibility = HRC_DEPTH_VISIBILITY_SEGMENTS;
    lighting.debug_view = HRC_DEBUG_SURFACE_LIGHT;
    render_view_2d view = { -128,           128, WIDTH, WIDTH, .crop_max_u = 1,
                            .crop_max_v = 1 };
    render_radiance_inputs input = { .material = { .scene = textures[SCENE],
                                                   .source = textures[SOURCE],
                                                   .albedo = textures[ALBEDO],
                                                   .normal = textures[NORMAL],
                                                   .cover = textures[COVER],
                                                   .visibility = textures[VISIBILITY] } };
    bool ok = r && output.id && render_radiance_resize(r, WIDTH, WIDTH);
    float means[3] = { 0 };
    for (int scenario = 0; scenario < 3; scenario++) {
        memset(data, 0, sizeof(float) * INPUTS * COUNT * 4);
        for (int y = 0; y < WIDTH; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int cell = y * WIDTH + x;
                float wx = -128 + (x + .5f) * 2;
                float wy = 128 - (y + .5f) * 2;
                data[SCENE][cell][3] = 1;
                data[SOURCE][cell][3] = 20;
                data[ALBEDO][cell][0] = data[ALBEDO][cell][1] = data[ALBEDO][cell][2] = 1;
                data[NORMAL][cell][2] = 1;
                data[COVER][cell][0] = data[COVER][cell][1] = data[COVER][cell][2] = 1;
                if (fabsf(wx + 70) < 4 && fabsf(wy) < 4) {
                    data[SOURCE][cell][0] = 4;
                    data[SOURCE][cell][3] = 0;
                    data[ALBEDO][cell][3] = 80;
                }
                float blocker_x = scenario == 2 ? -100 : 0;
                if (scenario != 1 && fabsf(wx - blocker_x) < 4 && fabsf(wy) < 35) {
                    data[ALBEDO][cell][3] = 40;
                }
            }
        }
        render_radiance_reset(r);
        ok = render_radiance_set_lighting(r, lighting) && ok;
        cf_app_update(NULL);
        for (int i = 0; i < INPUTS; i++) {
            cf_texture_update(textures[i], data[i], sizeof data[i]);
        }
        ok = render_radiance_draw(r, input, view, output) && ok;
        cf_app_draw_onto_screen(false);
        float minimum, maximum;
        // World x=56..64, y=-4..4, safely inside the expected umbra.
        ok = read_patch(output, WIDTH, 92, 62, 96, 66, &means[scenario], &minimum, &maximum) &&
             ok;
    }
    bool shadow = means[1] > .00001f && means[0] < means[1] * .25f &&
                  fabsf(means[2] - means[1]) < means[1] * .1f;
    printf(
        "Surface depth finite-segment shadow placement: %s; blocked %g clear "
        "%g beyond light %g\n",
        shadow ? "PASS" : "FAIL",
        means[0],
        means[1],
        means[2]
    );

    // The planar height changes 24 units per pixel, exceeding the old raw-height
    // compatibility tolerance even at adjacent pixels. Its normal identifies
    // a single continuous surface; all pixels must reconstruct some light.
    lighting.surface_depth_visibility = 0;
    lighting.surface_thickness = 0;
    lighting.surface_radius = lighting.surface_depth_radius = 1024;
    float slope = 12;
    float nx = -slope / sqrtf(1 + slope * slope);
    float plane_min = 0, plane_max = 0, plane_mean = 0, detached = 0;
    for (int scenario = 0; scenario < 2; scenario++) {
        memset(data, 0, sizeof(float) * INPUTS * COUNT * 4);
        for (int y = 0; y < WIDTH; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int cell = y * WIDTH + x;
                float wx = -128 + (x + .5f) * 2;
                data[SCENE][cell][3] = 1;
                data[SOURCE][cell][3] = 0;
                data[ALBEDO][cell][0] = data[ALBEDO][cell][1] = data[ALBEDO][cell][2] = 1;
                data[ALBEDO][cell][3] = slope * wx;
                data[NORMAL][cell][0] = nx;
                data[NORMAL][cell][2] = 1;
                data[COVER][cell][0] = data[COVER][cell][1] = data[COVER][cell][2] = 1;
                if (x >= 24 && x < 28 && y >= 62 && y < 66) {
                    data[SOURCE][cell][0] = 4;
                    data[ALBEDO][cell][3] = 400;
                }
                // Receiver centers are x=2 mod 4. This narrow detached surface
                // has none, and must not borrow light from the nearby plane.
                if (scenario && x >= 64 && x < 66) {
                    data[ALBEDO][cell][3] += 1000;
                }
            }
        }
        render_radiance_reset(r);
        ok = render_radiance_set_lighting(r, lighting) && ok;
        cf_app_update(NULL);
        for (int i = 0; i < INPUTS; i++) {
            cf_texture_update(textures[i], data[i], sizeof data[i]);
        }
        ok = render_radiance_draw(r, input, view, output) && ok;
        cf_app_draw_onto_screen(false);
        float mean, minimum, maximum;
        ok = read_patch(output, WIDTH, scenario ? 64 : 56, 60, scenario ? 66 : 72, 68, &mean, &minimum, &maximum) &&
             ok;
        if (scenario) {
            detached = maximum;
        } else {
            plane_min = minimum;
            plane_max = maximum;
            plane_mean = mean;
        }
    }
    // This slope changes receiver-to-emitter distance substantially, so a
    // physical lighting gradient is expected. Every interior pixel must be
    // finite and lit; compatibility failures instead produce exact black
    // stripes between the sampled receiver centers.
    bool reconstructed = isfinite(plane_min) && isfinite(plane_max) && plane_min > 1e-8f;
    bool separated = detached < plane_mean * .01f;
    printf(
        "Surface depth steep-plane reconstruction: %s; min %g max %g\n",
        reconstructed ? "PASS" : "FAIL",
        plane_min,
        plane_max
    );
    printf(
        "Surface depth disconnected-plane rejection: %s; detached max %g "
        "continuous mean %g\n",
        separated ? "PASS" : "FAIL",
        detached,
        plane_mean
    );
    for (int i = 0; i < INPUTS; i++) {
        cf_destroy_texture(textures[i]);
    }
    cf_destroy_canvas(output);
    render_radiance_free(r);
    free(data);
    return ok && shadow && reconstructed && separated;
}
static bool temporal_response(
    render_radiance *r,
    CF_Canvas output,
    CF_Texture textures[INPUTS],
    float data[INPUTS][PIXELS][4],
    HRC2D lighting,
    render_view_2d view
)
{
    render_surface_work work = { 0 };
    lighting.surface_depth_temporal = false;
    inputs(data, 0);
    for (int cell = 0; cell < PIXELS; cell++) {
        data[SOURCE][cell][0] *= 1.05f;
    }
    render_radiance_reset(r);
    float instant[4] = { 0 }, initial[4] = { 0 }, first[4] = { 0 },
          converged[4] = { 0 };
    bool ok = draw(r, output, textures, data, lighting, view, instant, &work);
    inputs(data, 0);
    lighting.surface_depth_temporal = true;
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, initial, &work) && ok;
    for (int cell = 0; cell < PIXELS; cell++) {
        data[SOURCE][cell][0] *= 1.05f;
    }
    ok = draw(r, output, textures, data, lighting, view, first, &work) && ok;
    for (int frame = 0; frame < 24; frame++) {
        ok = draw(r, output, textures, data, lighting, view, converged, &work) && ok;
    }
    float step = instant[0] - initial[0];
    bool responds = step > initial[0] * .02f && first[0] > initial[0] &&
                    first[0] < instant[0] - step * .2f &&
                    fabsf(converged[0] - instant[0]) < step * .25f;
    printf(
        "Surface depth temporal accumulates and converges: %s; initial %g "
        "first %g instant %g settled %g\n",
        responds ? "PASS" : "FAIL",
        initial[0],
        first[0],
        instant[0],
        converged[0]
    );

    // Move the camera one receiver cell right and reproject the same emitting
    // strip four pixels left. A uniform plane supplies compatible old samples,
    // but its lighting gradient requires selecting the translated history cell.
    inputs(data, 0);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 28; x < SIDE; x++) {
            int cell = y * SIDE + x;
            data[SOURCE][cell][0] = 0;
            data[SOURCE][cell][3] = 20;
            data[ALBEDO][cell][3] = 0;
        }
    }
    render_radiance_reset(r);
    float before[4] = { 0 }, translated[4] = { 0 }, reference[4] = { 0 };
    ok = draw(r, output, textures, data, lighting, view, before, &work) && ok;
    for (int i = 0; i < INPUTS; i++) {
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE - 4; x++) {
                memcpy(data[i][y * SIDE + x], data[i][y * SIDE + x + 4], sizeof data[i][0]);
            }
            for (int x = SIDE - 4; x < SIDE; x++) {
                // Newly revealed space is the same nonemitting plane.
                memcpy(data[i][y * SIDE + x], data[i][y * SIDE], sizeof data[i][0]);
            }
        }
    }
    view.min_x += 4;
    view.center_x += 4;
    ok = draw(r, output, textures, data, lighting, view, translated, &work) && ok;
    lighting.surface_depth_temporal = false;
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, reference, &work) && ok;
    bool reprojected = reference[0] > .00001f &&
                       fabsf(translated[0] - reference[0]) < reference[0] * .1f;
    printf(
        "Surface depth temporal camera translation: %s; previous center %g "
        "current %g history %g\n",
        reprojected ? "PASS" : "FAIL",
        before[0],
        reference[0],
        translated[0]
    );
    return ok && responds && reprojected;
}
static bool option_tests(
    render_radiance *r,
    CF_Canvas output,
    CF_Texture textures[INPUTS],
    float data[INPUTS][PIXELS][4],
    HRC2D lighting,
    render_view_2d view
)
{
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    lighting.debug_view = HRC_DEBUG_SURFACE_LIGHT;
    lighting.surface_depth_radius = 16;
    lighting.surface_depth_temporal = false;
    render_surface_work work = { 0 };
    bool ok = true, quality = true;
    for (int q = HRC_DEPTH_LOW; q <= HRC_DEPTH_HIGH; q++) {
        lighting.surface_depth_quality = q;
        for (int near = 0; near < 2; near++) {
            lighting.surface_depth_near_detail = near;
            lighting.surface_depth_shadow_softness = near ? .5f : 0;
            inputs(data, 0);
            render_radiance_reset(r);
            float color[4] = { 0 };
            quality &= draw(r, output, textures, data, lighting, view, color, &work) &&
                       color[0] > .00001f;
            float blocked[4] = { 0 };
            inputs(data, 1);
            render_radiance_reset(r);
            quality &= draw(r, output, textures, data, lighting, view, blocked, &work) &&
                       blocked[0] < color[0] * .6f;
        }
    }
    printf("Surface depth quality, near detail and soft visibility: %s\n", quality ? "PASS" : "FAIL");
    ok &= quality;
    lighting.surface_depth_quality = HRC_DEPTH_BALANCED;
    lighting.surface_depth_near_detail = false;
    lighting.surface_depth_shadow_softness = 0;
    lighting.surface_depth_history = .9f;
    float initial[4] = { 0 }, accumulated[4] = { 0 }, empty[4] = { 0 },
          restored[4] = { 0 };
    inputs(data, 0);
    render_radiance_reset(r);
    ok = draw(r, output, textures, data, lighting, view, initial, &work) && ok;
    lighting.surface_depth_temporal = true;
    for (int frame = 0; frame < 4; frame++) {
        ok = draw(r, output, textures, data, lighting, view, accumulated, &work) && ok;
    }
    bool history = initial[0] > .00001f;
    for (int c = 0; c < 3; c++) {
        history &= fabsf(initial[c] - accumulated[c]) < .001f;
    }
    // Reveal a receiver facing away from the light while emission remains.
    // Normal stores XY; Z is the solid-surface flag, and reconstructed NZ is
    // positive. Reusing the prior right-facing lighting would leave a ghost.
    for (int cell = 0; cell < PIXELS; cell++) {
        if (data[SOURCE][cell][0] > 0) {
            continue;
        }
        data[NORMAL][cell][0] = -.99f;
        data[NORMAL][cell][1] = 0;
    }
    float back[4] = { 0 };
    ok = draw(r, output, textures, data, lighting, view, back, &work) && ok;
    bool disoccluded = fabsf(back[0]) < .00001f && fabsf(back[1]) < .00001f &&
                       fabsf(back[2]) < .00001f;
    inputs(data, 0);
    ok = draw(r, output, textures, data, lighting, view, accumulated, &work) && ok;
    inputs(data, 4);
    ok = draw(r, output, textures, data, lighting, view, empty, &work) && ok;
    bool clear = fabsf(empty[0]) < .00001f && fabsf(empty[1]) < .00001f &&
                 fabsf(empty[2]) < .00001f;
    inputs(data, 0);
    lighting.surface_depth_temporal = false;
    ok = draw(r, output, textures, data, lighting, view, restored, &work) && ok;
    for (int c = 0; c < 3; c++) {
        history &= fabsf(initial[c] - restored[c]) < .001f;
    }
    lighting.surface_depth_temporal = true;
    render_radiance_reset(r);
    inputs(data, 4);
    ok = draw(r, output, textures, data, lighting, view, empty, &work) && ok;
    for (int c = 0; c < 3; c++) {
        clear &= fabsf(empty[c]) < .00001f;
    }
    printf(
        "Surface depth temporal toggle preserves stationary lighting: %s; "
        "initial %g history %g restored %g\n",
        history ? "PASS" : "FAIL",
        initial[0],
        accumulated[0],
        restored[0]
    );
    printf(
        "Surface depth temporal source removal and reset: %s; empty %g\n",
        clear ? "PASS" : "FAIL",
        empty[0]
    );
    printf(
        "Surface depth temporal changed receiver rejects history: %s; back "
        "face %g\n",
        disoccluded ? "PASS" : "FAIL",
        back[0]
    );
    lighting.surface_depth_history = 0; // Omitted strength uses the default.
    return temporal_response(r, output, textures, data, lighting, view) && ok &&
           history && clear && disoccluded;
}
bool test_surface_depth()
{
    if (getenv("CHIBI_TEST_DEPTH_SOURCE_STABILITY")) {
        return source_stability();
    }
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
    HRC2D lighting = hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.resolution = lighting.color_resolution = 1;
    lighting.radiance_hysteresis = 1;
    lighting.ambient = lighting.fog = lighting.shadow_floor = 0;
    lighting.surface_radius = 16;
    lighting.surface_relief = 1;
    lighting.surface_depth_radius = 16;
    lighting.surface_thickness = 12;
    lighting.specular = 0;
    lighting.debug_view = HRC_DEBUG_SURFACE_LIGHT;
    lighting.surface_mode = HRC_SURFACE_BLUR;
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
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    ok = draw(r, output, textures, data, lighting, view, open, &work) && ok;
    bool budget = work.mode == HRC_SURFACE_DEPTH && work.block == 4 &&
                  work.cells == 64 && work.candidates == (size_t)work.cells * 49 &&
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
    lighting.surface_mode = HRC_SURFACE_BLUR;
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
    ok = fill_tests(r, output, textures, data, lighting, view) && ok;
    ok = option_tests(r, output, textures, data, lighting, view) && ok;
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
    return source_stability() && segment_reconstruction_tests() && ok;
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
