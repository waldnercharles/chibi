#include "runtime/shaders/program.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <cute_shader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static shader_graph graph;
static char *glsl, *fragment, *vertex;
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
    free(vertex);
    glsl = fragment = vertex = NULL;
}

static bool particle_vertex_program(bool indexed)
{
    int index = shader_graph_add(&graph, indexed ? SHADER_PARAMETER : SHADER_CONSTANT, 1);
    graph.nodes[index].parameter = 0;
    int time = shader_graph_add(&graph, SHADER_TIME, 1);
    int position = shader_graph_add(&graph, SHADER_VECTOR3, 3);
    graph.nodes[position].input[0] = index;
    graph.nodes[position].input[1] = time;
    graph.nodes[position].input[2] = index;
    int size = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[size].value[0] = .25f;
    int color = shader_graph_add(&graph, SHADER_CONSTANT, 4);
    graph.nodes[color].value[0] = 2;
    graph.nodes[color].value[1] = .5f;
    graph.nodes[color].value[2] = .25f;
    graph.nodes[color].value[3] = 1;
    graph.output[0] = position;
    graph.output[1] = size;
    graph.output[2] = color;
    graph.output_count = 3;
    shader_error error = { 0 };
    glsl = shader_graph_glsl_named(&graph, "sg_particle", &error);
    REQUIRE(glsl);

    FILE *file = fopen(CHIBI_SOURCE_ROOT "/src/render/shaders/scene3d_particles.vert", "rb");
    REQUIRE(file);
    REQUIRE(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    REQUIRE(length > 0 && length < 65536);
    rewind(file);
    char source[65536];
    size_t read = fread(source, 1, (size_t)length, file);
    REQUIRE(fclose(file) == 0);
    REQUIRE(read == (size_t)length);
    source[length] = 0;
    const char *marker = "// SG_PARTICLE_PROGRAM is replaced by the prepared "
                         "shader function.";
    char *at = strstr(source, marker);
    REQUIRE(at);
    *at = 0;
    size_t bytes = (size_t)length + strlen(glsl) + 32;
    vertex = xmalloc(bytes);
    snprintf(vertex, bytes, "#version 450\n%s%s%s", source, glsl, at + strlen(marker));
    CF_ShaderCompilerConfig config = { .skip_glsl300 = true, .skip_hlsl = true };
    compiled = cute_shader_compile(vertex, CUTE_SHADER_STAGE_VERTEX, config);
    if (!compiled.success) {
        fprintf(stderr, "particle vertex shader: %s\n", compiled.error_message);
    }
    REQUIRE(compiled.success);
    REQUIRE(compiled.bytecode.wgsl_src);
    REQUIRE(compiled.bytecode.msl_src);
    if (indexed) {
        REQUIRE(strstr(compiled.bytecode.wgsl_src, "instance_index"));
    }
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "particle_frame"));
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "in_instance_offset"));
    REQUIRE(strstr(compiled.bytecode.msl_src, "in_instance_offset"));
    return true;
}

TEST_CASE(particle_vertex_program_transpiles_instanced_geometry_to_wgsl)
{
    return particle_vertex_program(true);
}

TEST_CASE(particle_vertex_keeps_instance_buffer_when_program_ignores_index)
{
    return particle_vertex_program(false);
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
    RUN_TEST_CASE(particle_vertex_program_transpiles_instanced_geometry_to_wgsl);
    RUN_TEST_CASE(particle_vertex_keeps_instance_buffer_when_program_ignores_index);
}
