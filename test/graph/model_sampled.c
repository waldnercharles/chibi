#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "runtime/models/model_bake.h"
#include "runtime/shaders/volume.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool resolve(void *data, const shader_image_source *, shader_image_view *view)
{
    *view = *(shader_image_view *)data;
    return true;
}
static shader_node fixture(shader_image_view *image)
{
    int size = 9, side = 27;
    unsigned char *pixels = xcalloc(side * side, 4);
    for (int i = 0; i < side * side; i++) {
        pixels[i * 4 + 3] = 255;
    }
    shader_node n = { .op = SHADER_SAMPLED_SDF,
                      .width = 1,
                      .parameter = size,
                      .value = { 1, 1.3f, .8f },
                      .volume_range = 2,
                      .volume_lipschitz = 2 };
    for (int z = 0; z < size; z++) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float p[] = { (x / 4.f - 1) * n.value[0],
                              (y / 4.f - 1) * n.value[1],
                              (z / 4.f - 1) * n.value[2] };
                float d = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - .55f;
                unsigned value = (unsigned)lroundf((d / n.volume_range + 1) * .5f * 65535);
                unsigned char *at = pixels + 4 * (((z / 3) * size + y) * side +
                                                  (z % 3) * size + x);
                at[0] = value >> 8;
                at[1] = value & 255;
            }
        }
    }
    *image = (shader_image_view){ pixels, side, side };
    return n;
}
static behavior_asset *asset(const shader_node *spec)
{
    behavior_asset *a = behavior_asset_new("Sampled test");
    a->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = a->graph;
    node *volume = graph_add_node(g, &NODE_ModelSampledSdf),
         *part = graph_add_node(g, &NODE_ModelPart),
         *out = model_graph_add_output(g);
    pin_value values[] = {
        { .type = PIN_STR, .str = "content/sdf/0000000000000000.sdf.png" },
        { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = (void *)spec->value },
        { .type = PIN_I32, .i = spec->parameter },
        { .type = PIN_F32, .f = spec->volume_range },
        { .type = PIN_F32, .f = spec->volume_lipschitz }
    };
    for (int i = 0; i < 5; i++) {
        assert(!graph_set_pin_default(g, volume, 0, i, &values[i]));
    }
    assert(!graph_set_node_reference(g, part, 0, "Sampled part"));
    assert(!graph_connect_data(g, volume, 0, part, 0));
    assert(!graph_connect_data(g, part, 0, out, 0));
    return a;
}
static float eval(const shader_graph *g, shader_image_view *image, const float p[3])
{
    shader_sample_context context = { .resolve_image = resolve, .image_userdata = image };
    memcpy(context.object_position, p, 3 * sizeof(float));
    float values[SHADER_OUTPUT_MAX][4];
    shader_error error = { 0 };
    assert(shader_graph_eval_outputs(g, &context, NULL, values, &error));
    return values[0][0];
}
/* A sampled SDF validates its atlas, interpolates between samples and extends
 * past its border, reloads, owns its hits, keeps the last good compilation and
 * bakes a mesh. */
