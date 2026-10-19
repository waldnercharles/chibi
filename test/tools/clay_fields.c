#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>

/* A fixed CPU field-contract corpus, not a replacement for the production
 * picker or GPU marcher. New selector execution requires independent admission. */
static behavior_asset *asset;
static model_program program;
static shader_error error;

static void setup()
{
    asset = behavior_asset_new("ClayFieldContract");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    model_program_init(&program);
    error = (shader_error){ 0 };
}

static void teardown()
{
    model_program_free(&program);
    behavior_asset_free(asset);
    asset = NULL;
}

static node *add(node_desc *type)
{
    return graph_add_node(asset->graph, type);
}

static void number(node *n, int pin, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(asset->graph, n, 0, pin, &v));
}

static void vector(node *n, int pin, float x, float y, float z)
{
    float data[3] = { x, y, z };
    pin_value v = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = data };
    assert(!graph_set_pin_default(asset->graph, n, 0, pin, &v));
}

static void connect(node *from, node *to, int pin)
{
    assert(!graph_connect_data(asset->graph, from, 0, to, pin));
}

static node *sphere(float radius, float x)
{
    node *solid = add(&NODE_ModelSphere);
    number(solid, 0, radius);
    if (!x) {
        return solid;
    }
    node *move = add(&NODE_ModelTranslate);
    vector(move, 1, x, 0, 0);
    connect(solid, move, 0);
    return move;
}

static bool compile()
{
    REQUIRE(model_compile(asset, &program, &error));
    REQUIRE(acount(program.shader.nodes) <= 128);
    REQUIRE(!program.bounds.empty);
    return true;
}

static bool sample(const float p[3], float *distance)
{
    shader_sample_context context = { .object_position = { p[0], p[1], p[2] } };
    float output[SHADER_OUTPUT_MAX][4];
    if (!shader_graph_eval_outputs(&program.shader, &context, NULL, output, &error)) {
        return false;
    }
    *distance = output[0][0];
    return isfinite(*distance);
}

static bool ray_sample(const float origin[3], const float direction[3], double t, float *value)
{
    float point[3];
    for (int axis = 0; axis < 3; axis++) {
        point[axis] = (float)(origin[axis] + direction[axis] * t);
    }
    return sample(point, value);
}

/* Slow, independent traversal reference for an explicitly bracketed crossing.
 * It cannot discover tangencies or prove the interval contains the first root. */
static bool bisect(const float origin[3], const float direction[3], double low, double high, double *root)
{
    float a, b;
    if (!ray_sample(origin, direction, low, &a) ||
        !ray_sample(origin, direction, high, &b) || a == 0 || b == 0 ||
        (a < 0) == (b < 0)) {
        return false;
    }
    for (int step = 0; step < 24; step++) {
        double middle = (low + high) * .5;
        float value;
        if (!ray_sample(origin, direction, middle, &value)) {
            return false;
        }
        if (value == 0) {
            *root = middle;
            return true;
        }
        if ((value < 0) == (a < 0)) {
            low = middle;
        } else {
            high = middle;
        }
    }
    *root = (low + high) * .5;
    return true;
}

TEST_CASE(hard_csg_is_a_bound_not_exact_distance)
{
    node *a = sphere(1, -.5f), *b = sphere(1, .5f);
    node *combine = add(&NODE_ModelUnion),
         *output = model_graph_add_output(asset->graph);
    connect(a, combine, 0);
    connect(b, combine, 1);
    connect(combine, output, 0);
    REQUIRE(compile());
    float distance;
    REQUIRE(sample((float[]){ 0, 0, 0 }, &distance));
    /* The nearest exterior of the union is its two-sphere seam circle. */
    double exact_inside = -sqrt(.75);
    REQUIRE(fabsf(distance + .5f) < 1e-6f);
    REQUIRE(distance > exact_inside + .3);
    REQUIRE(fabs(distance) < fabs(exact_inside));

    combine = add(&NODE_ModelIntersection);
    connect(a, combine, 0);
    connect(b, combine, 1);
    connect(combine, output, 0);
    REQUIRE(compile());
    REQUIRE(sample((float[]){ 0, 1.5f, 0 }, &distance));
    /* Symmetry puts the nearest point on the intersection at (0,sqrt(.75),0). */
    double exact_outside = 1.5 - sqrt(.75);
    REQUIRE(fabs(distance - (sqrt(2.5) - 1)) < 1e-6);
    REQUIRE(distance > 0 && distance < exact_outside - .05);
    return true;
}

