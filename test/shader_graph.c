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
    behavior_asset *asset = behavior_asset_load(json);
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
    behavior_asset *library = load_example("test/fixtures/content/shader-libraries/ShaderMath.kdl");
    assert(behavior_asset_validate_library(library) == NULL);
    behavior_asset *ring = load_linked_example("test/fixtures/content/materials/PulseRing.kdl", library);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error;
    assert(shader_graph_from_graph(ring->graph, &ir, &error));
    shader_sample_context sample = { .position = { 35, 0 } };
    float result[8][4], params[32][4] = { { 0 } };
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

    behavior_asset *gradient = load_linked_example("test/fixtures/content/materials/OutlineGradient.kdl", library);
    assert(shader_graph_from_graph(gradient->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    float right_red = result[0][0];
    sample.position[0] = -35;
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    assert(right_red > result[0][0]);
    behavior_asset_free(gradient);
    behavior_asset_free(library);

    behavior_asset *shape = load_example("test/fixtures/content/shapes/SmoothUnion.kdl");
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

    behavior_asset *flash = load_example("test/fixtures/content/behaviors/DamageFlashBehavior.kdl");
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
    assert(render_shader_parameters_validate(shader, "(array)Tint 1 -2 0.5; Opacity 4; \n", &error));
    assert(!render_shader_parameters_validate(shader, "(array)Tint 1; \n", &error));
    assert(strstr(error.message, "array of 3"));
    assert(!render_shader_parameters_validate(shader, "(array)Opacity 4; \n", &error));
    assert(strstr(error.message, "finite number or boolean"));
    assert(!render_shader_parameters_validate(shader, "Missing 1; \n", &error));
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
        load_example("test/fixtures/content/shader-libraries/ShaderMath.kdl")
    );
    game_live_definition *ring = game_live_definition_new(
        "PulseRing",
        load_example("test/fixtures/content/materials/PulseRing.kdl")
    );
    game_live_definition *gradient = game_live_definition_new(
        "OutlineGradient",
        load_example("test/fixtures/content/materials/OutlineGradient.kdl")
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
    float result[8][4];
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
    float values[32][4] = { 0 }, out[8][4];
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
    char *json = behavior_asset_save(asset);
    behavior_asset *copy = behavior_asset_load(json);
    assert(copy && shader_graph_from_graph(copy->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, out, &error));
    near(out[0][0], 0.253253296f);
    free(json);
    behavior_asset_free(copy);
    behavior_asset_free(asset);
    shader_graph_free(&ir);
}

static int derivative_node(shader_graph *g, shader_op op, int width, int a, int b, int c, int d)
{
    int id = shader_graph_add(g, op, width);
    g->nodes[id].input[0] = a;
    g->nodes[id].input[1] = b;
    g->nodes[id].input[2] = c;
    g->nodes[id].input[3] = d;
    return id;
}

static int derivative_constant(shader_graph *g, int width, float x, float y, float z)
{
    int id = shader_graph_add(g, SHADER_CONSTANT, width);
    g->nodes[id].value[0] = x;
    g->nodes[id].value[1] = y;
    g->nodes[id].value[2] = z;
    return id;
}

static void derivative_program(shader_graph *g)
{
    shader_graph_init(g);
    int p = shader_graph_add(g, SHADER_OBJECT_POSITION, 3);
    int zero = derivative_constant(g, 1, 0, 0, 0);
    int one = derivative_constant(g, 1, 1, 0, 0);
    int offset = derivative_constant(g, 3, .3f, -.2f, .1f);
    int shifted = derivative_node(g, SHADER_SUBTRACT, 3, p, offset, -1, -1);
    int scale = derivative_constant(g, 3, 1.2f, .8f, 1.4f);
    int scaled = derivative_node(g, SHADER_MULTIPLY, 3, shifted, scale, -1, -1);
    int radius = shader_graph_add(g, SHADER_PARAMETER, 1);
    int sphere = derivative_node(g, SHADER_LENGTH, 1, scaled, -1, -1, -1);
    g->output[0] = derivative_node(g, SHADER_SUBTRACT, 1, sphere, radius, -1, -1);
    g->output[1] = derivative_node(g, SHADER_DOT, 1, p, p, -1, -1);
    g->output[1] = derivative_node(g, SHADER_SATURATE, 1, g->output[1], -1, -1, -1);
    int absolute = derivative_node(g, SHADER_ABS, 3, p, -1, -1, -1);
    int extent = derivative_constant(g, 3, .4f, .6f, .5f);
    int q = derivative_node(g, SHADER_SUBTRACT, 3, absolute, extent, -1, -1);
    int outside = derivative_node(g, SHADER_MAX, 3, q, zero, -1, -1);
    int outside_length = derivative_node(g, SHADER_LENGTH, 1, outside, -1, -1, -1);
    int qx = derivative_node(g, SHADER_X, 1, q, -1, -1, -1);
    int qy = derivative_node(g, SHADER_Y, 1, q, -1, -1, -1);
    int qz = derivative_node(g, SHADER_Z, 1, q, -1, -1, -1);
    int maximum = derivative_node(g, SHADER_MAX, 1, qx, qy, -1, -1);
    maximum = derivative_node(g, SHADER_MAX, 1, maximum, qz, -1, -1);
    int inside = derivative_node(g, SHADER_MIN, 1, maximum, zero, -1, -1);
    g->output[2] = derivative_node(g, SHADER_ADD, 1, outside_length, inside, -1, -1);
    int x = derivative_node(g, SHADER_X, 1, p, -1, -1, -1);
    int y = derivative_node(g, SHADER_Y, 1, p, -1, -1, -1);
    int z = derivative_node(g, SHADER_Z, 1, p, -1, -1, -1);
    int kscale = derivative_constant(g, 1, .05f, 0, 0);
    int k = derivative_node(g, SHADER_MULTIPLY, 1, x, kscale, -1, -1);
    int kbase = derivative_constant(g, 1, .8f, 0, 0);
    k = derivative_node(g, SHADER_ADD, 1, k, kbase, -1, -1);
    int plain = derivative_node(g, SHADER_LENGTH, 1, p, -1, -1, -1);
    plain = derivative_node(g, SHADER_SUBTRACT, 1, plain, one, -1, -1);
    g->output[3] = derivative_node(g, SHADER_SMOOTH_UNION, 1, g->output[0], plain, k, -1);
    int xz = derivative_node(g, SHADER_VECTOR2, 2, x, z, -1, -1);
    int cylinder = derivative_node(g, SHADER_LENGTH, 1, xz, -1, -1, -1);
    int bore = derivative_constant(g, 1, .3f, 0, 0);
    cylinder = derivative_node(g, SHADER_SUBTRACT, 1, cylinder, bore, -1, -1);
    g->output[4] = derivative_node(g, SHADER_DIFFERENCE, 1, plain, cylinder, -1, -1);
    int sine = derivative_node(g, SHADER_SIN, 1, x, -1, -1, -1);
    int cosine = derivative_node(g, SHADER_COS, 1, y, -1, -1, -1);
    int blended = derivative_node(g, SHADER_MIX, 1, sine, cosine, z, -1);
    int denominator = derivative_node(g, SHADER_MULTIPLY, 1, z, z, -1, -1);
    denominator = derivative_node(g, SHADER_ADD, 1, denominator, one, -1, -1);
    g->output[5] = derivative_node(g, SHADER_DIVIDE, 1, blended, denominator, -1, -1);
    int negative_x = derivative_constant(g, 1, -1, 0, 0);
    negative_x = derivative_node(g, SHADER_MULTIPLY, 1, x, negative_x, -1, -1);
    g->output[5] = derivative_node(g, SHADER_CLAMP, 1, g->output[5], y, negative_x, -1);
    int time = shader_graph_add(g, SHADER_TIME, 1);
    int four = derivative_node(g, SHADER_VECTOR4, 4, x, y, z, time);
    g->output[6] = derivative_node(g, SHADER_DOT, 1, four, four, -1, -1);
    int three = derivative_node(g, SHADER_VECTOR3, 3, y, z, x, -1);
    int three_length = derivative_node(g, SHADER_LENGTH, 1, three, -1, -1, -1);
    int selected = derivative_node(g, SHADER_SELECT, 1, x, three_length, z, -1);
    int united = derivative_node(g, SHADER_UNION, 1, x, y, -1, -1);
    int intersected = derivative_node(g, SHADER_INTERSECTION, 1, x, z, -1, -1);
    int hard_csg = derivative_node(g, SHADER_ADD, 1, united, intersected, -1, -1);
    g->output[7] = derivative_node(g, SHADER_ADD, 1, hard_csg, selected, -1, -1);
    g->output_count = 8;
}

static void derivative_compound_program(shader_graph *g)
{
    shader_graph_init(g);
    int p = shader_graph_add(g, SHADER_OBJECT_POSITION, 3);
    int x = derivative_node(g, SHADER_X, 1, p, -1, -1, -1);
    int y = derivative_node(g, SHADER_Y, 1, p, -1, -1, -1);
    int z = derivative_node(g, SHADER_Z, 1, p, -1, -1, -1);
    int dot = derivative_node(g, SHADER_DOT, 1, p, p, -1, -1);
    int length = derivative_node(g, SHADER_SQRT, 1, dot, -1, -1, -1);
    int total_sqrt = derivative_node(g, SHADER_SQRT, 1, x, -1, -1, -1);
    g->output[0] = derivative_node(g, SHADER_ADD, 1, length, total_sqrt, -1, -1);
    int normal = derivative_node(g, SHADER_NORMALIZE, 3, p, -1, -1, -1);
    g->output[1] = derivative_node(g, SHADER_X, 1, normal, -1, -1, -1);
    int xy = derivative_node(g, SHADER_VECTOR2, 2, x, y, -1, -1);
    int rotation_scale = derivative_constant(g, 1, 20, 0, 0);
    int rotation = derivative_node(g, SHADER_MULTIPLY, 1, x, rotation_scale, -1, -1);
    int offset_scale = derivative_constant(g, 1, .1f, 0, 0);
    int offset_x = derivative_node(g, SHADER_MULTIPLY, 1, y, offset_scale, -1, -1);
    int offset_y = derivative_node(g, SHADER_MULTIPLY, 1, z, offset_scale, -1, -1);
    int offset = derivative_node(g, SHADER_VECTOR2, 2, offset_x, offset_y, -1, -1);
    int scale = derivative_constant(g, 2, 1.3f, .7f, 0);
    int mapped = derivative_node(g, SHADER_MAPPING, 2, xy, offset, rotation, scale);
    int radius = shader_graph_add(g, SHADER_PARAMETER, 1);
    g->output[2] = derivative_node(g, SHADER_CIRCLE, 1, mapped, radius, -1, -1);
    int time = shader_graph_add(g, SHADER_TIME, 1);
    int angle_x = derivative_node(g, SHADER_MULTIPLY, 1, time, rotation_scale, -1, -1);
    int angle_y = derivative_node(g, SHADER_MULTIPLY, 1, y, rotation_scale, -1, -1);
    int angle_z = derivative_constant(g, 1, 25, 0, 0);
    int rotation3 = derivative_node(g, SHADER_VECTOR3, 3, angle_x, angle_y, angle_z, -1);
    int offset3 = derivative_node(g, SHADER_MULTIPLY, 3, p, offset_scale, -1, -1);
    int scale3 = derivative_constant(g, 3, 1.2f, .8f, 1.4f);
    int mapped3 = derivative_node(g, SHADER_MAPPING3D, 3, p, offset3, rotation3, scale3);
    g->output[3] = derivative_node(g, SHADER_LENGTH, 1, mapped3, -1, -1, -1);
    int map_x = derivative_node(g, SHADER_X, 1, mapped3, -1, -1, -1);
    int map_z = derivative_node(g, SHADER_Z, 1, mapped3, -1, -1, -1);
    int map_xz = derivative_node(g, SHADER_VECTOR2, 2, map_x, map_z, -1, -1);
    int extent = derivative_constant(g, 2, .4f, .5f, 0);
    g->output[4] = derivative_node(g, SHADER_BOX, 1, map_xz, extent, -1, -1);
    int one = derivative_constant(g, 1, 1, 0, 0);
    int negative = derivative_constant(g, 1, -1, 0, 0);
    int low = derivative_node(g, SHADER_ADD, 1, y, one, -1, -1);
    int high = derivative_node(g, SHADER_MULTIPLY, 1, low, negative, -1, -1);
    int from = derivative_node(g, SHADER_VECTOR2, 2, low, high, -1, -1);
    int to_high = derivative_node(g, SHADER_ADD, 1, x, one, -1, -1);
    int to = derivative_node(g, SHADER_VECTOR2, 2, z, to_high, -1, -1);
    int remapped = derivative_node(g, SHADER_REMAP, 2, xy, from, to, radius);
    g->output[5] = derivative_node(g, SHADER_LENGTH, 1, remapped, -1, -1, -1);
    int reversed = derivative_node(g, SHADER_MULTIPLY, 1, y, negative, -1, -1);
    g->output[6] = derivative_node(g, SHADER_SMOOTHSTEP, 1, x, y, reversed, -1);
    g->output[7] = derivative_node(g, SHADER_SMOOTH_UNION, 1, g->output[2], g->output[4], radius, -1);
    g->output_count = 8;
}

static void gradient_codegen()
{
    shader_graph g;
    derivative_program(&g);
    shader_error error = { 0 };
    int original = g.output[0];
    g.output[0] = derivative_node(&g, SHADER_ABS, 1, original, -1, -1, -1);
    int world = shader_graph_add(&g, SHADER_WORLD_POSITION, 3);
    int unsupported = derivative_node(&g, SHADER_LENGTH, 1, world, -1, -1, -1);
    char *source = shader_graph_glsl_gradient_named(&g, "test_gradient", &error);
    assert(
        source && strstr(source, "vec3 test_gradient(") &&
        strstr(source, "sg_gradient_tolerance*length(") &&
        !strstr(source, "vec4(sg_sample.world_position")
    );
    free(source);
    g.output[0] = unsupported;
    assert(!shader_graph_glsl_gradient_named(&g, "test_gradient", &error));
    assert(!error.message[0]);
    int uv = shader_graph_add(&g, SHADER_UV, 2);
    int uv_length = derivative_node(&g, SHADER_LENGTH, 1, uv, -1, -1, -1);
    int noise = derivative_node(&g, SHADER_NOISE, 1, uv, derivative_constant(&g, 1, 1, 0, 0), -1, -1);
    int unsupported_outputs[] = { uv_length, noise };
    for (int i = 0; i < 2; i++) {
        /* Unreachable unsupported expressions must not disable a valid shape. */
        g.output[0] = original;
        source = shader_graph_glsl_gradient_named(&g, "test_gradient", &error);
        assert(source);
        free(source);
        g.output[0] = unsupported_outputs[i];
        assert(!shader_graph_glsl_gradient_named(&g, "test_gradient", &error));
        assert(!error.message[0]);
    }
    g.output[0] = original;
    assert(!shader_graph_glsl_gradient_named(&g, "invalid-name", &error));
    assert(error.message[0]);
    shader_graph_free(&g);

    /* Compound lowering has its own bounded budget, without imposing a new
     * node-count contract on a valid authored graph. */
    shader_graph_init(&g);
    int position = shader_graph_add(&g, SHADER_OBJECT_POSITION, 3);
    int offset = derivative_constant(&g, 3, .1f, .2f, .3f);
    int rotation = derivative_constant(&g, 3, 10, 20, 30);
    int scale = derivative_constant(&g, 3, 1, 1, 1);
    for (int i = 0; i < 64; i++) {
        position = derivative_node(&g, SHADER_MAPPING3D, 3, position, offset, rotation, scale);
    }
    g.output[0] = derivative_node(&g, SHADER_LENGTH, 1, position, -1, -1, -1);
    g.output_count = 1;
    error = (shader_error){ 0 };
    assert(shader_graph_validate(&g, &error));
    assert(!shader_graph_glsl_gradient_named(&g, "over_budget", &error));
    assert(!error.message[0]);
    shader_graph_free(&g);
}

/* Compare generated derivatives to central differences of the independent CPU
 * value interpreter, rather than duplicating the derivative formulas in tests. */
static void gradient_gpu(bool compound)
{
    shader_graph g;
    if (compound) {
        derivative_compound_program(&g);
    } else {
        derivative_program(&g);
    }
    int outputs[8];
    memcpy(outputs, g.output, sizeof outputs);
    shader_error error = { 0 };
    char *source = NULL;
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof name, "gradient_%d", i);
        g.output[0] = outputs[i];
        char *function = shader_graph_glsl_gradient_named(&g, name, &error);
        assert(function);
        for (const char *p = function; *p; p++) {
            apush(source, *p);
        }
        free(function);
    }
    int zero = derivative_constant(&g, 3, 0, 0, 0);
    int constant_length = derivative_node(&g, SHADER_LENGTH, 1, zero, -1, -1, -1);
    int constant_root_input = derivative_constant(&g, 1, 0, 0, 0);
    int constant_root = derivative_node(&g, SHADER_SQRT, 1, constant_root_input, -1, -1, -1);
    constant_length = derivative_node(&g, SHADER_ADD, 1, constant_length, constant_root, -1, -1);
    int constant_normal = derivative_node(&g, SHADER_NORMALIZE, 3, zero, -1, -1, -1);
    int constant_normal_x = derivative_node(&g, SHADER_X, 1, constant_normal, -1, -1, -1);
    constant_length = derivative_node(&g, SHADER_ADD, 1, constant_length, constant_normal_x, -1, -1);
    int spatial_length = derivative_node(&g, SHADER_LENGTH, 1, 0, -1, -1, -1);
    int x = derivative_node(&g, SHADER_X, 1, 0, -1, -1, -1);
    const int singularity_inputs[] = { constant_length, spatial_length };
    const char *singularity_names[] = { "gradient_constant_zero",
                                        "gradient_spatial_zero" };
    for (int i = 0; i < 2; i++) {
        g.output[0] = derivative_node(&g, SHADER_ADD, 1, singularity_inputs[i], x, -1, -1);
        char *function = shader_graph_glsl_gradient_named(&g, singularity_names[i], &error);
        assert(function);
        for (const char *p = function; *p; p++) {
            apush(source, *p);
        }
        free(function);
    }
    int scalar_zero = derivative_constant(&g, 1, 0, 0, 0);
    int scalar_one = derivative_constant(&g, 1, 1, 0, 0);
    int square = derivative_node(&g, SHADER_MULTIPLY, 1, x, x, -1, -1);
    int selection = derivative_node(&g, SHADER_SELECT, 1, scalar_zero, scalar_one, square, -1);
    int y = derivative_node(&g, SHADER_Y, 1, 0, -1, -1, -1);
    int smooth_zero = derivative_node(&g, SHADER_SMOOTH_UNION, 1, x, x, y, -1);
    int square_root = derivative_node(&g, SHADER_SQRT, 1, x, -1, -1, -1);
    int normalized = derivative_node(&g, SHADER_NORMALIZE, 3, 0, -1, -1, -1);
    int normalized_x = derivative_node(&g, SHADER_X, 1, normalized, -1, -1, -1);
    const int discontinuity_outputs[] = {
        derivative_node(&g, SHADER_ADD, 1, selection, x, -1, -1),
        smooth_zero,
        derivative_node(&g, SHADER_ADD, 1, square_root, x, -1, -1),
        derivative_node(&g, SHADER_ADD, 1, normalized_x, x, -1, -1)
    };
    const char *discontinuity_names[] = { "gradient_select_zero",
                                          "gradient_smooth_zero",
                                          "gradient_sqrt_zero",
                                          "gradient_normalize_zero" };
    for (int i = 0; i < 4; i++) {
        g.output[0] = discontinuity_outputs[i];
        char *function = shader_graph_glsl_gradient_named(&g, discontinuity_names[i], &error);
        assert(function);
        for (const char *p = function; *p; p++) {
            apush(source, *p);
        }
        free(function);
    }
    const char *
        body = "layout(local_size_x=8,local_size_y=1,local_size_z=1) in;\n"
               "layout(set=1,binding=0,rgba32f) uniform image2D result_image;\n"
               "layout(set=2,binding=0) uniform Inputs { vec4 point; vec4 "
               "values[32]; };\n"
               "void main(){vec4 p[32];for(int i=0;i<32;i++)p[i]=values[i];"
               "SGSample "
               "s;s.position=vec2(0);s.uv=vec2(0);s.object_position=point.xyz;"
               "s.part_position=vec3(0);s.world_position=vec3(0);"
               "s.normal=vec3(0);s.view_direction=vec3(0);s.time=point.w;"
               "for(int i=0;i<3;i++){s.object_from_world[i]=vec3(0);"
               "s.part_from_world[i]=vec3(0);}"
               "for(int i=0;i<2;i++)s.uv_from_world[i]=vec3(0);"
               "int x=int(gl_GlobalInvocationID.x);vec3 d=vec3(0);switch(x){"
               "case 0:d=gradient_0(s,p);break;case 1:d=gradient_1(s,p);break;"
               "case 2:d=gradient_2(s,p);break;case 3:d=gradient_3(s,p);break;"
               "case 4:d=gradient_4(s,p);break;case 5:d=gradient_5(s,p);break;"
               "case 6:d=gradient_6(s,p);break;case 7:d=gradient_7(s,p);break;}"
               "if(all(equal(point.xyz,vec3(0)))) {"
               "if(x==0)d=gradient_constant_zero(s,p);"
               "if(x==1)d=gradient_spatial_zero(s,p);"
               "if(x==2)d=gradient_select_zero(s,p);"
               "if(x==4)d=gradient_smooth_zero(s,p);"
               "if(x==6)d=gradient_sqrt_zero(s,p);"
               "if(x==7)d=gradient_normalize_zero(s,p); }"
               "imageStore(result_image,ivec2(x,0),vec4(d,1));}";
    for (const char *p = body; *p; p++) {
        apush(source, *p);
    }
    apush(source, 0);
    CF_ComputeShader shader = cf_make_compute_shader_from_source(source);
    if (!shader.id) {
        fprintf(stderr, "Generated gradient shader:\n%s\n", source);
    }
    assert(shader.id);
    CF_Material material = cf_make_material();
    CF_CanvasParams canvas_params = cf_canvas_defaults(8, 1);
    canvas_params.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    canvas_params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(canvas_params);
    CF_Texture texture = cf_canvas_get_target(canvas);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
    dispatch.rw_textures = &texture;
    dispatch.rw_texture_count = 1;
    memcpy(g.output, outputs, sizeof outputs);
    const float cases[][4] = { { 1.2f, .9f, .7f, .4f },
                               { -.8f, 1.1f, -.6f, 1.3f },
                               { .9f, -.7f, .8f, -.2f },
                               { .62f, .74f, .71f, .8f },
                               { -.8f, -.9f, .6f, .3f },
                               { .2f, .3f, .4f, .7f },
                               { 0, 0, 0, 0 } };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        float params[32][4] = { { .65f + .1f * i } }, actual[8][4];
        cf_app_update(NULL);
        cf_material_set_uniform_cs(material, "point", (void *)cases[i], CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_cs(material, "values", params, CF_UNIFORM_TYPE_FLOAT4, 32);
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        CF_Readback readback = cf_canvas_readback(canvas);
        uint64_t start = cf_get_ticks();
        while (!cf_readback_ready(readback)) {
            assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
            cf_sleep(1);
        }
        assert(cf_readback_data(readback, actual, sizeof actual) == sizeof actual);
        cf_destroy_readback(readback);
        if (i == 6) {
            /* A locally constant zero length is valid; a spatial zero length
             * invalidates even a nonzero combined gradient. */
            near(actual[0][0], 1);
            near(actual[0][1], 0);
            near(actual[0][2], 0);
            assert(!isfinite(actual[1][0]));
            assert(!isfinite(actual[2][0]));
            assert(!isfinite(actual[4][0]));
            assert(!isfinite(actual[6][0]));
            assert(!isfinite(actual[7][0]));
            if (!compound) {
                assert(!isfinite(actual[3][0]));
                /* Coincident spatial clamp bounds have differing derivatives. */
                assert(!isfinite(actual[5][0]));
            }
            continue;
        }
        for (int axis = 0; axis < 3; axis++) {
            shader_sample_context sample = { .time = cases[i][3] };
            memcpy(sample.object_position, cases[i], sizeof sample.object_position);
            float high[8][4], low[8][4];
            const float epsilon = .0005f;
            sample.object_position[axis] += epsilon;
            assert(shader_graph_eval_outputs(&g, &sample, params, high, &error));
            sample.object_position[axis] -= 2 * epsilon;
            assert(shader_graph_eval_outputs(&g, &sample, params, low, &error));
            for (int output = 0; output < 8; output++) {
                float want = (high[output][0] - low[output][0]) / (2 * epsilon);
                if (!isfinite(actual[output][axis]) ||
                    fabsf(actual[output][axis] - want) > .002f) {
                    fprintf(
                        stderr,
                        "gradient case %d output %d axis %d: %.9g != %.9g\n",
                        i,
                        output,
                        axis,
                        actual[output][axis],
                        want
                    );
                    assert(false);
                }
            }
        }
    }
    cf_destroy_canvas(canvas);
    cf_destroy_material(material);
    cf_destroy_compute_shader(shader);
    afree(source);
    shader_graph_free(&g);
    printf(
        "shader gradients %s GPU PASS: generated object derivatives match "
        "independent CPU central differences; singularities invalidate\n",
        compound ? "compound transforms" : "arithmetic/CSG"
    );
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
               "SGSample sg_test;sg_test.position=vec2(0);sg_test.uv=vec2(0);"
               "sg_test.object_position=vec3(0);sg_test.part_position=vec3(0);"
               "sg_test.world_position=vec3(0);sg_test.normal=vec3(0);"
               "sg_test.view_direction=vec3(0);sg_test.time=0.0;"
               "for(int i=0;i<3;i++){sg_test.object_from_world[i]=vec3(0);"
               "sg_test.part_from_world[i]=vec3(0);}"
               "for(int i=0;i<2;i++)sg_test.uv_from_world[i]=vec3(0);"
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
        float values[32][4] = { 0 }, expected[8][4], actual[8][4];
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
    gradient_gpu(false);
    gradient_gpu(true);
    puts("shader math GPU PASS: vector operations and noise match CPU including negative and extreme inputs");
    return 0;
}

