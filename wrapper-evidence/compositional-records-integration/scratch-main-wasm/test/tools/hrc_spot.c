#include "render/fullscreen.h"
#include "render/radiance.h"
#include "render/spot_lights.h"
#include "test/render/hidden_app.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
    SIDE = 64
};
/* Keep input indices independent of the image dimension. */
enum
{
    A = 0,
    C,
    N,
    S,
    TEXTURES
};
static float data[TEXTURES][SIDE * SIDE][4];
static uint16_t output[SIDE * SIDE][4];
static CF_Texture textures[TEXTURES];
static CF_Canvas canvas;
static CF_Shader shader;
static CF_Material material;
static CF_Mesh mesh;

static void setup()
{
    for (int i = 0; i < TEXTURES; i++) {
        CF_TextureParams p = cf_texture_defaults(SIDE, SIDE);
        p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        p.filter = CF_FILTER_NEAREST;
        textures[i] = cf_make_texture(p);
    }
    CF_CanvasParams p = cf_canvas_defaults(SIDE, SIDE);
    p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    canvas = cf_make_canvas(p);
    shader = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/spot.frag");
    material = cf_make_material();
    mesh = render_make_pass_mesh();
}
static void teardown()
{
    for (int i = 0; i < TEXTURES; i++) {
        cf_destroy_texture(textures[i]);
    }
    cf_destroy_canvas(canvas);
    cf_destroy_shader(shader);
    cf_destroy_material(material);
    cf_destroy_mesh(mesh);
}

