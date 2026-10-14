#include "render/shader_program.h"

#include "authoring/render_preview.h"
#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "render/material_raster.h"
#include "render/scene3d.h"
#include "render/scene3d_post.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

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
    assert(test_read_texture(render_material_raster_textures(raster).scene, pixels, PIXELS));
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
    assert(test_read_canvas(canvas, pixels, PIXELS * (int)sizeof *pixels));
}

TEST_CASE(fragment_ramps_recompile_only_for_structure)
{
    static const float initial[][4] = { { 1, 0, 0, 1 }, { 0, 0, 1, 1 } };
    static const float numeric[][4] = { { 0, 1, 0, 1 }, { 1, 1, 0, 1 } };
    static const float structural[][4] = { { 0, 1, 0, 1 },
                                           { 1, 0, 1, 1 },
                                           { 1, 1, 0, 1 } };
    behavior_asset *asset = make_ramp("Shader program transport", 2, initial, 0);
    int material = publish(asset);
    render_programs *programs = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    REQUIRE(programs && canvas.id);
    CF_Pixel before[PIXELS], after[PIXELS];
    unsigned compile_before = render_shader_compile_count();
    draw_fragment(programs, material, canvas, before);
    unsigned compile_warm = render_shader_compile_count();
    REQUIRE(compile_warm > compile_before);

    edit_ramp(asset, 2, numeric, 0);
    REQUIRE(publish(asset) == material);
    draw_fragment(programs, material, canvas, after);
    REQUIRE(render_shader_compile_count() == compile_warm);
    int changed = 0;
    for (int i = 0; i < PIXELS; i++) {
        changed |= memcmp(&before[i], &after[i], sizeof before[i]) != 0;
    }
    REQUIRE(changed);

    edit_ramp(asset, 2, numeric, 1);
    REQUIRE(publish(asset) == material);
    draw_fragment(programs, material, canvas, after);
    REQUIRE(render_shader_compile_count() == compile_warm + 1);

    edit_ramp(asset, 3, structural, 1);
    REQUIRE(publish(asset) == material);
    draw_fragment(programs, material, canvas, after);
    REQUIRE(render_shader_compile_count() == compile_warm + 2);
    cf_destroy_canvas(canvas);
    render_programs_free(programs);
    behavior_asset_free(asset);
    return true;
}

static void scene_frame(
    render_scene3d *scene,
    render_scene3d_post *post,
    const behavior_asset *asset,
    const render_scene3d_camera *camera,
    CF_Pixel pixels[PIXELS]
)
{
    cf_app_update(NULL);
    assert(
        authoring_render_scene3d_preview(scene, asset, camera, NULL, 0, &(render_scene3d_activity){ 0 })
    );
    cf_render_to(render_scene3d_post_target(post), true);
    assert(render_scene3d_post_process(post, 0));
    cf_app_draw_onto_screen(false);
    assert(
        test_read_canvas(render_scene3d_post_output(post), pixels, PIXELS * (int)sizeof *pixels)
    );
}