TEST_CASE(smooth_join_has_an_independent_surface_and_containing_bounds)
{
    node *a = sphere(.8f, -.6f), *b = sphere(.8f, .6f);
    node *join = add(&NODE_ModelSmoothUnion),
         *output = model_graph_add_output(asset->graph);
    number(join, 2, .4f);
    connect(a, join, 0);
    connect(b, join, 1);
    connect(join, output, 0);
    REQUIRE(compile());
    /* At equal child distances, the polynomial join subtracts k/4. */
    double expected = sqrt(.9 * .9 - .6 * .6), root = -1;
    REQUIRE(bisect((float[]){ 0, 0, 0 }, (float[]){ 0, 1, 0 }, 0, 1, &root));
    REQUIRE(fabs(root - expected) < 2e-6);
    const float directions[4][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int side = 0; side < 4; side++) {
        for (int offset = -1; offset <= 1; offset++) {
            float p[3];
            for (int axis = 0; axis < 3; axis++) {
                p[axis] = directions[side][axis] * (float)(expected + offset * .001);
                REQUIRE(p[axis] >= program.bounds.min[axis]);
                REQUIRE(p[axis] <= program.bounds.max[axis]);
            }
            float distance;
            REQUIRE(sample(p, &distance));
            REQUIRE(offset ? distance * offset > 0 : fabsf(distance) < 2e-6f);
        }
    }
    return true;
}

TEST_CASE(uniform_scale_preserves_outside_inside_and_grazing_sphere_roots)
{
    node *solid = sphere(1, 0), *scale = add(&NODE_ModelScale);
    node *output = model_graph_add_output(asset->graph);
    connect(solid, scale, 0);
    connect(scale, output, 0);
    const float scales[] = { .25f, 1, 4 };
    const float heights[] = { 0, .5f, .999f };
    for (int size = 0; size < 3; size++) {
        float s = scales[size];
        vector(scale, 1, s, s, s);
        REQUIRE(compile());
        for (int axis = 0; axis < 3; axis++) {
            REQUIRE(program.bounds.min[axis] == -s);
            REQUIRE(program.bounds.max[axis] == s);
        }
        for (int ray = 0; ray < 3; ray++) {
            float origin[3] = { 3 * s, heights[ray] * s, 0 };
            double root = -1;
            double expected = 3 * s -
                              s * sqrt(1 - (double)heights[ray] * heights[ray]);
            REQUIRE(bisect(origin, (float[]){ -1, 0, 0 }, s, 3 * s, &root));
            REQUIRE(fabs(root - expected) <= s * 2e-5);
        }
        double root = -1;
        REQUIRE(bisect((float[]){ 0, 0, 0 }, (float[]){ 1, 0, 0 }, 0, 2 * s, &root));
        REQUIRE(fabs(root - s) <= s * 2e-6);
        /* A miss is not a bracketed crossing; failure preserves the result. */
        root = -1;
        REQUIRE(
            !bisect((float[]){ 3 * s, 1.01f * s, 0 }, (float[]){ -1, 0, 0 }, s, 3 * s, &root)
        );
        REQUIRE(root == -1);
    }
    return true;
}

TEST_CASE(thin_difference_shell_retains_two_distinct_roots)
{
    node *outer = sphere(1, 0), *inner = sphere(.99f, 0);
    node *cut = add(&NODE_ModelDifference),
         *output = model_graph_add_output(asset->graph);
    connect(outer, cut, 0);
    connect(inner, cut, 1);
    connect(cut, output, 0);
    REQUIRE(compile());
    float origin[3] = { 3, 0, 0 }, direction[3] = { -1, 0, 0 };
    double a = -1, b = -1;
    REQUIRE(bisect(origin, direction, 1.99, 2.005, &a));
    REQUIRE(bisect(origin, direction, 2.005, 2.02, &b));
    REQUIRE(fabs(a - 2) < 2e-6);
    REQUIRE(fabs(b - 2.01) < 2e-6);
    REQUIRE(b - a > .00999 && b - a < .01001);
    float distance;
    REQUIRE(sample((float[]){ .995f, 0, 0 }, &distance));
    REQUIRE(distance < 0);
    REQUIRE(sample((float[]){ .98f, 0, 0 }, &distance));
    REQUIRE(distance > 0);
    return true;
}

TEST_CASE(small_conservative_residual_does_not_bound_ray_depth_error)
{
    node *solid = sphere(1, 0), *scale = add(&NODE_ModelScale);
    node *output = model_graph_add_output(asset->graph);
    vector(scale, 1, 100, 1, 1);
    connect(solid, scale, 0);
    connect(scale, output, 0);
    REQUIRE(compile());
    float distance;
    REQUIRE(sample((float[]){ 91, .5f, 0 }, &distance));
    float extent = program.bounds.max[0] - program.bounds.min[0];
    float residual_tolerance = fmaxf(1e-5f, extent * .0002f);
    REQUIRE(distance > 0 && distance < residual_tolerance);
    double exact_x = 100 * sqrt(.75);
    REQUIRE(91 - exact_x > 4);
    /* This is a limitation specimen, not an assertion that production picking
     * returns this point or that its residual is an intersection-error bound. */
    return true;
}

void test_tool_clay_fields()
{
    test_graph_packs_install();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(hard_csg_is_a_bound_not_exact_distance);
    RUN_TEST_CASE(smooth_join_has_an_independent_surface_and_containing_bounds);
    RUN_TEST_CASE(uniform_scale_preserves_outside_inside_and_grazing_sphere_roots);
    RUN_TEST_CASE(thin_difference_shell_retains_two_distinct_roots);
    RUN_TEST_CASE(small_conservative_residual_does_not_bound_ray_depth_error);
    pu_setup(NULL, NULL);
    test_graph_packs_restore();
}
