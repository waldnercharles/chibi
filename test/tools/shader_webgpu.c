#include "render/shader_text.h"
#include "runtime/shaders/prepared.h"
#include "runtime/shaders/program.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <cute_shader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static shader_graph graph;
static char *glsl, *fragment, *vertex;
static CF_ShaderCompilerResult compiled;
static shader_prepared_asset source_asset, decoded;
static unsigned char *encoded;

static void source_particle(const char *code)
{
    source_asset.kind = SHADER_ASSET_PARTICLE_TEXT;
    source_asset.name = xstrdup("Direct.Particles");
    source_asset.text.path = xstrdup("shaders/Direct.glsl");
    source_asset.text.entry = xstrdup("particles");
    source_asset.text.code = xstrdup(code);
    const char *names[] = { "Index",  "Count", "Seed", "Gain",
                            "Offset", "Axis",  "Tint" };
    const int widths[] = { 1, 1, 1, 1, 2, 3, 4 };
    for (int i = 0; i < 7; i++) {
        source_asset.names[i] = xstrdup(names[i]);
        source_asset.widths[i] = widths[i];
        source_asset.defaults[i][0] = .25f * i;
    }
}

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
    shader_prepared_asset_free(&source_asset);
    shader_prepared_asset_free(&decoded);
    free(encoded);
    encoded = NULL;
}

static bool compile_particle_vertex(const shader_text *text, int outputs)
{
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
    size_t bytes = (size_t)length + strlen(glsl) + 128;
    vertex = xmalloc(bytes);
    snprintf(
        vertex,
        bytes,
        "#version 450\n#define SG_PARTICLE_OUTPUT_COUNT %d\n%s%s%s%s",
        outputs,
        text && text->update ? "#define SG_PARTICLE_STATEFUL 1\n" : "",
        source,
        glsl,
        at + strlen(marker)
    );
    CF_ShaderCompilerFile files[SHADER_TEXT_INCLUDE_MAX + 1];
    int count = 0;
    if (text) {
        files[count++] = (CF_ShaderCompilerFile){ text->path, text->code };
        for (int i = 0; i < acount(text->includes); i++) {
            files[count++] = (CF_ShaderCompilerFile){ text->includes[i].path,
                                                      text->includes[i].code };
        }
    }
    CF_ShaderCompilerConfig config = { .builtin_includes = files,
                                       .num_builtin_includes = count };
    compiled = cute_shader_compile(vertex, CUTE_SHADER_STAGE_VERTEX, config);
    if (!compiled.success) {
        fprintf(stderr, "particle vertex shader: %s\n", compiled.error_message);
    }
    REQUIRE(compiled.success);
    REQUIRE(compiled.bytecode.wgsl_src);
    REQUIRE(compiled.bytecode.msl_src);
    return true;
}

static bool particle_vertex_program(bool indexed, bool lit)
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
    if (lit) {
        for (int i = 3; i < 6; i++) {
            int output = shader_graph_add(&graph, SHADER_PARAMETER, i == 5 ? 1 : 3);
            graph.nodes[output].parameter = i;
            graph.output[i] = output;
        }
        graph.output_count = 6;
    }
    shader_error error = { 0 };
    REQUIRE(shader_graph_particle_validate(&graph, &error));
    glsl = shader_graph_glsl_named(&graph, "sg_particle", &error);
    REQUIRE(glsl);

    REQUIRE(compile_particle_vertex(NULL, graph.output_count));
    if (indexed) {
        REQUIRE(strstr(compiled.bytecode.wgsl_src, "instance_index"));
    }
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "particle_frame"));
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "in_instance_offset"));
    REQUIRE(strstr(compiled.bytecode.msl_src, "in_instance_offset"));
    if (lit) {
        REQUIRE(strstr(compiled.bytecode.wgsl_src, "in_normal"));
        REQUIRE(strstr(compiled.bytecode.wgsl_src, "scene_light_positions"));
        REQUIRE(strstr(compiled.bytecode.msl_src, "scene_light_positions"));
    }
    return true;
}

TEST_CASE(particle_vertex_program_transpiles_instanced_geometry_to_wgsl)
{
    return particle_vertex_program(true, false);
}

TEST_CASE(particle_vertex_keeps_instance_buffer_when_program_ignores_index)
{
    return particle_vertex_program(false, false);
}

