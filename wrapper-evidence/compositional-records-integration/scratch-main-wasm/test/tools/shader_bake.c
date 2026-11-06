#include "runtime/shaders/bake.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static shader_graph graph;
static shader_bake_job *job, *other;
static shader_bake_result result, sliced;
static shader_error error;
static void setup()
{
    shader_graph_init(&graph);
    graph.output_count = SHADER_OUTPUT_MAX;
    for (int i = 0; i < SHADER_BAKE_CHANNEL_COUNT; i++) {
        int n = shader_graph_add(&graph, SHADER_CONSTANT, shader_bake_channel_width(i));
        graph.nodes[n].source_id = (uint64_t)i + 1;
        for (int j = 0; j < graph.nodes[n].width; j++) {
            graph.nodes[n].value[j] = (float)(i + j) + .25f;
        }
        graph.output[i] = n;
    }
    error = (shader_error){ 0 };
}
static void teardown()
{
    shader_bake_free(job);
    shader_bake_free(other);
    job = other = NULL;
    shader_bake_result_free(&result);
    shader_bake_result_free(&sliced);
    shader_graph_free(&graph);
}
static shader_bake_desc description()
{
    return (shader_bake_desc){ .width = 2,
                               .height = 3,
                               .channels = SHADER_BAKE_CHANNELS_ALL,
                               .axis_u = { 4, 0, 0 },
                               .axis_v = { 0, 6, 0 },
                               .uv_extent = { 1, 1 } };
}
TEST_CASE(constant_channels_preserve_linear_hdr_and_height)
{
    shader_bake_desc d = description();
    job = shader_bake_new(&graph, &d, &error);
    REQUIRE(job);
    REQUIRE(!shader_bake_complete(job));
    REQUIRE(shader_bake_step(job, 3, &error));
    REQUIRE(shader_bake_take(job, &result, &error));
    REQUIRE(result.stride == 15 && result.width == 2 && result.height == 3);
    for (int pixel = 0; pixel < 6; pixel++) {
        int offset = 0;
        for (int channel = 0; channel < SHADER_BAKE_CHANNEL_COUNT; channel++) {
            REQUIRE(shader_bake_channel_name(channel)[0]);
            for (int component = 0; component < shader_bake_channel_width(channel);
                 component++) {
                REQUIRE(
                    result.pixels[pixel * result.stride + offset++] ==
                    (float)(channel + component) + .25f
                );
            }
        }
    }
    REQUIRE(!shader_bake_take(job, &result, &error));
    return true;
}
TEST_CASE(pixel_centers_orientation_and_slices_match)
{
    graph.nodes[graph.output[SHADER_BAKE_EMISSION]].op = SHADER_WORLD_POSITION;
    int uv = shader_graph_add(&graph, SHADER_UV, 2);
    int x = shader_graph_add(&graph, SHADER_X, 1);
    graph.nodes[x].input[0] = uv;
    int y = shader_graph_add(&graph, SHADER_Y, 1);
    graph.nodes[y].input[0] = uv;
    graph.output[SHADER_BAKE_ROUGHNESS] = x;
    graph.output[SHADER_BAKE_HEIGHT] = y;
    shader_bake_desc d = description();
    d.origin[0] = 10;
    d.origin[1] = 20;
    d.channels = (1u << SHADER_BAKE_EMISSION) | (1u << SHADER_BAKE_ROUGHNESS) |
                 (1u << SHADER_BAKE_HEIGHT);
    job = shader_bake_new(&graph, &d, &error);
    other = shader_bake_new(&graph, &d, &error);
    REQUIRE(job && other);
    REQUIRE(shader_bake_step(job, INT_MAX, &error));
    REQUIRE(shader_bake_take(job, &result, &error));
    REQUIRE(shader_bake_step(other, 1, &error));
    REQUIRE(!shader_bake_take(other, &sliced, &error));
    REQUIRE(shader_bake_step(other, 1, &error));
    REQUIRE(shader_bake_step(other, 1, &error));
    REQUIRE(shader_bake_take(other, &sliced, &error));
    REQUIRE(!memcmp(result.pixels, sliced.pixels, 30 * sizeof(float)));
    REQUIRE(result.stride == 5);
    REQUIRE(result.pixels[0] == 11 && result.pixels[1] == 21 && result.pixels[2] == 0);
    REQUIRE(result.pixels[3] == .25f);
    REQUIRE(fabsf(result.pixels[4] - 1.f / 6) < .000001f);
    REQUIRE(result.pixels[25] == 13 && result.pixels[26] == 25);
    REQUIRE(result.pixels[28] == .75f);
    REQUIRE(fabsf(result.pixels[29] - 5.f / 6) < .000001f);
    return true;
}
TEST_CASE(parameters_and_graph_are_owned)
{
    shader_node *n = &graph.nodes[graph.output[SHADER_BAKE_EMISSION]];
    n->op = SHADER_PARAMETER;
    n->parameter = 7;
    shader_bake_parameter p = { .slot = 7, .width = 3, .value = { 2, 4, 8 } };
    shader_bake_desc d = description();
    d.channels = 1u << SHADER_BAKE_EMISSION;
    d.parameters = &p;
    d.parameter_count = 1;
    p.width = 1;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    p.width = 3;
    p.value[0] = NAN;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    p.value[0] = 2;
    job = shader_bake_new(&graph, &d, &error);
    REQUIRE(job);
    p.value[0] = 99;
    shader_graph_free(&graph);
    REQUIRE(shader_bake_step(job, 3, &error));
    REQUIRE(shader_bake_take(job, &result, &error));
    REQUIRE(
        result.stride == 3 && result.pixels[0] == 2 && result.pixels[1] == 4 &&
        result.pixels[2] == 8
    );
    return true;
}
TEST_CASE(invalid_grid_and_budget_are_rejected)
{
    shader_bake_desc d = description();
    d.width = INT_MAX;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.width = 0;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.width = 2;
    d.height = -1;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.height = 3;
    d.max_bytes = 1;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.max_bytes = 0;
    d.channels = 1u << SHADER_BAKE_CHANNEL_COUNT;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.channels = SHADER_BAKE_CHANNELS_ALL;
    d.axis_v[1] = 0;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.axis_v[1] = 6;
    d.context.time = INFINITY;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    d.context.time = 0;
    graph.output_count = 1;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    graph.output_count = SHADER_OUTPUT_MAX;
    graph.nodes[graph.output[SHADER_BAKE_HEIGHT]].width = 2;
    REQUIRE(!shader_bake_new(&graph, &d, &error));
    return true;
}
TEST_CASE(failed_job_preserves_previous_result)
{
    shader_bake_desc d = description();
    d.channels = 1u << SHADER_BAKE_HEIGHT;
    job = shader_bake_new(&graph, &d, &error);
    REQUIRE(job);
    REQUIRE(shader_bake_step(job, 3, &error));
    REQUIRE(shader_bake_take(job, &result, &error));
    float *previous = result.pixels;
    int huge = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[huge].value[0] = 3.e38f;
    int multiply = shader_graph_add(&graph, SHADER_MULTIPLY, 1);
    graph.nodes[multiply].input[0] = huge;
    graph.nodes[multiply].input[1] = huge;
    graph.nodes[multiply].source_id = 901;
    graph.output[SHADER_BAKE_HEIGHT] = multiply;
    other = shader_bake_new(&graph, &d, &error);
    REQUIRE(other);
    REQUIRE(!shader_bake_step(other, 1, &error));
    REQUIRE(error.node == 901 && strstr(error.message, "nonfinite"));
    REQUIRE(!shader_bake_complete(other));
    REQUIRE(!shader_bake_take(other, &result, &error));
    REQUIRE(result.pixels == previous && result.pixels[0] == 7.25f);
    REQUIRE(!shader_bake_step(other, 1, &error));
    return true;
}
static const unsigned char image_pixels[] = { 255, 128, 0, 255 };
static bool resolve(void *userdata, const shader_image_source *source, shader_image_view *view)
{
    int *calls = userdata;
    (*calls)++;
    if (strcmp(source->key, "content/textures/bake.png")) {
        return false;
    }
    *view = (shader_image_view){ .pixels = image_pixels, .width = 1, .height = 1 };
    return true;
}
TEST_CASE(images_resolve_owned_keys_and_failure_is_private)
{
    int uv = shader_graph_add(&graph, SHADER_UV, 2);
    shader_image_source source = { .key = "content/textures/bake.png",
                                   .color_space = SHADER_IMAGE_LINEAR,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    int image = shader_graph_add_image(&graph, &source);
    int texture = shader_graph_add(&graph, SHADER_IMAGE_TEXTURE, 3);
    graph.nodes[texture].input[0] = uv;
    graph.nodes[texture].image = image;
    graph.nodes[texture].source_id = 77;
    graph.output[SHADER_BAKE_BASE_COLOR] = texture;
    shader_bake_desc d = description();
    d.channels = 1u << SHADER_BAKE_BASE_COLOR;
    job = shader_bake_new(&graph, &d, &error);
    REQUIRE(job);
    REQUIRE(!shader_bake_step(job, 1, &error));
    REQUIRE(error.node == 77);
    REQUIRE(!shader_bake_take(job, &result, &error) && !result.pixels);
    int calls = 0;
    d.context.resolve_image = resolve;
    d.context.image_userdata = &calls;
    other = shader_bake_new(&graph, &d, &error);
    REQUIRE(other);
    graph.images[0].key[0] = 'X';
    shader_graph_free(&graph);
    REQUIRE(shader_bake_step(other, 3, &error));
    REQUIRE(shader_bake_take(other, &result, &error));
    REQUIRE(calls == 6 && result.pixels[0] == 1 && result.pixels[2] == 0);
    return true;
}
TEST_CASE(bump_samples_reuse_derivative_workspace)
{
    int position = shader_graph_add(&graph, SHADER_PART_POSITION, 3);
    int height = shader_graph_add(&graph, SHADER_X, 1);
    graph.nodes[height].input[0] = position;
    int strength = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[strength].value[0] = 1;
    int spacing = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[spacing].value[0] = .01f;
    int normal = shader_graph_add(&graph, SHADER_NORMAL, 3);
    int bump = shader_graph_add(&graph, SHADER_BUMP, 3);
    graph.nodes[bump].input[0] = height;
    graph.nodes[bump].input[1] = strength;
    graph.nodes[bump].input[2] = spacing;
    graph.nodes[bump].input[3] = normal;
    graph.output[SHADER_BAKE_NORMAL] = bump;
    shader_bake_desc d = description();
    d.channels = 1u << SHADER_BAKE_NORMAL;
    job = shader_bake_new(&graph, &d, &error);
    REQUIRE(job);
    REQUIRE(shader_bake_step(job, 3, &error));
    REQUIRE(shader_bake_take(job, &result, &error));
    for (int pixel = 0; pixel < 6; pixel++) {
        REQUIRE(fabsf(result.pixels[pixel * 3] + sqrtf(.5f)) < .0001f);
        REQUIRE(fabsf(result.pixels[pixel * 3 + 1]) < .0001f);
        REQUIRE(fabsf(result.pixels[pixel * 3 + 2] - sqrtf(.5f)) < .0001f);
    }
    // Position uses XZ, including finite-difference samples on a non-XY plane.
    graph.nodes[position].op = SHADER_POSITION;
    graph.nodes[position].width = 2;
    graph.nodes[height].op = SHADER_Y;
    d.axis_v[1] = 0;
    d.axis_v[2] = 6;
    d.context.normal[1] = 1;
    other = shader_bake_new(&graph, &d, &error);
    REQUIRE(other);
    REQUIRE(shader_bake_step(other, 3, &error));
    REQUIRE(shader_bake_take(other, &sliced, &error));
    for (int pixel = 0; pixel < 6; pixel++) {
        REQUIRE(fabsf(sliced.pixels[pixel * 3]) < .0001f);
        REQUIRE(fabsf(sliced.pixels[pixel * 3 + 1] - sqrtf(.5f)) < .0001f);
        REQUIRE(fabsf(sliced.pixels[pixel * 3 + 2] + sqrtf(.5f)) < .0001f);
    }
    shader_bake_free(other);
    other = NULL;
    graph.nodes[position].op = SHADER_UV;
    other = shader_bake_new(&graph, &d, &error);
    REQUIRE(other);
    REQUIRE(shader_bake_step(other, 3, &error));
    REQUIRE(shader_bake_take(other, &sliced, &error));
    for (int pixel = 0; pixel < 6; pixel++) {
        REQUIRE(fabsf(sliced.pixels[pixel * 3]) < .0001f);
        REQUIRE(fabsf(sliced.pixels[pixel * 3 + 1] - 6 / sqrtf(37)) < .0001f);
        REQUIRE(fabsf(sliced.pixels[pixel * 3 + 2] + 1 / sqrtf(37)) < .0001f);
    }
    return true;
}
static bool resolve_pattern(void *, const shader_image_source *, shader_image_view *view)
{
    static const unsigned char pixels[] = { 0,   16,  32,  32,  255, 128,
                                            64,  128, 64,  192, 255, 192,
                                            192, 64,  128, 255 };
    *view = (shader_image_view){ .pixels = pixels, .width = 2, .height = 2 };
    return true;
}
TEST_CASE(nested_triplanar_bump_matches_allocating_evaluator)
{
    int position = shader_graph_add(&graph, SHADER_PART_POSITION, 3);
    int sharp = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[sharp].value[0] = 1;
    int spacing = shader_graph_add(&graph, SHADER_CONSTANT, 1);
    graph.nodes[spacing].value[0] = .01f;
    shader_image_source source = { .key = "content/textures/pattern.png",
                                   .color_space = SHADER_IMAGE_LINEAR,
                                   .filter = SHADER_IMAGE_BILINEAR,
                                   .wrap = SHADER_IMAGE_REPEAT };
    int image = shader_graph_add_image(&graph, &source);
    int texture = shader_graph_add(&graph, SHADER_TRIPLANAR_TEXTURE, 3);
    graph.nodes[texture].input[0] = position;
    graph.nodes[texture].input[1] = sharp;
    graph.nodes[texture].input[2] = spacing;
    graph.nodes[texture].image = image;
    int height = shader_graph_add(&graph, SHADER_X, 1);
    graph.nodes[height].input[0] = texture;
    int normal = shader_graph_add(&graph, SHADER_NORMAL, 3);
    int bump = shader_graph_add(&graph, SHADER_BUMP, 3);
    graph.nodes[bump].input[0] = height;
    graph.nodes[bump].input[1] = sharp;
    graph.nodes[bump].input[2] = spacing;
    graph.nodes[bump].input[3] = normal;
    graph.output[SHADER_BAKE_NORMAL] = bump;
    shader_bake_desc d = description();
    d.channels = 1u << SHADER_BAKE_NORMAL;
    d.origin[0] = .1f;
    d.origin[1] = .2f;
    d.origin[2] = .15f;
    d.axis_u[0] = .5f;
    d.axis_v[1] = 0;
    d.axis_v[2] = .5f;
    d.context.normal[1] = 1;
    d.context.resolve_image = resolve_pattern;
    for (int variant = 0; variant < 2; variant++) {
        if (variant) {
            graph.nodes[texture].op = SHADER_TRIPLANAR_TEXTURE_ALPHA;
            graph.nodes[texture].width = 1;
            graph.nodes[bump].input[0] = texture;
            // The unused extractor remains valid with a scalar source.
        }
        job = shader_bake_new(&graph, &d, &error);
        REQUIRE(job);
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        bool stepped = shader_bake_step(job, 3, &error);
        xalloc_stats_get(&after);
        REQUIRE(stepped);
        REQUIRE(after.calls == before.calls);
        REQUIRE(after.requested_bytes == before.requested_bytes);
        REQUIRE(shader_bake_take(job, &result, &error));
        for (int y = 0; y < d.height; y++) {
            for (int x = 0; x < d.width; x++) {
                shader_sample_context context = d.context;
                for (int i = 0; i < 3; i++) {
                    context.world_position[i] = context.object_position[i] =
                        context.part_position[i] = (float)(d.origin[i] +
                                                           (x + .5) / d.width *
                                                               d.axis_u[i] +
                                                           (y + .5) / d.height *
                                                               d.axis_v[i]);
                    context.part_from_world[i][i] = context.object_from_world[i][i] = 1;
                }
                float expected[SHADER_OUTPUT_MAX][4];
                REQUIRE(shader_graph_eval_outputs(&graph, &context, NULL, expected, &error));
                for (int i = 0; i < 3; i++) {
                    REQUIRE(
                        fabsf(
                            result.pixels[(y * d.width + x) * 3 + i] -
                            expected[SHADER_BAKE_NORMAL][i]
                        ) < .00001f
                    );
                }
            }
        }
        shader_bake_free(job);
        job = NULL;
    }
    return true;
}
void test_tool_shader_bake()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(constant_channels_preserve_linear_hdr_and_height);
    RUN_TEST_CASE(pixel_centers_orientation_and_slices_match);
    RUN_TEST_CASE(parameters_and_graph_are_owned);
    RUN_TEST_CASE(invalid_grid_and_budget_are_rejected);
    RUN_TEST_CASE(failed_job_preserves_previous_result);
    RUN_TEST_CASE(images_resolve_owned_keys_and_failure_is_private);
    RUN_TEST_CASE(bump_samples_reuse_derivative_workspace);
    RUN_TEST_CASE(nested_triplanar_bump_matches_allocating_evaluator);
    pu_setup(NULL, NULL);
}
