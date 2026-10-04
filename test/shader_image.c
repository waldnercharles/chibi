#include "graph/shader/shader_image.h"

#include "graph/document/serialize.h"
#include "platform/cute.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char pixels[] = { 128, 0,   0, 64, 255, 255, 255, 255,
                                        0,   255, 0, 0,  0,   0,   255, 128 };
static bool resolve(void *userdata, const shader_image_source *source, shader_image_view *image)
{
    if (strcmp(source->key, "content/textures/check.png")) {
        return false;
    }
    *image = *(const shader_image_view *)userdata;
    return true;
}
static void program(
    shader_graph *g,
    shader_image_color_space color_space,
    shader_image_filter filter,
    shader_image_wrap wrap
)
{
    shader_graph_init(g);
    int uv = shader_graph_add(g, SHADER_UV, 2);
    shader_image_source source = { .key = "content/textures/check.png",
                                   .color_space = color_space,
                                   .filter = filter,
                                   .wrap = wrap };
    int image = shader_graph_add_image(g, &source);
    g->output_count = 2;
    for (int p = 0; p < 2; p++) {
        int n = shader_graph_add(g, p ? SHADER_IMAGE_TEXTURE_ALPHA : SHADER_IMAGE_TEXTURE, p ? 1 : 3);
        g->nodes[n].input[0] = uv;
        g->nodes[n].image = image;
        g->nodes[n].source_id = 91;
        g->output[p] = n;
    }
}
static void near(float got, float want)
{
    assert(isfinite(got) && fabsf(got - want) < .00001f);
}
static void sampling()
{
    shader_image_view image = { .pixels = pixels, .width = 2, .height = 2 };
    shader_sample_context sample = { .uv = { .25f, .25f },
                                     .resolve_image = resolve,
                                     .image_userdata = &image };
    shader_graph g;
    program(&g, SHADER_IMAGE_SRGB, SHADER_IMAGE_NEAREST, SHADER_IMAGE_REPEAT);
    shader_error error = { 0 };
    float result[8][4];
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[0][0], .2158605f);
    near(result[1][0], 64.0f / 255);
    g.images[0].filter = SHADER_IMAGE_BILINEAR;
    sample.uv[0] = sample.uv[1] = .5f;
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[0][0], (1 + .2158605f) / 4);
    near(result[0][1], .5f);
    near(result[0][2], .5f);
    near(result[1][0], 447.0f / 1020);
    // sRGB is decoded before interpolation, while alpha is never gamma corrected.
    g.images[0].color_space = SHADER_IMAGE_LINEAR;
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[0][0], (1 + 128.0f / 255) / 4);
    near(result[1][0], 447.0f / 1020);
    sample.uv[0] = sample.uv[1] = 0;
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[0][1], .5f); // Repeat blends across both seams at the origin.
    g.images[0].wrap = SHADER_IMAGE_CLAMP;
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[0][0], 128.0f / 255);
    near(result[0][1], 0);
    sample.uv[0] = INFINITY;
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[0][0], 0);
    near(result[1][0], 0);
    sample.uv[0] = NAN;
    assert(shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    near(result[1][0], 0);
    sample.uv[0] = 1;
    sample.resolve_image = NULL;
    assert(!shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    assert(error.node == 91 && strstr(error.message, "check.png"));
    sample.resolve_image = resolve;
    g.images[0].key[0] = 'X';
    assert(!shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    assert(error.node == 91 && strstr(error.message, "Xontent/textures/check.png"));
    g.images[0].key[0] = 'c';
    image.width = 0;
    assert(!shader_graph_eval_outputs(&g, &sample, NULL, result, &error));
    assert(error.node == 91);
    shader_graph_free(&g);
}
static void authoring()
{
    behavior_asset *asset = behavior_asset_new("Image Texture authoring");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *image = graph_add_node(g, registry_node_by_name("Shader Image Texture"));
    node *out = shader_graph_add_output(g, false);
    assert(image && shader_graph_init_node(g, image));
    assert(graph_connect_data(g, image, 0, out, 0) == 0);
    assert(graph_connect_data(g, image, 1, out, 5) == 0);
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    assert(!shader_graph_from_graph(g, &ir, &error));
    assert(error.node == image->id && strstr(error.message, "Choose an image"));
    char key[] = "content/textures/check.png";
    pin_value value = { .type = PIN_STR, .str = key };
    assert(graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_IMAGE, &value) == 0);
    key[0] = 'x';
    assert(shader_graph_from_graph(g, &ir, &error));
    assert(acount(ir.images) == 1 && !strcmp(ir.images[0].key, "content/textures/check.png"));
    assert(
        ir.images[0].color_space == SHADER_IMAGE_SRGB &&
        ir.images[0].filter == SHADER_IMAGE_BILINEAR && ir.images[0].wrap == SHADER_IMAGE_REPEAT
    );
    for (int p = SHADER_IMAGE_INPUT_COLOR_SPACE; p < SHADER_IMAGE_INPUT_COUNT; p++) {
        value = (pin_value){ .type = PIN_I32,
                             .i = p == SHADER_IMAGE_INPUT_FILTER ? 0 : 1 };
        assert(graph_set_pin_default(g, image, 0, p, &value) == 0);
    }
    char *json = behavior_asset_save(asset);
    behavior_asset *loaded = behavior_asset_load(json);
    assert(loaded);
    free(json);
    node *copy = graph_node_by_id(loaded->graph, image->id);
    assert(!strcmp(
        copy->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str,
        ir.images[0].key
    ));
    char *clipboard = graph_save_nodes(g, &image, 1);
    graph pasted;
    graph_init(&pasted);
    assert(graph_append(&pasted, clipboard, 0, 0) == 0);
    free(clipboard);
    behavior_asset_free(asset);
    assert(!strcmp(
        pasted.nodes[0]->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str,
        ir.images[0].key
    ));
    graph_free(&pasted);
    assert(shader_graph_from_graph(loaded->graph, &ir, &error));
    assert(
        ir.images[0].color_space == SHADER_IMAGE_LINEAR &&
        ir.images[0].filter == SHADER_IMAGE_NEAREST && ir.images[0].wrap == SHADER_IMAGE_CLAMP
    );
    shader_image_view view = { .pixels = pixels, .width = 2, .height = 2 };
    shader_sample_context sample = { .uv = { .75f, .25f },
                                     .resolve_image = resolve,
                                     .image_userdata = &view };
    float result[8][4];
    assert(shader_graph_eval_outputs(&ir, &sample, NULL, result, &error));
    near(result[0][0], 1);
    near(result[0][1], 1);
    near(result[5][0], 1);
    int count = acount(ir.nodes);
    value = (pin_value){ .type = PIN_I32, .i = 99 };
    assert(graph_set_pin_default(loaded->graph, copy, 0, SHADER_IMAGE_INPUT_FILTER, &value) == 0);
    assert(!shader_graph_from_graph(loaded->graph, &ir, &error));
    assert(error.node == copy->id && strstr(error.message, "filter") && acount(ir.nodes) == count);
    assert(acount(ir.images) == 1 && !strcmp(ir.images[0].key, "content/textures/check.png"));
    behavior_asset_free(loaded);
    shader_graph_free(&ir);
}
void test_shader_image_cpu()
{
    sampling();
    authoring();
    puts("shader image CPU PASS: color space, straight alpha, pixel centers, wrap/filter edges, missing image diagnostics and owned KDL/clipboard keys");
}

static void gpu_variant(shader_image_color_space color_space, shader_image_filter filter, shader_image_wrap wrap)
{
    shader_graph ir;
    program(&ir, color_space, filter, wrap);
    shader_error error = { 0 };
    char *function = shader_graph_glsl_named(&ir, "image_test", &error);
    assert(function);
    const char
        *wrapper = "layout(set=0,binding=0) uniform sampler2D image_pixels;\n"
                   "vec4 image_test_image_0(vec2 uv){"
                   "if(any(isnan(uv))||any(isinf(uv)))return vec4(0.0);"
                   "return textureLod(image_pixels,%s,0.0);}\n";
    const char *body =
        "layout(local_size_x=2,local_size_y=1,local_size_z=1) in;\n"
        "layout(set=1,binding=0,rgba32f) uniform image2D result_image;\n"
        "layout(set=2,binding=0) uniform Inputs {vec2 sample_uv;};\n"
        "void main(){vec4 values[8],p[32];for(int i=0;i<32;i++)p[i]=vec4(0);"
        "SGSample "
        "sg_test;sg_test.position=vec2(0);sg_test.uv=sample_uv;"
        "sg_test.object_position=vec3(0);sg_test.part_position=vec3(0);"
        "sg_test.world_position=vec3(0);sg_test.normal=vec3(0);"
        "sg_test.view_direction=vec3(0);sg_test.time=0.0;"
        "for(int i=0;i<3;i++){sg_test.object_from_world[i]=vec3(0);"
        "sg_test.part_from_world[i]=vec3(0);}"
        "for(int i=0;i<2;i++)sg_test.uv_from_world[i]=vec3(0);"
        "image_test(sg_test,p,values);int x=int(gl_GlobalInvocationID.x);"
        "imageStore(result_image,ivec2(x,0),values[x]);}\n";
    size_t size = strlen(wrapper) + strlen(function) + strlen(body) + 64;
    char *source = xmalloc(size);
    snprintf(source, size, wrapper, wrap == SHADER_IMAGE_REPEAT ? "fract(uv)" : "clamp(uv,vec2(0),vec2(1))");
    strcat(source, function);
    strcat(source, body);
    CF_ComputeShader shader = cf_make_compute_shader_from_source(source);
    assert(shader.id);
    CF_TextureParams tp = cf_texture_defaults(2, 2);
    tp.pixel_format = color_space == SHADER_IMAGE_SRGB
                        ? CF_PIXEL_FORMAT_R8G8B8A8_UNORM_SRGB
                        : CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    tp.filter = filter == SHADER_IMAGE_NEAREST ? CF_FILTER_NEAREST : CF_FILTER_LINEAR;
    tp.mip_filter = filter == SHADER_IMAGE_NEAREST ? CF_MIP_FILTER_NEAREST
                                                   : CF_MIP_FILTER_LINEAR;
    tp.wrap_u = tp.wrap_v = wrap == SHADER_IMAGE_REPEAT ? CF_WRAP_MODE_REPEAT
                                                        : CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(tp);
    assert(texture.id);
    cf_texture_update(texture, (void *)pixels, sizeof pixels);
    CF_Material material = cf_make_material();
    cf_material_set_texture_cs(material, "image_pixels", texture);
    CF_CanvasParams cp = cf_canvas_defaults(2, 1);
    cp.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    cp.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(cp);
    assert(canvas.id);
    CF_Texture target = cf_canvas_get_target(canvas);
    CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
    dispatch.rw_textures = &target;
    dispatch.rw_texture_count = 1;
    const float coordinates[][2] = {
        { 0, 0 },          { .25f, .25f }, { .75f, .75f },
        { .5f, .5f },      { 1, 1 },       { -.125f, .875f },
        { 1.25f, -.75f },  { NAN, .5f },   { INFINITY, -INFINITY },
        { 1e20f, -1e20f }, { .5f, .25f },  { .173f, .891f }
    };
    shader_image_view image = { .pixels = pixels, .width = 2, .height = 2 };
    for (int i = 0; i < (int)(sizeof coordinates / sizeof *coordinates); i++) {
        shader_sample_context sample = { .uv = { coordinates[i][0], coordinates[i][1] },
                                         .resolve_image = resolve,
                                         .image_userdata = &image };
        float expected[8][4], actual[2][4];
        assert(shader_graph_eval_outputs(&ir, &sample, NULL, expected, &error));
        cf_app_update(NULL);
        cf_material_set_uniform_cs(material, "sample_uv", sample.uv, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        CF_Readback readback = cf_canvas_readback(canvas);
        assert(readback.id);
        uint64_t start = cf_get_ticks();
        while (!cf_readback_ready(readback)) {
            assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
            cf_sleep(1);
        }
        assert(cf_readback_data(readback, actual, sizeof actual) == sizeof actual);
        cf_destroy_readback(readback);
        for (int p = 0; p < 2; p++) {
            for (int c = 0; c < (p ? 1 : 3); c++) {
                // Hardware texture interpolation and sRGB tables have finite precision.
                if (!isfinite(actual[p][c]) ||
                    fabsf(actual[p][c] - expected[p][c]) > .003f) {
                    fprintf(
                        stderr,
                        "image GPU color%d filter%d wrap%d uv%d output%d "
                        "channel%d: %.9g != %.9g\n",
                        color_space,
                        filter,
                        wrap,
                        i,
                        p,
                        c,
                        actual[p][c],
                        expected[p][c]
                    );
                    assert(false);
                }
            }
        }
    }
    cf_destroy_canvas(canvas);
    cf_destroy_material(material);
    cf_destroy_texture(texture);
    cf_destroy_compute_shader(shader);
    shader_graph_free(&ir);
    free(function);
    free(source);
}
int test_shader_image_gpu()
{
    for (int color_space = 0; color_space < 2; color_space++) {
        for (int filter = 0; filter < 2; filter++) {
            for (int wrap = 0; wrap < 2; wrap++) {
                gpu_variant(color_space, filter, wrap);
            }
        }
    }
    puts("shader image GPU PASS: RGB/alpha parity across sRGB/linear, nearest/bilinear, repeat/clamp and finite/nonfinite UV edges");
    return 0;
}