TEST_CASE(lit_particles_compile_nonuniform_shape_rotation_and_scene_lights)
{
    return particle_vertex_program(true, true);
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

static const char
    *direct_code = "#include \"Trajectory.glsl\"\n"
                   "ChibiParticle particles(ChibiParticleInput p, "
                   "ChibiParameters u) {\n"
                   " ChibiParticle result=chibi_particle();\n"
                   " float total=0.0; for(int i=0;i<3;i++){ "
                   "total+=trajectory(float(i)+p.time); }\n"
                   " vec3 shift; locate(p.index,p.count,shift);\n"
                   " result.position=shift+u.Axis+vec3(u.Offset,0.0);\n"
                   " result.size=total*u.Gain; result.color=u.Tint;\n"
                   " result.rotation=vec3(p.seed); "
                   "result.scale=vec3(1.0,2.0,1.0); result.lighting=1.0;\n"
                   " return result;\n"
                   "}\n";
static const char
    *trajectory_code = "float trajectory(float t) { return "
                       "exp(-abs(t))*cos(t); }\n"
                       "void locate(float index,float count,out vec3 position) "
                       "{ position=vec3(index/max(count,1.0),0.0,0.0); }\n";
static const char *stateful_code =
    "ChibiParticleState advect(ChibiParticleInput p, ChibiParameters u, "
    "ChibiParticleState state) {\n"
    " if (p.reset) { state.position=vec3(p.index/max(p.count,1.0),p.seed,0.0); "
    "state.velocity=u.Axis; state.age=0.0; return state; }\n"
    " state.velocity+=vec3(u.Offset,u.Gain)*p.delta;\n"
    " state.position+=state.velocity*p.delta; state.age+=p.delta; return "
    "state;\n}\n"
    "ChibiParticle particles(ChibiParticleInput p, ChibiParameters u) {\n"
    " ChibiParticle result=chibi_particle(); result.position=p.position;\n"
    " result.rotation=p.velocity*p.time; result.size=1.0+p.age+p.delta;\n"
    " result.color=p.reset?vec4(1.0):u.Tint; return result;\n}\n";

static void included_particle()
{
    source_particle(direct_code);
    apush(
        source_asset.text.includes,
        ((shader_text_include){ xstrdup("Trajectory.glsl"), xstrdup(trajectory_code) })
    );
}

TEST_CASE(direct_particle_compiles_standard_functions_and_typed_parameters)
{
    included_particle();
    shader_error error = { 0 };
    glsl = shader_text_particle_glsl(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    );
    REQUIRE(glsl);
    REQUIRE(compile_particle_vertex(&source_asset.text, 6));
    REQUIRE(compiled.bytecode.wgsl_src);
    REQUIRE(compiled.bytecode.msl_src);
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "instance_index"));
    REQUIRE(strstr(compiled.bytecode.msl_src, "in_instance_offset"));
    REQUIRE(render_shader_text_validate(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    ));
    return true;
}

TEST_CASE(direct_particle_reports_original_include_path_and_line)
{
    included_particle();
    free(source_asset.text.includes[0].code);
    source_asset.text.includes[0].code = xstrdup("// authored helper\nfloat trajectory(float t) {\n return missing_value;\n}\n");
    shader_error error = { 0 };
    REQUIRE(!render_shader_text_validate(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    ));
    REQUIRE(strstr(error.message, "Trajectory.glsl:3:"));
    REQUIRE(strstr(error.message, "missing_value"));
    return true;
}

TEST_CASE(stateful_particle_compiles_render_and_update_with_typed_parameters)
{
    source_particle(stateful_code);
    source_asset.text.update = xstrdup("advect");
    shader_error error = { 0 };
    glsl = shader_text_particle_glsl(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    );
    REQUIRE(glsl);
    REQUIRE(compile_particle_vertex(&source_asset.text, 6));
    REQUIRE(strstr(compiled.bytecode.wgsl_src, "particle_step"));
    REQUIRE(strstr(compiled.bytecode.msl_src, "particle_step"));
    REQUIRE(render_shader_text_validate(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    ));
    return true;
}

TEST_CASE(stateful_particle_rejects_wrong_update_result_and_missing_entry)
{
    source_particle(
        "float advect(ChibiParticleInput p,ChibiParameters "
        "u,ChibiParticleState state) { return p.delta; }\n"
        "ChibiParticle particles(ChibiParticleInput p,ChibiParameters u) { "
        "return chibi_particle(); }\n"
    );
    source_asset.text.update = xstrdup("advect");
    shader_error error = { 0 };
    REQUIRE(!render_shader_text_validate(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    ));
    REQUIRE(*error.message);
    free(source_asset.text.update);
    source_asset.text.update = xstrdup("missing_update");
    REQUIRE(!render_shader_text_validate(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    ));
    REQUIRE(strstr(error.message, "missing_update"));
    return true;
}

