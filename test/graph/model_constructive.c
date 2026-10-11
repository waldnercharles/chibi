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
static int input(const node *n, const char *name)
{
    for (int i = 0; i < n->input_count; i++) {
        if (!strcmp(n->input_pins[i].name, name)) {
            return i;
        }
    }
    assert(false);
    return -1;
}
static void scalar(graph *g, node *n, const char *name, float number)
{
    pin_value value = { .type = PIN_F32, .f = number };
    assert(!graph_set_pin_default(g, n, false, input(n, name), &value));
}
static void integer(graph *g, node *n, const char *name, int number)
{
    int pin = input(n, name);
    pin_value value = n->input_pins[pin].default_value;
    value.i = number;
    assert(!graph_set_pin_default(g, n, false, pin, &value));
}
static void boolean(graph *g, node *n, const char *name, bool number)
{
    pin_value value = { .type = PIN_BOOL, .b = number };
    assert(!graph_set_pin_default(g, n, false, input(n, name), &value));
}
static void vector(graph *g, node *n, const char *name, const char *type, const float *numbers)
{
    pin_value value = { .type = PIN_STRUCT, .type_name = type, .ptr = (void *)numbers };
    assert(!graph_set_pin_default(g, n, false, input(n, name), &value));
}
static void points(graph *g, node *n, const char *name, const char *type, const float *vertices, int count, int width)
{
    pin_array *array = pin_array_new(PIN_STRUCT, type, count);
    for (int i = 0; i < count; i++) {
        pin_value v = { .type = PIN_STRUCT,
                        .type_name = type,
                        .ptr = (void *)(vertices + i * width) };
        assert(pin_array_push(array, &v) == i);
    }
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array };
    assert(!graph_set_pin_default(g, n, false, input(n, name), &value));
    pin_array_free(array);
}
static void connect(graph *g, node *from, node *to, const char *name)
{
    assert(!graph_connect_data(g, from, 0, to, input(to, name)));
}
static void publish(graph *g, node *source)
{
    connect(g, source, model_graph_add_output(g), "Geometry");
}
static void compile(behavior_asset *a, model_program *p)
{
    shader_error error = { 0 };
    if (!model_compile(a, p, &error)) {
        fprintf(stderr, "Constructive model %s: %s\n", a->name, error.message);
        assert(false);
    }
}
static float sample(const model_program *p, const float values[32][4], float x, float y, float z)
{
    shader_sample_context context = { .object_position = { x, y, z } };
    float result[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(shader_graph_eval_outputs(&p->shader, &context, values, result, &error));
    return result[0][0];
}
static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < .0002f;
}
static bool contains(const model_bounds *b, float x, float y, float z)
{
    float p[3] = { x, y, z };
    bool inside = !b->empty;
    for (int i = 0; i < 3; i++) {
        inside = inside && p[i] >= b->min[i] - .0002f && p[i] <= b->max[i] + .0002f;
    }
    return inside;
}
static node *circle(graph *g, float radius, float x, float y)
{
    node *n = add(g, "Model Profile Circle");
    scalar(g, n, "Radius", radius);
    vector(g, n, "Center", "ShaderVec2", (float[2]){ x, y });
    return n;
}
static node *extrude(graph *g, node *profile)
{
    node *n = add(g, "Model Extrude");
    scalar(g, n, "Half Depth", .5f);
    connect(g, profile, n, "Profile");
    return n;
}
TEST_CASE(profile_offset)
{
    behavior_asset *a = model("OffsetCircleSections");
    graph *g = a->graph;
    node *offset = add(g, "Model Profile Offset");
    connect(g, circle(g, 2, 1, -1), offset, "Profile");
    scalar(g, offset, "Distance", .5f);
    publish(g, extrude(g, offset));
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 3.5f, -1, 0), 0));
    REQUIRE(near(sample(&p, NULL, 1, 1.5f, 0), 0));
    REQUIRE(contains(&p.bounds, 3.5f, -1, 0));
    scalar(g, offset, "Distance", -.5f);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 2.5f, -1, 0), 0));
    REQUIRE(sample(&p, NULL, 3, -1, 0) > 0);
    REQUIRE(sample(&p, NULL, 1, -1, 0) < 0);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(profile_booleans)
{
    const char *operations[] = { "Model Profile Union",
                                 "Model Profile Intersection",
                                 "Model Profile Difference" };
    for (int i = 0; i < 3; i++) {
        behavior_asset *a = model(operations[i]);
        graph *g = a->graph;
        node *operation = add(g, operations[i]);
        connect(g, circle(g, 1.5f, -1, 0), operation, "A");
        connect(g, circle(g, 1.5f, 1, 0), operation, "B");
        publish(g, extrude(g, operation));
        model_program p;
        model_program_init(&p);
        compile(a, &p);
        if (i == 0) {
            REQUIRE(sample(&p, NULL, -2, 0, 0) < 0);
            REQUIRE(sample(&p, NULL, 2, 0, 0) < 0);
            REQUIRE(near(sample(&p, NULL, 2.5f, 0, 0), 0));
        }
        if (i == 1) {
            REQUIRE(sample(&p, NULL, -2, 0, 0) > 0);
            REQUIRE(sample(&p, NULL, 0, 0, 0) < 0);
            REQUIRE(near(sample(&p, NULL, .5f, 0, 0), 0));
        }
        if (i == 2) {
            REQUIRE(sample(&p, NULL, -2, 0, 0) < 0);
            REQUIRE(sample(&p, NULL, 0, 0, 0) > 0);
            REQUIRE(near(sample(&p, NULL, -.5f, 0, 0), 0));
        }
        model_program_free(&p);
        behavior_asset_free(a);
    }
    behavior_asset *a = model("RectangularPlateCircularHole");
    graph *g = a->graph;
    node *rectangle = add(g, "Model Profile Rectangle"),
         *hole = add(g, "Model Profile Difference");
    vector(g, rectangle, "Half Size", "ShaderVec2", (float[2]){ 3, 2 });
    connect(g, rectangle, hole, "A");
    connect(g, circle(g, 1, 0, 0), hole, "B");
    publish(g, extrude(g, hole));
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 0, 0, 0), 1));
    REQUIRE(near(sample(&p, NULL, 1, 0, 0), 0));
    REQUIRE(sample(&p, NULL, 1.5f, 0, 0) < 0);
    REQUIRE(near(sample(&p, NULL, 3, 1, 0), 0));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(rim_sections)
{
    const char *operations[] = { "Model Rounded Extrude", "Model Beveled Extrude" };
    for (int i = 0; i < 2; i++) {
        behavior_asset *a = model(operations[i]);
        graph *g = a->graph;
        node *rim = add(g, operations[i]);
        connect(g, circle(g, 2, 0, 0), rim, "Profile");
        scalar(g, rim, "Half Depth", 1);
        scalar(g, rim, i == 0 ? "Radius" : "Width", .25f);
        publish(g, rim);
        model_program p;
        model_program_init(&p);
        compile(a, &p);
        // Independent cross-section: a quarter circle or a 45-degree chamfer
        // joins the side at (r=2,z=.75) to the cap at (r=1.75,z=1).
        float rim_point = i == 0 ? .75f + .25f / sqrtf(2) : .875f;
        REQUIRE(near(sample(&p, NULL, rim_point + 1, 0, rim_point), 0));
        REQUIRE(near(sample(&p, NULL, 2, 0, 0), 0));
        REQUIRE(near(sample(&p, NULL, 0, 0, 1), 0));
        REQUIRE(sample(&p, NULL, 2, 0, 1) > 0);
        REQUIRE(sample(&p, NULL, 1.7f, 0, .7f) < 0);
        REQUIRE(contains(&p.bounds, 2, 0, 0));
        REQUIRE(contains(&p.bounds, 0, 0, 1));
        char *saved = behavior_asset_save(a);
        behavior_asset *loaded = behavior_asset_load(saved);
        REQUIRE(loaded);
        model_program copy;
        model_program_init(&copy);
        compile(loaded, &copy);
        REQUIRE(near(sample(&copy, NULL, rim_point + 1, 0, rim_point), 0));
        model_program_free(&copy);
        behavior_asset_free(loaded);
        free(saved);
        model_program_free(&p);
        behavior_asset_free(a);
    }
    return true;
}
static node *sphere(graph *g, float radius)
{
    node *n = add(g, "Model Sphere");
    scalar(g, n, "Radius", radius);
    return n;
}
static node *empty_lens(graph *g)
{
    node *source = sphere(g, 1), *left = add(g, "Model Translate"),
         *right = add(g, "Model Translate"),
         *intersection = add(g, "Model Intersection");
    connect(g, source, left, "Geometry");
    connect(g, source, right, "Geometry");
    vector(g, left, "Offset", "ShaderVec3", (float[3]){ -1.5f, 0, 0 });
    vector(g, right, "Offset", "ShaderVec3", (float[3]){ 1.5f, 0, 0 });
    connect(g, left, intersection, "A");
    connect(g, right, intersection, "B");
    return intersection;
}
TEST_CASE(level_sets)
{
    behavior_asset *a = model("SphereLevelOffset");
    graph *g = a->graph;
    node *offset = add(g, "Model Offset");
    connect(g, sphere(g, 2), offset, "Geometry");
    scalar(g, offset, "Distance", .5f);
    publish(g, offset);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 2.5f, 0, 0), 0));
    REQUIRE(contains(&p.bounds, 2.5f, 0, 0));
    scalar(g, offset, "Distance", -.5f);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 1.5f, 0, 0), 0));
    model_program_free(&p);
    behavior_asset_free(a);
    for (int side = 0; side < 3; side++) {
        a = model("SphereShellSections");
        g = a->graph;
        node *shell = add(g, "Model Shell");
        connect(g, sphere(g, 2), shell, "Geometry");
        scalar(g, shell, "Thickness", .4f);
        integer(g, shell, "Side", side);
        publish(g, shell);
        model_program_init(&p);
        compile(a, &p);
        float inner[] = { 1.8f, 2, 1.6f }, outer[] = { 2.2f, 2.4f, 2 };
        REQUIRE(near(sample(&p, NULL, inner[side], 0, 0), 0));
        REQUIRE(near(sample(&p, NULL, outer[side], 0, 0), 0));
        REQUIRE(sample(&p, NULL, (inner[side] + outer[side]) / 2, 0, 0) < 0);
        REQUIRE(sample(&p, NULL, 0, 0, 0) > 0);
        REQUIRE(contains(&p.bounds, outer[side], 0, 0));
        model_program_free(&p);
        behavior_asset_free(a);
    }
    for (int side = -1; side < 2; side++) {
        a = model("PositiveLevelRevivesEmptyIntersection");
        g = a->graph;
        node *level = add(g, side < 0 ? "Model Offset" : "Model Shell");
        connect(g, empty_lens(g), level, "Geometry");
        scalar(g, level, side < 0 ? "Distance" : "Thickness", side == 0 ? 1.5f : .75f);
        if (side >= 0) {
            integer(g, level, "Side", side);
        }
        publish(g, level);
        model_program_init(&p);
        compile(a, &p);
        REQUIRE(sample(&p, NULL, 0, 0, 0) < 0);
        // Intersection of two independently inflated unit spheres reaches this
        // section even though their original zero-level AABBs do not overlap.
        float height = sqrtf(1.75f * 1.75f - 1.5f * 1.5f);
        REQUIRE(near(sample(&p, NULL, 0, height, 0), 0));
        REQUIRE(contains(&p.bounds, 0, height, 0));
        model_program_free(&p);
        behavior_asset_free(a);
    }
    return true;
}
TEST_CASE(swept_segments)
{
    behavior_asset *a = model("OpenBentTube");
    graph *g = a->graph;
    node *tube = add(g, "Model Tube Sweep");
    float path[][3] = { { -2, 0, 0 }, { 0, 0, 0 }, { 0, 2, 0 } };
    points(g, tube, "Points", "ShaderVec3", &path[0][0], 3, 3);
    scalar(g, tube, "Radius", .5f);
    publish(g, tube);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, -1, .5f, 0), 0));
    REQUIRE(near(sample(&p, NULL, -2.5f, 0, 0), 0));
    REQUIRE(near(sample(&p, NULL, 0, 0, .5f), 0));
    REQUIRE(near(sample(&p, NULL, -1, 1, 0), .5f));
    boolean(g, tube, "Closed", true);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, -1, 1, 0), -.5f));
    REQUIRE(contains(&p.bounds, -2.5f, 0, 0));
    REQUIRE(contains(&p.bounds, 0, 2.5f, 0));
    float duplicated[][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
    points(g, tube, "Points", "ShaderVec3", &duplicated[0][0], 2, 3);
    shader_error error = { 0 };
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(near(sample(&p, NULL, -1, 1, 0), -.5f));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(concave_sections)
{
    behavior_asset *a = model("ConcaveLPlate");
    graph *g = a->graph;
    node *profile = add(g, "Model Profile Polygon");
    float vertices[][2] = { { 0, 0 }, { 3, 0 }, { 3, 1 },
                            { 1, 1 }, { 1, 3 }, { 0, 3 } };
    points(g, profile, "Vertices", "ShaderVec2", &vertices[0][0], 6, 2);
    publish(g, extrude(g, profile));
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, .5f, .5f, 0), -.5f));
    REQUIRE(near(sample(&p, NULL, 2, 2, 0), 1));
    REQUIRE(near(sample(&p, NULL, 1.2f, 1.2f, 0), .2f));
    REQUIRE(sample(&p, NULL, .999f, 2, 0) < 0);
    REQUIRE(sample(&p, NULL, 1.001f, 2, 0) > 0);
    char *saved = behavior_asset_save(a);
    behavior_asset *loaded = behavior_asset_load(saved);
    REQUIRE(loaded);
    model_program copy;
    model_program_init(&copy);
    compile(loaded, &copy);
    REQUIRE(near(sample(&copy, NULL, 2, 2, 0), 1));
    model_program_free(&copy);
    behavior_asset_free(loaded);
    free(saved);
    float crossed[][2] = { { 0, 0 }, { 3, 3 }, { 0, 3 }, { 3, 0 } };
    points(g, profile, "Vertices", "ShaderVec2", &crossed[0][0], 4, 2);
    shader_error error = { 0 };
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(near(sample(&p, NULL, 2, 2, 0), 1));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
static node *parameter(graph *g, const char *name, float value, float low, float high)
{
    node *n = add(g, "Model Parameter");
    assert(!graph_set_node_reference(g, n, 0, name));
    scalar(g, n, "Default", value);
    scalar(g, n, "Minimum", low);
    scalar(g, n, "Maximum", high);
    return n;
}
static int parameter_slot(const model_program *p, const node *n)
{
    for (int i = 0; i < acount(p->parameters); i++) {
        if (p->parameters[i].node == n->id && !p->parameters[i].hidden) {
            return p->parameters[i].slot;
        }
    }
    assert(false);
    return -1;
}
TEST_CASE(runtime_transaction)
{
    behavior_asset *a = model("RuntimeFilletDepthContract");
    graph *g = a->graph;
    node *depth = parameter(g, "Depth", 1, .1f, 2),
         *radius = parameter(g, "Radius", .25f, .05f, 1);
    node *rim = add(g, "Model Rounded Extrude");
    connect(g, circle(g, 2, 0, 0), rim, "Profile");
    connect(g, depth, rim, "Half Depth");
    connect(g, radius, rim, "Radius");
    publish(g, rim);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    float incoming[32][4], prepared[32][4];
    shader_error error = { 0 };
    REQUIRE(model_program_parameters(&p, incoming, &error));
    model_bounds bounds = { 0 };
    REQUIRE(model_program_prepare(&p, incoming, prepared, &bounds, &error));
    float old_prepared[32][4];
    memcpy(old_prepared, prepared, sizeof prepared);
    model_bounds old_bounds = bounds;
    incoming[parameter_slot(&p, depth)][0] = .1f;
    REQUIRE(!model_program_prepare(&p, incoming, prepared, &bounds, &error));
    REQUIRE(!memcmp(prepared, old_prepared, sizeof prepared));
    REQUIRE(!memcmp(&bounds, &old_bounds, sizeof bounds));
    REQUIRE(near(sample(&p, prepared, 0, 0, 1), 0));
    incoming[parameter_slot(&p, radius)][0] = .05f;
    REQUIRE(model_program_prepare(&p, incoming, prepared, &bounds, &error));
    REQUIRE(near(sample(&p, prepared, 0, 0, .1f), 0));
    REQUIRE(contains(&bounds, 0, 0, .1f));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(bezier_sections)
{
    behavior_asset *a = model("StraightCubicRectangle");
    graph *g = a->graph;
    node *profile = add(g, "Model Profile Bezier");
    // Four independent straight cubic spans trace a 6 by 4 rectangle.
    float controls[][2] = { { -3, -2 }, { -1, -2 },      { 1, -2 },
                            { 3, -2 },  { 3, -2.f / 3 }, { 3, 2.f / 3 },
                            { 3, 2 },   { 1, 2 },        { -1, 2 },
                            { -3, 2 },  { -3, 2.f / 3 }, { -3, -2.f / 3 } };
    points(g, profile, "Control Points", "ShaderVec2", &controls[0][0], 12, 2);
    integer(g, profile, "Steps", 2);
    publish(g, extrude(g, profile));
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 3, 1, 0), 0));
    REQUIRE(near(sample(&p, NULL, 0, 2, 0), 0));
    REQUIRE(near(sample(&p, NULL, 4, 3, 0), sqrtf(2)));
    REQUIRE(sample(&p, NULL, 0, 0, 0) < 0);
    shader_error error = { 0 };
    integer(g, profile, "Steps", 0);
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(near(sample(&p, NULL, 3, 1, 0), 0));
    integer(g, profile, "Steps", 9);
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(near(sample(&p, NULL, 4, 3, 0), sqrtf(2)));
    model_program_free(&p);
    behavior_asset_free(a);

    a = model("StraightCubicCapsule");
    g = a->graph;
    node *tube = add(g, "Model Bezier Tube");
    float line[][3] = { { 0, -3, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 3, 0 } };
    points(g, tube, "Control Points", "ShaderVec3", &line[0][0], 4, 3);
    scalar(g, tube, "Radius", .5f);
    integer(g, tube, "Steps", 4);
    boolean(g, tube, "Closed", false);
    publish(g, tube);
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, .5f, 2, 0), 0));
    REQUIRE(near(sample(&p, NULL, 0, 3.5f, 0), 0));
    REQUIRE(near(sample(&p, NULL, 0, -4, 0), .5f));
    REQUIRE(contains(&p.bounds, 0, -3.5f, 0));
    REQUIRE(contains(&p.bounds, .5f, 3, 0));
    integer(g, tube, "Steps", 33);
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(near(sample(&p, NULL, 0, -4, 0), .5f));
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
static node *vector_parameter(graph *g, const char *name, const float value[3])
{
    node *n = add(g, "Model Parameter");
    assert(!graph_set_node_reference(g, n, 0, name));
    assert(!graph_set_pin_type(g, n, false, 0, PIN_STRUCT, "ShaderVec3"));
    vector(g, n, "Default", "ShaderVec3", value);
    vector(g, n, "Minimum", "ShaderVec3", (float[3]){ -20, -20, -20 });
    vector(g, n, "Maximum", "ShaderVec3", (float[3]){ 20, 20, 20 });
    return n;
}
static node *move(graph *g, node *geometry, float x, float y, float z)
{
    node *n = add(g, "Model Translate");
    connect(g, geometry, n, "Geometry");
    vector(g, n, "Offset", "ShaderVec3", (float[3]){ x, y, z });
    return n;
}
TEST_CASE(pivot_sections)
{
    const float half_pi = 1.57079632679f;
    behavior_asset *a = model("AsymmetricPivotPose");
    graph *g = a->graph;
    node *box = add(g, "Model Box");
    vector(g, box, "Half Size", "ShaderVec3", (float[3]){ .5f, .25f, .125f });
    node *pivot = vector_parameter(g, "Pivot", (float[3]){ 1, 0, 0 });
    node *angles = vector_parameter(g, "Angles", (float[3]){ 0, 0, half_pi });
    node *rotate = add(g, "Model Rotate Around");
    connect(g, move(g, box, 3, 0, 0), rotate, "Geometry");
    connect(g, pivot, rotate, "Pivot");
    connect(g, angles, rotate, "Angles");
    publish(g, rotate);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 1, 2.5f, 0), 0));
    REQUIRE(near(sample(&p, NULL, 1.25f, 2, 0), 0));
    REQUIRE(contains(&p.bounds, 1, 2.5f, 0));
    float incoming[32][4], prepared[32][4];
    model_bounds bounds;
    shader_error error = { 0 };
    REQUIRE(model_program_parameters(&p, incoming, &error));
    incoming[parameter_slot(&p, pivot)][0] = 2;
    incoming[parameter_slot(&p, angles)][2] = -half_pi;
    REQUIRE(model_program_prepare(&p, incoming, prepared, &bounds, &error));
    REQUIRE(near(sample(&p, prepared, 2, -1.5f, 0), 0));
    REQUIRE(contains(&bounds, 2, -1.5f, 0));
    model_program_free(&p);
    behavior_asset_free(a);

    a = model("AsymmetricPivotScale");
    g = a->graph;
    box = add(g, "Model Box");
    vector(g, box, "Half Size", "ShaderVec3", (float[3]){ .5f, .25f, .125f });
    node *scale = add(g, "Model Scale Around");
    connect(g, move(g, box, 3, 0, 0), scale, "Geometry");
    vector(g, scale, "Pivot", "ShaderVec3", (float[3]){ 1, 0, 0 });
    vector(g, scale, "Factors", "ShaderVec3", (float[3]){ 2, 3, 4 });
    publish(g, scale);
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 6, 0, 0), 0));
    REQUIRE(near(sample(&p, NULL, 5, .75f, 0), 0));
    REQUIRE(near(sample(&p, NULL, 5, 0, .5f), 0));
    REQUIRE(contains(&p.bounds, 6, .75f, .5f));
    REQUIRE(sample(&p, NULL, 5, 0, 0) < 0);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
