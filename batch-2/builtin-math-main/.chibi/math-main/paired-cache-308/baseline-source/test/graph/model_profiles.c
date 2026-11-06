#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *model(const char *name)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MODEL;
    return a;
}
static node *add(graph *g, const char *name)
{
    const node_desc *d = registry_node_by_name(name);
    assert(d);
    return graph_add_node(g, d);
}
static void scalar(graph *g, node *n, int pin, float number)
{
    pin_value value = { .type = PIN_F32, .f = number };
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}
static void vector(graph *g, node *n, int pin, const char *type, float *numbers)
{
    pin_value value = { .type = PIN_STRUCT, .type_name = type, .ptr = numbers };
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}
static void connect(graph *g, node *from, node *to, int pin)
{
    assert(!graph_connect_data(g, from, 0, to, pin));
}
static void publish(graph *g, node *from)
{
    connect(g, from, model_graph_add_output(g), 0);
}
static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < .0001f;
}
static float sample(const model_program *p, const float values[32][4], float x, float y, float z)
{
    shader_sample_context context = { .object_position = { x, y, z } };
    float result[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(shader_graph_eval_outputs(&p->shader, &context, values, result, &error));
    return result[0][0];
}
static void compile(behavior_asset *a, model_program *p)
{
    shader_error error = { 0 };
    assert(model_compile(a, p, &error));
}
static void polygon(graph *g, node *profile, const float (*vertices)[2], int count)
{
    pin_array *array = pin_array_new(PIN_STRUCT, "ShaderVec2", count);
    for (int i = 0; i < count; i++) {
        pin_value value = { .type = PIN_STRUCT,
                            .type_name = "ShaderVec2",
                            .ptr = (void *)vertices[i] };
        assert(pin_array_push(array, &value) == i);
    }
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = "ShaderVec2",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array };
    assert(!graph_set_pin_default(g, profile, false, 0, &value));
    pin_array_free(array);
}
TEST_CASE(extrusion_sections)
{
    behavior_asset *a = model("ProfileCircleExtrusion");
    graph *g = a->graph;
    node *circle = add(g, "Model Profile Circle"), *extrude = add(g, "Model Extrude");
    scalar(g, circle, 0, 2);
    vector(g, circle, 1, "ShaderVec2", (float[2]){ 1, -1 });
    scalar(g, extrude, 1, 1);
    connect(g, circle, extrude, 0);
    publish(g, extrude);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 1, -1, 0), -1));
    REQUIRE(near(sample(&p, NULL, 3, -1, 0), 0));
    REQUIRE(near(sample(&p, NULL, 4, -1, 2), sqrtf(2)));
    REQUIRE(near(p.bounds.min[0], -1));
    REQUIRE(near(p.bounds.min[1], -3));
    REQUIRE(near(p.bounds.max[2], 1));
    model_program_free(&p);
    behavior_asset_free(a);

    a = model("ProfileRectangleExtrusion");
    g = a->graph;
    node *rectangle = add(g, "Model Profile Rectangle");
    extrude = add(g, "Model Extrude");
    vector(g, rectangle, 0, "ShaderVec2", (float[2]){ 2, 1 });
    scalar(g, extrude, 1, .5f);
    connect(g, rectangle, extrude, 0);
    publish(g, extrude);
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 0, 0, 0), -.5f));
    REQUIRE(near(sample(&p, NULL, 3, 2, 1.5f), sqrtf(3)));
    REQUIRE(near(sample(&p, NULL, 2, 1, .5f), 0));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(revolution_sections)
{
    behavior_asset *a = model("ProfileCircleRevolution");
    graph *g = a->graph;
    node *profile = add(g, "Model Profile Circle"), *revolve = add(g, "Model Revolve");
    vector(g, profile, 1, "ShaderVec2", (float[2]){ 3, 2 });
    connect(g, profile, revolve, 0);
    publish(g, revolve);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 3, 2, 0), -1));
    REQUIRE(near(sample(&p, NULL, 0, 2, 0), 2));
    REQUIRE(near(sample(&p, NULL, 4, 2, 0), 0));
    REQUIRE(near(sample(&p, NULL, 3, 4, 0), 1));
    REQUIRE(near(sample(&p, NULL, 0, 2, -4), 0));
    REQUIRE(near(p.bounds.min[0], -4));
    REQUIRE(near(p.bounds.min[1], 1));
    REQUIRE(near(p.bounds.max[2], 4));
    vector(g, profile, 1, "ShaderVec2", (float[2]){ .5f, 2 });
    shader_error error = { 0 };
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(error.node == profile->id || error.node == revolve->id);
    REQUIRE(near(sample(&p, NULL, 3, 2, 0), -1));
    model_program_free(&p);
    behavior_asset_free(a);

    a = model("ProfileRectangleRevolution");
    g = a->graph;
    profile = add(g, "Model Profile Rectangle");
    revolve = add(g, "Model Revolve");
    vector(g, profile, 0, "ShaderVec2", (float[2]){ 1, 2 });
    vector(g, profile, 1, "ShaderVec2", (float[2]){ 2, 1 });
    connect(g, profile, revolve, 0);
    publish(g, revolve);
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 2, 1, 0), -1));
    REQUIRE(near(sample(&p, NULL, 0, 1, 0), 1));
    REQUIRE(near(sample(&p, NULL, 3.5f, 3.5f, 0), sqrtf(.5f)));
    REQUIRE(near(p.bounds.min[1], -1));
    REQUIRE(near(p.bounds.max[1], 3));
    vector(g, profile, 1, "ShaderVec2", (float[2]){ 1, 0 });
    compile(a, &p);
    /* The profile's radial-zero edge disappears inside the revolved solid. */
    REQUIRE(near(sample(&p, NULL, 0, 0, 0), -2));
    REQUIRE(near(sample(&p, NULL, 0, 1, 0), -1));
    REQUIRE(near(sample(&p, NULL, 0, 2, 0), 0));
    REQUIRE(near(sample(&p, NULL, 3, 0, 0), 1));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(polygon_revolution_axis)
{
    behavior_asset *a = model("ProfileFilledVesselRevolution");
    graph *g = a->graph;
    node *profile = add(g, "Model Profile Polygon"),
         *revolve = add(g, "Model Revolve");
    float trapezoid[][2] = { { 0, -2 }, { 2, -2 }, { 3, 2 }, { 0, 2 } };
    polygon(g, profile, trapezoid, 4);
    connect(g, profile, revolve, 0);
    publish(g, revolve);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 0, 0, 0), -2));
    REQUIRE(near(sample(&p, NULL, 0, 1, 0), -1));
    REQUIRE(near(sample(&p, NULL, 0, -1, 0), -1));
    REQUIRE(near(sample(&p, NULL, 0, 2, 0), 0));
    REQUIRE(near(sample(&p, NULL, 2.5f, 0, 0), 0));
    REQUIRE(near(p.bounds.min[0], -3));
    REQUIRE(near(p.bounds.max[1], 2));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(polygon_sections)
{
    behavior_asset *a = model("ProfileTrianglePrism");
    graph *g = a->graph;
    node *profile = add(g, "Model Profile Polygon"),
         *extrude = add(g, "Model Extrude");
    float triangle[][2] = { { 0, 0 }, { 4, 0 }, { 0, 3 } };
    polygon(g, profile, triangle, 3);
    scalar(g, extrude, 1, 1);
    connect(g, profile, extrude, 0);
    publish(g, extrude);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 1, 1, 0), -1));
    REQUIRE(near(sample(&p, NULL, 4, 3, 0), 2.4f));
    REQUIRE(near(sample(&p, NULL, 1, 1, 2), 1));
    REQUIRE(near(sample(&p, NULL, 2, 1.5f, 0), 0));
    char *saved = behavior_asset_save(a);
    behavior_asset *reloaded = behavior_asset_load(saved);
    REQUIRE(reloaded);
    model_program copy;
    model_program_init(&copy);
    compile(reloaded, &copy);
    REQUIRE(near(sample(&copy, NULL, 4, 3, 0), 2.4f));
    model_program_free(&copy);
    behavior_asset_free(reloaded);
    free(saved);
    float invalid[][5][2] = { { { 0, 0 }, { 0, 3 }, { 4, 0 } },
                              { { 0, 0 }, { 4, 0 }, { 4, 0 }, { 0, 3 } },
                              { { 0, 0 }, { 4, 3 }, { 0, 3 }, { 4, 0 } },
                              { { 0, 0 }, { 2, 0 }, { 4, 0 } } };
    int counts[] = { 3, 4, 4, 3 };
    shader_error error = { 0 };
    for (int i = 0; i < 4; i++) {
        polygon(g, profile, invalid[i], counts[i]);
        REQUIRE(!model_compile(a, &p, &error));
        REQUIRE(error.node == profile->id);
        REQUIRE(near(sample(&p, NULL, 1, 1, 0), -1));
    }
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(smooth_profile_bounds)
{
    behavior_asset *a = model("ProfileSmoothEnvelope");
    graph *g = a->graph;
    node *circle = add(g, "Model Profile Circle"), *extrude = add(g, "Model Extrude");
    scalar(g, extrude, 1, .5f);
    connect(g, circle, extrude, 0);
    node *scale = graph_add_node(g, &NODE_ModelScale);
    connect(g, extrude, scale, 0);
    vector(g, scale, 1, "ShaderVec3", (float[3]){ 2, 1, .5f });
    node *smooth = graph_add_node(g, &NODE_ModelSmoothUnion);
    connect(g, scale, smooth, 0);
    connect(g, scale, smooth, 1);
    scalar(g, smooth, 2, 2);
    publish(g, smooth);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    /* Smooth union of identical fields subtracts exactly k/4. The scaled
     * field uses min scale=.5, so its zero expands to the child's d=1. */
    float boundary[][3] = { { 4, 0, 0 }, { 0, 2, 0 }, { 0, 0, .75f } };
    for (int i = 0; i < 3; i++) {
        REQUIRE(near(sample(&p, NULL, boundary[i][0], boundary[i][1], boundary[i][2]), 0));
        for (int axis = 0; axis < 3; axis++) {
            REQUIRE(boundary[i][axis] >= p.bounds.min[axis]);
            REQUIRE(boundary[i][axis] <= p.bounds.max[axis]);
        }
    }
    /* From this exterior point, distance cannot exceed the distance to the
     * known nearest X-axis surface; otherwise marching could skip it. */
    float distance = sample(&p, NULL, 5, 0, 0);
    REQUIRE(distance > 0 && distance <= 1);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
static node *parameter(graph *g, const char *name, float value, float minimum, float maximum)
{
    node *n = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, n, 0, name));
    scalar(g, n, 0, value);
    scalar(g, n, 1, minimum);
    scalar(g, n, 2, maximum);
    return n;
}
static int slot(const model_program *p, const node *n)
{
    for (int i = 0; i < acount(p->parameters); i++) {
        if (!p->parameters[i].hidden && p->parameters[i].node == n->id) {
            return p->parameters[i].slot;
        }
    }
    assert(false);
    return -1;
}
TEST_CASE(runtime_profile_dimensions)
{
    behavior_asset *a = model("DynamicProfileExtrusion");
    graph *g = a->graph;
    node *radius = parameter(g, "Radius", 1, .5f, 3);
    node *depth = parameter(g, "Half Depth", 1, .25f, 3);
    node *center = graph_add_node(g, &NODE_ModelParameter);
    REQUIRE(!graph_set_node_reference(g, center, 0, "Center"));
    REQUIRE(!graph_set_pin_type(g, center, false, 0, PIN_STRUCT, "ShaderVec2"));
    vector(g, center, 0, "ShaderVec2", (float[2]){ 0, 0 });
    vector(g, center, 1, "ShaderVec2", (float[2]){ -10, -10 });
    vector(g, center, 2, "ShaderVec2", (float[2]){ 10, 10 });
    node *circle = add(g, "Model Profile Circle"), *extrude = add(g, "Model Extrude");
    connect(g, radius, circle, 0);
    connect(g, center, circle, 1);
    connect(g, circle, extrude, 0);
    connect(g, depth, extrude, 1);
    publish(g, extrude);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    const shader_node *generation = p.shader.nodes;
    float incoming[32][4], payload[32][4];
    model_bounds bounds;
    shader_error error = { 0 };
    REQUIRE(model_program_parameters(&p, incoming, &error));
    incoming[slot(&p, radius)][0] = 2;
    incoming[slot(&p, depth)][0] = .5f;
    incoming[slot(&p, center)][0] = 3;
    incoming[slot(&p, center)][1] = -2;
    REQUIRE(model_program_prepare(&p, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&p, payload, 3, -2, 0), -.5f));
    REQUIRE(near(sample(&p, payload, 5, -2, 0), 0));
    REQUIRE(near(bounds.min[0], 1));
    REQUIRE(near(bounds.max[0], 5));
    REQUIRE(near(bounds.min[1], -4));
    REQUIRE(near(bounds.max[2], .5f));
    REQUIRE(p.shader.nodes == generation);
    char *saved = behavior_asset_save(a);
    behavior_asset *reloaded = behavior_asset_load(saved);
    REQUIRE(reloaded);
    model_program copy;
    model_program_init(&copy);
    compile(reloaded, &copy);
    REQUIRE(model_program_prepare(&copy, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&copy, payload, 5, -2, .5f), 0));
    REQUIRE(model_program_prepare(&copy, NULL, payload, &bounds, &error));
    REQUIRE(near(sample(&copy, payload, 0, 0, 0), -1));
    REQUIRE(near(bounds.min[0], -1));
    REQUIRE(near(bounds.max[2], 1));
    model_program_free(&copy);
    behavior_asset_free(reloaded);
    free(saved);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(dynamic_revolution_axis)
{
    behavior_asset *a = model("DynamicFilledAnnularRevolution");
    graph *g = a->graph;
    node *center = graph_add_node(g, &NODE_ModelParameter);
    REQUIRE(!graph_set_node_reference(g, center, 0, "Center"));
    REQUIRE(!graph_set_pin_type(g, center, false, 0, PIN_STRUCT, "ShaderVec2"));
    vector(g, center, 0, "ShaderVec2", (float[2]){ 1, 0 });
    vector(g, center, 1, "ShaderVec2", (float[2]){ 1, -3 });
    vector(g, center, 2, "ShaderVec2", (float[2]){ 3, 3 });
    node *rectangle = add(g, "Model Profile Rectangle"),
         *revolve = add(g, "Model Revolve");
    vector(g, rectangle, 0, "ShaderVec2", (float[2]){ 1, 2 });
    connect(g, center, rectangle, 1);
    connect(g, rectangle, revolve, 0);
    publish(g, revolve);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    const shader_node *generation = p.shader.nodes;
    float incoming[32][4], payload[32][4];
    model_bounds bounds;
    shader_error error = { 0 };
    REQUIRE(model_program_parameters(&p, incoming, &error));
    REQUIRE(model_program_prepare(&p, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&p, payload, 0, 0, 0), -2));
    incoming[slot(&p, center)][0] = 2;
    REQUIRE(model_program_prepare(&p, incoming, payload, &bounds, &error));
    REQUIRE(near(sample(&p, payload, 0, 0, 0), 1));
    REQUIRE(near(sample(&p, payload, 2, 0, 0), -1));
    REQUIRE(near(bounds.max[0], 3));
    REQUIRE(model_program_prepare(&p, NULL, payload, &bounds, &error));
    REQUIRE(near(sample(&p, payload, 0, 0, 0), -2));
    REQUIRE(near(bounds.max[0], 2));
    REQUIRE(p.shader.nodes == generation);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
void test_model_profiles()
{
    test_graph_packs_install();
    RUN_TEST_CASE(extrusion_sections);
    RUN_TEST_CASE(revolution_sections);
    RUN_TEST_CASE(polygon_revolution_axis);
    RUN_TEST_CASE(polygon_sections);
    RUN_TEST_CASE(smooth_profile_bounds);
    RUN_TEST_CASE(runtime_profile_dimensions);
    RUN_TEST_CASE(dynamic_revolution_axis);
    test_graph_packs_restore();
}
