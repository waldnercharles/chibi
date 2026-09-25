#include "render/shader_program.h"

#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/arena3d.h"
#include "render/arena_post.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static node *shader_test_node(graph *g, const char *name)
{
    node *result = graph_add_node(g, registry_node_by_name(name));
    assert(result && shader_graph_init_node(g, result));
    return result;
}

static node *ramp_node(behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (!strcmp(n->desc->name, "Shader Color Ramp")) {
            return n;
        }
    }
    assert(false);
    return NULL;
}

static void edit_ramp(behavior_asset *asset, int count, const float colors[][4], int interpolation)
{
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    for (int i = 0; i < count; i++) {
        stops[i].position = (float)i / (count - 1);
        memcpy(stops[i].color, colors[i], sizeof stops[i].color);
    }
    graph *g = asset->graph;
    node *ramp = ramp_node(asset);
    assert(shader_node_ramp_set(g, ramp, stops, count));
    pin_value value = { .type = PIN_I32, .i = interpolation };
    assert(graph_set_pin_default(g, ramp, 0, 2, &value) == 0);
}

static int publish(behavior_asset *asset)
{
    shader_error error = { 0 };
    if (!render_shader_upsert(asset, &error)) {
        fprintf(stderr, "shader program asset %s: %s\n", asset->name, error.message);
        assert(false);
    }
    int id = render_shader_find(asset->name);
    assert(id);
    return id;
}

static behavior_asset *make_ramp(const char *name, int count, const float colors[][4], int interpolation)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, false);
    node *ramp = shader_test_node(g, "Shader Color Ramp");
    assert(graph_connect_data(g, ramp, 0, out, 0) == 0);
    assert(graph_connect_data(g, ramp, 1, out, 5) == 0);
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    assert(graph_set_pin_default(g, out, 0, 6, &unlit) == 0);
    edit_ramp(asset, count, colors, interpolation);
    return asset;
}

static void readback(CF_Canvas canvas, void *data, int bytes)
{
    CF_Readback read = cf_canvas_readback(canvas);
    assert(read.id);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(read)) {
        assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
        cf_sleep(1);
    }
    assert(cf_readback_data(read, data, bytes) == bytes);
    cf_destroy_readback(read);
}

static void read_canvas_pixels(CF_Canvas canvas, CF_Pixel pixels[PIXELS])
{
    readback(canvas, pixels, PIXELS * (int)sizeof *pixels);
}

static render_material_entity raster_entity(int material, float x)
{
    return (render_material_entity){ .inv_rot = { 1, 0, 0, 1 },
                                     .pos = { x, 0 },
                                     .extent = { 12, 12 },
                                     .shape_type = RENDER_SHAPE_CIRCLE,
                                     .radius = 10,
                                     .aa = .5f,
                                     .reserved = { (float)material } };
}

static void read_scene(render_material_raster *raster, uint16_t pixels[PIXELS * 4])
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = render_material_raster_textures(raster).scene;
    CF_Canvas canvas = cf_make_canvas(params);
    assert(canvas.id);
    readback(canvas, pixels, PIXELS * 4 * (int)sizeof *pixels);
    cf_destroy_canvas(canvas);
}

