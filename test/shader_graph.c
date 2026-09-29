#include "graph/shader/shader_graph.h"

#include "graph/compiler/compile.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/type.h"
#include "graph/nodes/conv_nodes.h"
#include "platform/cute.h"
#include "render/shader_catalog.h"
#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node *add(graph *g, const char *name)
{
    const node_desc *d = registry_node_by_name(name);
    assert(d);
    return graph_add_node(g, d);
}
static void scalar(graph *g, node *n, int output, int pin, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(graph_set_pin_default(g, n, output, pin, &v) == 0);
}
static void near(float a, float b)
{
    assert(fabsf(a - b) < 0.0001f);
}
static behavior_asset *load_example(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *json = malloc((size_t)size + 1);
    assert(json);
    assert(fread(json, 1, (size_t)size, file) == (size_t)size);
    json[size] = 0;
    fclose(file);
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    assert(asset);
    return asset;
}

static behavior_asset *load_linked_example(const char *path, const behavior_asset *library)
{
    behavior_asset *source = load_example(path);
    const behavior_asset *catalog[] = { library };
    char error[256] = { 0 };
    behavior_asset *linked = behavior_asset_link_libraries(source, catalog, 1, error, sizeof error);
    assert(linked);
    assert(acount(linked->graphs) > acount(source->graphs));
    behavior_asset_free(source);
    return linked;
}

static void examples()
{
    behavior_asset *library = load_example(
        "test/fixtures/content/shader-libraries/ShaderMath.yaml"
    );
    assert(behavior_asset_validate_library(library) == NULL);
    behavior_asset *ring = load_linked_example("test/fixtures/content/materials/PulseRing.yaml", library);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error;
    assert(shader_graph_from_graph(ring->graph, &ir, &error));
    shader_sample_context sample = { .position = { 35, 0 } };
    float result[SHADER_OUTPUT_MAX][4], params[32][4] = { { 0 } };
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    near(result[5][0], 1);
    near(result[6][0], 1);
    float emission = result[1][2];
    sample.time = .78539816f;
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(result[1][2] > emission);
    for (int i = 0; i < acount(ir.nodes); i++) {
        if (ir.nodes[i].op == SHADER_PARAMETER) {
            memcpy(params[ir.nodes[i].parameter], ir.nodes[i].value, sizeof params[0]);
        }
    }
    sample.time = 0;
    params[4][0] = 3;
    assert(shader_graph_eval_outputs(&ir, &sample, params, result, &error));
    assert(result[1][2] > emission + 2.9f);
    sample.position[0] = 0;
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    near(result[5][0], 0);
    sample.position[0] = 35;
    behavior_asset_free(ring);

    behavior_asset *gradient = load_linked_example(
        "test/fixtures/content/materials/OutlineGradient.yaml",
        library
    );
    assert(shader_graph_from_graph(gradient->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    float right_red = result[0][0];
    sample.position[0] = -35;
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(right_red > result[0][0]);
    behavior_asset_free(gradient);
    behavior_asset_free(library);

    behavior_asset *shape = load_example("test/fixtures/content/shapes/SmoothUnion.yaml");
    assert(shader_graph_from_graph(shape->graph, &ir, &error));
    sample.position[0] = sample.position[1] = 0;
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    near(result[0][0], -9);
    sample.position[0] = shape->bounds[0];
    sample.position[1] = shape->bounds[1];
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(result[0][0] > 0);
    behavior_asset_free(shape);
    shader_graph_free(&ir);

    behavior_asset *flash = load_example(
        "test/fixtures/content/behaviors/DamageFlashBehavior.yaml"
    );
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(flash, &report) == 0);
    compile_report_free(&report);
    behavior_asset_free(flash);
}

static void preview_parameter_validation()
{
    behavior_asset *asset = behavior_asset_new("Preview parameters");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *output = shader_graph_add_output(g, false);
    node *color = add(g, "Shader Parameter");
    node *opacity = add(g, "Shader Parameter");
    assert(graph_set_node_reference(g, color, 0, "Tint") == 0);
    assert(graph_set_node_reference(g, opacity, 1, "Opacity") == 0);
    assert(graph_set_pin_type(g, color, 1, 0, PIN_STRUCT, "ShaderVec3") == 0);
    float tint[3] = { 1, 1, 1 };
    pin_value color_default = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = tint };
    assert(graph_set_pin_default(g, color, 1, 0, &color_default) == 0);
    scalar(g, opacity, 1, 0, 1);
    assert(graph_connect_data(g, color, 0, output, 0) == 0);
    assert(graph_connect_data(g, opacity, 0, output, 5) == 0);
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    int shader = render_shader_find(asset->name);
    assert(shader);
    assert(render_shader_parameters_validate(shader, "{\"Tint\":[1,-2,0.5],\"Opacity\":4}", &error));
    assert(!render_shader_parameters_validate(shader, "{\"Tint\":[1]}", &error));
    assert(strstr(error.message, "array of 3"));
    assert(!render_shader_parameters_validate(shader, "{\"Opacity\":[4]}", &error));
    assert(strstr(error.message, "finite number or boolean"));
    assert(!render_shader_parameters_validate(shader, "{\"Missing\":1}", &error));
    assert(strstr(error.message, "Unknown shader preview parameter"));
    behavior_asset_free(asset);
    render_shader_catalog_clear();
}

