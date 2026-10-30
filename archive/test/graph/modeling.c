#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/core_nodes.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < .00001f;
}
static void radius(graph *g, node *sphere, float value)
{
    pin_value literal = { .type = PIN_F32, .f = value };
    assert(graph_set_pin_default(g, sphere, 0, 0, &literal) == 0);
}
static float sample(const model_program *program, float x, float y, float z)
{
    shader_sample_context context = { .object_position = { x, y, z } };
    float values[SHADER_OUTPUT_MAX][4];
    shader_error error;
    assert(shader_graph_eval_outputs(&program->shader, &context, NULL, values, &error));
    return values[0][0];
}
TEST_CASE(sphere_distances_bounds_roundtrip_and_diagnostics)
{
    REQUIRE(registry_node_by_name("Model Sphere") == &NODE_ModelSphere);
    REQUIRE(registry_node_by_name("Model Output") == &NODE_ModelOutput);
    behavior_asset *asset = behavior_asset_new("SphereModel");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    node *output = model_graph_add_output(g);
    radius(g, sphere, 5);
    shader_error error;
    model_program program;
    model_program_init(&program);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node == output->id && strstr(error.message, "required"));
    REQUIRE(program.bounds.empty && acount(program.shader.nodes) == 0);
    REQUIRE(graph_connect_data(g, sphere, 0, output, 0) == 0);
    REQUIRE(model_compile(asset, &program, &error));
    REQUIRE(model_graph_validate(g, &error));
    REQUIRE(near(sample(&program, 0, 0, 0), -5));
    REQUIRE(near(sample(&program, 5, 0, 0), 0));
    REQUIRE(near(sample(&program, 0, -5, 0), 0));
    REQUIRE(near(sample(&program, 0, 0, 6), 1));
    REQUIRE(near(sample(&program, 3, 4, 12), 8));
    REQUIRE(near(sample(&program, -3, -4, -12), 8));
    model_bounds bounds;
    REQUIRE(model_program_bounds(&program, NULL, &bounds, &error));
    REQUIRE(!bounds.empty);
    for (int axis = 0; axis < 3; axis++) {
        REQUIRE(near(bounds.min[axis], -5));
        REQUIRE(near(bounds.max[axis], 5));
    }
    char *source = behavior_asset_save(asset);
    REQUIRE(source && strstr(source, "model"));
    behavior_asset *reloaded = behavior_asset_load(source);
    REQUIRE(reloaded && reloaded->domain == BEHAVIOR_DOMAIN_MODEL);
    REQUIRE(reloaded->graph->nodes[0]->id == sphere->id);
    REQUIRE(reloaded->graph->nodes[1]->id == output->id);
    model_program loaded;
    model_program_init(&loaded);
    REQUIRE(model_compile(reloaded, &loaded, &error));
    REQUIRE(near(sample(&loaded, 3, 4, 12), 8));
    for (int i = 0; i < acount(loaded.shader.nodes); i++) {
        REQUIRE(loaded.shader.nodes[i].source_id == sphere->id);
    }
    free(source);
    behavior_asset_free(reloaded);
    model_program_free(&loaded);
    float invalid[] = { 0, -1, INFINITY, NAN };
    for (int i = 0; i < 4; i++) {
        radius(g, sphere, invalid[i]);
        REQUIRE(!model_compile(asset, &program, &error));
        REQUIRE(error.node == sphere->id && strstr(error.message, "positive"));
        REQUIRE(near(sample(&program, 3, 4, 12), 8));
        REQUIRE(near(program.bounds.max[0], 5));
    }
    float overflow[] = { 1e20f, 3e38f };
    for (int i = 0; i < 2; i++) {
        radius(g, sphere, overflow[i]);
        REQUIRE(!model_compile(asset, &program, &error));
        REQUIRE(error.node == sphere->id && strstr(error.message, "distance range"));
        REQUIRE(near(sample(&program, 3, 4, 12), 8));
        REQUIRE(near(program.bounds.max[0], 5));
    }
    model_program large;
    model_program_init(&large);
    radius(g, sphere, 1e18f);
    REQUIRE(model_compile(asset, &large, &error));
    float corner = sample(&large, 1e18f, -1e18f, 1e18f);
    REQUIRE(isfinite(corner) && corner > 0);
    REQUIRE(fabsf(corner / 1e18f - (sqrtf(3) - 1)) < .00001f);
    model_program_free(&large);
    radius(g, sphere, 2);
    REQUIRE(model_compile(asset, &program, &error));
    REQUIRE(near(sample(&program, 0, 0, 0), -2));
    REQUIRE(near(program.bounds.max[2], 2));
    node *unconnected = graph_add_node(g, &NODE_ModelSphere);
    radius(g, unconnected, -3);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node == unconnected->id);
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

