#include "graph/shader/shader_graph.h"

#include "graph/compiler/compile.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/type.h"
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
    assert(acount(source->graphs) == 1);
    assert(acount(linked->graphs) > 1);
    behavior_asset_free(source);
    return linked;
}

static void examples()
{
    behavior_asset *library = load_example("content/shader-libraries/ShaderMath.json");
    assert(behavior_asset_validate_library(library) == NULL);
    behavior_asset *ring = load_linked_example("content/materials/PulseRing.json", library);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error;
    assert(shader_graph_from_graph(ring->graph, &ir, &error));
    float point[2] = { 35, 0 }, result[8][4], params[32][4] = { { 0 } };
    assert(shader_graph_eval_outputs(&ir, point, point, 0, NULL, result, &error));
    near(result[0][3], 1);
    float emission = result[1][2];
    assert(shader_graph_eval_outputs(&ir, point, point, .78539816f, NULL, result, &error));
    assert(result[1][2] > emission);
    for (int i = 0; i < acount(ir.nodes); i++) {
        if (ir.nodes[i].op == SHADER_PARAMETER) {
            memcpy(params[ir.nodes[i].parameter], ir.nodes[i].value, sizeof params[0]);
        }
    }
    params[4][0] = 3;
    assert(shader_graph_eval_outputs(&ir, point, point, 0, params, result, &error));
    assert(result[1][2] > emission + 2.9f);
    behavior_asset_free(ring);

    behavior_asset *gradient = load_linked_example("content/materials/OutlineGradient.json", library);
    assert(shader_graph_from_graph(gradient->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, point, point, 0, NULL, result, &error));
    float right_red = result[0][0];
    point[0] = -35;
    assert(shader_graph_eval_outputs(&ir, point, point, 0, NULL, result, &error));
    assert(right_red > result[0][0]);
    behavior_asset_free(gradient);
    behavior_asset_free(library);

    behavior_asset *shape = load_example("content/shapes/SmoothUnion.json");
    assert(shader_graph_from_graph(shape->graph, &ir, &error));
    point[0] = point[1] = 0;
    assert(shader_graph_eval_outputs(&ir, point, point, 0, NULL, result, &error));
    near(result[0][0], -9);
    point[0] = shape->bounds[0];
    point[1] = shape->bounds[1];
    assert(shader_graph_eval_outputs(&ir, point, point, 0, NULL, result, &error));
    assert(result[0][0] > 0);
    behavior_asset_free(shape);
    shader_graph_free(&ir);

    behavior_asset *flash = load_example("content/behaviors/DamageFlashBehavior.json");
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(flash, &report) == 0);
    compile_report_free(&report);
    behavior_asset_free(flash);
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
        load_example("content/shader-libraries/ShaderMath.json")
    );
    game_live_definition *ring = game_live_definition_new(
        "PulseRing",
        load_example("content/materials/PulseRing.json")
    );
    game_live_definition *gradient = game_live_definition_new(
        "OutlineGradient",
        load_example("content/materials/OutlineGradient.json")
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
    float point[2] = { 35, 0 }, result[8][4];
    assert(shader_graph_from_graph(ring->applied->graph, &ir, &error));
    assert(shader_graph_eval_outputs(&ir, point, point, 0, NULL, result, &error));
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

void test_shader_graph()
{
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
    float local[2] = { 3, 4 }, world[2] = { 10, 20 }, outputs[8][4],
          params[32][4] = { { 0 } };
    assert(shader_graph_eval_outputs(&ir, local, world, 0, NULL, outputs, &error));
    near(outputs[0][0], 3);
    params[3][0] = 4;
    assert(shader_graph_eval_outputs(&ir, local, world, 0, params, outputs, &error));
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
    assert(shader_graph_eval_outputs(&ir, local, world, 0, NULL, outputs, &error));
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
    assert(shader_graph_eval_outputs(&ir, local, world, 0, NULL, outputs, &error));
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
    assert(shader_graph_eval_outputs(&ir, local, world, 0, NULL, outputs, &error));
    near(outputs[0][0], 1);
    near(outputs[0][3], 1);
    near(outputs[1][0], 0);
    near(outputs[3][0], 0);
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
    assert(shader_graph_eval_outputs(&ir, local, world, 0, NULL, outputs, &error));
    near(outputs[0][0], 0);
    graph_free(&specialized);
    shader_graph_free(&ir);

    examples();
    live_shader_library();
    puts("shader graph PASS: parameter instances, SDF evaluation, portable GLSL, serialization, functions and cycle rejection");
}