TEST_CASE(scene3d_previews_update_ramps_without_recompiling)
{
    static const float initial[][4] = { { 0, 1, 1, 1 }, { 0, .1f, 1, 1 }, { 0, 1, 0, 1 } };
    static const float numeric[][4] = { { 1, .1f, 0, 1 }, { 1, 0, 1, 1 }, { 1, 1, 0, 1 } };
    behavior_asset *asset = make_ramp("Shader program transport", 3, initial, 1);
    int material = publish(asset);
    render_scene3d *scene = render_scene3d_new();
    render_scene3d_post *post = render_scene3d_post_new();
    Environment environment = environment_defaults();
    environment.bloom_strength = 0;
    REQUIRE(scene && post && render_scene3d_post_prepare(post, SIDE, SIDE, &environment));
    render_scene3d_camera camera = render_scene3d_camera_make(0, 0, 140, 140);
    CF_Pixel before[PIXELS], after[PIXELS];
    unsigned compile_before = render_shader_compile_count();
    scene_frame(scene, post, asset, &camera, before);
    unsigned compile_warm = render_shader_compile_count();
    REQUIRE(compile_warm > compile_before);

    edit_ramp(asset, 3, numeric, 1);
    REQUIRE(publish(asset) == material);
    scene_frame(scene, post, asset, &camera, after);
    REQUIRE(render_shader_compile_count() == compile_warm);
    int center = SIDE * (SIDE / 2) + SIDE / 2;
    REQUIRE(memcmp(&before[center], &after[center], sizeof before[center]) != 0);
    render_scene3d_free(scene);
    render_scene3d_post_free(post);
    behavior_asset_free(asset);
    return true;
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

TEST_CASE(raster_ramps_keep_the_last_good_frame)
{
    static const float initial[][4] = { { .1f, .2f, 1, 1 },
                                        { .8f, .1f, .4f, 1 },
                                        { .1f, 1, .2f, 1 } };
    static const float numeric[][4] = { { 1, .2f, .1f, 1 },
                                        { 1, .8f, .1f, 1 },
                                        { .1f, .1f, 1, 1 } };
    static const float invalid[][4] = { { 0, 1, 0, 1 }, { 1, 0, 0, 1 }, { 0, 0, 1, 1 } };
    behavior_asset *asset = make_ramp("Shader program transport", 3, initial, 1);
    int material = publish(asset);
    render_material_raster *raster = render_material_raster_new();
    REQUIRE(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_entity entity = raster_entity(material, 0);
    uint16_t before[PIXELS * 4], after[PIXELS * 4];
    unsigned compile_before = render_shader_compile_count();
    compute_frame(raster, &entity, before);
    unsigned compile_warm = render_shader_compile_count();
    REQUIRE(compile_warm > compile_before);

    edit_ramp(asset, 3, numeric, 1);
    REQUIRE(publish(asset) == material);
    compute_frame(raster, &entity, after);
    REQUIRE(render_shader_compile_count() == compile_warm);
    int center = ((SIDE / 2) * SIDE + SIDE / 2) * 4;
    REQUIRE(
        fabsf(test_half(before[center]) - test_half(after[center])) > .05f ||
        fabsf(test_half(before[center + 1]) - test_half(after[center + 1])) > .05f ||
        fabsf(test_half(before[center + 2]) - test_half(after[center + 2])) > .05f
    );
    memcpy(before, after, sizeof before);

    edit_ramp(asset, 3, invalid, 2);
    REQUIRE(publish(asset) == material);
    char mount[] = "/tmp/chibi-shader-program-XXXXXX";
    REQUIRE(mkdtemp(mount));
    char noise[sizeof mount + 16];
    snprintf(noise, sizeof noise, "%s/noise.shd", mount);
    FILE *file = fopen(noise, "wb");
    REQUIRE(file);
    REQUIRE(fputs("this is invalid shader source", file) >= 0);
    REQUIRE(!fclose(file));
    REQUIRE(!cf_is_error(cf_fs_mount(mount, "/chibi-render", false)));
    unsigned compile_before_invalid = render_shader_compile_count();
    compute_frame(raster, &entity, after);
    REQUIRE(render_shader_compile_count() == compile_before_invalid + 1);
    REQUIRE(render_material_raster_error(raster)[0]);
    REQUIRE(!memcmp(before, after, sizeof before));
    REQUIRE(!cf_is_error(cf_fs_dismount(mount)));
    REQUIRE(!unlink(noise));
    REQUIRE(!rmdir(mount));
    render_material_raster_free(raster);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(programs_read_each_material_at_its_offset)
{
    static const float red[][4] = { { 1, 0, 0, 1 }, { 1, 0, 0, 1 } };
    static const float green[][4] = { { 0, 1, 0, 1 }, { 0, 1, 0, 1 } };
    behavior_asset *left = make_ramp("Shader program offset left", 2, red, 0);
    behavior_asset *right = make_ramp("Shader program offset right", 2, green, 0);
    int left_id = publish(left), right_id = publish(right);
    render_material_raster *raster = render_material_raster_new();
    REQUIRE(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_entity entities[] = { raster_entity(left_id, -16),
                                          raster_entity(right_id, 16) };
    uint16_t pixels[PIXELS * 4];
    cf_app_update(NULL);
    REQUIRE(
        render_material_raster_draw(raster, entities, 2, (render_material_view){ -32, 32, 64, 64 })
    );
    cf_app_draw_onto_screen(false);
    read_scene(raster, pixels);
    int left_pixel = ((SIDE / 2) * SIDE + SIDE / 4) * 4;
    int right_pixel = ((SIDE / 2) * SIDE + SIDE * 3 / 4) * 4;
    REQUIRE(test_half(pixels[left_pixel]) > .8f && test_half(pixels[left_pixel + 1]) < .1f);
    REQUIRE(test_half(pixels[right_pixel]) < .1f && test_half(pixels[right_pixel + 1]) > .8f);
    render_material_raster_free(raster);
    behavior_asset_free(left);
    behavior_asset_free(right);
    return true;
}

TEST_CASE(ramp_capacity_keeps_the_last_good_frame)
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
    behavior_asset *valid = make_ramp("Shader program capacity valid", 2, valid_colors, 0);
    int valid_id = publish(valid);
    render_material_raster *raster = render_material_raster_new();
    REQUIRE(raster && render_material_raster_resize(raster, SIDE, SIDE));
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
    REQUIRE(rejected && render_material_raster_resize(rejected, SIDE, SIDE));
    REQUIRE(
        !render_material_raster_draw(rejected, &entity, 1, (render_material_view){ -32, 32, 64, 64 })
    );
    REQUIRE(render_shader_compile_count() == compile_before_reject);
    REQUIRE(strstr(render_material_raster_error(rejected), "limit"));
    render_material_raster_free(rejected);
    compute_frame(raster, &entity, after);
    REQUIRE(render_shader_compile_count() == compile_warm);
    REQUIRE(strstr(render_material_raster_error(raster), "limit"));
    REQUIRE(!memcmp(before, after, sizeof before));
    render_material_raster_free(raster);
    behavior_asset_free(valid);
    for (int i = 0; i < 17; i++) {
        behavior_asset_free(large[i]);
    }
    return true;
}

TEST_CASE(program_sets_keep_their_ids_as_tables_grow)
{
    enum
    {
        COUNT = 20
    };
    behavior_asset *shapes[COUNT];
    render_shader_set sets[COUNT];
    int ids[COUNT];
    for (int i = 0; i < COUNT; i++) {
        char name[64];
        snprintf(name, sizeof name, "Indexed program sphere %d", i);
        shapes[i] = behavior_asset_new(name);
        shapes[i]->domain = BEHAVIOR_DOMAIN_SHAPE;
        shapes[i]->bounds[0] = shapes[i]->bounds[1] = (float)i + 2;
        graph *g = shapes[i]->graph;
        node *position = shader_test_node(g, "Shader ObjectPosition");
        node *length = shader_test_node(g, "Shader Length");
        node *distance = shader_test_node(g, "Shader Subtract");
        REQUIRE(!graph_connect_data(g, position, 0, length, 0));
        REQUIRE(!graph_connect_data(g, length, 0, distance, 0));
        REQUIRE(!graph_connect_data(g, distance, 0, shader_graph_add_output(g, true), 0));
        pin_value radius = { .type = PIN_F32, .f = (float)i + 1 };
        REQUIRE(!graph_set_pin_default(g, distance, 0, 1, &radius));
        sets[i] = (render_shader_set){ .shape = publish(shapes[i]) };
    }
    render_programs *programs = render_programs_new(RENDER_PROGRAM_SDF_MATERIAL);
    for (int i = 0; i < COUNT; i++) {
        ids[i] = render_programs_prepare(programs, &sets[i]);
        REQUIRE(ids[i]);
        for (int j = 0; j < i; j++) {
            REQUIRE(ids[i] != ids[j]);
        }
    }
    unsigned warm = render_shader_compile_count();
    // Reverse/repeated lookup must survive table growth without recompiling.
    for (int pass = 0; pass < 3; pass++) {
        for (int i = COUNT - 1; i >= 0; i--) {
            REQUIRE(render_programs_prepare(programs, &sets[i]) == ids[i]);
        }
    }
    REQUIRE(render_shader_compile_count() == warm);
    graph *g = shapes[0]->graph;
    node *distance = g->nodes[2];
    pin_value radius = { .type = PIN_F32, .f = 2.5f };
    REQUIRE(!graph_set_pin_default(g, distance, 0, 1, &radius));
    REQUIRE(publish(shapes[0]) == sets[0].shape);
    REQUIRE(render_programs_prepare(programs, &sets[0]) == ids[0]);
    REQUIRE(render_shader_compile_count() == warm + 1);
    render_programs_free(programs);
    for (int i = 0; i < COUNT; i++) {
        behavior_asset_free(shapes[i]);
    }
    return true;
}

static void teardown()
{
    authoring_shader_catalog_clear();
}

void test_shader_image_routes();

void test_shader_programs()
{
    test_open_hidden_app("shader-programs", SIDE, SIDE);
    test_graph_packs_install();
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(fragment_ramps_recompile_only_for_structure);
    RUN_TEST_CASE(scene3d_previews_update_ramps_without_recompiling);
    RUN_TEST_CASE(raster_ramps_keep_the_last_good_frame);
    RUN_TEST_CASE(programs_read_each_material_at_its_offset);
    RUN_TEST_CASE(ramp_capacity_keeps_the_last_good_frame);
    RUN_TEST_CASE(program_sets_keep_their_ids_as_tables_grow);
    test_shader_image_routes();
    pu_clear_setup();
    test_graph_packs_restore();
    cf_destroy_app();
}