static void model_vector(graph *g, node *n, int pin, float x, float y, float z)
{
    float data[3] = { x, y, z };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = data };
    assert(graph_set_pin_default(g, n, 0, pin, &value) == 0);
}
static void model_number(graph *g, node *n, int pin, float x)
{
    pin_value value = { .type = PIN_F32, .f = x };
    assert(graph_set_pin_default(g, n, 0, pin, &value) == 0);
}
static behavior_asset *model_asset()
{
    behavior_asset *asset = behavior_asset_new("SolidModel");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    return asset;
}
static node *model_node(graph *g, node_desc *descriptor)
{
    node *n = graph_add_node(g, descriptor);
    assert(n);
    return n;
}
static void connect_model(graph *g, node *source, node *target, int pin)
{
    assert(graph_connect_data(g, source, 0, target, pin) == 0);
}
static void finish_model(behavior_asset *asset, node *root, model_program *program)
{
    node *output = model_graph_add_output(asset->graph);
    connect_model(asset->graph, root, output, 0);
    model_program_init(program);
    shader_error error;
    if (!model_compile(asset, program, &error)) {
        fprintf(
            stderr,
            "Model fixture compile failed at %llu: %s\n",
            (unsigned long long)error.node,
            error.message
        );
        assert(false);
    }
}
static node *translated_sphere(graph *g, float r, float x)
{
    node *sphere = model_node(g, &NODE_ModelSphere);
    model_number(g, sphere, 0, r);
    node *translate = model_node(g, &NODE_ModelTranslate);
    model_vector(g, translate, 1, x, 0, 0);
    connect_model(g, sphere, translate, 0);
    return translate;
}
TEST_CASE(primitive_solids)
{
    node_desc *solids[] = { &NODE_ModelBox,
                            &NODE_ModelRoundedBox,
                            &NODE_ModelCylinder,
                            &NODE_ModelCapsule,
                            &NODE_ModelTorus };
    for (int i = 0; i < 5; i++) {
        behavior_asset *asset = model_asset();
        graph *g = asset->graph;
        node *solid = model_node(g, solids[i]);
        if (i < 2) {
            model_vector(g, solid, 0, 2, 3, 4);
            if (i == 1) {
                model_number(g, solid, 1, .5f);
            }
        } else {
            model_number(g, solid, 0, i == 4 ? 3 : 2);
            model_number(g, solid, 1, i == 4 ? 1 : 3);
        }
        model_program program;
        finish_model(asset, solid, &program);
        if (i == 0) {
            REQUIRE(near(sample(&program, 0, 0, 0), -2));
            REQUIRE(near(sample(&program, 2, 3, 4), 0));
            REQUIRE(near(sample(&program, 3, 4, 5), sqrtf(3)));
        }
        if (i == 1) {
            REQUIRE(near(sample(&program, 2, 0, 0), 0));
            REQUIRE(near(sample(&program, 2, 3, 4), (sqrtf(3) - 1) * .5f));
            REQUIRE(near(program.bounds.max[2], 4));
        }
        if (i == 2) {
            REQUIRE(near(sample(&program, 0, 0, 0), -2));
            REQUIRE(near(sample(&program, 0, 3, 0), 0));
            REQUIRE(near(sample(&program, 3, 4, 0), sqrtf(2)));
        }
        if (i == 3) {
            REQUIRE(near(sample(&program, 0, 5, 0), 0));
            REQUIRE(near(sample(&program, 3, 3, 0), 1));
            REQUIRE(near(sample(&program, 3, 7, 0), 3));
            REQUIRE(near(program.bounds.max[1], 5));
        }
        if (i == 4) {
            REQUIRE(near(sample(&program, 3, 0, 0), -1));
            REQUIRE(near(sample(&program, 3, 1, 0), 0));
            REQUIRE(near(sample(&program, 0, 0, 0), 2));
            REQUIRE(near(sample(&program, 0, 0, 4), 0));
        }
        for (int corner = 0; corner < 8; corner++) {
            float p[3];
            for (int axis = 0; axis < 3; axis++) {
                p[axis] = (corner & (1 << axis)) ? program.bounds.max[axis]
                                                 : program.bounds.min[axis];
            }
            REQUIRE(isfinite(sample(&program, p[0], p[1], p[2])));
        }
        model_program_free(&program);
        behavior_asset_free(asset);
    }
    return true;
}
TEST_CASE(transformed_solids)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *box = model_node(g, &NODE_ModelBox);
    model_vector(g, box, 0, 1, 2, 3);
    node *translate = model_node(g, &NODE_ModelTranslate);
    model_vector(g, translate, 1, 2, 3, 4);
    connect_model(g, box, translate, 0);
    node *rotate = model_node(g, &NODE_ModelRotate);
    model_vector(g, rotate, 1, 0, 0, 1.57079632679f);
    connect_model(g, translate, rotate, 0);
    model_program program;
    finish_model(asset, rotate, &program);
    REQUIRE(near(sample(&program, -3, 2, 4), -1));
    REQUIRE(near(sample(&program, -3, 3, 4), 0));
    REQUIRE(near(program.bounds.min[0], -5));
    REQUIRE(near(program.bounds.max[0], -1));
    REQUIRE(near(program.bounds.min[1], 1));
    REQUIRE(near(program.bounds.max[1], 3));
    model_vector(g, translate, 1, 0, 0, 0);
    model_vector(g, rotate, 1, 1.57079632679f, 1.57079632679f, 1.57079632679f);
    shader_error error;
    REQUIRE(model_compile(asset, &program, &error));
    /* Independent successive quarter turns: (1,2,3) -> (1,-3,2)
     * -> (2,-3,-1) -> (3,2,-1). */
    REQUIRE(near(sample(&program, 3, 2, -1), 0));
    REQUIRE(near(program.bounds.max[0], 3));
    REQUIRE(near(program.bounds.max[1], 2));
    REQUIRE(near(program.bounds.max[2], 1));
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    node *sphere = model_node(g, &NODE_ModelSphere);
    node *scale = model_node(g, &NODE_ModelScale);
    model_vector(g, scale, 1, 2, 1, .5f);
    connect_model(g, sphere, scale, 0);
    finish_model(asset, scale, &program);
    REQUIRE(near(sample(&program, 2, 0, 0), 0));
    REQUIRE(near(sample(&program, 0, 0, .5f), 0));
    REQUIRE(near(sample(&program, 3, 0, 0), .25f));
    REQUIRE(near(program.bounds.max[0], 2));
    REQUIRE(near(program.bounds.max[2], .5f));
    float ray = 4;
    for (int step = 0; step < 100; step++) {
        float distance = sample(&program, ray, 0, 0);
        REQUIRE(distance >= 0 && distance <= ray - 2 + .00001f);
        ray -= distance;
        REQUIRE(ray >= 2 - .00001f);
    }
    REQUIRE(ray < 2.0001f);
    for (int x = -4; x <= 4; x++) {
        for (int y = -4; y <= 4; y++) {
            for (int z = -4; z <= 4; z++) {
                float p[3] = { x * .4f, y * .2f, z * .1f };
                double implicit = p[0] * p[0] / 4.0 + p[1] * p[1] +
                                  p[2] * p[2] / .25 - 1;
                float d = sample(&program, p[0], p[1], p[2]);
                REQUIRE(isfinite(d));
                if (fabs(implicit) > .00001) {
                    REQUIRE((d < 0) == (implicit < 0));
                }
                float gradient[3];
                for (int axis = 0; axis < 3; axis++) {
                    float a[3] = { p[0], p[1], p[2] }, b[3] = { p[0], p[1], p[2] };
                    a[axis] += .001f;
                    b[axis] -= .001f;
                    gradient[axis] = (sample(&program, a[0], a[1], a[2]) -
                                      sample(&program, b[0], b[1], b[2])) /
                                     .002f;
                }
                REQUIRE(
                    gradient[0] * gradient[0] + gradient[1] * gradient[1] +
                        gradient[2] * gradient[2] <=
                    1.001f
                );
            }
        }
    }
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    node *offset = translated_sphere(g, 1, 3);
    node *reflect = model_node(g, &NODE_ModelReflect);
    connect_model(g, offset, reflect, 0);
    finish_model(asset, reflect, &program);
    REQUIRE(near(sample(&program, -3, 0, 0), -1));
    REQUIRE(near(program.bounds.min[0], -4));
    REQUIRE(near(program.bounds.max[0], -2));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(boolean_solids)
{
    node_desc *operations[] = { &NODE_ModelUnion,
                                &NODE_ModelIntersection,
                                &NODE_ModelDifference,
                                &NODE_ModelSmoothUnion };
    for (int i = 0; i < 4; i++) {
        behavior_asset *asset = model_asset();
        graph *g = asset->graph;
        node *a = translated_sphere(g, i == 2 ? 3 : 1, i == 2 ? 0 : -1);
        node *b = translated_sphere(g, 1, i == 2 ? 0 : 1);
        node *combine = model_node(g, operations[i]);
        connect_model(g, a, combine, 0);
        connect_model(g, b, combine, 1);
        if (i == 3) {
            model_number(g, combine, 2, 1);
        }
        model_program program;
        finish_model(asset, combine, &program);
        if (i == 0) {
            REQUIRE(near(sample(&program, 0, 0, 0), 0));
            REQUIRE(near(sample(&program, 3, 0, 0), 1));
            REQUIRE(near(program.bounds.min[0], -2));
        }
        if (i == 1) {
            REQUIRE(program.bounds.empty);
            REQUIRE(near(sample(&program, 0, 0, 0), 0));
        }
        if (i == 2) {
            REQUIRE(near(sample(&program, 0, 0, 0), 1));
            REQUIRE(near(sample(&program, 1, 0, 0), 0));
            REQUIRE(near(sample(&program, 2, 0, 0), -1));
            REQUIRE(near(program.bounds.max[0], 3));
        }
        if (i == 3) {
            REQUIRE(near(sample(&program, 0, 0, 0), -.25f));
            REQUIRE(near(program.bounds.min[0], -2.25f));
            REQUIRE(near(program.bounds.max[0], 2.25f));
        }
        model_program_free(&program);
        behavior_asset_free(asset);
    }
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *sphere = model_node(g, &NODE_ModelSphere);
    node *scale = model_node(g, &NODE_ModelScale);
    model_vector(g, scale, 1, 100, 1, 1);
    connect_model(g, sphere, scale, 0);
    node *smooth = model_node(g, &NODE_ModelSmoothUnion);
    model_number(g, smooth, 2, 1);
    connect_model(g, scale, smooth, 0);
    connect_model(g, scale, smooth, 1);
    model_program program;
    finish_model(asset, smooth, &program);
    REQUIRE(near(sample(&program, 125, 0, 0), 0));
    REQUIRE(near(program.bounds.max[0], 125));
    REQUIRE(near(program.bounds.min[0], -125));
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    node *a = translated_sphere(g, 1, -2), *b = translated_sphere(g, 1, 2);
    node *intersection = model_node(g, &NODE_ModelIntersection);
    connect_model(g, a, intersection, 0);
    connect_model(g, b, intersection, 1);
    smooth = model_node(g, &NODE_ModelSmoothUnion);
    model_number(g, smooth, 2, 8);
    connect_model(g, intersection, smooth, 0);
    connect_model(g, intersection, smooth, 1);
    finish_model(asset, smooth, &program);
    REQUIRE(!program.bounds.empty);
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    REQUIRE(near(program.bounds.min[0], -1));
    REQUIRE(near(program.bounds.max[0], 1));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(solid_diagnostics)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *sphere = model_node(g, &NODE_ModelSphere), *root = sphere;
    for (int i = 0; i < 50; i++) {
        node *u = model_node(g, &NODE_ModelUnion);
        connect_model(g, root, u, 0);
        connect_model(g, root, u, 1);
        root = u;
    }
    node *output = model_graph_add_output(g);
    connect_model(g, root, output, 0);
    model_program program;
    model_program_init(&program);
    shader_error error;
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node && strstr(error.message, "1024"));
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    node *a = model_node(g, &NODE_ModelTranslate),
         *b = model_node(g, &NODE_ModelTranslate);
    connect_model(g, a, b, 0);
    connect_model(g, b, a, 0);
    output = model_graph_add_output(g);
    connect_model(g, a, output, 0);
    model_program_init(&program);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node && strstr(error.message, "cycle"));
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    sphere = model_node(g, &NODE_ModelSphere);
    node *scale = model_node(g, &NODE_ModelScale);
    connect_model(g, sphere, scale, 0);
    output = model_graph_add_output(g);
    connect_model(g, scale, output, 0);
    model_program_init(&program);
    float invalid[] = { 0, -1, INFINITY };
    for (int i = 0; i < 3; i++) {
        model_vector(g, scale, 1, invalid[i], 1, 1);
        REQUIRE(!model_compile(asset, &program, &error));
        REQUIRE(error.node == scale->id);
    }
    model_vector(g, scale, 1, 1e20f, 1, 1);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(strstr(error.message, "distance range"));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}
