#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GLSL wrappers size their output arrays by the C contract. */
#define QUOTE(x) #x
#define QUOTED(x) QUOTE(x)
#define OUTPUT_ROWS QUOTED(SHADER_OUTPUT_MAX)

/* Compute shaders of shader graph GLSL compute what the CPU evaluator does. */

void test_shader_effects_gpu();
void test_shader_image_gpu();
#ifdef CHIBI_TEST_MODEL_GRAPHS
void test_model_sampled_gpu();
#endif

static int parameter(shader_graph *g, int slot)
{
    int i = shader_graph_add(g, SHADER_PARAMETER, 1);
    g->nodes[i].parameter = slot;
    return i;
}
TEST_CASE(decal_masks_match_cpu_on_gpu)
{
    shader_graph graph;
    shader_graph_init(&graph);
    int position = shader_graph_add(&graph, SHADER_PART_POSITION, 3);
    int half = shader_graph_add(&graph, SHADER_CONSTANT, 3);
    memcpy(
        graph.nodes[half].value,
        (float[]){ 1, 2, .1f, 0 },
        sizeof graph.nodes[half].value
    );
    int feather = parameter(&graph, 0), facing = parameter(&graph, 1);
    int mask = shader_graph_add(&graph, SHADER_DECAL_MASK, 1);
    memcpy(
        graph.nodes[mask].input,
        (int[]){ position, half, feather, facing },
        sizeof graph.nodes[mask].input
    );
    graph.output_count = 1;
    graph.output[0] = mask;
    shader_error error = { 0 };
    char *function = shader_graph_glsl_named(&graph, "decal_test", &error);
    REQUIRE(function);
    const char *body =
        "layout(local_size_x=1,local_size_y=1,local_size_z=1) in;\n"
        "layout(set=1,binding=0,rgba32f) uniform image2D result_image;\n"
        "layout(set=2,binding=0) uniform Inputs { vec4 values[32]; };\n"
        "void main(){vec4 result[" OUTPUT_ROWS
        "],p[32];for(int i=0;i<32;i++)p[i]=values[i];"
        "SGSample "
        "s;s.position=vec2(0);s.uv=vec2(0);s.object_position=values[3].xyz;"
        "s.part_position=values[3].xyz;s.world_position=values[3].xyz;"
        "s.normal=values[2].xyz;s.view_direction=vec3(0,0,1);s.time=0.0;"
        "for(int "
        "i=0;i<3;i++){s.object_from_world[i]=vec3(0);s.part_from_world[i]=vec3("
        "0);"
        "s.object_from_world[i][i]=1.0;s.part_from_world[i][i]=1.0;}"
        "s.uv_from_world[0]=vec3(1,0,0);s.uv_from_world[1]=vec3(0,0,1);"
        "decal_test(s,p,result);imageStore(result_image,ivec2(0),result[0]);}"
        "\n";
    char *source = xmalloc(strlen(function) + strlen(body) + 1);
    strcpy(source, function);
    strcat(source, body);
    CF_ComputeShader shader = cf_make_compute_shader_from_source(source);
    REQUIRE(shader.id);
    CF_Material material = cf_make_material();
    CF_CanvasParams params = cf_canvas_defaults(1, 1);
    params.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(params);
    REQUIRE(canvas.id);
    CF_Texture texture = cf_canvas_get_target(canvas);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
    dispatch.rw_textures = &texture;
    dispatch.rw_texture_count = 1;
    // Feather, facing threshold, Z position, Z normal, independent expected mask.
    const float cases[][5] = {
        { 0, -1, 0, 1, 1 },     { 0, -1, .1f, 1, 1 },
        { 0, -1, .101f, 1, 0 }, { .1f, -1, .05f, 1, .5f },
        { .1f, -1, .1f, 1, 0 }, { 0, .5f, 0, 1, 1 },
        { 0, .5f, 0, -1, 0 }
    };
    for (int i = 0; i < 7; i++) {
        float values[32][4] = { 0 }, actual[4], expected[SHADER_OUTPUT_MAX][4];
        values[0][0] = cases[i][0];
        values[1][0] = cases[i][1];
        values[2][2] = cases[i][3];
        values[3][2] = cases[i][2];
        shader_sample_context sample = { .normal = { 0, 0, cases[i][3] },
                                         .part_position = { 0, 0, cases[i][2] } };
        for (int axis = 0; axis < 3; axis++) {
            sample.part_from_world[axis][axis] = 1;
        }
        REQUIRE(shader_graph_eval_outputs(&graph, &sample, values, expected, &error));
        REQUIRE(fabsf(expected[0][0] - cases[i][4]) < .0001f);
        cf_app_update(NULL);
        cf_material_set_uniform_cs(material, "values", values, CF_UNIFORM_TYPE_FLOAT4, 32);
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        REQUIRE(test_read_canvas(canvas, actual, sizeof actual));
        REQUIRE(isfinite(actual[0]) && fabsf(actual[0] - cases[i][4]) <= .001f);
    }
    cf_destroy_canvas(canvas);
    cf_destroy_material(material);
    cf_destroy_compute_shader(shader);
    shader_graph_free(&graph);
    free(source);
    free(function);
    return true;
}
void test_shader_parity()
{
    test_open_hidden_app("shader-parity", 32, 32);
    RUN_TEST_CASE(decal_masks_match_cpu_on_gpu);
    test_shader_effects_gpu();
    test_shader_image_gpu();
#ifdef CHIBI_TEST_MODEL_GRAPHS
    test_model_sampled_gpu();
#endif
    cf_destroy_app();
}
