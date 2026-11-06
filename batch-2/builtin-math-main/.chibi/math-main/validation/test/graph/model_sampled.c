#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "runtime/models/model_bake.h"
#include "runtime/shaders/volume.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

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
        { .type = PIN_STR,
          .text = &(
              const pin_string
          ){ .bytes = "content/sdf/0000000000000000.sdf.png",
             .length = sizeof("content/sdf/0000000000000000.sdf.png") - 1 } },
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
    error = (shader_error){ 0 };
    REQUIRE(!shader_volume_eval(&invalid, &image, point, &rejected, &error));
    REQUIRE(error.node == invalid.source_id && strstr(error.message, "metadata"));
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

#ifdef CHIBI_TEST_RENDER_RUNTIME
#include "authoring/image_prepare.h"
#include "authoring/shader_prepare.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "runtime/images/catalog.h"
#include "test/render/hidden_app.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* GLSL wrappers size their output arrays by the C contract. */
#define QUOTE(x) #x
#define QUOTED(x) QUOTE(x)
#define OUTPUT_ROWS QUOTED(SHADER_OUTPUT_MAX)

/* Whether a compute shader of g's GLSL computes the distances and normals the
 * CPU evaluator does at points across and past `half` extents. */
static bool gpu_matches(shader_graph *g, shader_image_view *image, const float half[3])
{
    enum
    {
        COUNT = 256
    };
    shader_error error = { 0 };
    char *function = shader_graph_glsl_named(g, "volume_test", &error);
    assert(function);
    const char *wrapper = "layout(set=0,binding=0) uniform sampler2D "
                          "image_pixels;\nvec4 volume_test_image_0(vec2 "
                          "uv){return textureLod(image_pixels,uv,0.0);}\n";
    const char
        *body = "layout(local_size_x=1,local_size_y=1,local_size_z=1) "
                "in;\nlayout(set=1,binding=0,rgba32f) uniform image2D "
                "result_image;\nlayout(set=2,binding=0) uniform Inputs {vec4 "
                "points[256];};\n"
                "float sample_at(vec3 q){SGSample s;s.object_position=q;vec4 "
                "p[32],r[" OUTPUT_ROWS "];for(int "
                "k=0;k<32;k++)p[k]=vec4(0);volume_test(s,p,r);return r[0].x;}\n"
                "void main(){int i=int(gl_GlobalInvocationID.x);vec3 "
                "p=points[i].xyz;vec3 "
                "n=vec3(sample_at(p+vec3(.01,0,0))-sample_at(p-vec3(.01,0,0)),"
                "sample_at(p+vec3(0,.01,0))-sample_at(p-vec3(0,.01,0)),sample_"
                "at(p+vec3(0,0,.01))-sample_at(p-vec3(0,0,.01)));if(length(n)>"
                "1e-6)n=normalize(n);else "
                "n=vec3(0);imageStore(result_image,ivec2(i,0),vec4(sample_at(p)"
                ",n));}\n";
    char *text = xmalloc(strlen(wrapper) + strlen(function) + strlen(body) + 1);
    strcpy(text, wrapper);
    strcat(text, function);
    strcat(text, body);
    CF_ComputeShader shader = cf_make_compute_shader_from_source(text);
    assert(shader.id);
    CF_TextureParams tp = cf_texture_defaults(image->width, image->height);
    tp.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    tp.filter = CF_FILTER_NEAREST;
    tp.wrap_u = tp.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(tp);
    assert(texture.id);
    cf_texture_update(texture, (void *)image->pixels, image->width * image->height * 4);
    CF_Material material = cf_make_material();
    cf_material_set_texture_cs(material, "image_pixels", texture);
    CF_CanvasParams cp = cf_canvas_defaults(COUNT, 1);
    cp.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    cp.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(cp);
    assert(canvas.id);
    CF_Texture target = cf_canvas_get_target(canvas);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(COUNT, 1, 1);
    dispatch.rw_textures = &target;
    dispatch.rw_texture_count = 1;
    float points[COUNT][4] = { 0 }, expected[COUNT][4] = { 0 }, actual[COUNT][4];
    for (int i = 0; i < COUNT; i++) {
        for (int j = 0; j < 3; j++) {
            points[i][j] = half[j] *
                           (((i * (37 + 24 * j) + 13 * j) % 257) / 256.f * 2.6f - 1.3f);
        }
        if (i < 8) {
            for (int j = 0; j < 3; j++) {
                points[i][j] = half[j] * ((i & (1 << j)) ? 1 : -1);
            }
        }
        expected[i][0] = eval(g, image, points[i]);
        float squared = 0;
        for (int j = 0; j < 3; j++) {
            float q[3];
            memcpy(q, points[i], sizeof q);
            q[j] += .01f;
            float hi = eval(g, image, q);
            q[j] -= .02f;
            expected[i][j + 1] = hi - eval(g, image, q);
            squared += expected[i][j + 1] * expected[i][j + 1];
        }
        for (int j = 0; j < 3; j++) {
            expected[i][j + 1] = squared > 1e-12f
                                   ? expected[i][j + 1] / sqrtf(squared)
                                   : 0;
        }
    }
    cf_app_update(NULL);
    cf_material_set_uniform_cs(material, "points", points, CF_UNIFORM_TYPE_FLOAT4, COUNT);
    cf_dispatch_compute(shader, material, dispatch);
    cf_app_draw_onto_screen(false);
    bool read = test_read_canvas(canvas, actual, sizeof actual);
    float maximum = 0, normal = 0;
    for (int i = 0; read && i < COUNT; i++) {
        float delta = fabsf(actual[i][0] - expected[i][0]);
        maximum = isfinite(delta) ? fmaxf(maximum, delta) : INFINITY;
        for (int j = 1; j < 4; j++) {
            normal = fmaxf(normal, fabsf(actual[i][j] - expected[i][j]));
        }
    }
    cf_destroy_canvas(canvas);
    cf_destroy_material(material);
    cf_destroy_texture(texture);
    cf_destroy_compute_shader(shader);
    free(text);
    free(function);
    return read && maximum < .0002f && normal < .01f;
}
static void save_fixture(const char *path, const shader_image_view *view)
{
    CF_Image image = { view->width, view->height, (CF_Pixel *)view->pixels };
    void *bytes = NULL;
    int size = 0;
    assert(!cf_is_error(cf_image_save_png_to_memory(&image, &bytes, &size)));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, size, file) == (size_t)size);
    assert(!fclose(file));
    cf_free(bytes);
}
/* A sampled SDF's image keeps its pixels, texture and the last good shader
 * when a reload or an edit is rejected. */