TEST_CASE(function_and_array_solids)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    graph *fn = behavior_asset_add_function(asset, "Offset Geometry", 1);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    pin_spec geometry_pin = { .name = "Geometry",
                              .type = PIN_HANDLE,
                              .type_name = "Geometry3D",
                              .default_value = { .type = PIN_HANDLE,
                                                 .type_name = "Geometry3D" } };
    pin_spec offset_pin = { .name = "Offset",
                            .type = PIN_STRUCT,
                            .type_name = "ShaderVec3",
                            .default_value = { .type = PIN_STRUCT,
                                               .type_name = "ShaderVec3" } };
    REQUIRE(graph_add_pin(fn, entry, 1, &geometry_pin) >= 0);
    REQUIRE(graph_add_pin(fn, entry, 1, &offset_pin) >= 0);
    REQUIRE(graph_add_pin(fn, ret, 0, &geometry_pin) >= 0);
    node *translated = model_node(fn, &NODE_ModelTranslate);
    REQUIRE(graph_connect_data(fn, entry, 0, translated, 0) == 0);
    REQUIRE(graph_connect_data(fn, entry, 1, translated, 1) == 0);
    REQUIRE(graph_connect_data(fn, translated, 0, ret, 0) == 0);
    node *sphere = model_node(g, &NODE_ModelSphere);
    node *call = behavior_asset_add_function_call_node(asset, fn);
    REQUIRE(call);
    connect_model(g, sphere, call, 0);
    model_vector(g, call, 1, 4, 5, 6);
    model_program program;
    finish_model(asset, call, &program);
    REQUIRE(near(sample(&program, 4, 5, 6), -1));
    REQUIRE(near(sample(&program, 5, 5, 6), 0));
    REQUIRE(near(program.bounds.min[0], 3));
    REQUIRE(near(program.bounds.max[2], 7));
    char *saved = behavior_asset_save(asset);
    behavior_asset *copy = behavior_asset_load(saved);
    REQUIRE(copy);
    free(saved);
    model_program reloaded;
    model_program_init(&reloaded);
    shader_error error;
    REQUIRE(model_compile(copy, &reloaded, &error));
    REQUIRE(near(sample(&reloaded, 4, 5, 6), -1));
    model_program_free(&reloaded);
    behavior_asset_free(copy);
    node *recursive = behavior_asset_add_function_call_node_to_graph(asset, fn, fn);
    REQUIRE(recursive);
    connect_model(fn, entry, recursive, 0);
    REQUIRE(graph_connect_data(fn, entry, 1, recursive, 1) == 0);
    REQUIRE(graph_connect_data(fn, recursive, 0, ret, 0) == 0);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(strstr(error.message, "cycle"));
    REQUIRE(near(sample(&program, 4, 5, 6), -1));
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    graph *numeric = behavior_asset_add_function(asset, "Double Radius", 1);
    entry = graph_node_by_id(numeric, numeric->entry_id);
    ret = graph_node_by_id(numeric, numeric->return_id);
    pin_spec scalar_pin = { .name = "Radius",
                            .type = PIN_F32,
                            .default_value = { .type = PIN_F32 } };
    REQUIRE(graph_add_pin(numeric, entry, 1, &scalar_pin) >= 0);
    REQUIRE(graph_add_pin(numeric, ret, 0, &scalar_pin) >= 0);
    node *multiply = model_node(numeric, (node_desc *)registry_node_by_name("Shader Multiply"));
    model_number(numeric, multiply, 1, 2);
    REQUIRE(graph_connect_data(numeric, entry, 0, multiply, 0) == 0);
    REQUIRE(graph_connect_data(numeric, multiply, 0, ret, 0) == 0);
    call = behavior_asset_add_function_call_node(asset, numeric);
    model_number(g, call, 0, 3);
    sphere = model_node(g, &NODE_ModelSphere);
    REQUIRE(graph_connect_data(g, call, 0, sphere, 0) == 0);
    finish_model(asset, sphere, &program);
    REQUIRE(near(sample(&program, 0, 0, 0), -6));
    REQUIRE(near(program.bounds.max[0], 6));
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    sphere = translated_sphere(g, .25f, 3);
    node *array = model_node(g, &NODE_ModelRadialArray);
    connect_model(g, sphere, array, 0);
    pin_value count = { .type = PIN_I32, .i = 4 };
    REQUIRE(graph_set_pin_default(g, array, 0, 1, &count) == 0);
    model_number(g, array, 2, 1.57079632679f);
    finish_model(asset, array, &program);
    REQUIRE(near(sample(&program, 3, 0, 0), -.25f));
    REQUIRE(near(sample(&program, -3, 0, 0), -.25f));
    REQUIRE(near(sample(&program, 0, 0, 3), -.25f));
    REQUIRE(near(sample(&program, 0, 0, -3), -.25f));
    REQUIRE(sample(&program, 0, 0, 0) > 2.74f);
    REQUIRE(program.bounds.max[0] >= 3.25f && program.bounds.max[0] < 3.251f);
    count.i = 65;
    REQUIRE(graph_set_pin_default(g, array, 0, 1, &count) == 0);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node == array->id && strstr(error.message, "64"));
    REQUIRE(near(sample(&program, 3, 0, 0), -.25f));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(unused_function_diagnostics)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *sphere = model_node(g, &NODE_ModelSphere);
    model_program program;
    finish_model(asset, sphere, &program);
    graph *a = behavior_asset_add_function(asset, "Unused A", 1),
          *b = behavior_asset_add_function(asset, "Unused B", 1);
    node *ab = behavior_asset_add_function_call_node_to_graph(asset, a, b),
         *ba = behavior_asset_add_function_call_node_to_graph(asset, b, a);
    REQUIRE(ab && ba);
    shader_error error;
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node == ba->id && strstr(error.message, "cycle"));
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    REQUIRE(graph_remove_node(b, ba) == 0);
    ab->reference_id = 999999;
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node == ab->id && strstr(error.message, "Missing"));
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

