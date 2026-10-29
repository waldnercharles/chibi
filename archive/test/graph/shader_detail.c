#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "runtime/shaders/detail.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int operation(shader_graph *g, shader_op op, int width, int a, int b, int c, int d)
{
    int i = shader_graph_add(g, op, width);
    int inputs[] = { a, b, c, d };
    memcpy(g->nodes[i].input, inputs, sizeof inputs);
    g->nodes[i].source_id = 1000 + i;
    g->output_count = 1;
    g->output[0] = i;
    return i;
}
static int constant(shader_graph *g, int width, float x, float y, float z)
{
    int i = operation(g, SHADER_CONSTANT, width, -1, -1, -1, -1);
    float values[] = { x, y, z, 0 };
    memcpy(g->nodes[i].value, values, sizeof values);
    return i;
}
static shader_sample_context sample()
{
    shader_sample_context s = { .normal = { 0, 1, 0 } };
    for (int i = 0; i < 3; i++) {
        s.object_from_world[i][i] = s.part_from_world[i][i] = 1;
    }
    s.uv_from_world[0][0] = s.uv_from_world[1][2] = 1;
    return s;
}
static bool near(float actual, float expected)
{
    if (fabsf(actual - expected) <= .0005f) {
        return true;
    }
    fprintf(stderr, "surface detail: %.9g expected %.9g\n", actual, expected);
    return false;
}
static bool direction(const float actual[4], float x, float y, float z)
{
    float length = sqrtf(x * x + y * y + z * z);
    return near(actual[0], x / length) && near(actual[1], y / length) &&
           near(actual[2], z / length);
}
static void evaluate(const shader_graph *g, const shader_sample_context *s, float result[4])
{
    float outputs[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    bool ok = shader_graph_eval_outputs(g, s, NULL, outputs, &error);
    if (!ok) {
        fprintf(
            stderr,
            "surface detail node %llu: %s\n",
            (unsigned long long)error.node,
            error.message
        );
    }
    assert(ok);
    memcpy(result, outputs[0], sizeof outputs[0]);
}
/* Whether g's GLSL avoids the identifier GLSL ES reserves as "sample". */
static bool emitted_source(const shader_graph *g, const char *prefix)
{
    shader_error error = { 0 };
    char *glsl = shader_graph_glsl_named(g, prefix, &error);
    assert(glsl);
    bool clean = true;
    // Check identifier tokens, allowing sg_sample and comments without
    // rejecting valid substrings.
    for (const char *p = glsl; *p;) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') {
                p++;
            }
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                p++;
            }
            if (*p) {
                p += 2;
            }
            continue;
        }
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *start = p++;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            clean = clean && (p - start != 6 || memcmp(start, "sample", 6));
            continue;
        }
        p++;
    }
    free(glsl);
    return clean;
}
static int projection(shader_graph *g, shader_op op, int position, int axis, float u, float v)
{
    int scale = constant(g, 2, u, v, 0);
    int offset = constant(g, 2, .1f, .2f, 0);
    int i = operation(g, op, 2, position, scale, offset, -1);
    g->nodes[i].value[0] = axis;
    return i;
}
TEST_CASE(mapping_landmarks)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    shader_sample_context s = sample();
    memcpy(s.part_position, (float[]){ 2, 3, 4 }, sizeof s.part_position);
    float result[4];
    int planar = projection(&g, SHADER_PLANAR_MAPPING, p, 0, 2, -3);
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 8.1f));
    REQUIRE(near(result[1], 9.2f));
    g.nodes[planar].value[0] = 1;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 4.1f));
    REQUIRE(near(result[1], -11.8f));
    g.nodes[planar].value[0] = 2;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 4.1f));
    REQUIRE(near(result[1], 9.2f));
    int cylinder = projection(&g, SHADER_CYLINDRICAL_MAPPING, p, 1, 2, -3);
    const float points[][3] = { { 0, 3, 1 }, { 1, 3, 0 }, { -1, 3, 0 } };
    const float expected[] = { 1.1f, 1.6f, .6f };
    for (int i = 0; i < 3; i++) {
        memcpy(s.part_position, points[i], sizeof s.part_position);
        evaluate(&g, &s, result);
        REQUIRE(near(result[0], expected[i]));
        REQUIRE(near(result[1], -8.8f));
    }
    g.nodes[cylinder].value[0] = 0;
    memcpy(s.part_position, (float[]){ 3, 1, 0 }, sizeof s.part_position);
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 1.1f));
    REQUIRE(near(result[1], -8.8f));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(bump_planes)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    int slopes = constant(&g, 3, 2, -1, 3);
    int height = operation(&g, SHADER_DOT, 1, p, slopes, -1, -1);
    int strength = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .01f, 0, 0);
    int normal = operation(&g, SHADER_NORMAL, 3, -1, -1, -1, -1);
    int bump = operation(&g, SHADER_BUMP, 3, height, strength, spacing, normal);
    shader_sample_context s = sample();
    float result[4];
    evaluate(&g, &s, result);
    REQUIRE(direction(result, -2, 1, -3));
    // Local x=-world z/2, local y=world y, local z=2*world x.
    // h=2*x-y+3*z therefore has world gradient (6,-1,-1).
    memcpy(
        s.part_from_world,
        (float[][3]){ { 0, 0, -.5f }, { 0, 1, 0 }, { 2, 0, 0 } },
        sizeof s.part_from_world
    );
    evaluate(&g, &s, result);
    REQUIRE(direction(result, -6, 1, 1));
    g.nodes[strength].value[0] = -1;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, 6, 1, -1));
    g.nodes[strength].value[0] = 0;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, 0, 1, 0));
    g.nodes[strength].value[0] = 1;
    g.nodes[spacing].value[0] = 0;
    shader_error error = { 0 };
    REQUIRE(!shader_graph_validate(&g, &error));
    REQUIRE(error.node == g.nodes[bump].source_id);
    REQUIRE(strstr(error.message, "Distance"));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(normal_frames)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    int uv = projection(&g, SHADER_PLANAR_MAPPING, p, 1, 1, 1);
    int color = constant(&g, 3, .75f, .75f, 1);
    int strength = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .01f, 0, 0);
    int map = operation(&g, SHADER_NORMAL_MAP, 3, color, uv, strength, spacing);
    shader_sample_context s = sample();
    float result[4];
    evaluate(&g, &s, result);
    REQUIRE(direction(result, .5f, 1, .5f));
    REQUIRE(emitted_source(&g, "detail_normal_test"));
    g.nodes[map].value[0] = 1;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, .5f, 1, -.5f));
    g.nodes[map].value[0] = 0;
    memcpy(
        s.part_from_world,
        (float[][3]){ { 0, 0, -.5f }, { 0, 1, 0 }, { 2, 0, 0 } },
        sizeof s.part_from_world
    );
    evaluate(&g, &s, result);
    REQUIRE(direction(result, .5f, 1, -.125f));
    g.nodes[color].value[0] = g.nodes[color].value[1] = .5f;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, 0, 1, 0));
    g.nodes[color].value[0] = g.nodes[color].value[1] = .75f;
    memset(s.part_from_world, 0, sizeof s.part_from_world);
    evaluate(&g, &s, result);
    REQUIRE(direction(result, 0, 1, 0));
    s = sample();
    // UV frequency and derivative spacing are independent. A two-unit delta
    // in an affine map is not a repeating angular seam.
    g.nodes[g.nodes[uv].input[1]].value[0] = 100;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, .5f, 1, .005f));
    // A common finite UV frequency must not erase the decoded normal when
    // squaring its covectors would overflow float arithmetic.
    g.nodes[g.nodes[uv].input[1]].value[0] = 1e20f;
    g.nodes[g.nodes[uv].input[1]].value[1] = 1e20f;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, .5f, 1, .5f));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(scaled_cylindrical_seam)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    int uv = projection(&g, SHADER_CYLINDRICAL_MAPPING, p, 1, 2, 1);
    int color = constant(&g, 3, .75f, .75f, 1);
    int strength = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .001f, 0, 0);
    operation(&g, SHADER_NORMAL_MAP, 3, color, uv, strength, spacing);
    shader_sample_context s = sample();
    memcpy(s.part_position, (float[]){ 0, 0, -1 }, sizeof s.part_position);
    memcpy(s.normal, (float[]){ 0, 0, -1 }, sizeof s.normal);
    float result[4];
    evaluate(&g, &s, result);
    // Two turns of U: dU/dx=-1/pi at the seam; dV/dy=1.
    REQUIRE(direction(result, -.5f / 3.14159265f, .5f, -1));
    g.nodes[g.nodes[uv].input[1]].value[0] = -2;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, .5f / 3.14159265f, .5f, -1));
    int shift = constant(&g, 2, 0, 0, 0);
    int indirect = operation(&g, SHADER_ADD, 2, uv, shift, -1, -1);
    int rejected = operation(&g, SHADER_NORMAL_MAP, 3, color, indirect, strength, spacing);
    shader_error error = { 0 };
    REQUIRE(!shader_graph_validate(&g, &error));
    REQUIRE(error.node == g.nodes[rejected].source_id);
    REQUIRE(strstr(error.message, "Cylindrical"));
    shader_graph_free(&g);
    return true;
}
static const unsigned char image_pixels[] = {
    255, 0, 0, 64, 0, 255, 0, 128, 0, 0, 255, 192, 255, 255, 255, 255,
};
static bool resolve_image(void *userdata, const shader_image_source *source, shader_image_view *view)
{
    assert(!strcmp(source->key, "analytic"));
    *view = userdata ? *(shader_image_view *)userdata
                     : (shader_image_view){ image_pixels, 2, 2 };
    return true;
}
static int image_source(shader_graph *g, shader_image_filter filter)
{
    shader_image_source image = { .key = "analytic",
                                  .color_space = SHADER_IMAGE_LINEAR,
                                  .filter = filter,
                                  .wrap = SHADER_IMAGE_CLAMP };
    return shader_graph_add_image(g, &image);
}
TEST_CASE(sampled_height_and_linear_data)
{
    shader_graph g;
    shader_graph_init(&g);
    int uv = operation(&g, SHADER_UV, 2, -1, -1, -1, -1);
    int texture = operation(&g, SHADER_IMAGE_TEXTURE, 3, uv, -1, -1, -1);
    g.nodes[texture].image = image_source(&g, SHADER_IMAGE_BILINEAR);
    int height = operation(&g, SHADER_X, 1, texture, -1, -1, -1);
    int strength = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .01f, 0, 0);
    int normal = operation(&g, SHADER_NORMAL, 3, -1, -1, -1, -1);
    operation(&g, SHADER_BUMP, 3, height, strength, spacing, normal);
    const unsigned char pixels[] = {
        0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255,
    };
    shader_image_view view = { pixels, 2, 2 };
    shader_sample_context s = sample();
    s.resolve_image = resolve_image;
    s.image_userdata = &view;
    s.uv[0] = s.uv[1] = .5f;
    float result[4];
    evaluate(&g, &s, result);
    // Bilinear interpolation changes from black at u=.25 to white at u=.75.
    // Its unit-height world slope is therefore 2, with no v slope.
    REQUIRE(direction(result, -2, 1, 0));
    s.uv_from_world[0][0] = -.5f;
    evaluate(&g, &s, result);
    REQUIRE(direction(result, 1, 1, 0));
    int map = operation(&g, SHADER_NORMAL_MAP, 3, texture, uv, strength, spacing);
    g.images[0].color_space = SHADER_IMAGE_SRGB;
    shader_error error = { 0 };
    REQUIRE(!shader_graph_validate(&g, &error));
    REQUIRE(error.node == g.nodes[map].source_id);
    REQUIRE(strstr(error.message, "Linear"));
    g.images[0].color_space = SHADER_IMAGE_LINEAR;
    REQUIRE(shader_graph_validate(&g, &error));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(triplanar_height)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    int sharp = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .01f, 0, 0);
    int texture = operation(&g, SHADER_TRIPLANAR_TEXTURE, 3, p, sharp, spacing, -1);
    g.nodes[texture].image = image_source(&g, SHADER_IMAGE_BILINEAR);
    int height = operation(&g, SHADER_X, 1, texture, -1, -1, -1);
    int strength = constant(&g, 1, 1, 0, 0);
    int normal = operation(&g, SHADER_NORMAL, 3, -1, -1, -1, -1);
    operation(&g, SHADER_BUMP, 3, height, strength, spacing, normal);
    const unsigned char pixels[] = {
        0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255,
    };
    shader_image_view view = { pixels, 2, 2 };
    shader_sample_context s = sample();
    s.resolve_image = resolve_image;
    s.image_userdata = &view;
    s.part_position[0] = s.part_position[2] = .5f;
    float result[4];
    evaluate(&g, &s, result);
    REQUIRE(direction(result, -2, 1, 0));
    // One original three-axis texture plus six shifted three-axis textures.
    REQUIRE(shader_detail_texture_samples(&g) >= 21);
    REQUIRE(emitted_source(&g, "triplanar_height"));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(triplanar_axes)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    int sharp = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .01f, 0, 0);
    int texture = operation(&g, SHADER_TRIPLANAR_TEXTURE, 3, p, sharp, spacing, -1);
    g.nodes[texture].image = image_source(&g, SHADER_IMAGE_NEAREST);
    shader_sample_context s = sample();
    s.resolve_image = resolve_image;
    memcpy(s.part_position, (float[]){ .25f, -.25f, .75f }, sizeof s.part_position);
    float result[4];
    const float normals[][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    const float colors[][3] = { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } };
    for (int i = 0; i < 3; i++) {
        memcpy(s.normal, normals[i], sizeof s.normal);
        evaluate(&g, &s, result);
        for (int c = 0; c < 3; c++) {
            REQUIRE(near(result[c], colors[i][c]));
        }
    }
    memcpy(s.normal, (float[]){ 1, 1, 1 }, sizeof s.normal);
    evaluate(&g, &s, result);
    for (int i = 0; i < 3; i++) {
        REQUIRE(near(result[i], 1.0f / 3));
    }
    memcpy(s.normal, (float[]){ 1, 1, 0 }, sizeof s.normal);
    s.part_from_world[0][0] = -2;
    s.part_from_world[2][2] = .5f;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 0));
    REQUIRE(near(result[1], 1.0f / 3));
    REQUIRE(near(result[2], 2.0f / 3));
    memset(s.part_from_world[2], 0, sizeof s.part_from_world[2]);
    float outputs[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    REQUIRE(!shader_graph_eval_outputs(&g, &s, NULL, outputs, &error));
    REQUIRE(error.node == g.nodes[texture].source_id);
    REQUIRE(strstr(error.message, "nonsingular"));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(decal_volume)
{
    shader_graph g;
    shader_graph_init(&g);
    int p = operation(&g, SHADER_PART_POSITION, 3, -1, -1, -1, -1);
    int half = constant(&g, 3, 1, 2, .1f);
    int feather = constant(&g, 1, .1f, 0, 0);
    int facing = constant(&g, 1, -1, 0, 0);
    int decal = operation(&g, SHADER_DECAL_MASK, 1, p, half, feather, facing);
    shader_sample_context s = sample();
    float result[4];
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 1));
    s.part_position[2] = .05f;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], .5f));
    s.part_position[2] = .1f;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 0));
    s.part_position[2] = .2f;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 0));
    s.part_position[2] = 0;
    g.nodes[facing].value[0] = .5f;
    memcpy(s.normal, (float[]){ 0, 0, 1 }, sizeof s.normal);
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 1));
    s.normal[2] = -1;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 0));
    s.part_from_world[2][2] = -1;
    evaluate(&g, &s, result);
    REQUIRE(near(result[0], 1));
    REQUIRE(emitted_source(&g, "detail_decal_test"));
    g.nodes[half].value[2] = 0;
    shader_error error = { 0 };
    float outputs[SHADER_OUTPUT_MAX][4];
    REQUIRE(!shader_graph_eval_outputs(&g, &s, NULL, outputs, &error));
    REQUIRE(error.node == g.nodes[decal].source_id);
    shader_graph_free(&g);
    return true;
}
TEST_CASE(nested_derivative_rejection)
{
    shader_graph g;
    shader_graph_init(&g);
    int height = constant(&g, 1, 0, 0, 0);
    int strength = constant(&g, 1, 1, 0, 0);
    int spacing = constant(&g, 1, .01f, 0, 0);
    int normal = operation(&g, SHADER_NORMAL, 3, -1, -1, -1, -1);
    int first = operation(&g, SHADER_BUMP, 3, height, strength, spacing, normal);
    int component = operation(&g, SHADER_X, 1, first, -1, -1, -1);
    int second = operation(&g, SHADER_BUMP, 3, component, strength, spacing, normal);
    shader_error error = { 0 };
    REQUIRE(!shader_graph_validate(&g, &error));
    REQUIRE(error.node == g.nodes[second].source_id);
    REQUIRE(strstr(error.message, "nested derivative"));
    shader_graph_free(&g);
    return true;
}
TEST_CASE(authored_roundtrip)
{
    behavior_asset *asset = behavior_asset_new("Analytical detail");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *output = shader_graph_add_output(g, false);
    node *position = graph_add_node(g, registry_node_by_name("Shader PartPosition"));
    node *component = graph_add_node(g, registry_node_by_name("Shader X"));
    node *bump = graph_add_node(g, registry_node_by_name("Shader Bump"));
    REQUIRE(position && component && bump);
    REQUIRE(graph_connect(g, position, 0, component, 0) == 0);
    REQUIRE(graph_connect(g, component, 0, bump, 0) == 0);
    REQUIRE(graph_connect(g, bump, 0, output, 4) == 0);
    char *text = behavior_asset_save(asset);
    REQUIRE(text);
    behavior_asset *loaded = behavior_asset_load(text);
    free(text);
    REQUIRE(loaded && loaded->domain == BEHAVIOR_DOMAIN_MATERIAL);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    REQUIRE(shader_graph_from_graph(loaded->graph, &ir, &error));
    shader_sample_context s = sample();
    float outputs[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&ir, &s, NULL, outputs, &error));
    REQUIRE(direction(outputs[4], -1, 1, 0));
    char *glsl = shader_graph_glsl_named(&ir, "detail_roundtrip", &error);
    REQUIRE(glsl && strstr(glsl, "sgd_"));
    free(glsl);
    REQUIRE(emitted_source(&ir, "detail_bump_test"));
    shader_graph_free(&ir);
    behavior_asset_free(loaded);
    behavior_asset_free(asset);
    return true;
}
void test_shader_detail()
{
    test_graph_packs_install();
    RUN_TEST_CASE(mapping_landmarks);
    RUN_TEST_CASE(bump_planes);
    RUN_TEST_CASE(normal_frames);
    RUN_TEST_CASE(scaled_cylindrical_seam);
    RUN_TEST_CASE(sampled_height_and_linear_data);
    RUN_TEST_CASE(triplanar_height);
    RUN_TEST_CASE(triplanar_axes);
    RUN_TEST_CASE(decal_volume);
    RUN_TEST_CASE(nested_derivative_rejection);
    RUN_TEST_CASE(authored_roundtrip);
    test_graph_packs_restore();
}
