#include "runtime/shaders/program.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <cute_shader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static shader_graph graph;
static char *glsl, *fragment;
static CF_ShaderCompilerResult compiled;

static void setup()
{
    shader_graph_init(&graph);
}

static void cleanup()
{
    cute_shader_free_result(compiled);
    compiled = (CF_ShaderCompilerResult){ 0 };
    shader_graph_free(&graph);
    free(glsl);
    free(fragment);
    glsl = fragment = NULL;
}

TEST_CASE(noise_shader_transpiles_without_ambiguous_wgsl_comparison)
{
    int position = shader_graph_add(&graph, SHADER_WORLD_POSITION, 3);
    int scale = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[scale].value[0] = 1;
    int noise = shader_graph_add(&graph, SHADER_NOISE, 1);
    graph.nodes[noise].input[0] = position;
    graph.nodes[noise].input[1] = scale;
    graph.output[0] = noise;
    graph.output_count = 1;
    shader_error error = { 0 };
    glsl = shader_graph_glsl_named(&graph, "noise_material", &error);
    REQUIRE(glsl);

    const char *header = "#version 450\nlayout(location=0) out vec4 color;\n";
    const char *main = "void main() {\n"
                       " SGSample sg_sample;\n"
                       " sg_sample.world_position=vec3(gl_FragCoord.xy,0.5);\n"
                       " vec4 parameters[32]; vec4 outputs[9];\n"
                       " noise_material(sg_sample,parameters,outputs);\n"
                       " color=outputs[0];\n"
                       "}\n";
    size_t size = strlen(header) + strlen(glsl) + strlen(main) + 1;
    fragment = xmalloc(size);
    snprintf(fragment, size, "%s%s%s", header, glsl, main);
    CF_ShaderCompilerConfig config = { .skip_glsl300 = true,
                                       .skip_hlsl = true,
                                       .skip_msl = true };
    compiled = cute_shader_compile(fragment, CUTE_SHADER_STAGE_FRAGMENT, config);
    if (!compiled.success) {
        fprintf(stderr, "noise shader: %s\n", compiled.error_message);
    }
    REQUIRE(compiled.success);
    REQUIRE(compiled.bytecode.wgsl_src);
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "sgx_noise"));
    /* Cute accepts this GLSL but drops the shift's parentheses in WGSL.
     * The resulting `< ... >>` is parsed as template arguments to `i`.
     * Check the actual runtime compiler output, not the GLSL spelling. */
    REQUIRE(!strstr(compiled.bytecode.wgsl_src, "i < 4 >> u32(axis)"));
    return true;
}

void test_tool_shader_webgpu()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(noise_shader_transpiles_without_ambiguous_wgsl_comparison);
}