int shader_graph_gpu_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Shader graph GPU regression",
        0,
        0,
        0,
        8,
        8,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "%s\n", result.details);
        return 1;
    }
    int failed = test_shader_graph_gpu();
    cf_destroy_app();
    return failed;
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
    char *json = behavior_asset_save(asset);
    behavior_asset *loaded = behavior_asset_load(json);
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
        char *json = behavior_asset_save(asset);
        behavior_asset *copy = behavior_asset_load(json);
        shader_graph ir = { 0 };
        shader_error error = { 0 };
        assert(copy && shader_graph_from_graph(copy->graph, &ir, &error));
        float out[8][4];
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

static void vector_mix_literals_roundtrip()
{
    for (int scalar_first = 0; scalar_first < 2; scalar_first++) {
        behavior_asset *asset = behavior_asset_new("VectorMix");
        asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
        graph *g = asset->graph;
        node *mix = add(g, "Shader Mix");
        node *alpha = add(g, "Shader Constant");
        scalar(g, alpha, 1, 0, .25f);
        if (scalar_first) {
            assert(graph_connect_data(g, alpha, 0, mix, 2) == 0);
        }
        assert(graph_set_pin_type(g, mix, false, 0, PIN_STRUCT, "ShaderVec3") == 0);
        float a[] = { .06f, .16f, .36f }, b[] = { .7f, .83f, .95f };
        pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = a };
        assert(graph_set_pin_default(g, mix, false, 0, &value) == 0);
        value.ptr = b;
        assert(graph_set_pin_default(g, mix, false, 1, &value) == 0);
        if (!scalar_first) {
            assert(graph_connect_data(g, alpha, 0, mix, 2) == 0);
        }
        node *surface = shader_graph_add_output(g, false);
        assert(graph_connect_data(g, mix, 0, surface, 0) == 0);
        uint64_t id = mix->id, counter = g->id_counter;
        char *text = behavior_asset_save(asset);
        behavior_asset *loaded = behavior_asset_load(text);
        free(text);
        assert(loaded);
        node *reloaded = graph_node_by_id(loaded->graph, id);
        bool preserved = reloaded && reloaded->input_count == 3 &&
                         loaded->graph->id_counter == counter;
        for (int p = 0; preserved && p < 2; p++) {
            const pin_spec *pin = &reloaded->input_pins[p];
            preserved &= pin->type == PIN_STRUCT && pin->type_name &&
                         !strcmp(pin->type_name, "ShaderVec3") &&
                         pin->default_value.ptr &&
                         !memcmp(pin->default_value.ptr, p ? b : a, sizeof a);
        }
        assert(preserved);
        shader_graph ir;
        shader_graph_init(&ir);
        shader_error error = { 0 };
        float outputs[8][4];
        shader_sample_context sample = { 0 };
        assert(shader_graph_from_graph(loaded->graph, &ir, &error));
        assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
        for (int axis = 0; axis < 3; axis++) {
            near(outputs[0][axis], a[axis] + (b[axis] - a[axis]) * .25f);
        }
        shader_graph_free(&ir);
        behavior_asset_free(loaded);
        behavior_asset_free(asset);
    }
}

void test_shader_graph()
{
    gradient_codegen();
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
    float outputs[8][4], params[32][4] = { { 0 } };
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, outputs, &error));
    near(outputs[0][0], 3);
    params[3][0] = 4;
    assert(shader_graph_eval_outputs(&ir, &sample, params, outputs, &error));
    near(outputs[0][0], 1);
    char *glsl = shader_graph_glsl_named(&ir, "test_shape", &error);
    assert(glsl && strstr(glsl, "void test_shape(") && strstr(glsl, "sg_params[3]"));
    free(glsl);
    assert(!shader_graph_glsl_named(&ir, "invalid-name", &error));
    char *json = behavior_asset_save(asset);
    assert(json);
    behavior_asset *copy = behavior_asset_load(json);
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
    assert(ir.output_count == 8);
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

    vector_mix_literals_roundtrip();
    examples();
    live_shader_library();
    math_nodes();
    puts("shader graph PASS: parameter instances, SDF evaluation, portable GLSL, serialization, functions and cycle rejection");
}