static node *anchor(graph *g, const char *name, const float position[3], const float angles[3])
{
    node *n = add(g, "Model Anchor");
    assert(!graph_set_node_reference(g, n, 0, name));
    vector(g, n, "Position", "ShaderVec3", position);
    vector(g, n, "Angles", "ShaderVec3", angles);
    return n;
}
TEST_CASE(anchor_sections)
{
    const float half_pi = 1.57079632679f;
    behavior_asset *a = model("RotatedAnchorAttachment");
    graph *g = a->graph;
    node *box = add(g, "Model Box");
    vector(g, box, "Half Size", "ShaderVec3", (float[3]){ .5f, .25f, .125f });
    node *source = anchor(g, "Source", (float[3]){ 1, 2, 3 }, (float[3]){ half_pi, 0, half_pi });
    node *rotate = add(g, "Model Rotate");
    connect(g, move(g, box, 2, 0, 0), rotate, "Geometry");
    vector(g, rotate, "Angles", "ShaderVec3", (float[3]){ half_pi, 0, half_pi });
    node *target_position = vector_parameter(g, "Target", (float[3]){ 10, 0, 0 });
    node *target = anchor(g, "Target", (float[3]){ 10, 0, 0 }, (float[3]){ 0, half_pi, 0 });
    connect(g, target_position, target, "Position");
    node *attach = add(g, "Model Attach");
    connect(g, move(g, rotate, 1, 2, 3), attach, "Geometry");
    connect(g, source, attach, "Source Anchor");
    connect(g, target, attach, "Target Anchor");
    publish(g, attach);
    model_program p;
    model_program_init(&p);
    compile(a, &p);
    REQUIRE(near(sample(&p, NULL, 10, 0, -2.5f), 0));
    REQUIRE(near(sample(&p, NULL, 10.125f, 0, -2), 0));
    REQUIRE(contains(&p.bounds, 10, 0, -2.5f));
    float incoming[32][4], prepared[32][4], position[3], angles[3];
    model_bounds bounds;
    shader_error error = { 0 };
    REQUIRE(model_program_parameters(&p, incoming, &error));
    incoming[parameter_slot(&p, target_position)][0] = -4;
    REQUIRE(model_program_prepare(&p, incoming, prepared, &bounds, &error));
    REQUIRE(near(sample(&p, prepared, -4, 0, -2.5f), 0));
    REQUIRE(contains(&bounds, -4, 0, -2.5f));
    REQUIRE(acount(p.anchors) == 2);
    int target_index = !strcmp(p.anchors[0].name, "Target") ? 0 : 1;
    REQUIRE(model_program_anchor(&p, target_index, prepared, position, angles, &error));
    REQUIRE(near(position[0], -4));
    REQUIRE(near(angles[1], half_pi));
    REQUIRE(!graph_set_node_reference(g, source, 0, "Socket"));
    char *saved = behavior_asset_save(a);
    behavior_asset *loaded = behavior_asset_load(saved);
    REQUIRE(loaded);
    compile(loaded, &p);
    bool renamed = false;
    for (int i = 0; i < acount(p.anchors); i++) {
        renamed |= !strcmp(p.anchors[i].name, "Socket");
    }
    REQUIRE(renamed);
    REQUIRE(near(sample(&p, NULL, 10, 0, -2.5f), 0));
    free(saved);
    behavior_asset_free(loaded);
    model_program_free(&p);
    behavior_asset_free(a);
    return true;
}
TEST_CASE(reused_anchor)
{
    behavior_asset *base = model("ReusableSocket"),
                   *parent = model("ReusedSocketAttachment");
    graph *g = base->graph;
    node *position = vector_parameter(g, "Socket Position", (float[3]){ 1, 2, 0 });
    node *socket = anchor(g, "Socket", (float[3]){ 0, 0, 0 }, (float[3]){ 0, 0, 0 });
    connect(g, position, socket, "Position");
    node *sphere = add(g, "Model Sphere"), *shift = add(g, "Model Translate");
    scalar(g, sphere, "Radius", .5f);
    connect(g, sphere, shift, "Geometry");
    connect(g, position, shift, "Offset");
    publish(g, shift);
    g = parent->graph;
    node *instance = add(g, "Model Instance");
    instance->reference_asset = graph_own_str(g, base->name, strlen(base->name));
    float override_position[3] = { 4, 2, 0 };
    pin_spec override = { .name = "Socket Position",
                          .type = PIN_STRUCT,
                          .type_name = "ShaderVec3",
                          .declaration_id = position->id,
                          .default_value = { .type = PIN_STRUCT,
                                             .type_name = "ShaderVec3",
                                             .ptr = override_position } };
    REQUIRE(graph_add_pin(g, instance, false, &override) == 0);
    pin_spec exported = { .name = "Socket",
                          .type = PIN_HANDLE,
                          .type_name = "Anchor3D",
                          .declaration_id = socket->id,
                          .default_value = { .type = PIN_HANDLE,
                                             .type_name = "Anchor3D" } };
    int output = graph_add_pin(g, instance, true, &exported);
    REQUIRE(output == 1);
    node *target = anchor(g, "Destination", (float[3]){ 10, 0, 0 }, (float[3]){ 0, 0, 0 });
    node *attach = add(g, "Model Attach");
    connect(g, instance, attach, "Geometry");
    REQUIRE(!graph_connect_data(g, instance, output, attach, input(attach, "Source Anchor")));
    connect(g, target, attach, "Target Anchor");
    publish(g, attach);
    const behavior_asset *catalog[] = { base, parent };
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile_with_catalog(parent, catalog, 2, &p, &error));
    REQUIRE(near(sample(&p, NULL, 10.5f, 0, 0), 0));
    float point[3], angles[3];
    bool found = false;
    for (int i = 0; i < acount(p.anchors); i++) {
        if (strcmp(p.anchors[i].asset, base->name)) {
            continue;
        }
        REQUIRE(p.anchors[i].node == socket->id && *p.anchors[i].scope);
        REQUIRE(model_program_anchor(&p, i, NULL, point, angles, &error));
        REQUIRE(near(point[0], 4));
        REQUIRE(near(point[1], 2));
        found = true;
    }
    REQUIRE(found);
    // Removing the sparse override exposes one authoritative public declaration.
    // Reading the anchor and its attached geometry must not fork that instance.
    REQUIRE(!graph_remove_pin(g, instance, false, 0));
    REQUIRE(model_compile_with_catalog(parent, catalog, 2, &p, &error));
    int public_count = 0, slot = -1;
    for (int i = 0; i < acount(p.parameters); i++) {
        if (!p.parameters[i].hidden) {
            public_count++;
            slot = p.parameters[i].slot;
        }
    }
    REQUIRE(public_count == 1);
    float incoming[32][4], prepared[32][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&p, incoming, &error));
    incoming[slot][0] = -3;
    REQUIRE(model_program_prepare(&p, incoming, prepared, &bounds, &error));
    REQUIRE(near(sample(&p, prepared, 10.5f, 0, 0), 0));
    for (int i = 0; i < acount(p.anchors); i++) {
        if (!strcmp(p.anchors[i].asset, base->name)) {
            REQUIRE(model_program_anchor(&p, i, prepared, point, angles, &error));
            REQUIRE(near(point[0], -3));
        }
    }
    model_program_free(&p);
    behavior_asset_free(parent);
    behavior_asset_free(base);
    return true;
}
void test_model_constructive()
{
    test_graph_packs_install();
    RUN_TEST_CASE(profile_offset);
    RUN_TEST_CASE(profile_booleans);
    RUN_TEST_CASE(rim_sections);
    RUN_TEST_CASE(level_sets);
    RUN_TEST_CASE(swept_segments);
    RUN_TEST_CASE(concave_sections);
    RUN_TEST_CASE(runtime_transaction);
    RUN_TEST_CASE(bezier_sections);
    RUN_TEST_CASE(pivot_sections);
    RUN_TEST_CASE(anchor_sections);
    RUN_TEST_CASE(reused_anchor);
    test_graph_packs_restore();
}
