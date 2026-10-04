#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/runtime/value_array.h"

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
static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < .0001f);
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
static void extrusion_sections()
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
    near(sample(&p, NULL, 1, -1, 0), -1);
    near(sample(&p, NULL, 3, -1, 0), 0);
    near(sample(&p, NULL, 4, -1, 2), sqrtf(2));
    near(p.bounds.min[0], -1);
    near(p.bounds.min[1], -3);
    near(p.bounds.max[2], 1);
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
    near(sample(&p, NULL, 0, 0, 0), -.5f);
    near(sample(&p, NULL, 3, 2, 1.5f), sqrtf(3));
    near(sample(&p, NULL, 2, 1, .5f), 0);
    model_program_free(&p);
    behavior_asset_free(a);
}
static void revolution_sections()
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
    near(sample(&p, NULL, 3, 2, 0), -1);
    near(sample(&p, NULL, 0, 2, 0), 2);
    near(sample(&p, NULL, 4, 2, 0), 0);
    near(sample(&p, NULL, 3, 4, 0), 1);
    near(sample(&p, NULL, 0, 2, -4), 0);
    near(p.bounds.min[0], -4);
    near(p.bounds.min[1], 1);
    near(p.bounds.max[2], 4);
    vector(g, profile, 1, "ShaderVec2", (float[2]){ .5f, 2 });
    shader_error error = { 0 };
    assert(!model_compile(a, &p, &error));
    assert(error.node == profile->id || error.node == revolve->id);
    near(sample(&p, NULL, 3, 2, 0), -1);
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
    near(sample(&p, NULL, 2, 1, 0), -1);
    near(sample(&p, NULL, 0, 1, 0), 1);
    near(sample(&p, NULL, 3.5f, 3.5f, 0), sqrtf(.5f));
    near(p.bounds.min[1], -1);
    near(p.bounds.max[1], 3);
    vector(g, profile, 1, "ShaderVec2", (float[2]){ 1, 0 });
    compile(a, &p);
    /* The profile's radial-zero edge disappears inside the revolved solid. */
    near(sample(&p, NULL, 0, 0, 0), -2);
    near(sample(&p, NULL, 0, 1, 0), -1);
    near(sample(&p, NULL, 0, 2, 0), 0);
    near(sample(&p, NULL, 3, 0, 0), 1);
    model_program_free(&p);
    behavior_asset_free(a);
}
static void polygon_revolution_axis()
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
    near(sample(&p, NULL, 0, 0, 0), -2);
    near(sample(&p, NULL, 0, 1, 0), -1);
    near(sample(&p, NULL, 0, -1, 0), -1);
    near(sample(&p, NULL, 0, 2, 0), 0);
    near(sample(&p, NULL, 2.5f, 0, 0), 0);
    near(p.bounds.min[0], -3);
    near(p.bounds.max[1], 2);
    model_program_free(&p);
    behavior_asset_free(a);
}
static void polygon_sections()
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
    near(sample(&p, NULL, 1, 1, 0), -1);
    near(sample(&p, NULL, 4, 3, 0), 2.4f);
    near(sample(&p, NULL, 1, 1, 2), 1);
    near(sample(&p, NULL, 2, 1.5f, 0), 0);
    char *saved = behavior_asset_save(a);
    behavior_asset *reloaded = behavior_asset_load(saved);
    assert(reloaded);
    model_program copy;
    model_program_init(&copy);
    compile(reloaded, &copy);
    near(sample(&copy, NULL, 4, 3, 0), 2.4f);
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
        assert(!model_compile(a, &p, &error));
        assert(error.node == profile->id);
        near(sample(&p, NULL, 1, 1, 0), -1);
    }
    model_program_free(&p);
    behavior_asset_free(a);
}
static void smooth_profile_bounds()
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
        near(sample(&p, NULL, boundary[i][0], boundary[i][1], boundary[i][2]), 0);
        for (int axis = 0; axis < 3; axis++) {
            assert(boundary[i][axis] >= p.bounds.min[axis]);
            assert(boundary[i][axis] <= p.bounds.max[axis]);
        }
    }
    /* From this exterior point, distance cannot exceed the distance to the
     * known nearest X-axis surface; otherwise marching could skip it. */
    float distance = sample(&p, NULL, 5, 0, 0);
    assert(distance > 0 && distance <= 1);
    model_program_free(&p);
    behavior_asset_free(a);
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
static void runtime_profile_dimensions()
{
    behavior_asset *a = model("DynamicProfileExtrusion");
    graph *g = a->graph;
    node *radius = parameter(g, "Radius", 1, .5f, 3);
    node *depth = parameter(g, "Half Depth", 1, .25f, 3);
    node *center = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, center, 0, "Center"));
    assert(!graph_set_pin_type(g, center, false, 0, PIN_STRUCT, "ShaderVec2"));
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
    assert(model_program_parameters(&p, incoming, &error));
    incoming[slot(&p, radius)][0] = 2;
    incoming[slot(&p, depth)][0] = .5f;
    incoming[slot(&p, center)][0] = 3;
    incoming[slot(&p, center)][1] = -2;
    assert(model_program_prepare(&p, incoming, payload, &bounds, &error));
    near(sample(&p, payload, 3, -2, 0), -.5f);
    near(sample(&p, payload, 5, -2, 0), 0);
    near(bounds.min[0], 1);
    near(bounds.max[0], 5);
    near(bounds.min[1], -4);
    near(bounds.max[2], .5f);
    assert(p.shader.nodes == generation);
    char *saved = behavior_asset_save(a);
    behavior_asset *reloaded = behavior_asset_load(saved);
    assert(reloaded);
    model_program copy;
    model_program_init(&copy);
    compile(reloaded, &copy);
    assert(model_program_prepare(&copy, incoming, payload, &bounds, &error));
    near(sample(&copy, payload, 5, -2, .5f), 0);
    assert(model_program_prepare(&copy, NULL, payload, &bounds, &error));
    near(sample(&copy, payload, 0, 0, 0), -1);
    near(bounds.min[0], -1);
    near(bounds.max[2], 1);
    model_program_free(&copy);
    behavior_asset_free(reloaded);
    free(saved);
    model_program_free(&p);
    behavior_asset_free(a);
}
static void dynamic_revolution_axis()
{
    behavior_asset *a = model("DynamicFilledAnnularRevolution");
    graph *g = a->graph;
    node *center = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, center, 0, "Center"));
    assert(!graph_set_pin_type(g, center, false, 0, PIN_STRUCT, "ShaderVec2"));
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
    assert(model_program_parameters(&p, incoming, &error));
    assert(model_program_prepare(&p, incoming, payload, &bounds, &error));
    near(sample(&p, payload, 0, 0, 0), -2);
    incoming[slot(&p, center)][0] = 2;
    assert(model_program_prepare(&p, incoming, payload, &bounds, &error));
    near(sample(&p, payload, 0, 0, 0), 1);
    near(sample(&p, payload, 2, 0, 0), -1);
    near(bounds.max[0], 3);
    assert(model_program_prepare(&p, NULL, payload, &bounds, &error));
    near(sample(&p, payload, 0, 0, 0), -2);
    near(bounds.max[0], 2);
    assert(p.shader.nodes == generation);
    model_program_free(&p);
    behavior_asset_free(a);
}
void test_model_profiles()
{
    extrusion_sections();
    revolution_sections();
    polygon_revolution_axis();
    polygon_sections();
    smooth_profile_bounds();
    runtime_profile_dimensions();
    dynamic_revolution_axis();
    puts("Model profile analytical sections, bounds, conservative stepping, invalid polygons and roundtrip: PASS");
}