static node *runtime_parameter(graph *g, const char *name, float value, float min, float max)
{
    node *n = model_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, n, 0, name));
    model_number(g, n, 0, value);
    model_number(g, n, 1, min);
    model_number(g, n, 2, max);
    return n;
}
static int runtime_slot(const model_program *p, const node *n)
{
    for (int i = 0; i < acount(p->parameters); i++) {
        if (!p->parameters[i].hidden && p->parameters[i].node == n->id) {
            return p->parameters[i].slot;
        }
    }
    assert(false);
    return -1;
}
TEST_CASE(runtime_numeric_arrays)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *step = runtime_parameter(g, "Step", 2, 0, 8),
         *denominator = runtime_parameter(g, "Divisor", 2, 0, 4);
    node *divide = model_node(g, (node_desc *)registry_node_by_name("Shader Divide"));
    connect_model(g, step, divide, 0);
    connect_model(g, denominator, divide, 1);
    node *vector = model_node(g, (node_desc *)registry_node_by_name("Shader Vector3"));
    connect_model(g, divide, vector, 0);
    node *sphere = model_node(g, &NODE_ModelSphere);
    model_number(g, sphere, 0, .25f);
    node *array = model_node(g, &NODE_ModelLinearArray);
    connect_model(g, sphere, array, 0);
    connect_model(g, vector, array, 2);
    pin_value count = { .type = PIN_I32, .i = 3 };
    REQUIRE(!graph_set_pin_default(g, array, 0, 1, &count));
    model_program program;
    finish_model(asset, array, &program);
    shader_error error;
    float incoming[32][4], prepared[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    incoming[runtime_slot(&program, step)][0] = 6;
    int node_count = acount(program.shader.nodes);
    shader_node *before = malloc(node_count * sizeof *before);
    memcpy(before, program.shader.nodes, node_count * sizeof *before);
    REQUIRE(model_program_prepare(&program, incoming, prepared, &bounds, &error));
    REQUIRE(near(bounds.max[0], 6.25f));
    REQUIRE(near(bounds.min[0], -.25f));
    shader_sample_context context = { .object_position = { 3, 0, 0 } };
    float values[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&program.shader, &context, prepared, values, &error));
    REQUIRE(near(values[0][0], -.25f));
    REQUIRE(!memcmp(before, program.shader.nodes, node_count * sizeof *before));
    incoming[runtime_slot(&program, denominator)][0] = 0;
    float preserved[32][4];
    memcpy(preserved, prepared, sizeof preserved);
    model_bounds previous = bounds;
    REQUIRE(!model_program_prepare(&program, incoming, prepared, &bounds, &error));
    REQUIRE(error.node == divide->id && strstr(error.message, "division"));
    REQUIRE(
        !memcmp(preserved, prepared, sizeof preserved) &&
        !memcmp(&previous, &bounds, sizeof bounds)
    );
    free(before);
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    step = runtime_parameter(g, "Angle Step", 1.57079632679f, 0, 3.14159265359f);
    sphere = translated_sphere(g, .25f, 3);
    array = model_node(g, &NODE_ModelRadialArray);
    connect_model(g, sphere, array, 0);
    connect_model(g, step, array, 2);
    count.i = 4;
    REQUIRE(!graph_set_pin_default(g, array, 0, 1, &count));
    finish_model(asset, array, &program);
    REQUIRE(model_program_parameters(&program, incoming, &error));
    incoming[runtime_slot(&program, step)][0] = 3.14159265359f;
    REQUIRE(model_program_prepare(&program, incoming, prepared, &bounds, &error));
    context = (shader_sample_context){ .object_position = { 0, 0, 3 } };
    REQUIRE(shader_graph_eval_outputs(&program.shader, &context, prepared, values, &error));
    REQUIRE(values[0][0] > 3.9f);
    REQUIRE(bounds.max[2] < .251f && bounds.min[2] > -.251f);
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = model_asset();
    g = asset->graph;
    for (int i = 0; i < 30; i++) {
        char name[32];
        snprintf(name, sizeof name, "Unused %d", i);
        runtime_parameter(g, name, 0, 0, 1);
    }
    step = runtime_parameter(g, "Bend", 0, -1, 1);
    vector = model_node(g, (node_desc *)registry_node_by_name("Shader Vector3"));
    connect_model(g, step, vector, 2);
    sphere = model_node(g, &NODE_ModelSphere);
    node *rotate = model_node(g, &NODE_ModelRotate);
    connect_model(g, sphere, rotate, 0);
    connect_model(g, vector, rotate, 1);
    connect_model(g, rotate, model_graph_add_output(g), 0);
    model_program_init(&program);
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node == rotate->id && strstr(error.message, "32"));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(profile_function_smoke)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    graph *fn = behavior_asset_add_function(asset, "Extrude Profile", 1);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    pin_spec profile = { .name = "Profile",
                         .type = PIN_HANDLE,
                         .type_name = "Profile2D",
                         .default_value = { .type = PIN_HANDLE,
                                            .type_name = "Profile2D" } };
    pin_spec depth = { .name = "Depth",
                       .type = PIN_F32,
                       .default_value = { .type = PIN_F32, .f = 1 } };
    pin_spec geometry_pin = { .name = "Geometry",
                              .type = PIN_HANDLE,
                              .type_name = "Geometry3D",
                              .default_value = { .type = PIN_HANDLE,
                                                 .type_name = "Geometry3D" } };
    REQUIRE(graph_add_pin(fn, entry, 1, &profile) >= 0);
    REQUIRE(graph_add_pin(fn, entry, 1, &depth) >= 0);
    REQUIRE(graph_add_pin(fn, ret, 0, &geometry_pin) >= 0);
    node *extrude = model_node(fn, &NODE_ModelExtrude);
    connect_model(fn, entry, extrude, 0);
    REQUIRE(!graph_connect_data(fn, entry, 1, extrude, 1));
    connect_model(fn, extrude, ret, 0);
    node *circle = model_node(g, &NODE_ModelProfileCircle),
         *call = behavior_asset_add_function_call_node(asset, fn);
    model_number(g, circle, 0, 2);
    connect_model(g, circle, call, 0);
    model_number(g, call, 1, .5f);
    model_program program;
    finish_model(asset, call, &program);
    REQUIRE(near(sample(&program, 0, 0, 0), -.5f));
    REQUIRE(near(sample(&program, 2, 0, 0), 0));
    REQUIRE(near(sample(&program, 3, 0, 1.5f), sqrtf(2)));
    REQUIRE(near(program.bounds.max[0], 2));
    REQUIRE(near(program.bounds.max[2], .5f));
    char *saved = behavior_asset_save(asset);
    behavior_asset *copy = behavior_asset_load(saved);
    free(saved);
    REQUIRE(copy);
    model_program reloaded;
    model_program_init(&reloaded);
    shader_error error;
    REQUIRE(model_compile(copy, &reloaded, &error));
    REQUIRE(near(sample(&reloaded, 0, 0, 0), -.5f));
    model_program_free(&reloaded);
    behavior_asset_free(copy);
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(material_function_smoke)
{
    behavior_asset *asset = model_asset(),
                   *material = behavior_asset_new("FunctionPaint");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *slot = model_node(g, &NODE_ModelMaterialSlot);
    REQUIRE(!graph_set_node_reference(g, slot, 0, "Paint"));
    slot->reference_asset = graph_own_str(g, material->name, strlen(material->name));
    graph *fn = behavior_asset_add_function(asset, "Paint Geometry", 1);
    node *entry = graph_node_by_id(fn, fn->entry_id),
         *ret = graph_node_by_id(fn, fn->return_id);
    pin_spec geometry = { .name = "Geometry",
                          .type = PIN_HANDLE,
                          .type_name = "Geometry3D",
                          .default_value = { .type = PIN_HANDLE,
                                             .type_name = "Geometry3D" } };
    pin_spec binding = {
        .name = "Material",
        .type = PIN_HANDLE,
        .type_name = "ModelMaterialSlot",
        .default_value = { .type = PIN_HANDLE, .type_name = "ModelMaterialSlot" }
    };
    REQUIRE(graph_add_pin(fn, entry, 1, &geometry) >= 0);
    REQUIRE(graph_add_pin(fn, entry, 1, &binding) >= 0);
    REQUIRE(graph_add_pin(fn, ret, 0, &geometry) >= 0);
    node *part = model_node(fn, &NODE_ModelPart);
    REQUIRE(!graph_set_node_reference(fn, part, 0, "Painted Surface"));
    connect_model(fn, entry, part, 0);
    REQUIRE(!graph_connect_data(fn, entry, 1, part, 1));
    connect_model(fn, part, ret, 0);
    node *sphere = model_node(g, &NODE_ModelSphere),
         *call = behavior_asset_add_function_call_node(asset, fn);
    connect_model(g, sphere, call, 0);
    connect_model(g, slot, call, 1);
    connect_model(g, call, model_graph_add_output(g), 0);
    const behavior_asset *catalog[] = { asset, material };
    model_program program;
    model_program_init(&program);
    shader_error error;
    REQUIRE(model_compile_with_catalog(asset, catalog, 2, &program, &error));
    model_hit hit;
    float point[3] = { 1, 0, 0 };
    REQUIRE(model_program_hit(&program, NULL, point, &hit, &error));
    REQUIRE(program.parts[hit.part].node == part->id);
    REQUIRE(hit.material == 0);
    REQUIRE(program.material_slots[0].node == slot->id);
    REQUIRE(near(hit.position[0], 1));
    model_program_free(&program);
    behavior_asset_free(material);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(hit_resolver_limit)
{
    behavior_asset *previous = model_asset();
    node *sphere = model_node(previous->graph, &NODE_ModelSphere);
    model_program program;
    finish_model(previous, sphere, &program);
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *combined = NULL;
    for (int i = 0; i < 120; i++) {
        node *s = model_node(g, &NODE_ModelSphere),
             *part = model_node(g, &NODE_ModelPart),
             *placed = model_node(g, &NODE_ModelTranslate);
        char name[32];
        snprintf(name, sizeof name, "Part %d", i);
        REQUIRE(!graph_set_node_reference(g, part, 0, name));
        connect_model(g, s, part, 0);
        connect_model(g, part, placed, 0);
        model_vector(g, placed, 1, i * 3.0f, 0, 0);
        if (!combined) {
            combined = placed;
            continue;
        }
        node *u = model_node(g, &NODE_ModelUnion);
        connect_model(g, combined, u, 0);
        connect_model(g, placed, u, 1);
        combined = u;
    }
    connect_model(g, combined, model_graph_add_output(g), 0);
    shader_error error;
    REQUIRE(!model_compile(asset, &program, &error));
    REQUIRE(error.node && strstr(error.message, "hit resolver") && strstr(error.message, "1024"));
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    model_hit hit;
    float point[3] = { 1, 0, 0 };
    REQUIRE(model_program_hit(&program, NULL, point, &hit, &error));
    REQUIRE(hit.part == -1 && hit.material == -1);
    model_program_free(&program);
    behavior_asset_free(asset);
    behavior_asset_free(previous);
    return true;
}

TEST_CASE(runtime_model_expression)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *r = runtime_parameter(g, "Radius", 1.8f, .18f, 5.4f);
    behavior_expression_error expression_error;
    node *expression = behavior_expression_add(asset, g, "r / 1.8", &expression_error);
    REQUIRE(expression && expression->desc == &NODE_BehaviorExpression);
    connect_model(g, r, expression, 0);
    node *sphere = model_node(g, &NODE_ModelSphere);
    connect_model(g, expression, sphere, 0);
    model_program program;
    finish_model(asset, sphere, &program);
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    shader_error error;
    float incoming[32][4], prepared[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    incoming[runtime_slot(&program, r)][0] = 3.6f;
    int count = acount(program.shader.nodes);
    REQUIRE(model_program_prepare(&program, incoming, prepared, &bounds, &error));
    REQUIRE(near(bounds.max[0], 2));
    shader_sample_context context = { .object_position = { 2, 0, 0 } };
    float values[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&program.shader, &context, prepared, values, &error));
    REQUIRE(near(values[0][0], 0));
    REQUIRE(acount(program.shader.nodes) == count);
    char *text = behavior_asset_save(asset);
    behavior_asset *loaded = behavior_asset_load(text);
    REQUIRE(loaded && model_compile(loaded, &program, &error));
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    free(text);
    behavior_asset_free(loaded);
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(shared_model_expressions)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *combined = NULL;
    uint64_t first = 0;
    for (int i = 0; i < 80; i++) {
        node *sphere = model_node(g, &NODE_ModelSphere),
             *part = model_node(g, &NODE_ModelPart);
        char name[32];
        snprintf(name, sizeof name, "Identical Part %d", i);
        REQUIRE(!graph_set_node_reference(g, part, 0, name));
        connect_model(g, sphere, part, 0);
        if (!combined) {
            combined = part;
            first = part->id;
            continue;
        }
        node *u = model_node(g, &NODE_ModelUnion);
        connect_model(g, combined, u, 0);
        connect_model(g, part, u, 1);
        combined = u;
    }
    model_program program;
    finish_model(asset, combined, &program);
    REQUIRE(acount(program.shader.nodes) < 100);
    REQUIRE(acount(program.hit_shader.nodes) < 600);
    REQUIRE(near(sample(&program, 1, 0, 0), 0));
    REQUIRE(near(sample(&program, 0, 0, 0), -1));
    model_hit hit;
    shader_error error;
    float point[3] = { 1, 0, 0 };
    REQUIRE(model_program_hit(&program, NULL, point, &hit, &error));
    REQUIRE(hit.part >= 0 && program.parts[hit.part].node == first);
    REQUIRE(near(hit.position[0], 1));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(mixed_hit_ownership)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *sphere = model_node(g, &NODE_ModelSphere),
         *inner = model_node(g, &NODE_ModelPart),
         *left = model_node(g, &NODE_ModelTranslate),
         *right = translated_sphere(g, 1, 3), *u = model_node(g, &NODE_ModelUnion),
         *outer = model_node(g, &NODE_ModelPart);
    REQUIRE(!graph_set_node_reference(g, inner, 0, "Inner"));
    REQUIRE(!graph_set_node_reference(g, outer, 0, "Outer"));
    connect_model(g, sphere, inner, 0);
    connect_model(g, inner, left, 0);
    model_vector(g, left, 1, -3, 0, 0);
    connect_model(g, left, u, 0);
    connect_model(g, right, u, 1);
    connect_model(g, u, outer, 0);
    model_program program;
    finish_model(asset, outer, &program);
    model_hit hit;
    shader_error error;
    float point[3] = { -2, 0, 0 };
    REQUIRE(model_program_hit(&program, NULL, point, &hit, &error));
    REQUIRE(program.parts[hit.part].node == inner->id);
    REQUIRE(near(hit.position[0], 1));
    point[0] = 4;
    REQUIRE(model_program_hit(&program, NULL, point, &hit, &error));
    REQUIRE(program.parts[hit.part].node == outer->id);
    REQUIRE(near(hit.position[0], 4));
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(rectangular_polygon_lowering)
{
    for (int revolved = 0; revolved < 2; revolved++) {
        for (int filled = 0; filled < 2; filled++) {
            behavior_asset *asset = model_asset();
            graph *g = asset->graph;
            node *profile = model_node(g, &NODE_ModelProfilePolygon);
            float min = filled ? 0 : 1.69f, max = 1.8f;
            float corners[4][2] = { { min, -.04f },
                                    { max, -.04f },
                                    { max, .04f },
                                    { min, .04f } };
            pin_array *points = pin_array_new(PIN_STRUCT, "ShaderVec2", 4);
            for (int i = 0; i < 4; i++) {
                pin_value value = { .type = PIN_STRUCT,
                                    .type_name = "ShaderVec2",
                                    .ptr = corners[(i + 2) % 4] };
                REQUIRE(pin_array_push(points, &value) == i);
            }
            pin_value value = { .type = PIN_STRUCT,
                                .type_name = "ShaderVec2",
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = points };
            REQUIRE(!graph_set_pin_default(g, profile, false, 0, &value));
            pin_array_free(points);
            node *solid = model_node(g, revolved ? &NODE_ModelRevolve : &NODE_ModelExtrude);
            connect_model(g, profile, solid, 0);
            if (!revolved) {
                model_number(g, solid, 1, .5f);
            }
            model_program program;
            finish_model(asset, solid, &program);
            REQUIRE(acount(program.shader.nodes) < 45);
            for (int i = 0; i < 7; i++) {
                float x = i * .4f, z = i % 2 ? .7f : 0;
                for (int j = -2; j <= 2; j++) {
                    float y = j * .03f;
                    float u = revolved ? hypotf(x, z) : x;
                    float a = revolved && filled ? u - max : fmaxf(min - u, u - max);
                    float b = fabsf(y) - .04f;
                    float d = revolved ? -INFINITY : fabsf(z) - .5f;
                    float outside = sqrtf(
                        fmaxf(a, 0) * fmaxf(a, 0) + fmaxf(b, 0) * fmaxf(b, 0) +
                        fmaxf(d, 0) * fmaxf(d, 0)
                    );
                    float expected = outside + fminf(fmaxf(a, fmaxf(b, d)), 0);
                    REQUIRE(near(sample(&program, x, y, z), expected));
                }
            }
            REQUIRE(profile->desc == &NODE_ModelProfilePolygon);
            REQUIRE(program.bounds.max[0] >= max);
            if (revolved && filled) {
                REQUIRE(near(sample(&program, 0, 0, 0), -.04f));
            }
            model_program_free(&program);
            behavior_asset_free(asset);
        }
    }
    return true;
}

/* Literal geometry has no numeric program. Its selected subtree must still
 * prepare bounds in authored object space through enclosing transforms. */
TEST_CASE(static_part_bounds)
{
    behavior_asset *asset = model_asset();
    graph *g = asset->graph;
    node *sphere = model_node(g, &NODE_ModelSphere),
         *part = model_node(g, &NODE_ModelPart),
         *translate = model_node(g, &NODE_ModelTranslate),
         *scale = model_node(g, &NODE_ModelScale);
    radius(g, sphere, 2);
    REQUIRE(!graph_set_node_reference(g, part, 0, "Literal sphere"));
    model_vector(g, translate, 1, 3, 4, 5);
    model_vector(g, scale, 1, 2, 1, 3);
    connect_model(g, sphere, part, 0);
    connect_model(g, part, translate, 0);
    connect_model(g, translate, scale, 0);
    model_program program;
    finish_model(asset, scale, &program);
    REQUIRE(acount(program.parameters) == 0 && acount(program.parts) == 1);
    shader_error error = { 0 };
    model_bounds bounds;
    REQUIRE(model_program_part_bounds(&program, NULL, 0, &bounds, &error));
    REQUIRE(!bounds.empty);
    float lo[3] = { 2, 2, 9 }, hi[3] = { 10, 6, 21 };
    for (int i = 0; i < 3; i++) {
        REQUIRE(near(bounds.min[i], lo[i]));
        REQUIRE(near(bounds.max[i], hi[i]));
    }
    model_program_free(&program);
    behavior_asset_free(asset);
    return true;
}

void test_modeling()
{
    test_graph_packs_install();
    RUN_TEST_CASE(sphere_distances_bounds_roundtrip_and_diagnostics);
    RUN_TEST_CASE(primitive_solids);
    RUN_TEST_CASE(transformed_solids);
    RUN_TEST_CASE(boolean_solids);
    RUN_TEST_CASE(solid_diagnostics);
    RUN_TEST_CASE(function_and_array_solids);
    RUN_TEST_CASE(unused_function_diagnostics);
    RUN_TEST_CASE(runtime_numeric_arrays);
    RUN_TEST_CASE(profile_function_smoke);
    RUN_TEST_CASE(material_function_smoke);
    RUN_TEST_CASE(hit_resolver_limit);
    RUN_TEST_CASE(runtime_model_expression);
    RUN_TEST_CASE(shared_model_expressions);
    RUN_TEST_CASE(mixed_hit_ownership);
    RUN_TEST_CASE(rectangular_polygon_lowering);
    RUN_TEST_CASE(static_part_bounds);
    test_graph_packs_restore();
}