TEST_CASE(sampled_sdf_models)
{
    shader_image_view image;
    shader_node spec = fixture(&image);
    shader_error error = { 0 };
    REQUIRE(shader_volume_validate(&spec, &image, &error));
    shader_image_view bad = image;
    bad.width--;
    REQUIRE(!shader_volume_validate(&spec, &bad, &error));
    shader_node invalid = spec;
    invalid.volume_lipschitz = 1;
    REQUIRE(!shader_volume_validate(&invalid, &image, &error));
    unsigned char *pixels = (unsigned char *)image.pixels;
    unsigned char saved[4];
    memcpy(saved, pixels, 4);
    pixels[0] = pixels[1] = 0;
    REQUIRE(!shader_volume_validate(&spec, &image, &error));
    memcpy(pixels, saved, 4);
    behavior_asset *a = asset(&spec);
    model_program p;
    model_program_init(&p);
    REQUIRE(model_compile(a, &p, &error));
    p.resolve_image = resolve;
    p.image_userdata = &image;
    REQUIRE(acount(p.shader.images) == 1 && acount(p.shader.nodes) < 10);
    REQUIRE(fabsf(eval(&p.shader, &image, (float[]){ 0, 0, 0 }) + .55f) < .00004f);
    float lo = eval(&p.shader, &image, (float[]){ 0, .325f, 0 }),
          hi = eval(&p.shader, &image, (float[]){ .25f, .325f, 0 });
    REQUIRE(
        fabsf(eval(&p.shader, &image, (float[]){ .125f, .325f, 0 }) - (lo + hi) * .5f) < .00001f
    );
    float edge = eval(&p.shader, &image, (float[]){ 1, 0, 0 }),
          outside = eval(&p.shader, &image, (float[]){ 2, 0, 0 });
    REQUIRE(fabsf(outside - edge - 1) < .00001f);
    model_hit hit;
    REQUIRE(model_program_hit(&p, NULL, (float[]){ .5f, 0, 0 }, &hit, &error));
    REQUIRE(hit.part == 0);
    char *source = behavior_asset_save(a);
    behavior_asset *copy = behavior_asset_load(source);
    REQUIRE(copy);
    model_program q;
    model_program_init(&q);
    REQUIRE(model_compile(copy, &q, &error));
    REQUIRE(
        eval(&q.shader, &image, (float[]){ .125f, .325f, 0 }) ==
        eval(&p.shader, &image, (float[]){ .125f, .325f, 0 })
    );
    node *volume = a->graph->nodes[0];
    pin_value wrong = { .type = PIN_I32, .i = 1 };
    REQUIRE(!graph_set_pin_default(a->graph, volume, 0, 2, &wrong));
    REQUIRE(!model_compile(a, &p, &error));
    REQUIRE(fabsf(eval(&p.shader, &image, (float[]){ 0, 0, 0 }) + .55f) < .00004f);
    wrong.i = 9;
    REQUIRE(!graph_set_pin_default(a->graph, volume, 0, 2, &wrong));
    model_bake_options options = model_bake_defaults();
    options.cell_size = .2f;
    model_bake_result baked = { 0 };
    REQUIRE(model_bake(&p, NULL, &options, &baked, &error));
    REQUIRE(acount(baked.mesh.vertices) > 0);
    model_bake_result_free(&baked);
    node *scale = graph_add_node(a->graph, &NODE_ModelScale),
         *sphere = graph_add_node(a->graph, &NODE_ModelSphere),
         *join = graph_add_node(a->graph, &NODE_ModelUnion);
    float factors[] = { 2, 1, .5f };
    pin_value factors_value = { .type = PIN_STRUCT,
                                .type_name = "ShaderVec3",
                                .ptr = factors };
    REQUIRE(!graph_set_pin_default(a->graph, scale, 0, 1, &factors_value));
    pin_value radius = { .type = PIN_F32, .f = .1f };
    REQUIRE(!graph_set_pin_default(a->graph, sphere, 0, 0, &radius));
    REQUIRE(!graph_connect_data(a->graph, a->graph->nodes[1], 0, scale, 0));
    REQUIRE(!graph_connect_data(a->graph, scale, 0, join, 0));
    REQUIRE(!graph_connect_data(a->graph, sphere, 0, join, 1));
    REQUIRE(!graph_connect_data(a->graph, join, 0, a->graph->nodes[2], 0));
    REQUIRE(model_compile(a, &p, &error));
    float point[] = { .7f, .1f, .05f }, local[] = { .35f, .1f, .1f };
    float expected = fminf(
        eval(&q.shader, &image, local) * .5f,
        sqrtf(.7f * .7f + .1f * .1f + .05f * .05f) - .1f
    );
    REQUIRE(fabsf(eval(&p.shader, &image, point) - expected) < .00001f);
    REQUIRE(p.bounds.max[0] >= 2 && p.bounds.max[1] >= 1.3f && p.bounds.max[2] >= .4f);
    invalid = spec;
    invalid.parameter = -20;
    REQUIRE(!shader_volume_validate(&invalid, &image, &error));
    float rejected;
    REQUIRE(!shader_volume_eval(&invalid, &image, point, &rejected));
    shader_graph part_distance, part_hit;
    shader_graph_init(&part_distance);
    shader_graph_init(&part_hit);
    REQUIRE(model_program_part_shaders(&q, 0, &part_distance, &part_hit, &error));
    REQUIRE(acount(part_distance.images) == 1 && acount(part_hit.images) == 1);
    REQUIRE(part_distance.images[0].key != q.shader.images[0].key);
    model_program_free(&q);
    REQUIRE(fabsf(eval(&part_distance, &image, (float[]){ 0, 0, 0 }) + .55f) < .00004f);
    shader_graph_free(&part_distance);
    shader_graph_free(&part_hit);
    model_program_free(&p);
    behavior_asset_free(copy);
    behavior_asset_free(a);
    free(source);
    free(pixels);
    return true;
}

void test_model_sampled()
{
    test_graph_packs_install();
    RUN_TEST_CASE(sampled_sdf_models);
    test_graph_packs_restore();
}
