#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <cute_shader.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const registry_pack **previous_packs;
static behavior_asset *asset, *loaded;
static shader_graph program;
static char *document, *glsl, *fragment;
static CF_ShaderCompilerResult compiled;

static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &CORE_FLOW_PACK, &SHADER_PACK };
    registry_set_packs(packs, 2);
    shader_graph_init(&program);
}

static void cleanup()
{
    cute_shader_free_result(compiled);
    compiled = (CF_ShaderCompilerResult){ 0 };
    shader_graph_free(&program);
    behavior_asset_free(asset);
    behavior_asset_free(loaded);
    asset = loaded = NULL;
    free(document);
    free(glsl);
    free(fragment);
    document = glsl = fragment = NULL;
    registry_set_packs(previous_packs, acount(previous_packs));
    afree(previous_packs);
}

static bool curves_roundtrip(int width)
{
    asset = behavior_asset_new("Exponential curves");
    asset->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *g = behavior_asset_add_function(asset, "Curves", true);
    REQUIRE(g);
    node *entry = graph_node_by_id(g, g->entry_id);
    node *ret = graph_node_by_id(g, g->return_id);
    REQUIRE(
        graph_add_pin(
            g,
            entry,
            true,
            &(pin_spec){ .name = "x", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" }
        ) == 0
    );
    char type[16];
    snprintf(type, sizeof type, "ShaderVec%d", width);
    REQUIRE(
        graph_add_pin(
            g,
            ret,
            false,
            &(pin_spec){ .name = "Value",
                         .kind = PIN_DATA,
                         .type = width == 1 ? PIN_F32 : PIN_STRUCT,
                         .type_name = width == 1 ? "float" : type }
        ) == 0
    );
    const char *values[] = { "x", "vec2(x, x+1)", "vec3(x, x+1, x+2)", "vec4(x, x+1, x+2, x+3)" };
    const char *v = values[width - 1];
    char source[512];
    snprintf(source, sizeof source, "exp(-%s) + log(%s) + ln(%s) + pow(%s, 2) + pow(2, %s)", v, v, v, v, v);
    behavior_expression_error expression_error = { 0 };
    node *expression = behavior_expression_add(asset, g, source, &expression_error);
    if (!expression) {
        fprintf(stderr, "shader curve expression: %s\n", expression_error.message);
    }
    REQUIRE(expression);
    REQUIRE(graph_connect_data(g, entry, 0, expression, 0) == 0);
    REQUIRE(graph_connect_data(g, expression, 0, ret, 0) == 0);
    uint64_t function_id = g->graph_id, expression_id = expression->id;
    document = behavior_asset_save(asset);
    REQUIRE(document);
    loaded = behavior_asset_load(document);
    REQUIRE(loaded);
    REQUIRE(behavior_asset_equal(asset, loaded));
    g = behavior_asset_find_graph(loaded, function_id);
    REQUIRE(g);
    expression = graph_node_by_id(g, expression_id);
    REQUIRE(expression);
    REQUIRE(!strcmp(behavior_expression_source(loaded, expression), source));
    shader_error error = { 0 };
    REQUIRE(shader_graph_from_function(g, &program, &error));
    REQUIRE(program.nodes[program.output[0]].width == width);
    float parameters[SHADER_PARAMETER_MAX][4] = { { 2 } };
    float results[SHADER_OUTPUT_MAX][4];
    REQUIRE(
        shader_graph_eval_outputs(&program, &(shader_sample_context){ 0 }, parameters, results, &error)
    );
    for (int i = 0; i < width; i++) {
        float x = (float)i + 2;
        float expected = expf(-x) + 2 * logf(x) + x * x + exp2f(x);
        REQUIRE(fabsf(results[0][i] - expected) < 0.00002f);
    }
    glsl = shader_graph_glsl_named(&program, "curves", &error);
    REQUIRE(glsl);
    const char *header = "#version 450\nlayout(location=0) out vec4 color;\n";
    const char *main = "void main() { SGSample sample; vec4 parameters[32]; "
                       "vec4 outputs[9];\n"
                       "parameters[0]=vec4(gl_FragCoord.x); "
                       "curves(sample,parameters,outputs);\n"
                       "color=outputs[0]; }\n";
    size_t size = strlen(header) + strlen(glsl) + strlen(main) + 1;
    fragment = xmalloc(size);
    snprintf(fragment, size, "%s%s%s", header, glsl, main);
    compiled = cute_shader_compile(
        fragment,
        CUTE_SHADER_STAGE_FRAGMENT,
        (CF_ShaderCompilerConfig){ .skip_glsl300 = true, .skip_hlsl = true, .skip_msl = true }
    );
    if (!compiled.success) {
        fprintf(stderr, "shader curves: %s\n", compiled.error_message);
    }
    REQUIRE(compiled.success);
    REQUIRE(compiled.bytecode.wgsl_src);
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "exp("));
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "log("));
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "pow("));
    if (width == 3) {
        program.output[1] = shader_graph_add(&program, SHADER_CONSTANT, 1);
        program.output[2] = shader_graph_add(&program, SHADER_CONSTANT, 4);
        program.output_count = 3;
        REQUIRE(shader_graph_particle_validate(&program, &error));
    }
    return true;
}

TEST_CASE(scalar_curves_roundtrip_and_compile_to_wgsl)
{
    return curves_roundtrip(1);
}
TEST_CASE(vector2_curves_roundtrip_and_compile_to_wgsl)
{
    return curves_roundtrip(2);
}
TEST_CASE(vector3_curves_roundtrip_and_compile_to_wgsl)
{
    return curves_roundtrip(3);
}
TEST_CASE(vector4_curves_roundtrip_and_compile_to_wgsl)
{
    return curves_roundtrip(4);
}

void test_tool_shader_curves()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(scalar_curves_roundtrip_and_compile_to_wgsl);
    RUN_TEST_CASE(vector2_curves_roundtrip_and_compile_to_wgsl);
    RUN_TEST_CASE(vector3_curves_roundtrip_and_compile_to_wgsl);
    RUN_TEST_CASE(vector4_curves_roundtrip_and_compile_to_wgsl);
}