TEST_CASE(direct_particle_refuses_unbundled_include)
{
    source_particle("#include \"not-bundled.glsl\"\nChibiParticle particles(ChibiParticleInput p,ChibiParameters u){return chibi_particle();}\n");
    shader_error error = { 0 };
    REQUIRE(!render_shader_text_validate(
        &source_asset.text,
        source_asset.names,
        source_asset.widths,
        &error
    ));
    REQUIRE(strstr(error.message, "not-bundled.glsl"));
    return true;
}

TEST_CASE(direct_particle_prepared_roundtrip_owns_sources_and_defaults)
{
    included_particle();
    shader_error error = { 0 };
    size_t length = 0;
    REQUIRE(shader_asset_prepared_write(&source_asset, &encoded, &length, &error));
    size_t budget = 8u * 1024u * 1024u;
    REQUIRE(shader_asset_prepared_read_budget(encoded, length, &budget, &decoded, &error));
    REQUIRE(decoded.kind == SHADER_ASSET_PARTICLE_TEXT);
    REQUIRE(!decoded.text.update);
    REQUIRE(acount(decoded.graph.nodes) == 0);
    REQUIRE(!strcmp(decoded.text.code, direct_code));
    REQUIRE(acount(decoded.text.includes) == 1);
    REQUIRE(!strcmp(decoded.text.includes[0].code, trajectory_code));
    REQUIRE(decoded.widths[4] == 2 && decoded.widths[5] == 3 && decoded.widths[6] == 4);
    REQUIRE(decoded.defaults[5][0] == 1.25f);
    shader_prepared_asset_free(&source_asset);
    REQUIRE(render_shader_text_validate(&decoded.text, decoded.names, decoded.widths, &error));
    return true;
}

TEST_CASE(stateful_particle_prepared_roundtrip_owns_update_and_compiles)
{
    source_particle(stateful_code);
    source_asset.text.update = xstrdup("advect");
    shader_error error = { 0 };
    size_t length = 0, budget = 8u * 1024u * 1024u;
    REQUIRE(shader_asset_prepared_write(&source_asset, &encoded, &length, &error));
    REQUIRE(shader_asset_prepared_read_budget(encoded, length, &budget, &decoded, &error));
    REQUIRE(decoded.text.update != source_asset.text.update);
    shader_prepared_asset_free(&source_asset);
    REQUIRE(!strcmp(decoded.text.update, "advect"));
    REQUIRE(!strcmp(decoded.text.code, stateful_code));
    REQUIRE(render_shader_text_validate(&decoded.text, decoded.names, decoded.widths, &error));
    return true;
}

TEST_CASE(direct_particle_prepared_failures_preserve_destination_and_budget)
{
    included_particle();
    shader_error error = { 0 };
    size_t length = 0;
    REQUIRE(shader_asset_prepared_write(&source_asset, &encoded, &length, &error));
    decoded.name = xstrdup("retained");
    char *retained = decoded.name;
    size_t budget = 8u * 1024u * 1024u;
    REQUIRE(!shader_asset_prepared_read_budget(encoded, length - 1, &budget, &decoded, &error));
    REQUIRE(decoded.name == retained && !strcmp(decoded.name, "retained"));
    REQUIRE(budget == 8u * 1024u * 1024u);
    budget = 1;
    REQUIRE(!shader_asset_prepared_read_budget(encoded, length, &budget, &decoded, &error));
    REQUIRE(decoded.name == retained && budget == 1);
    return true;
}

void test_tool_shader_webgpu()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(direct_particle_compiles_standard_functions_and_typed_parameters);
    RUN_TEST_CASE(stateful_particle_compiles_render_and_update_with_typed_parameters);
    RUN_TEST_CASE(stateful_particle_rejects_wrong_update_result_and_missing_entry);
    RUN_TEST_CASE(direct_particle_reports_original_include_path_and_line);
    RUN_TEST_CASE(direct_particle_refuses_unbundled_include);
    RUN_TEST_CASE(direct_particle_prepared_roundtrip_owns_sources_and_defaults);
    RUN_TEST_CASE(stateful_particle_prepared_roundtrip_owns_update_and_compiles);
    RUN_TEST_CASE(direct_particle_prepared_failures_preserve_destination_and_budget);
    RUN_TEST_CASE(noise_shader_transpiles_without_ambiguous_wgsl_comparison);
    RUN_TEST_CASE(particle_vertex_program_transpiles_instanced_geometry_to_wgsl);
    RUN_TEST_CASE(particle_vertex_keeps_instance_buffer_when_program_ignores_index);
    RUN_TEST_CASE(lit_particles_compile_nonuniform_shape_rotation_and_scene_lights);
}
