#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *model(const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    return asset;
}
static void scalar(graph *g, node *n, int pin, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(g, n, false, pin, &v));
}
static void vector(graph *g, node *n, int pin, float x, float y, float z)
{
    float xyz[3] = { x, y, z };
    pin_value v = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = xyz };
    assert(!graph_set_pin_default(g, n, false, pin, &v));
}
static void connect(graph *g, node *from, node *to, int pin)
{
    assert(!graph_connect_data(g, from, 0, to, pin));
}
static node *instance(graph *g, const char *asset)
{
    node *n = graph_add_node(g, &NODE_ModelInstance);
    n->reference_asset = graph_own_str(g, asset, strlen(asset));
    return n;
}
static node *translated(graph *g, node *source, float x)
{
    node *n = graph_add_node(g, &NODE_ModelTranslate);
    connect(g, source, n, 0);
    vector(g, n, 1, x, 0, 0);
    return n;
}
static void publish(graph *g, node *source)
{
    connect(g, source, model_graph_add_output(g), 0);
}
static float sample(const model_program *program, float x, float y)
{
    shader_sample_context context = { .object_position = { x, y, 0 } };
    float values[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(shader_graph_eval_outputs(&program->shader, &context, NULL, values, &error));
    return values[0][0];
}
static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < .0001f);
}
static bool dependency(const model_program *program, const char *name)
{
    for (int i = 0; i < acount(program->dependencies); i++) {
        if (!strcmp(program->dependencies[i], name)) {
            return true;
        }
    }
    return false;
}
static void composed_geometry_function()
{
    behavior_asset *asset = model("ComposedOffsets");
    graph *g = asset->graph;
    graph *function = behavior_asset_add_function(asset, "Offset Twice", true);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec geometry = { .name = "Geometry",
                          .type = PIN_HANDLE,
                          .type_name = "Geometry3D",
                          .default_value = { .type = PIN_HANDLE,
                                             .type_name = "Geometry3D" } };
    assert(graph_add_pin(function, entry, true, &geometry) == 0);
    assert(graph_add_pin(function, ret, false, &geometry) == 0);
    connect(function, translated(function, entry, 2), ret, 0);
    node *inner = behavior_asset_add_function_call_node(asset, function);
    node *outer = behavior_asset_add_function_call_node(asset, function);
    assert(inner && outer);
    connect(g, graph_add_node(g, &NODE_ModelSphere), inner, 0);
    connect(g, inner, outer, 0);
    publish(g, outer);
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    assert(model_compile(asset, &program, &error));
    near(sample(&program, 4, 0), -1);
    near(sample(&program, 5, 0), 0);
    near(sample(&program, 0, 0), 3);
    node *output = g->nodes[acount(g->nodes) - 1];
    assert(output->desc == &NODE_ModelOutput);
    output->disabled = true;
    assert(!model_compile(asset, &program, &error));
    assert(error.node == output->id);
    near(sample(&program, 4, 0), -1);
    model_program_free(&program);
    behavior_asset_free(asset);
}
void test_model_reuse()
{
    composed_geometry_function();
    behavior_asset *base = model("ReusableCapsule"), *parent = model("CapsulePair");
    graph *g = base->graph, *p = parent->graph;
    node *parameter = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, parameter, 0, "Half Segment"));
    scalar(g, parameter, 0, 2);
    scalar(g, parameter, 1, 0);
    scalar(g, parameter, 2, 10);
    node *capsule = graph_add_node(g, &NODE_ModelCapsule);
    scalar(g, capsule, 0, 1);
    connect(g, parameter, capsule, 1);
    node *part = graph_add_node(g, &NODE_ModelPart);
    assert(!graph_set_node_reference(g, part, 0, "Body"));
    connect(g, capsule, part, 0);
    publish(g, part);
    node *left = instance(p, base->name), *right = instance(p, base->name);
    pin_spec override = { .name = "Half Segment",
                          .type = PIN_F32,
                          .declaration_id = parameter->id,
                          .default_value = { .type = PIN_F32, .f = 0 } };
    assert(graph_add_pin(p, right, false, &override) == 0);
    node *pair = graph_add_node(p, &NODE_ModelUnion);
    connect(p, translated(p, left, -10), pair, 0);
    connect(p, translated(p, right, 10), pair, 1);
    publish(p, pair);
    const behavior_asset *catalog[] = { base, parent };
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    assert(model_compile_with_catalog(parent, catalog, 2, &program, &error));
    near(sample(&program, -10, 5), 2);
    near(sample(&program, 10, 5), 4);
    assert(dependency(&program, base->name));
    assert(acount(program.parts) == 2);
    assert(program.parts[0].node == part->id && program.parts[1].node == part->id);
    assert(strcmp(program.parts[0].path, program.parts[1].path));
    char *identity[2] = { xstrdup(program.parts[0].path),
                          xstrdup(program.parts[1].path) };
    assert(identity[0] && identity[1]);

    scalar(g, parameter, 0, 4);
    assert(model_compile_with_catalog(parent, catalog, 2, &program, &error));
    near(sample(&program, -10, 5), 0);
    near(sample(&program, 10, 5), 4);
    assert(!graph_set_node_reference(g, parameter, 0, "Segment Length"));
    assert(!graph_set_node_reference(g, part, 0, "Hull"));
    assert(model_compile_with_catalog(parent, catalog, 2, &program, &error));
    near(sample(&program, -10, 5), 0);
    near(sample(&program, 10, 5), 4);
    for (int i = 0; i < 2; i++) {
        assert(!strcmp(identity[i], program.parts[i].path));
        assert(!strcmp(program.parts[i].name, "Hull"));
        free(identity[i]);
    }

    char *base_source = behavior_asset_save(base),
         *parent_source = behavior_asset_save(parent);
    behavior_asset *base_reload = behavior_asset_load(base_source),
                   *parent_reload = behavior_asset_load(parent_source);
    assert(base_reload && parent_reload);
    const behavior_asset *reloaded[] = { base_reload, parent_reload };
    assert(model_compile_with_catalog(parent_reload, reloaded, 2, &program, &error));
    near(sample(&program, -10, 5), 0);
    near(sample(&program, 10, 5), 4);
    assert(program.parts[0].node == part->id && program.parts[1].node == part->id);
    assert(!model_compile_with_catalog(parent, NULL, 0, &program, &error));
    assert(error.node == left->id || error.node == right->id);
    near(sample(&program, -10, 5), 0);
    near(sample(&program, 10, 5), 4);

    behavior_asset *a = model("CycleA"), *b = model("CycleB");
    node *ab = instance(a->graph, b->name), *ba = instance(b->graph, a->name);
    publish(a->graph, ab);
    publish(b->graph, ba);
    const behavior_asset *cycle[] = { a, b };
    assert(!model_compile_with_catalog(a, cycle, 2, &program, &error));
    assert(error.node == ab->id || error.node == ba->id);
    assert(strstr(error.message, "cycle") || strstr(error.message, "recursive"));
    near(sample(&program, 10, 5), 4);

    behavior_asset *array = model("CapsuleRow");
    graph *row = array->graph;
    node *repeated = graph_add_node(row, &NODE_ModelLinearArray);
    connect(row, instance(row, base->name), repeated, 0);
    pin_value count = { .type = PIN_I32, .i = 3 };
    assert(!graph_set_pin_default(row, repeated, false, 1, &count));
    vector(row, repeated, 2, 6, 0, 0);
    publish(row, repeated);
    const behavior_asset *array_catalog[] = { base, array };
    assert(model_compile_with_catalog(array, array_catalog, 2, &program, &error));
    near(sample(&program, 0, 0), -1);
    near(sample(&program, 6, 0), -1);
    near(sample(&program, 12, 0), -1);
    near(sample(&program, 3, 0), 2);
    assert(acount(program.parts) == 3);
    for (int i = 0; i < 3; i++) {
        assert(program.parts[i].node == part->id);
        for (int j = 0; j < i; j++) {
            assert(strcmp(program.parts[i].path, program.parts[j].path));
        }
    }

    model_program_free(&program);
    behavior_asset_free(array);
    behavior_asset_free(a);
    behavior_asset_free(b);
    behavior_asset_free(base_reload);
    behavior_asset_free(parent_reload);
    free(base_source);
    free(parent_source);
    behavior_asset_free(base);
    behavior_asset_free(parent);
    puts("Model references, inherited defaults, zero overrides, stable identities, roundtrip and dependency diagnostics: PASS");
}