TEST_CASE(sampled_images_keep_the_last_good_on_reload)
{
    shader_image_view image;
    shader_node spec = fixture(&image);
    char directory[] = "/tmp/chibi-sampled-XXXXXX";
    REQUIRE(mkdtemp(directory));
    char message[256], path[512], key[128];
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < image.width * image.height * 4; i++) {
        hash = (hash ^ image.pixels[i]) * UINT64_C(1099511628211);
    }
    snprintf(key, sizeof key, "content/%016llx.sdf.png", (unsigned long long)hash);
    snprintf(path, sizeof path, "%s/%016llx.sdf.png", directory, (unsigned long long)hash);
    save_fixture(path, &image);
    REQUIRE(render_image_mount_content(directory, message, sizeof message));
    shader_image_source source = { .key = key,
                                   .color_space = SHADER_IMAGE_LINEAR,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    shader_error error = { 0 };
    int variant = render_image_variant(&source, &error);
    REQUIRE(variant);
    CF_Texture texture = render_image_texture(variant, &error);
    REQUIRE(texture.id);
    shader_image_view retained;
    REQUIRE(render_image_resolve(NULL, &source, &retained));
    const unsigned char *old = retained.pixels;
    unsigned char *pixels = (unsigned char *)image.pixels;
    pixels[0] ^= 1;
    save_fixture(path, &image);
    REQUIRE(!render_image_reload(key, message, sizeof message));
    REQUIRE(strstr(message, "digest"));
    REQUIRE(render_image_resolve(NULL, &source, &retained));
    REQUIRE(retained.pixels == old);
    REQUIRE(render_image_texture(variant, &error).id == texture.id);
    pixels[0] ^= 1;
    save_fixture(path, &image);
    REQUIRE(render_image_reload(key, message, sizeof message));
    behavior_asset *model = asset(&spec);
    pin_value image_key = {
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = key, .length = (uint32_t)strlen(key) }
    };
    REQUIRE(!graph_set_pin_default(model->graph, model->graph->nodes[0], 0, 0, &image_key));
    REQUIRE(render_shader_upsert(model, &error));
    int id = render_shader_find(model->name);
    unsigned revision = render_shader_revision();
    shader_sample_context context = { 0 };
    float before[SHADER_OUTPUT_MAX][4], after[SHADER_OUTPUT_MAX][4];
    REQUIRE(render_shader_sample(id, &context, NULL, before, &error));
    pin_value invalid_range = { .type = PIN_F32, .f = 200 };
    REQUIRE(!graph_set_pin_default(model->graph, model->graph->nodes[0], 0, 3, &invalid_range));
    REQUIRE(!render_shader_upsert(model, &error));
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(render_shader_sample(id, &context, NULL, after, &error));
    REQUIRE(before[0][0] == after[0][0]);
    behavior_asset_free(model);
    authoring_shader_catalog_clear();
    render_images_clear();
    REQUIRE(!unlink(path));
    REQUIRE(!rmdir(directory));
    free(pixels);
    return true;
}
TEST_CASE(sampled_sdfs_match_cpu_on_gpu)
{
    shader_image_view image;
    shader_node spec = fixture(&image);
    behavior_asset *a = asset(&spec);
    model_program p;
    model_program_init(&p);
    shader_error error = { 0 };
    REQUIRE(model_compile(a, &p, &error));
    REQUIRE(gpu_matches(&p.shader, &image, spec.value));
    model_program_free(&p);
    behavior_asset_free(a);
    free((void *)image.pixels);
    return true;
}

/* Runs in an open hidden app. */
void test_model_sampled_gpu()
{
    test_graph_packs_install();
    RUN_TEST_CASE(sampled_sdfs_match_cpu_on_gpu);
    RUN_TEST_CASE(sampled_images_keep_the_last_good_on_reload);
    render_images_clear();
    test_graph_packs_restore();
}
#endif