static float half(uint16_t value)
{
    int exponent = (value >> 10) & 31, mantissa = value & 1023;
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

static void draw_fragment(render_programs *programs, int material, CF_Canvas canvas, CF_Pixel pixels[PIXELS])
{
    render_material_entity entity = raster_entity(material, 0);
    entity.pos[0] = entity.pos[1] = SIDE * .5f;
    entity.extent[0] = entity.extent[1] = SIDE * .5f;
    entity.shape_type = RENDER_SHAPE_BOX;
    entity.points[0] = entity.points[1] = SIDE * .5f;
    entity.aa = .5f;
    shader_error error = { 0 };
    int program = render_shader_fragment_prepare(programs, &entity, &error);
    if (!program) {
        fprintf(stderr, "shader program fragment: %s\n", error.message);
    }
    assert(program);
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    render_shader_fragment_draw(programs, program, &entity, (CF_V2){ 1, 1 });
    cf_render_to(canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    read_canvas_pixels(canvas, pixels);
}

static void fragment_checks(behavior_asset *asset)
{
    static const float initial[][4] = { { 1, 0, 0, 1 }, { 0, 0, 1, 1 } };
    static const float numeric[][4] = { { 0, 1, 0, 1 }, { 1, 1, 0, 1 } };
    static const float structural[][4] = { { 0, 1, 0, 1 },
                                           { 1, 0, 1, 1 },
                                           { 1, 1, 0, 1 } };
    edit_ramp(asset, 2, initial, 0);
    int material = publish(asset);
    render_programs *programs = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    assert(programs && canvas.id);
    CF_Pixel before[PIXELS], after[PIXELS];
    unsigned compile_before = render_shader_compile_count();
    draw_fragment(programs, material, canvas, before);
    unsigned compile_warm = render_shader_compile_count();
    assert(compile_warm > compile_before);

    edit_ramp(asset, 2, numeric, 0);
    assert(publish(asset) == material);
    draw_fragment(programs, material, canvas, after);
    assert(render_shader_compile_count() == compile_warm);
    int changed = 0;
    for (int i = 0; i < PIXELS; i++) {
        changed |= memcmp(&before[i], &after[i], sizeof before[i]) != 0;
    }
    assert(changed);

    edit_ramp(asset, 2, numeric, 1);
    assert(publish(asset) == material);
    draw_fragment(programs, material, canvas, after);
    assert(render_shader_compile_count() == compile_warm + 1);

    edit_ramp(asset, 3, structural, 1);
    assert(publish(asset) == material);
    draw_fragment(programs, material, canvas, after);
    assert(render_shader_compile_count() == compile_warm + 2);
    cf_destroy_canvas(canvas);
    render_programs_free(programs);
}

static void arena_frame(
    render_arena *arena,
    render_arena_post *post,
    const behavior_asset *asset,
    const render_arena_camera *camera,
    CF_Pixel pixels[PIXELS]
)
{
    cf_app_update(NULL);
    assert(render_arena_preview(arena, asset, camera, NULL, 0, &(render_arena_activity){ 0 }));
    cf_render_to(render_arena_post_target(post), true);
    assert(render_arena_post_process(post));
    cf_app_draw_onto_screen(false);
    read_canvas_pixels(render_arena_post_output(post), pixels);
}

static void arena_checks(behavior_asset *asset)
{
    static const float initial[][4] = { { 0, 1, 1, 1 }, { 0, .1f, 1, 1 }, { 0, 1, 0, 1 } };
    static const float numeric[][4] = { { 1, .1f, 0, 1 }, { 1, 0, 1, 1 }, { 1, 1, 0, 1 } };
    edit_ramp(asset, 3, initial, 1);
    int material = publish(asset);
    (void)material;
    render_arena *arena = render_arena_new();
    render_arena_post *post = render_arena_post_new();
    GameEnvironment environment = render_arena_post_defaults();
    environment.bloom_strength = 0;
    assert(arena && post && render_arena_post_prepare(post, SIDE, SIDE, &environment));
    render_arena_camera camera = render_arena_camera_make(0, 0, 140, 140);
    CF_Pixel before[PIXELS], after[PIXELS];
    unsigned compile_before = render_shader_compile_count();
    arena_frame(arena, post, asset, &camera, before);
    unsigned compile_warm = render_shader_compile_count();
    assert(compile_warm > compile_before);

    edit_ramp(asset, 3, numeric, 1);
    assert(publish(asset) == material);
    arena_frame(arena, post, asset, &camera, after);
    assert(render_shader_compile_count() == compile_warm);
    int center = SIDE * (SIDE / 2) + SIDE / 2;
    assert(memcmp(&before[center], &after[center], sizeof before[center]) != 0);
    render_arena_free(arena);
    render_arena_post_free(post);
}

static void compute_frame(
    render_material_raster *raster,
    const render_material_entity *entity,
    uint16_t pixels[PIXELS * 4]
)
{
    cf_app_update(NULL);
    assert(
        render_material_raster_draw(raster, entity, 1, (render_material_view){ -32, 32, 64, 64 })
    );
    cf_app_draw_onto_screen(false);
    read_scene(raster, pixels);
}

static void compute_checks(behavior_asset *asset)
{
    static const float initial[][4] = { { .1f, .2f, 1, 1 },
                                        { .8f, .1f, .4f, 1 },
                                        { .1f, 1, .2f, 1 } };
    static const float numeric[][4] = { { 1, .2f, .1f, 1 },
                                        { 1, .8f, .1f, 1 },
                                        { .1f, .1f, 1, 1 } };
    static const float invalid[][4] = { { 0, 1, 0, 1 }, { 1, 0, 0, 1 }, { 0, 0, 1, 1 } };
    edit_ramp(asset, 3, initial, 1);
    int material = publish(asset);
    render_material_raster *raster = render_material_raster_new();
    assert(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_entity entity = raster_entity(material, 0);
    uint16_t before[PIXELS * 4], after[PIXELS * 4];
    unsigned compile_before = render_shader_compile_count();
    compute_frame(raster, &entity, before);
    unsigned compile_warm = render_shader_compile_count();
    assert(compile_warm > compile_before);

    edit_ramp(asset, 3, numeric, 1);
    assert(publish(asset) == material);
    compute_frame(raster, &entity, after);
    assert(render_shader_compile_count() == compile_warm);
    int center = ((SIDE / 2) * SIDE + SIDE / 2) * 4;
    assert(
        fabsf(half(before[center]) - half(after[center])) > .05f ||
        fabsf(half(before[center + 1]) - half(after[center + 1])) > .05f ||
        fabsf(half(before[center + 2]) - half(after[center + 2])) > .05f
    );
    memcpy(before, after, sizeof before);

    edit_ramp(asset, 3, invalid, 2);
    assert(publish(asset) == material);
    char mount[] = "/tmp/chibi-shader-program-XXXXXX";
    assert(mkdtemp(mount));
    char noise[sizeof mount + 16];
    snprintf(noise, sizeof noise, "%s/noise.shd", mount);
    FILE *file = fopen(noise, "wb");
    assert(file);
    assert(fputs("this is invalid shader source", file) >= 0);
    assert(!fclose(file));
    assert(!cf_is_error(cf_fs_mount(mount, "/chibi-render", false)));
    unsigned compile_before_invalid = render_shader_compile_count();
    compute_frame(raster, &entity, after);
    assert(render_shader_compile_count() == compile_before_invalid + 1);
    assert(render_material_raster_error(raster)[0]);
    assert(!memcmp(before, after, sizeof before));
    assert(!cf_is_error(cf_fs_dismount(mount)));
    assert(!unlink(noise));
    assert(!rmdir(mount));
    render_material_raster_free(raster);
}

static void offsets_checks()
{
    static const float red[][4] = { { 1, 0, 0, 1 }, { 1, 0, 0, 1 } };
    static const float green[][4] = { { 0, 1, 0, 1 }, { 0, 1, 0, 1 } };
    behavior_asset *left = make_ramp("Shader program offset left", 2, red, 0);
    behavior_asset *right = make_ramp("Shader program offset right", 2, green, 0);
    int left_id = publish(left), right_id = publish(right);
    render_material_raster *raster = render_material_raster_new();
    assert(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_entity entities[] = { raster_entity(left_id, -16),
                                          raster_entity(right_id, 16) };
    uint16_t pixels[PIXELS * 4];
    cf_app_update(NULL);
    assert(
        render_material_raster_draw(raster, entities, 2, (render_material_view){ -32, 32, 64, 64 })
    );
    cf_app_draw_onto_screen(false);
    read_scene(raster, pixels);
    int left_pixel = ((SIDE / 2) * SIDE + SIDE / 4) * 4;
    int right_pixel = ((SIDE / 2) * SIDE + SIDE * 3 / 4) * 4;
    assert(half(pixels[left_pixel]) > .8f && half(pixels[left_pixel + 1]) < .1f);
    assert(half(pixels[right_pixel]) < .1f && half(pixels[right_pixel + 1]) > .8f);
    render_material_raster_free(raster);
    behavior_asset_free(left);
    behavior_asset_free(right);
}

static void capacity_checks()
{
    static const float valid_colors[][4] = { { .8f, .2f, .1f, 1 },
                                             { .8f, .2f, .1f, 1 } };
    static const float full_colors[SHADER_RAMP_MAX][4] = {
        { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 },
        { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 },
        { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 },
        { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 },
        { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 }, { .8f, .2f, .1f, 1 },
        { .8f, .2f, .1f, 1 }
    };
    render_shader_catalog_clear();
    behavior_asset *valid = make_ramp("Shader program capacity valid", 2, valid_colors, 0);
    int valid_id = publish(valid);
    render_material_raster *raster = render_material_raster_new();
    assert(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_entity entity = raster_entity(valid_id, 0);
    uint16_t before[PIXELS * 4], after[PIXELS * 4];
    compute_frame(raster, &entity, before);
    unsigned compile_warm = render_shader_compile_count();
    behavior_asset *large[17] = { 0 };
    for (int i = 0; i < 17; i++) {
        char name[64];
        snprintf(name, sizeof name, "Shader program capacity %d", i);
        large[i] = make_ramp(name, SHADER_RAMP_MAX, full_colors, 0);
        publish(large[i]);
    }
    unsigned compile_before_reject = render_shader_compile_count();
    render_material_raster *rejected = render_material_raster_new();
    assert(rejected && render_material_raster_resize(rejected, SIDE, SIDE));
    assert(
        !render_material_raster_draw(rejected, &entity, 1, (render_material_view){ -32, 32, 64, 64 })
    );
    assert(render_shader_compile_count() == compile_before_reject);
    assert(strstr(render_material_raster_error(rejected), "limit"));
    render_material_raster_free(rejected);
    compute_frame(raster, &entity, after);
    assert(render_shader_compile_count() == compile_warm);
    assert(strstr(render_material_raster_error(raster), "limit"));
    assert(!memcmp(before, after, sizeof before));
    render_material_raster_free(raster);
    behavior_asset_free(valid);
    for (int i = 0; i < 17; i++) {
        behavior_asset_free(large[i]);
    }
}

int test_shader_program_gpu()
{
    render_shader_catalog_clear();
    static const float colors[][4] = { { 1, 0, 0, 1 }, { 0, 0, 1, 1 } };
    behavior_asset *asset = make_ramp("Shader program transport", 2, colors, 0);
    publish(asset);
    fragment_checks(asset);
    arena_checks(asset);
    compute_checks(asset);
    behavior_asset_free(asset);
    offsets_checks();
    capacity_checks();
    render_shader_catalog_clear();
    puts("shader program GPU PASS: Arena, fragment and compute ramp payloads, structural recompiles, program offsets, compile failure retention and ramp capacity");
    return 0;
}