static bool draw(int scenario, float *received)
{
    memset(data, 0, sizeof data);
    for (int i = 0; i < SIDE * SIDE; i++) {
        data[A][i][0] = data[A][i][1] = data[A][i][2] = .4f;
        data[A][i][3] = -8;
        data[C][i][0] = data[C][i][1] = 1;
        data[N][i][2] = 1;
        if (scenario >= 10 && scenario != 15 && scenario != 16) {
            data[C][i][0] = data[C][i][1] = 0;
        }
        if (scenario == 15) {
            data[A][i][3] = 120;
        }
        if (scenario == 16) {
            data[A][i][3] = 0;
        }
    }
    if (scenario == 13 || scenario == 14) {
        for (int y = 0; y < SIDE; y++) {
            if (scenario == 14 && y >= 30 && y <= 34) {
                continue;
            }
            int i = y * SIDE + 32;
            data[C][i][0] = data[C][i][1] = 1;
            data[A][i][3] = 120;
            data[S][i][3] = 100;
        }
    }
    /* One texel thick, fully opaque foreground slab; cut reveals rear wall. */
    if (scenario >= 2 && scenario <= 7) {
        for (int y = 0; y < SIDE; y++) {
            if (scenario == 3 && y >= 30 && y <= 34) {
                continue;
            }
            int i = y * SIDE + 32;
            data[A][i][3] = scenario == 5 ? 32 : 8;
            data[S][i][3] = scenario >= 6 ? 0 : 100;
            if (scenario == 7) {
                data[S][i][0] = 1; // Nonabsorbing emitter.
            }
            if (scenario == 4) {
                data[C][i][1] = 0; // A planar mask with no height.
            }
        }
    }
    cf_app_update(NULL);
    static const char *const names[] = { "u_albedo", "u_cover", "u_normal", "u_source" };
    for (int i = 0; i < TEXTURES; i++) {
        cf_texture_update(textures[i], data[i], sizeof data[i]);
        cf_material_set_texture_fs(material, names[i], textures[i]);
    }
    CF_V4 positions[4] = { { 8.5f, -32.5f, 8, 100 } },
          colors[4] = { { 1, .5f, .2f, 8 } },
          cones[4] = { { .9284767f, 0, -.3713907f, .9396926f } },
          options[4] = { { .9848077f, 1, 0, 0 } };
    if (scenario == 1 || scenario == 12) {
        cones[0].x = -cones[0].x;
        cones[0].z = -cones[0].z;
    }
    if (scenario == 8 || scenario == 9) {
        positions[0].x = -1000;
        positions[0].w = 2000;
        cones[0].x = .9998836f;
        cones[0].z = -.0152578f;
        if (scenario == 9) {
            options[0].y = 0;
        }
    }
    if (scenario >= 16) {
        positions[0].x = 48.5f;
        positions[0].z = -8;
        cones[0].x = 0;
        cones[0].z = 1;
    }
    cf_material_set_uniform_fs(material, "u_positions", positions, CF_UNIFORM_TYPE_FLOAT4, 4);
    cf_material_set_uniform_fs(material, "u_colors", colors, CF_UNIFORM_TYPE_FLOAT4, 4);
    cf_material_set_uniform_fs(material, "u_cones", cones, CF_UNIFORM_TYPE_FLOAT4, 4);
    cf_material_set_uniform_fs(material, "u_options", options, CF_UNIFORM_TYPE_FLOAT4, 4);
    CF_V4 projection = { 0, 0, 0, 1 };
    CF_V2 spacing = { 1, 1 };
    float thickness = scenario >= 10 && scenario < 16 ? 240 : 20, specular = 0;
    float density = scenario > 10 ? .01f : 0;
    int count = 1;
    cf_material_set_uniform_fs(material, "u_projection", &projection, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "u_spacing", &spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(material, "u_thickness", &thickness, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(material, "u_specular", &specular, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(material, "u_density", &density, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(material, "u_count", &count, CF_UNIFORM_TYPE_INT, 1);
    cf_apply_canvas(canvas, true);
    cf_apply_mesh(mesh);
    cf_apply_shader(shader, material);
    cf_draw_elements();
    cf_app_draw_onto_screen(false);
    REQUIRE(test_read_canvas(canvas, output, sizeof output));
    for (int i = 0; i < SIDE * SIDE; i++) {
        for (int c = 0; c < 4; c++) {
            REQUIRE(isfinite(test_half(output[i][c])));
        }
    }
    *received = test_half(output[32 * SIDE + 48][0]);
    return true;
}

TEST_CASE(spot_aim_and_single_texel_barriers_follow_actual_geometry)
{
    REQUIRE(shader.id && canvas.id);
    char notes[256];
    HRC2D lighting = hrc2d_defaults();
    REQUIRE(lighting.volume_density == 0);
    lighting.volume_density = -1;
    REQUIRE(render_radiance_normalize(lighting, notes, sizeof notes).volume_density == 0);
    lighting.volume_density = NAN;
    REQUIRE(render_radiance_normalize(lighting, notes, sizeof notes).volume_density == 0);
    lighting.volume_density = .001f;
    REQUIRE(render_radiance_normalize(lighting, notes, sizeof notes).volume_density == .001f);
    float observed[18];
    for (int i = 0; i < 18; i++) {
        REQUIRE(draw(i, &observed[i]));
    }
    fprintf(
        stderr,
        "hrc-spot forward=%g rear=%g wall=%g breach=%g planar=%g above=%g "
        "nonabsorbing=%g emitter=%g offscreen=%g offscreen-unshadowed=%g\n",
        observed[0],
        observed[1],
        observed[2],
        observed[3],
        observed[4],
        observed[5],
        observed[6],
        observed[7],
        observed[8],
        observed[9]
    );
    REQUIRE(observed[0] > .1f);
    REQUIRE(observed[1] < .0001f);
    REQUIRE(observed[2] < .0001f);
    REQUIRE(fabsf(observed[3] - observed[0]) < .001f);
    REQUIRE(observed[4] < .0001f);
    REQUIRE(fabsf(observed[5] - observed[0]) < .001f);
    REQUIRE(observed[6] < .0001f); // Nonabsorbing geometry casts depth shadow.
    REQUIRE(fabsf(observed[7] - observed[0]) < .001f);
    REQUIRE(observed[8] > .001f); // Offscreen source visits only known canvas.
    REQUIRE(fabsf(observed[9] - observed[8]) < .001f);
    fprintf(
        stderr,
        "hrc-volume zero=%g forward=%g rear=%g wall=%g breach=%g clipped=%g\n",
        observed[10],
        observed[11],
        observed[12],
        observed[13],
        observed[14],
        observed[15]
    );
    REQUIRE(observed[10] == 0);
    REQUIRE(observed[11] > .0001f);
    REQUIRE(observed[12] == 0);
    REQUIRE(observed[13] == 0);
    REQUIRE(fabsf(observed[14] - observed[11]) < .0001f);
    REQUIRE(observed[15] == 0);
    fprintf(stderr, "hrc-volume same-pixel-behind=%g same-pixel-clear=%g\n", observed[16], observed[17]);
    REQUIRE(observed[16] == 0);
    REQUIRE(observed[17] > .0001f);
    return true;
}

void test_hrc_spot()
{
    test_open_hidden_app("hrc-spot", SIDE, SIDE);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(spot_aim_and_single_texel_barriers_follow_actual_geometry);
    pu_clear_setup();
    cf_destroy_app();
}
