#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GLSL wrappers size their output arrays by the C contract. */
#define QUOTE(x) #x
#define QUOTED(x) QUOTE(x)
#define OUTPUT_ROWS QUOTED(SHADER_OUTPUT_MAX)

static int parameter(shader_graph *g, int slot, int width)
{
    int i = shader_graph_add(g, SHADER_PARAMETER, width);
    g->nodes[i].parameter = slot;
    return i;
}
static int effect(shader_graph *g, shader_op op, int width, int a, int b, int c, int d)
{
    int i = shader_graph_add(g, op, width);
    g->nodes[i].input[0] = a;
    g->nodes[i].input[1] = b;
    g->nodes[i].input[2] = c;
    g->nodes[i].input[3] = d;
    return i;
}
static const shader_ramp_stop stops[] = { { .1f, { 0, 0, 0, 0 } },
                                          { .4f, { 0, 1, 1, .25f } },
                                          { 1, { 1, .2f, .1f, 1 } } };
static void program(shader_graph *g, bool ramp)
{
    shader_graph_init(g);
    int p2 = parameter(g, 0, 2), p3 = parameter(g, 0, 3),
        o2 = parameter(g, 1, 2), o3 = parameter(g, 1, 3);
    int r = parameter(g, 2, 1), r3 = parameter(g, 2, 3),
        s2 = parameter(g, 3, 2), s3 = parameter(g, 3, 3);
    int f = parameter(g, 4, 1), from = parameter(g, 5, 2),
        to = parameter(g, 6, 2), mode = parameter(g, 7, 1);
    int scale = parameter(g, 8, 1), detail = parameter(g, 9, 1),
        rough = parameter(g, 10, 1), random = parameter(g, 11, 1);
    int power = parameter(g, 12, 1), base = parameter(g, 13, 1),
        view = parameter(g, 14, 3), bounded = parameter(g, 15, 1);
    int gradient_scale = parameter(g, 16, 1);
    int normal = shader_graph_add(g, SHADER_CONSTANT, 3);
    g->nodes[normal].value[2] = 1;
    if (ramp) {
        g->output_count = 4;
        g->output[0] = effect(g, SHADER_COLOR_RAMP, 3, f, mode, -1, -1);
        g->output[1] = effect(g, SHADER_COLOR_RAMP_ALPHA, 1, f, mode, -1, -1);
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 3; i++) {
                apush(g->nodes[g->output[j]].ramp, stops[i]);
            }
        }
        g->output[2] = effect(g, SHADER_VORONOI, 1, p2, scale, random, -1);
        g->output[3] = effect(g, SHADER_VORONOI_COLOR, 3, p2, scale, random, -1);
        return;
    }
    g->output_count = 8;
    g->output[0] = effect(g, SHADER_MAPPING, 2, p2, o2, r, s2);
    g->output[1] = effect(g, SHADER_MAPPING3D, 3, p3, o3, r3, s3);
    g->output[2] = effect(g, SHADER_FRESNEL, 1, normal, view, power, base);
    g->output[3] = effect(g, SHADER_REMAP, 2, p2, from, to, bounded);
    g->output[4] = effect(g, SHADER_GRADIENT, 1, p2, mode, gradient_scale, f);
    g->output[5] = effect(g, SHADER_FRACTAL_NOISE, 1, p3, scale, detail, rough);
    g->output[6] = effect(g, SHADER_VORONOI, 1, p3, scale, random, -1);
    g->output[7] = effect(g, SHADER_VORONOI_COLOR, 3, p3, scale, random, -1);
}
static void inputs(float v[32][4], int test)
{
    memset(v, 0, 32 * 4 * sizeof(float));
    float defaults[][4] = { { 2, 1, 3 }, { -1, 2, 4 }, { 90, 0, 90 },
                            { 2, 3, 1 }, { .25f },     { 0, 1 },
                            { -2, 4 },   { 0 },        { 1 },
                            { 4 },       { .5f },      { 1 },
                            { 5 },       { .04f },     { 0, 0, 1 },
                            { 1 },       { 1 } };
    memcpy(v, defaults, sizeof defaults);
    const float factors[] = { -.5f, .1f, .39999f, .4f, .7f, 1, 1.5f };
    v[4][0] = factors[test % 7];
    v[7][0] = (float)(test % 3);
    if (test == 0) {
        return;
    }
    v[0][0] = -.71f + (float)test * .131f;
    v[0][1] = .28f;
    v[0][2] = -3.37f;
    v[2][0] = -17;
    v[2][1] = 43;
    v[2][2] = 21;
    v[9][0] = test % 2 ? 3.5f : 8;
    if (test == 2) {
        v[10][0] = 0;
        v[11][0] = 0;
        v[14][0] = 1;
        v[14][2] = 0;
    }
    if (test == 3) {
        v[5][1] = 0;
        v[12][0] = 0;
    }
    if (test == 4) {
        v[5][0] = 1;
        v[5][1] = -1;
        v[15][0] = 0;
        v[11][0] = .5f;
    }
    if (test == 5) {
        v[0][0] = 1e10f;
        v[8][0] = 1e30f;
        v[9][0] = .1f;
    }
    if (test == 6) {
        v[0][0] = .5f;
        v[0][1] = .5f;
        v[0][2] = .5f;
        v[11][0] = 0;
        v[7][0] = 3;
    }
}
static node *add(graph *g, const char *name)
{
    node *n = graph_add_node(g, registry_node_by_name(name));
    assert(n && shader_graph_init_node(g, n));
    return n;
}
TEST_CASE(authored_ramps_own_their_stops)
{
    behavior_asset *a = behavior_asset_new("Ramp ownership");
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(a->graph, false),
         *ramp = add(a->graph, "Shader Color Ramp");
    shader_ramp_stop edit[3];
    memcpy(edit, stops, sizeof edit);
    REQUIRE(shader_node_ramp_set(a->graph, ramp, edit, 3));
    edit[1].color[1] = 99;
    shader_ramp_stop got[SHADER_RAMP_MAX];
    REQUIRE(shader_node_ramp_get(ramp, got) == 3 && got[1].color[1] == 1);
    REQUIRE(graph_connect_data(a->graph, ramp, 0, out, 0) == 0);
    REQUIRE(graph_connect_data(a->graph, ramp, 1, out, 5) == 0);
    node *value = add(a->graph, "Shader Constant");
    REQUIRE(graph_set_pin_type(a->graph, value, 1, 0, PIN_I32, NULL) == 0);
    REQUIRE(graph_connect_data(a->graph, value, 0, ramp, 2) != 0);
    char *json = behavior_asset_save(a);
    REQUIRE(json);
    behavior_asset *copy = behavior_asset_load(json);
    REQUIRE(copy);
    node *loaded = graph_node_by_id(copy->graph, ramp->id);
    REQUIRE(shader_node_ramp_get(loaded, got) == 3 && !memcmp(got, stops, sizeof stops));
    graph pasted;
    graph_init(&pasted);
    char *clipboard = graph_save_nodes(a->graph, &ramp, 1);
    REQUIRE(clipboard && graph_append(&pasted, clipboard, 20, 30) == 0);
    free(clipboard);
    REQUIRE(acount(pasted.nodes) == 1);
    REQUIRE(shader_node_ramp_get(pasted.nodes[0], got) == 3 && !memcmp(got, stops, sizeof stops));
    graph_free(&pasted);
    char *color = strstr(json, "Color ");
    REQUIRE(color);
    char *channel = strstr(color, "x=");
    REQUIRE(channel);
    channel[0] = 'q';
    REQUIRE(!behavior_asset_load(json));
    REQUIRE(strstr(graph_last_error(), "ShaderVec4.x"));
    channel[0] = 'x';
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    REQUIRE(shader_graph_from_graph(copy->graph, &ir, &error));
    int valid_count = acount(ir.nodes);
    edit[1].position = edit[0].position;
    REQUIRE(!shader_node_ramp_set(copy->graph, loaded, edit, 3));
    pin_array *array = loaded->input_pins[1].default_value.ptr;
    ((shader_ramp_stop *)pin_array_get(array, 1).ptr)->position = .1f;
    REQUIRE(!shader_graph_from_graph(copy->graph, &ir, &error));
    REQUIRE(
        error.node == loaded->id && strstr(error.message, "stops") &&
        acount(ir.nodes) == valid_count
    );
    shader_graph_free(&ir);
    behavior_asset_free(copy);
    behavior_asset_free(a);
    free(json);
    return true;
}
TEST_CASE(fresnel_reads_the_sample_context)
{
    behavior_asset *a = behavior_asset_new("Effect context");
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(a->graph, false),
         *fresnel = add(a->graph, "Shader Fresnel");
    REQUIRE(graph_connect_data(a->graph, fresnel, 0, out, 5) == 0);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    REQUIRE(shader_graph_from_graph(a->graph, &ir, &error));
    shader_sample_context sample = { .normal = { 0, 0, 1 },
                                     .view_direction = { 0, 0, 1 } };
    float result[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    REQUIRE(fabsf(result[5][0] - .04f) < .00001f);
    sample.view_direction[0] = 1;
    sample.view_direction[2] = 0;
    REQUIRE(
        shader_graph_eval_outputs(&ir, &sample, NULL, result, &error) &&
        result[5][0] == 1
    );
    shader_graph_free(&ir);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(effects_evaluate_transforms_remaps_cells_and_ramps)
{
    shader_graph g;
    program(&g, false);
    float p[32][4], out[SHADER_OUTPUT_MAX][4];
    inputs(p, 0);
    shader_sample_context sample = { 0 };
    shader_error error = { 0 };
    REQUIRE(shader_graph_eval_outputs(&g, &sample, p, out, &error));
    REQUIRE(fabsf(out[0][0] + 4) < .00001f && fabsf(out[0][1] - 6) < .00001f);
    REQUIRE(
        fabsf(out[1][0] - 2) < .00001f && fabsf(out[1][1] - 6) < .00001f &&
        fabsf(out[1][2] - 7) < .00001f
    );
    REQUIRE(fabsf(out[2][0] - .04f) < .00001f && out[3][0] == 4 && out[3][1] == 4);
    inputs(p, 6);
    REQUIRE(shader_graph_eval_outputs(&g, &sample, p, out, &error));
    REQUIRE(out[6][0] == 0); // Zero jitter puts a feature at the exact cell center.
    shader_graph_free(&g);
    program(&g, true);
    inputs(p, 3);
    REQUIRE(shader_graph_eval_outputs(&g, &sample, p, out, &error));
    REQUIRE(out[0][0] == 0 && out[0][1] == 1 && out[0][2] == 1 && out[1][0] == .25f);
    shader_graph_free(&g);
    return true;
}
/* Star Field along World Position with density and brightness parameters. */
static void star_program(shader_graph *g)
{
    shader_graph_init(g);
    int direction = shader_graph_add(g, SHADER_WORLD_POSITION, 3);
    g->output_count = 1;
    g->output[0] = effect(g, SHADER_STAR_FIELD, 3, direction, parameter(g, 0, 1), parameter(g, 1, 1), -1);
}
static float star_at(const shader_graph *g, float density, float brightness, float x, float y, float z)
{
    float p[32][4] = { { density }, { brightness } }, out[SHADER_OUTPUT_MAX][4];
    shader_sample_context sample = { .world_position = { x, y, z } };
    shader_error error = { 0 };
    bool ok = shader_graph_eval_outputs(g, &sample, p, out, &error);
    assert(ok);
    (void)ok;
    return out[0][0] + out[0][1] + out[0][2];
}
TEST_CASE(star_field_draws_sparse_points_by_direction)
{
    shader_graph g;
    star_program(&g);
    shader_error error = { 0 };
    REQUIRE(shader_graph_validate(&g, &error));
    /* Local maxima on a grid finer than a star over a 2 x 2 degree patch
     * around -Y: about density x 4 stars. */
    enum
    {
        SIDE = 176
    };
    static float field[SIDE][SIDE];
    const float step = .0002f;
    for (int j = 0; j < SIDE; j++) {
        for (int i = 0; i < SIDE; i++) {
            field[j][i] = star_at(&g, 10, 1, (i - SIDE / 2) * step, -1, (j - SIDE / 2) * step);
        }
    }
    int stars = 0;
    for (int j = 1; j < SIDE - 1; j++) {
        for (int i = 1; i < SIDE - 1; i++) {
            bool peak = field[j][i] > .005f;
            for (int n = 0; n < 9 && peak; n++) {
                peak = n == 4 || field[j][i] > field[j + n / 3 - 1][i + n % 3 - 1];
            }
            stars += peak;
        }
    }
    REQUIRE(stars >= 20 && stars <= 60);
    /* Direction length, zero density and brightness. */
    float x = 0, z = 0, best = 0;
    for (int j = 0; j < SIDE; j++) {
        for (int i = 0; i < SIDE; i++) {
            if (field[j][i] > best) {
                best = field[j][i];
                x = (i - SIDE / 2) * step;
                z = (j - SIDE / 2) * step;
            }
        }
    }
    REQUIRE(best > .015f);
    REQUIRE(fabsf(star_at(&g, 10, 1, x * 3, -3, z * 3) - best) < 1e-6f);
    REQUIRE(fabsf(star_at(&g, 10, 2, x, -1, z) - 2 * best) < 1e-6f);
    REQUIRE(star_at(&g, 0, 1, x, -1, z) == 0 && star_at(&g, 10, 0, x, -1, z) == 0);
    REQUIRE(star_at(&g, 10, 1, 0, 0, 0) == 0);
    /* Its GLSL reads screen derivatives and only comes with the node. */
    char *glsl = shader_graph_glsl_named(&g, "test_stars", &error);
    REQUIRE(glsl && strstr(glsl, "sgx_star_field(") && strstr(glsl, "fwidth"));
    free(glsl);
    shader_graph_free(&g);
    program(&g, false);
    glsl = shader_graph_glsl_named(&g, "test_effects", &error);
    REQUIRE(glsl && !strstr(glsl, "sgx_star_field"));
    free(glsl);
    shader_graph_free(&g);
    return true;
}
void test_shader_effects_cpu()
{
    test_graph_packs_install();
    RUN_TEST_CASE(authored_ramps_own_their_stops);
    RUN_TEST_CASE(fresnel_reads_the_sample_context);
    RUN_TEST_CASE(effects_evaluate_transforms_remaps_cells_and_ramps);
    RUN_TEST_CASE(star_field_draws_sparse_points_by_direction);
    test_graph_packs_restore();
}

/* Whether a compute shader of the program's GLSL computes what the CPU
 * evaluator does for every input case. */
static bool gpu_matches(bool ramp)
{
    shader_graph ir;
    program(&ir, ramp);
    shader_error error = { 0 };
    char *function = shader_graph_glsl_named(&ir, "test_effects", &error);
    assert(function);
    const char *
        body = "layout(local_size_x=8,local_size_y=1,local_size_z=1) in;\n"
               "layout(set=1,binding=0,rgba32f) uniform image2D result_image;\n"
               "layout(set=2,binding=0) uniform Inputs { vec4 values[32]; };\n"
               "void main(){vec4 result[" OUTPUT_ROWS
               "],p[32];for(int i=0;i<32;i++)p[i]=values[i];"
               "SGSample "
               "sg_test;sg_test.position=vec2(0);sg_test.uv=vec2(0);"
               "sg_test.object_position=vec3(0);sg_test.part_position=vec3(0);"
               "sg_test.world_position=vec3(0);sg_test.normal=vec3(0);"
               "sg_test.view_direction=vec3(0);sg_test.time=0.0;"
               "for(int i=0;i<3;i++){sg_test.object_from_world[i]=vec3(0);"
               "sg_test.part_from_world[i]=vec3(0);}"
               "for(int i=0;i<2;i++)sg_test.uv_from_world[i]=vec3(0);"
               "test_effects(sg_test,p,result);int "
               "x=int(gl_GlobalInvocationID.x);imageStore(result_image,ivec2(x,"
               "0),"
               "result[x]);}\n";
    char *source = xmalloc(strlen(function) + strlen(body) + 1);
    strcpy(source, function);
    strcat(source, body);
    CF_ComputeShader shader = cf_make_compute_shader_from_source(source);
    assert(shader.id);
    CF_Material material = cf_make_material();
    CF_CanvasParams cp = cf_canvas_defaults(8, 1);
    cp.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    cp.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(cp);
    assert(canvas.id);
    CF_Texture texture = cf_canvas_get_target(canvas);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
    dispatch.rw_textures = &texture;
    dispatch.rw_texture_count = 1;
    bool same = true;
    for (int test = 0; same && test < 7; test++) {
        float values[32][4], expected[SHADER_OUTPUT_MAX][4], actual[8][4];
        inputs(values, test);
        shader_sample_context sample = { 0 };
        assert(shader_graph_eval_outputs(&ir, &sample, values, expected, &error));
        cf_app_update(NULL);
        cf_material_set_uniform_cs(material, "values", values, CF_UNIFORM_TYPE_FLOAT4, 32);
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        if (!test_read_canvas(canvas, actual, sizeof actual)) {
            same = false;
            break;
        }
        for (int i = 0; i < ir.output_count; i++) {
            for (int c = 0; c < ir.nodes[ir.output[i]].width; c++) {
                float want = expected[i][c], got = actual[i][c];
                if (!isfinite(got) ||
                    fabsf(got - want) > .0003f * fmaxf(1, fabsf(want))) {
                    fprintf(stderr, "effects GPU ramp%d case%d output%d channel%d: %.9g != %.9g\n", ramp, test, i, c, got, want);
                    same = false;
                }
            }
        }
    }
    cf_destroy_canvas(canvas);
    cf_destroy_material(material);
    cf_destroy_compute_shader(shader);
    shader_graph_free(&ir);
    free(source);
    free(function);
    return same;
}
TEST_CASE(effects_match_cpu_on_gpu)
{
    REQUIRE(gpu_matches(false));
    return true;
}
TEST_CASE(ramps_and_cells_match_cpu_on_gpu)
{
    REQUIRE(gpu_matches(true));
    return true;
}
/* Runs in an open hidden app. */
void test_shader_effects_gpu()
{
    RUN_TEST_CASE(effects_match_cpu_on_gpu);
    RUN_TEST_CASE(ramps_and_cells_match_cpu_on_gpu);
}
