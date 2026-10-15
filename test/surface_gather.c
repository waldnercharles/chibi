#include "platform/cute.h"
#include "render/fullscreen.h"
#include "render/radiance.h"
#include "render/scene_renderer.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SIDE = 32,
    PIXELS = SIDE * SIDE,
    INPUTS = 10
};

static float half_float(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    float fraction = value & 1023;
    float result = exponent ? ldexpf(1 + fraction / 1024, exponent - 15)
                            : ldexpf(fraction, -24);
    return value & 0x8000 ? -result : result;
}
static bool read_center(CF_Texture texture, float color[4])
{
    CF_CanvasParams p = cf_canvas_defaults(0, 0);
    p.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(p);
    CF_Readback read = cf_canvas_readback(canvas);
    uint16_t pixels[PIXELS][4];
    uint64_t start = cf_get_ticks();
    while (read.id && !cf_readback_ready(read) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    bool ok = read.id && cf_readback_ready(read) &&
              cf_readback_data(read, pixels, sizeof pixels) == sizeof pixels;
    for (int i = 0; ok && i < 4; i++) {
        color[i] = half_float(pixels[(SIDE / 2) * SIDE + SIDE / 2][i]);
    }
    if (read.id) {
        cf_destroy_readback(read);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

static void inputs(float data[INPUTS][PIXELS][4], int scenario)
{
    memset(data, 0, sizeof(float) * INPUTS * PIXELS * 4);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int p = y * SIDE + x;
            data[4][p][3] = 1; // Camera-opaque floor, transparent to transport.
            data[5][p][0] = data[5][p][1] = data[5][p][2] = 1;
            data[6][p][1] = data[6][p][2] = 1; // Solid heightfield receiver.
            data[7][p][2] = 1;
            bool illuminated[] = { x >= 20, y >= 20, x <= 12, y <= 12 };
            for (int q = 0; q < 4; q++) {
                bool light = illuminated[q];
                if (scenario == 3) {
                    light = false; // No emitter / no incoming field.
                }
                if (scenario == 4) {
                    light = q == 0 && x >= 30; // Outside finite reach.
                }
                if (light) {
                    data[q][p][q % 3] = 1;
                }
            }
            if (scenario == 1 || scenario == 2) {
                // An empty gap before a broad crest. Four sparse visibility
                // probes must continue across the gap to find this blocker.
                if (x >= 18 && x <= 19) {
                    data[6][p][1] = data[6][p][2] = 0;
                }
                if (x >= 21 && x <= 24) {
                    data[5][p][3] = scenario == 1 ? 10 : 0;
                }
            }
        }
    }
}

static bool orientation_reset(CF_Texture textures[INPUTS], float data[INPUTS][PIXELS][4])
{
    render_radiance *r = render_radiance_new();
    CF_CanvasParams p = cf_canvas_defaults(SIDE, SIDE);
    p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas output = cf_make_canvas(p);
    HRC2D lighting = hrc2d_defaults();
    lighting.adaptive = false;
    lighting.feedback = false;
    lighting.resolution = lighting.color_resolution = 1;
    lighting.surface_mode = HRC_SURFACE_GATHER;
    lighting.surface_radius = 8;
    lighting.surface_relief = 1;
    lighting.radiance_hysteresis = .05f;
    lighting.debug_view = HRC_DEBUG_SURFACE_LIGHT;
    lighting.ambient = lighting.fog = lighting.shadow_floor = 0;
    bool ok = r && output.id && render_radiance_set_lighting(r, lighting) &&
              render_radiance_resize(r, SIDE, SIDE);
    render_radiance_inputs input = { .material = { .scene = textures[4],
                                                   .source = textures[8],
                                                   .albedo = textures[5],
                                                   .normal = textures[7],
                                                   .cover = textures[6],
                                                   .visibility = textures[9] } };
    render_view_2d view = { -.5f * SIDE, .5f * SIDE,      SIDE,
                            SIDE,        .crop_max_u = 1, .crop_max_v = 1 };
    for (int axis = 0; ok && axis < 2; axis++) {
        render_radiance_reset(r);
        view.yaw = view.roll = 0;
        float bright[4] = { 0 }, rotated[4] = { 0 };
        inputs(data, 0);
        for (int y = 0; y < SIDE; y++) {
            for (int x = 20; x < SIDE; x++) {
                data[8][y * SIDE + x][0] = 2;
            }
        }
        for (int frame = 0; ok && frame < 4; frame++) {
            cf_app_update(NULL);
            for (int i = 4; i < INPUTS; i++) {
                cf_texture_update(textures[i], data[i], sizeof data[i]);
            }
            ok = render_radiance_draw(r, input, view, output);
            cf_app_draw_onto_screen(false);
        }
        ok = ok && read_center(cf_canvas_get_target(output), bright) &&
             bright[0] > .01f;
        memset(data[8], 0, sizeof data[8]);
        if (axis == 0) {
            view.yaw = .7f;
        } else {
            view.roll = .7f;
        }
        if (ok) {
            cf_app_update(NULL);
            cf_texture_update(textures[8], data[8], sizeof data[8]);
            ok = render_radiance_draw(r, input, view, output);
            cf_app_draw_onto_screen(false);
            ok = ok && read_center(cf_canvas_get_target(output), rotated) &&
                 fabsf(rotated[0]) < .001f && fabsf(rotated[1]) < .001f &&
                 fabsf(rotated[2]) < .001f;
        }
        printf(
            "Surface gather %s reset %s: bright%g rotated-black%g\n",
            axis == 0 ? "yaw" : "roll",
            ok ? "PASS" : "FAIL",
            bright[0],
            rotated[0]
        );
    }
    render_radiance_free(r);
    if (output.id) {
        cf_destroy_canvas(output);
    }
    return ok;
}

bool test_surface_gather()
{
    float (*data)[PIXELS][4] = malloc(sizeof(float) * INPUTS * PIXELS * 4);
    if (!data) {
        return false;
    }
    CF_Texture textures[INPUTS] = { 0 };
    for (int i = 0; i < INPUTS; i++) {
        CF_TextureParams p = cf_texture_defaults(SIDE, SIDE);
        p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        p.filter = CF_FILTER_NEAREST;
        p.wrap_u = p.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        textures[i] = cf_make_texture(p);
    }
    CF_CanvasParams p = cf_canvas_defaults(SIDE, SIDE);
    p.target_count = 4;
    for (int i = 0; i < 4; i++) {
        p.targets[i].pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    }
    CF_Canvas output = cf_make_canvas(p);
    CF_Material material = cf_make_material();
    CF_Mesh mesh = render_make_pass_mesh();
    CF_Shader shader = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/surface_gather.frag");
    bool ok = output.id && material.id && mesh.id && shader.id;
    for (int i = 0; i < INPUTS; i++) {
        ok &= textures[i].id != 0;
    }
    const char *names[] = { "u_q0",    "u_q1",     "u_q2",
                            "u_q3",    "u_scene",  "u_albedo",
                            "u_cover", "u_normal", "u_source" };
    for (int i = 0; i < 9; i++) {
        cf_material_set_texture_fs(material, names[i], textures[i]);
    }
    float spacing[] = { 1, 1 }, radius = 8, relief = 1;
    int block = 1;
    cf_material_set_uniform_fs(material, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(material, "u_radius", &radius, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(material, "u_relief", &relief, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(material, "u_block", &block, CF_UNIFORM_TYPE_INT, 1);
    float red[5] = { 0 };
    for (int scenario = 0; ok && scenario < 5; scenario++) {
        inputs(data, scenario);
        cf_app_update(NULL);
        for (int i = 0; i < INPUTS; i++) {
            cf_texture_update(textures[i], data[i], sizeof data[i]);
        }
        cf_apply_canvas(output, true);
        cf_apply_mesh(mesh);
        cf_apply_shader(shader, material);
        cf_apply_viewport(0, 0, SIDE, SIDE);
        cf_apply_scissor(0, 0, SIDE, SIDE);
        cf_draw_elements();
        cf_app_draw_onto_screen(false);
        for (int q = 0; ok && q < 4; q++) {
            float color[4] = { 0 };
            ok = read_center(cf_canvas_get_target2(output, q), color);
            if (q == 0) {
                red[scenario] = color[0];
            }
            if (scenario == 0) {
                ok &= fabsf(color[q % 3] - 1) < .001f;
            }
            if (scenario == 3 || scenario == 4) {
                ok &= fabsf(color[0]) + fabsf(color[1]) + fabsf(color[2]) < .001f;
            }
        }
    }
    ok = ok && red[0] > .99f && red[1] < red[0] * .5f && red[2] > .99f;
    printf(
        "Surface gather %s: flat%g crest-beyond-gap%g low-crest%g black%g "
        "outside-reach%g\n",
        ok ? "PASS" : "FAIL",
        red[0],
        red[1],
        red[2],
        red[3],
        red[4]
    );
    if (ok) {
        ok = orientation_reset(textures, data);
    }
    for (int i = 0; i < INPUTS; i++) {
        if (textures[i].id) {
            cf_destroy_texture(textures[i]);
        }
    }
    if (output.id) {
        cf_destroy_canvas(output);
    }
    if (material.id) {
        cf_destroy_material(material);
    }
    if (mesh.id) {
        cf_destroy_mesh(mesh);
    }
    if (shader.id) {
        cf_destroy_shader(shader);
    }
    free(data);
    return ok;
}

int surface_gather_run(const char *program, bool gles)
{
    CF_Result r = cf_make_app(
        "Surface gather regression",
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
    bool ok = test_surface_gather();
    cf_destroy_app();
    return !ok;
}