static void live_shader_library()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    game_live_definition *library = game_live_definition_new(
        "ShaderMath",
        load_example("test/fixtures/content/shader-libraries/ShaderMath.yaml")
    );
    game_live_definition *ring = game_live_definition_new(
        "PulseRing",
        load_example("test/fixtures/content/materials/PulseRing.yaml")
    );
    game_live_definition *gradient = game_live_definition_new(
        "OutlineGradient",
        load_example("test/fixtures/content/materials/OutlineGradient.yaml")
    );
    assert(library && ring && gradient);
    assert(game_live_add_definition(live, library) == 0);
    assert(game_live_add_definition(live, ring) == 0);
    assert(game_live_add_definition(live, gradient) == 0);
    behavior_asset *old_ring = ring->applied, *old_gradient = gradient->applied;
    graph *pulse = behavior_asset_find_graph_by_name(library->working, "Pulse");
    assert(pulse);
    node *half = NULL;
    for (int i = 0; i < acount(pulse->nodes); i++) {
        node *n = pulse->nodes[i];
        if (!strcmp(n->desc->name, "Shader Multiply") &&
            n->input_pins[1].default_value.f == .5f) {
            half = n;
        }
    }
    assert(half);
    scalar(pulse, half, 0, 1, .25f);
    assert(game_live_mark_dirty(library) == 0);
    char message[256];
    assert(game_live_preview(live, library, message, sizeof message) == 0);
    assert(ring->applied != old_ring && gradient->applied != old_gradient);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error;
    shader_sample_context sample = { .position = { 35, 0 } };
    float result[SHADER_OUTPUT_MAX][4];
    assert(shader_graph_from_graph(ring->applied->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    near(result[1][2], .55f);
    shader_graph_free(&ir);
    old_ring = ring->applied;
    old_gradient = gradient->applied;
    unsigned revision = render_shader_revision();
    graph_add_node(pulse, &NODE_Delay);
    assert(game_live_mark_dirty(library) == 0);
    assert(game_live_preview(live, library, message, sizeof message) < 0);
    assert(ring->applied == old_ring && gradient->applied == old_gradient);
    assert(render_shader_revision() == revision);
    game_live_world_free(live);
    ecs_world_free(world);
    render_shader_catalog_clear();
}

static void math_program(shader_graph *ir)
{
    shader_graph_init(ir);
    for (int i = 0; i < 4; i++) {
        int parameter = shader_graph_add(ir, SHADER_PARAMETER, i ? 1 : 3);
        ir->nodes[parameter].parameter = i;
    }
    shader_op operations[] = { SHADER_NORMALIZE,  SHADER_CLAMP,
                               SHADER_SATURATE,   SHADER_STEP,
                               SHADER_SMOOTHSTEP, SHADER_NOISE };
    for (int i = 0; i < 6; i++) {
        int index = shader_graph_add(ir, operations[i], i == 5 ? 1 : 3);
        shader_node *n = &ir->nodes[index];
        n->input[0] = 0;
        n->input[1] = i == 5 ? 3 : 1;
        n->input[2] = 2;
        ir->output[i] = index;
    }
    int channels[3];
    for (int i = 0; i < 3; i++) {
        channels[i] = shader_graph_add(ir, SHADER_X + i, 1);
        ir->nodes[channels[i]].input[0] = 0;
    }
    int xy = shader_graph_add(ir, SHADER_VECTOR2, 2);
    ir->nodes[xy].input[0] = channels[0];
    ir->nodes[xy].input[1] = channels[1];
    int noise2 = shader_graph_add(ir, SHADER_NOISE, 1);
    ir->nodes[noise2].input[0] = xy;
    ir->nodes[noise2].input[1] = 3;
    ir->output[6] = noise2;
    int xyz = shader_graph_add(ir, SHADER_VECTOR3, 3);
    memcpy(ir->nodes[xyz].input, channels, sizeof channels);
    ir->output[7] = xyz;
    ir->output_count = 8;
}

static const float math_cases[][4][4] = {
    { { 3, 4, 0 }, { 0 }, { 1 }, { 0.25f } },
    { { 0, 0, 0 }, { 0 }, { 0 }, { 1 } },
    { { -0.25f, 0.5f, 1.5f }, { 1 }, { 0 }, { 2 } },
    { { 1e20f, -2e20f, 3e20f }, { -1 }, { 1 }, { 0.75f } },
    { { 1e-20f, -2e-20f, 3e-20f }, { 0 }, { 1 }, { 4096 } },
    { { 4095.75f, -0.25f, 0.125f }, { -0.5f }, { 2 }, { 1 } },
    { { -4096.25f, -7.25f, 2.125f }, { 0.25f }, { 0.25f }, { -1 } },
    { { 1, 2, 3 }, { 0 }, { 1 }, { 1 } },
    { { -1, -2, -3 }, { 0 }, { 1 }, { 1 } },
};

static void math_nodes()
{
    shader_graph ir;
    math_program(&ir);
    shader_sample_context sample = { 0 };
    float values[32][4] = { 0 }, out[SHADER_OUTPUT_MAX][4];
    shader_error error;
    for (int i = 0; i < (int)(sizeof math_cases / sizeof *math_cases); i++) {
        memcpy(values, math_cases[i], sizeof math_cases[i]);
        assert(shader_graph_eval_outputs(&ir, &sample, values, out, &error));
        for (int j = 0; j < 8; j++) {
            for (int k = 0; k < ir.nodes[ir.output[j]].width; k++) {
                assert(isfinite(out[j][k]));
            }
        }
        if (i == 0) {
            near(out[0][0], 0.6f);
            near(out[0][1], 0.8f);
            near(out[1][0], 1);
            near(out[5][0], out[6][0]);
        }
        if (i == 1) {
            near(out[0][0], 0);
            near(out[4][0], 1);
        }
        if (i == 2) {
            near(out[1][0], 0);
            near(out[1][2], 1);
            near(out[4][0], 1);
            near(out[4][1], 0.5f);
            near(out[4][2], 0);
        }
        if (i == 3 || i == 4) {
            near(out[0][0], 1 / sqrtf(14));
            near(out[0][1], -2 / sqrtf(14));
            near(out[0][2], 3 / sqrtf(14));
        }
        if (i == 7) {
            near(out[5][0], 0.500847012f);
        }
        if (i == 8) {
            near(out[5][0], 0.913893337f);
        }
        assert(out[5][0] >= 0 && out[5][0] <= 1);
    }
    values[0][0] = 0.25f;
    values[0][1] = values[0][2] = 0.5f;
    assert(shader_graph_eval_outputs(&ir, &sample, values, out, &error));
    float first = out[5][0];
    values[0][0] += 4096;
    assert(shader_graph_eval_outputs(&ir, &sample, values, out, &error));
    near(out[5][0], first);
    values[0][0] = -0.0001f;
    assert(shader_graph_eval_outputs(&ir, &sample, values, out, &error));
    first = out[5][0];
    values[0][0] = 0.0001f;
    assert(shader_graph_eval_outputs(&ir, &sample, values, out, &error));
    near(out[5][0], first);
    shader_graph_free(&ir);

    behavior_asset *asset = behavior_asset_new("Procedural math");
    graph *g = asset->graph;
    node *vector = add(g, "Shader Vector3");
    scalar(g, vector, 0, 0, 1);
    scalar(g, vector, 0, 1, 2);
    scalar(g, vector, 0, 2, 3);
    node *previous = vector;
    const char *chain[] = { "Shader Normalize",  "Shader Clamp",
                            "Shader Saturate",   "Shader Step",
                            "Shader Smoothstep", "Shader Noise" };
    for (int i = 0; i < 6; i++) {
        node *n = add(g, chain[i]);
        assert(graph_connect_data(g, previous, 0, n, 0) == 0);
        if (i == 1 || i == 4) {
            assert(n->input_pins[2].type == PIN_F32);
            near(n->input_pins[2].default_value.f, 1);
        }
        previous = n;
    }
    node *root = shader_graph_add_output(g, true);
    assert(graph_connect_data(g, previous, 0, root, 0) == 0);
    char *json = behavior_asset_save_json(asset);
    behavior_asset *copy = behavior_asset_load_json(json);
    assert(copy && shader_graph_from_graph(copy->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, out, &error));
    near(out[0][0], 0.253253296f);
    free(json);
    behavior_asset_free(copy);
    behavior_asset_free(asset);
    shader_graph_free(&ir);
}

/* Runs inside the existing native render checks' active Cute context. */
int test_shader_graph_gpu()
{
    shader_graph ir;
    math_program(&ir);
    shader_error error;
    char *function = shader_graph_glsl_named(&ir, "test_math", &error);
    assert(function);
    const char *
        body = "layout(local_size_x=8,local_size_y=1,local_size_z=1) in;\n"
               "layout(set=1,binding=0,rgba32f) uniform image2D result_image;\n"
               "layout(set=2,binding=0) uniform Inputs { vec4 values[32]; };\n"
               "void main(){vec4 result[8],p[32]; for(int i=0;i<32;i++) "
               "p[i]=values[i];"
               "SGSample sg_test=SGSample(vec2(0),vec2(0),vec3(0),"
               "vec3(0),vec3(0),vec3(0),0.0);"
               "test_math(sg_test,p,result);"
               "int x=int(gl_GlobalInvocationID.x); "
               "imageStore(result_image,ivec2(x,0),result[x]);}\n";
    char *source = malloc(strlen(function) + strlen(body) + 1);
    assert(source);
    strcpy(source, function);
    strcat(source, body);
    CF_ComputeShader shader = cf_make_compute_shader_from_source(source);
    assert(shader.id);
    CF_Material material = cf_make_material();
    CF_CanvasParams canvas_params = cf_canvas_defaults(8, 1);
    canvas_params.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    canvas_params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(canvas_params);
    assert(canvas.id);
    CF_Texture texture = cf_canvas_get_target(canvas);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
    dispatch.rw_textures = &texture;
    dispatch.rw_texture_count = 1;
    for (int i = 0; i < (int)(sizeof math_cases / sizeof *math_cases); i++) {
        shader_sample_context sample = { 0 };
        float values[32][4] = { 0 }, expected[SHADER_OUTPUT_MAX][4],
              actual[SHADER_OUTPUT_MAX][4];
        memcpy(values, math_cases[i], sizeof math_cases[i]);
        assert(shader_graph_eval_outputs(&ir, &sample, values, expected, &error));
        cf_app_update(NULL);
        cf_material_set_uniform_cs(material, "values", values, CF_UNIFORM_TYPE_FLOAT4, 32);
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        CF_Readback readback = cf_canvas_readback(canvas);
        assert(readback.id);
        uint64_t start = cf_get_ticks();
        while (!cf_readback_ready(readback)) {
            assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
            cf_sleep(1);
        }
        assert(cf_readback_data(readback, actual, sizeof actual) == sizeof actual);
        cf_destroy_readback(readback);
        for (int output = 0; output < ir.output_count; output++) {
            for (int channel = 0; channel < ir.nodes[ir.output[output]].width;
                 channel++) {
                float want = expected[output][channel], got = actual[output][channel];
                if (!isfinite(got) ||
                    fabsf(got - want) > 0.0001f * fmaxf(1, fabsf(want))) {
                    fprintf(stderr, "shader math GPU case %d output %d channel %d: %.9g != %.9g\n", i, output, channel, got, want);
                    assert(false);
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
    puts("shader math GPU PASS: vector operations and noise match CPU including negative and extreme inputs");
    return 0;
}

static void parameter_slots()
{
    behavior_asset *asset = behavior_asset_new("Parameter slot conflicts");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *root = shader_graph_add_output(g, false);
    node *first = add(g, "Shader Parameter"), *second = add(g, "Shader Parameter");
    assert(graph_set_node_reference(g, first, 2, "Roughness") == 0);
    assert(graph_set_node_reference(g, second, 2, "Opacity") == 0);
    scalar(g, first, 1, 0, .3f);
    scalar(g, second, 1, 0, .7f);
    assert(graph_connect_data(g, first, 0, root, 2) == 0);
    assert(graph_connect_data(g, first, 0, root, 5) == 0);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(shader_graph_from_graph(g, &ir, &error));
    assert(graph_connect_data(g, second, 0, root, 5) == 0);
    /* Loading a serialized collision must fail without replacing the last valid program. */
    char *json = behavior_asset_save_json(asset);
    behavior_asset *loaded = behavior_asset_load_json(json);
    assert(loaded);
    free(json);
    assert(!shader_graph_from_graph(loaded->graph, &ir, &error));
    assert(error.node == second->id && strstr(error.message, "different slots"));
    float result[SHADER_OUTPUT_MAX][4];
    assert(shader_graph_eval_outputs(&ir, &(shader_sample_context){ 0 }, NULL, result, &error));
    near(result[2][0], .3f);
    near(result[5][0], .3f);
    behavior_asset_free(loaded);
    assert(graph_set_node_reference(g, second, 7, "Opacity") == 0);
    assert(shader_graph_from_graph(g, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &(shader_sample_context){ 0 }, NULL, result, &error));
    near(result[5][0], .7f);
    assert(graph_set_node_reference(g, second, UINT64_C(1) << 32, "Opacity") == 0);
    assert(!shader_graph_from_graph(g, &ir, &error));
    assert(error.node == second->id && strstr(error.message, "between 0 and 31"));

    graph *function = behavior_asset_add_function(asset, "Shared parameter", 1);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec pin = { .name = "Value", .type = PIN_F32, .kind = PIN_DATA };
    assert(graph_add_pin(function, ret, 0, &pin) >= 0);
    node *shared = add(function, "Shader Parameter");
    assert(graph_set_node_reference(function, shared, 2, "Shared") == 0);
    assert(graph_connect_data(function, shared, 0, ret, 0) == 0);
    node *call = behavior_asset_add_function_call_node(asset, function);
    assert(graph_connect_data(g, call, 0, root, 5) == 0);
    assert(!shader_graph_from_graph(g, &ir, &error));
    assert(error.node == shared->id && strstr(error.message, "different slots"));
    assert(graph_set_node_reference(function, shared, 7, "Shared") == 0);
    assert(shader_graph_from_graph(g, &ir, &error));
    /* Inlining the same parameter node through two calls is one shared parameter. */
    node *other_call = behavior_asset_add_function_call_node(asset, function);
    assert(graph_connect_data(g, other_call, 0, root, 2) == 0);
    assert(shader_graph_from_graph(g, &ir, &error));
    shader_graph_free(&ir);
    behavior_asset_free(asset);
}

static void vector_inline_sources()
{
    behavior_asset *asset = behavior_asset_new("Vector sources");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *root = shader_graph_add_output(g, false);
    node *constant = add(g, "Shader Constant");
    scalar(g, constant, true, 0, .375f);
    const node_desc *conversion = conversion_for_pins(&constant->output_pins[0], &root->input_pins[0]);
    assert(conversion && graph_connect_data_conversion(g, constant, 0, root, 0, conversion) == 0);
    assert(graph_set_node_presentation(g, constant, NODE_PRESENTATION_INLINE) == 0);
    for (int mixed = 0; mixed < 2; mixed++) {
        if (mixed) {
            node *vector = add(g, "Shader Vector3");
            scalar(g, vector, false, 1, .5f);
            scalar(g, vector, false, 2, .75f);
            assert(graph_connect_data(g, vector, 0, root, 0) == 0);
            assert(graph_set_node_presentation(g, vector, NODE_PRESENTATION_INLINE) == 0);
            assert(graph_connect_data(g, constant, 0, vector, 0) == 0);
            assert(graph_node_is_inline(g, constant));
        }
        char *json = behavior_asset_save_json(asset);
        behavior_asset *copy = behavior_asset_load_json(json);
        shader_graph ir = { 0 };
        shader_error error = { 0 };
        assert(copy && shader_graph_from_graph(copy->graph, &ir, &error));
        float out[SHADER_OUTPUT_MAX][4];
        shader_sample_context sample = { 0 };
        assert(shader_graph_eval_outputs(&ir, &sample, NULL, out, &error));
        near(out[0][0], .375f);
        near(out[0][1], mixed ? .5f : .375f);
        near(out[0][2], mixed ? .75f : .375f);
        shader_graph_free(&ir);
        behavior_asset_free(copy);
        free(json);
    }
    behavior_asset_free(asset);
}

void test_shader_graph()
{
    vector_inline_sources();
    parameter_slots();
    preview_parameter_validation();
    /* Reflected scalar reads must clear a previous array tag before serialization. */
    float component = 0.6f;
    type_field field = { .name = "x",
                         .type = PIN_F32,
                         .size = sizeof(float),
                         .type_name = "float" };
    pin_value read = { .container = PIN_CONTAINER_ARRAY };
    pin_read_field(&component, &field, &read);
    assert(read.container == PIN_CONTAINER_SINGLE && read.f == component);
    behavior_asset *asset = behavior_asset_new("Shader test");
    graph *g = asset->graph;
    node *root = shader_graph_add_output(g, true);
    node *position = add(g, "Shader Position");
    node *radius = add(g, "Shader Parameter");
    radius->reference_id = 3;
    scalar(g, radius, 1, 0, 2);
    node *circle = add(g, "Shader Circle");
    assert(graph_connect_data(g, position, 0, circle, 0) == 0);
    assert(graph_connect_data(g, radius, 0, circle, 1) == 0);
    assert(graph_connect_data(g, circle, 0, root, 0) == 0);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(shader_graph_from_graph(g, &ir, &error));
    shader_sample_context sample = { .position = { 3, 4 },
                                     .world_position = { 10, 20, 0 } };
    float outputs[SHADER_OUTPUT_MAX][4], params[32][4] = { { 0 } };
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
    near(outputs[0][0], 3);
    params[3][0] = 4;
    assert(shader_graph_eval_outputs(&ir, &sample, params, outputs, &error));
    near(outputs[0][0], 1);
    char *glsl = shader_graph_glsl_named(&ir, "test_shape", &error);
    assert(glsl && strstr(glsl, "void test_shape(") && strstr(glsl, "sg_params[3]"));
    free(glsl);
    assert(!shader_graph_glsl_named(&ir, "invalid-name", &error));
    char *json = behavior_asset_save_json(asset);
    assert(json);
    behavior_asset *copy = behavior_asset_load_json(json);
    assert(copy);
    free(json);
    assert(shader_graph_from_graph(copy->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
    near(outputs[0][0], 3);
    behavior_asset_free(copy);

    /* Local authored pure functions are inlined with explicit arguments. */
    graph *fn = behavior_asset_add_function(asset, "Double distance", 1);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    pin_spec pin = { .name = "Distance", .type = PIN_F32, .kind = PIN_DATA };
    assert(graph_add_pin(fn, entry, 1, &pin) >= 0);
    assert(graph_add_pin(fn, ret, 0, &pin) >= 0);
    node *multiply = add(fn, "Shader Multiply");
    scalar(fn, multiply, 0, 1, 2);
    assert(graph_connect_data(fn, entry, 0, multiply, 0) == 0);
    assert(graph_connect_data(fn, multiply, 0, ret, 0) == 0);
    node *call = behavior_asset_add_function_call_node(asset, fn);
    assert(call);
    assert(graph_connect_data(g, circle, 0, call, 0) == 0);
    assert(graph_connect_data(g, call, 0, root, 0) == 0);
    assert(shader_graph_from_graph(g, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
    near(outputs[0][0], 6);

    /* Reject a cycle and preserve the previously compiled program. */
    assert(graph_connect_data(fn, multiply, 0, multiply, 0) == 0);
    int previous = acount(ir.nodes);
    assert(!shader_graph_from_graph(g, &ir, &error));
    assert(error.node == multiply->id);
    assert(acount(ir.nodes) == previous);
    behavior_asset_free(asset);
    shader_graph_free(&ir);

    graph material;
    graph_init(&material);
    shader_graph_add_output(&material, false);
    assert(shader_graph_from_graph(&material, &ir, &error));
    assert(ir.output_count == SHADER_OUTPUT_MAX);
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
    near(outputs[0][0], 1);
    near(outputs[0][1], 1);
    near(outputs[0][2], 1);
    near(outputs[1][0], 0);
    near(outputs[2][0], .5f);
    near(outputs[3][0], 0);
    near(outputs[4][0], 0);
    near(outputs[5][0], 1);
    near(outputs[6][0], 0);
    near(outputs[7][0], 0);
    graph_free(&material);
    shader_graph_free(&ir);
    /* Editing a scalar default before connecting a vector must discard the old union bits. */
    graph specialized;
    graph_init(&specialized);
    node *vector_position = add(&specialized, "Shader Position");
    node *vector_multiply = add(&specialized, "Shader Multiply");
    scalar(&specialized, vector_multiply, 0, 1, 7);
    assert(graph_connect_data(&specialized, vector_position, 0, vector_multiply, 0) == 0);
    assert(vector_multiply->input_pins[1].type == PIN_STRUCT);
    assert(vector_multiply->input_pins[1].default_value.ptr == NULL);
    node *vector_length = add(&specialized, "Shader Length");
    node *distance_output = shader_graph_add_output(&specialized, true);
    assert(graph_connect_data(&specialized, vector_multiply, 0, vector_length, 0) == 0);
    assert(graph_connect_data(&specialized, vector_length, 0, distance_output, 0) == 0);
    assert(shader_graph_from_graph(&specialized, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
    near(outputs[0][0], 0);
    graph_free(&specialized);
    shader_graph_free(&ir);

    examples();
    live_shader_library();
    math_nodes();
    puts("shader graph PASS: parameter instances, SDF evaluation, portable GLSL, serialization, functions and cycle rejection");
}
