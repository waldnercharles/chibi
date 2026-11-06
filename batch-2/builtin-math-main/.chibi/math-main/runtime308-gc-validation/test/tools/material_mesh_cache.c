#include "../render/memory_images.h"
#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "render/material_mesh.h"
#include "runtime/component_catalog.h"
#include "runtime/images/catalog.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "test/render/hidden_app.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
    SIDE = 64
};
static ecs_world *world;
static ecs_entity entity;
static render_material_raster *raster;
static render_material_mesh_cache *cache;
static material_mask mask;
static const MaterialSurface3D options = { .enabled = true,
                                           .elevation = 8,
                                           .depth = 4,
                                           .bevel_width = 2,
                                           .relief = 1,
                                           .relief_scale = 8,
                                           .spacing = 4,
                                           .seed = 42 };
static char diagnostic[256];
static unsigned char height_pixels[8];
static const test_image height_image = { "content/test-height.png", 2, 1, height_pixels };

static void setup()
{
    world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    entity = ecs_world_create_entity(world);
    Material material = { .scene = 0x808080ffu,
                          .albedo = 0x808080ffu,
                          .absorption = 1,
                          .edge_light = 1 };
    assert(ecs_world_record_add(world, entity, COMPONENT_Material.id, &material, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    raster = render_material_raster_new();
    assert(raster && render_material_raster_resize(raster, SIDE, SIDE));
    render_material_raster_set_quality(raster, (SurfaceQuality){ .resolution = SURFACE_FULL });
    cache = render_material_mesh_cache_new();
    mask = material_mask_new(8, 8, 4, false);
}

static void teardown()
{
    render_material_mesh_cache_free(cache, raster);
    render_material_raster_free(raster);
    material_mask_free(&mask);
    ecs_world_free(world);
    authoring_shader_catalog_clear();
    render_image_catalog_clear();
    render_images_set_loader((render_image_loader){ 0 });
}

static void height_material(float offset, bool dynamic)
{
    behavior_asset *asset = behavior_asset_new("Mesh height parity");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, false);
    node *position = graph_add_node(g, registry_node_by_name(dynamic ? "Shader Time" : "Shader PartPosition"));
    if (dynamic) {
        assert(!graph_connect_data(g, position, 0, out, SHADER_SURFACE_HEIGHT));
    } else {
        node *x = graph_add_node(g, registry_node_by_name("Shader X"));
        node *gain = graph_add_node(g, registry_node_by_name("Shader Multiply"));
        node *add = graph_add_node(g, registry_node_by_name("Shader Add"));
        assert(!graph_connect_data(g, position, 0, x, 0));
        assert(!graph_connect_data(g, x, 0, gain, 0));
        pin_value scale = { .type = PIN_F32, .f = .1f };
        pin_value raised = { .type = PIN_F32, .f = offset };
        assert(!graph_set_pin_default(g, gain, 0, 1, &scale));
        assert(!graph_connect_data(g, gain, 0, add, 0));
        assert(!graph_set_pin_default(g, add, 0, 1, &raised));
        assert(!graph_connect_data(g, add, 0, out, SHADER_SURFACE_HEIGHT));
    }
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    behavior_asset_free(asset);
}

static void image_height_material(shader_image_color_space color_space, shader_image_filter filter)
{
    behavior_asset *asset = behavior_asset_new("Mesh height parity");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, false);
    node *image = graph_add_node(g, registry_node_by_name("Shader Image Texture"));
    node *red = graph_add_node(g, registry_node_by_name("Shader X"));
    node *gain = graph_add_node(g, registry_node_by_name("Shader Parameter"));
    node *multiply = graph_add_node(g, registry_node_by_name("Shader Multiply"));
    assert(!graph_set_node_reference(g, gain, 0, "Gain"));
    float uv[] = { .49f, .5f };
    pin_value coord = { .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = uv };
    pin_value key = { .type = PIN_STR,
                      .text = &(const pin_string){
                          .bytes = (char *)height_image.key,
                          .length = (uint32_t)strlen((char *)height_image.key) } };
    pin_value linear = {
        .type = PIN_I32,
        .type_name = image->input_pins[SHADER_IMAGE_INPUT_COLOR_SPACE].type_name,
        .i = color_space
    };
    pin_value interpolation = {
        .type = PIN_I32,
        .type_name = image->input_pins[SHADER_IMAGE_INPUT_FILTER].type_name,
        .i = filter
    };
    pin_value clamp = { .type = PIN_I32,
                        .type_name = image->input_pins[SHADER_IMAGE_INPUT_WRAP].type_name,
                        .i = SHADER_IMAGE_CLAMP };
    assert(!graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_UV, &coord));
    assert(!graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_IMAGE, &key));
    assert(!graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_COLOR_SPACE, &linear));
    assert(!graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_FILTER, &interpolation));
    assert(!graph_set_pin_default(g, image, 0, SHADER_IMAGE_INPUT_WRAP, &clamp));
    assert(!graph_connect_data(g, image, 0, red, 0));
    assert(!graph_connect_data(g, red, 0, multiply, 0));
    assert(!graph_connect_data(g, gain, 0, multiply, 1));
    assert(!graph_connect_data(g, multiply, 0, out, SHADER_SURFACE_HEIGHT));
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    behavior_asset_free(asset);
    /* Runtime lookup consumes prepared values, just as a loaded prototype
     * does. Graph replacement invalidates the previous prepared generation. */
    const Shader *binding = Shader_get_const(world, entity);
    if (binding) {
        assert(authoring_shader_binding_prepare(binding, &error));
    }
}

static const render_mesh_surface *surface(GlobalTransform2D placement)
{
    render_material_mesh_cache_begin(cache);
    const render_mesh_surface *result = render_material_mesh_cache_get(
        cache,
        material_mask_occupancy(&mask),
        options,
        (MaterialFrame){ 0 },
        placement,
        world,
        entity,
        diagnostic,
        sizeof diagnostic
    );
    if (!result) {
        fprintf(stderr, "material Height preparation: %s\n", diagnostic);
    }
    render_material_mesh_cache_end(cache, raster);
    return result;
}

static bool draw(const render_mesh_surface *surface)
{
    render_material_entity marker = { .shape_type = RENDER_SHAPE_MESH };
    render_mesh_material_actor actor = { .entity = entity, .surface = surface };
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(
        raster,
        &marker,
        1,
        (render_material_view){ -32, 32, SIDE, SIDE },
        world,
        &actor,
        1,
        0
    );
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "material mesh cache draw: %s\n", render_material_raster_error(raster));
    }
    return ok;
}

static bool center_blocks(bool expected)
{
    CF_Texture texture = render_material_raster_textures(raster).source;
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (readback.id && !cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 5) {
        cf_sleep(1);
    }
    uint16_t pixels[SIDE * SIDE * 4];
    bool ok = readback.id && cf_readback_ready(readback) &&
              cf_readback_size(readback) == sizeof pixels &&
              cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
    if (ok) {
        int center = (SIDE / 2 * SIDE + SIDE / 2) * 4;
        uint16_t absorption = pixels[center + 3];
        /* Air's -log(1) is negative zero. Test optical depth, not the sign
         * bit of its half-float encoding; reject nonfinite/negative depth. */
        bool finite = (absorption & 0x7c00u) != 0x7c00u;
        bool blocks = finite && !(absorption & 0x8000u) && (absorption & 0x7fffu) != 0;
        bool clear = (absorption & 0x7fffu) == 0;
        bool matches = expected ? blocks : clear;
        if (!matches) {
            fprintf(
                stderr,
                "material-mesh coverage: center source=(%04x,%04x,%04x,%04x), "
                "expected absorption %s\n",
                pixels[center],
                pixels[center + 1],
                pixels[center + 2],
                pixels[center + 3],
                expected ? "positive" : "zero"
            );
        }
        ok = matches;
    }
    if (readback.id) {
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

TEST_CASE(mask_mesh_reuses_uploads_and_retires_replaced_geometry)
{
    const render_mesh_surface *first = surface((GlobalTransform2D){ 0 });
    REQUIRE(first && acount(first->parts) == 1);
    uint64_t identity = first->parts[0].dynamic_identity;
    uint64_t revision = first->parts[0].dynamic_revision;
    REQUIRE(draw(first));
    render_mesh_material_work work = render_material_raster_mesh_work(raster);
    REQUIRE(work.mesh_uploads == 1 && work.dynamic_meshes == 1);
    REQUIRE(center_blocks(true));

    const render_mesh_surface *moved = surface((GlobalTransform2D){
        .position = { 2, 0 } });
    REQUIRE(moved == first && moved->parts[0].dynamic_revision == revision);
    REQUIRE(draw(moved));
    work = render_material_raster_mesh_work(raster);
    REQUIRE(work.mesh_uploads == 0 && work.dynamic_meshes == 1);

    REQUIRE(material_mask_carve(&mask, 0, 0, 8) > 0);
    const render_mesh_surface *cut = surface((GlobalTransform2D){ 0 });
    REQUIRE(cut == first && cut->parts[0].dynamic_identity == identity);
    REQUIRE(cut->parts[0].dynamic_revision > revision);
    REQUIRE(!material_mask_contains(&mask, .5f, -.5f));
    float picked_height;
    REQUIRE(!render_mesh_surface_pick(cut, (CF_V2){ .5f, -.5f }, &picked_height));
    REQUIRE(draw(cut));
    work = render_material_raster_mesh_work(raster);
    REQUIRE(work.mesh_uploads == 1 && work.dynamic_meshes == 1);
    REQUIRE(center_blocks(false));

    render_material_mesh_cache_begin(cache);
    render_material_mesh_cache_end(cache, raster);
    REQUIRE(render_material_raster_mesh_work(raster).dynamic_meshes == 0);
    const render_mesh_surface *rebuilt = surface((GlobalTransform2D){ 0 });
    REQUIRE(rebuilt && draw(rebuilt));
    work = render_material_raster_mesh_work(raster);
    REQUIRE(work.mesh_uploads == 1 && work.dynamic_meshes == 1);
    REQUIRE(material_mask_carve(&mask, 0, 0, 100) > 0);
    const render_mesh_surface *empty = surface((GlobalTransform2D){ 0 });
    REQUIRE(empty && acount(empty->parts) == 0);
    REQUIRE(render_material_raster_mesh_work(raster).dynamic_meshes == 0);
    render_material_mesh_cache_clear(cache, raster);
    REQUIRE(render_material_raster_mesh_work(raster).dynamic_meshes == 0);
    return true;
}

static const render_mesh_surface *height_surface(GlobalTransform2D placement)
{
    MaterialSurface3D sampled = options;
    sampled.material_height = true;
    render_material_mesh_cache_begin(cache);
    const render_mesh_surface *result = render_material_mesh_cache_get(
        cache,
        material_mask_occupancy(&mask),
        sampled,
        (MaterialFrame){ 0 },
        placement,
        world,
        entity,
        diagnostic,
        sizeof diagnostic
    );
    if (!result) {
        fprintf(stderr, "material Height preparation: %s\n", diagnostic);
    }
    render_material_mesh_cache_end(cache, raster);
    return result;
}

static float half_float(uint16_t bits)
{
    int exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    if (exponent == 31) {
        return NAN;
    }
    float value = exponent ? ldexpf(1 + mantissa / 1024.f, exponent - 15)
                           : ldexpf((float)mantissa, -24);
    return bits & 0x8000u ? -value : value;
}

static bool height_matches(float expected)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = render_material_raster_textures(raster).albedo;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (readback.id && !cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 5) {
        cf_sleep(1);
    }
    uint16_t pixels[SIDE * SIDE * 4];
    bool ok = readback.id && cf_readback_ready(readback) &&
              cf_readback_size(readback) == sizeof pixels &&
              cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
    if (ok) {
        float actual = half_float(pixels[(SIDE / 2 * SIDE + SIDE / 2) * 4 + 3]);
        ok = isfinite(actual) && fabsf(actual - expected) < .02f;
        if (!ok) {
            fprintf(stderr, "material Height raster parity: actual=%g geometry=%g\n", actual, expected);
        }
    }
    if (readback.id) {
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

TEST_CASE(surface_height_displaces_geometry_once_and_rejects_dynamic_roots)
{
    height_material(1, false);
    Shader binding = { .material = "Mesh height parity" };
    REQUIRE(ecs_world_record_add(world, entity, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    const render_mesh_surface *surface = height_surface((GlobalTransform2D){ 0 });
    REQUIRE(surface && surface->parts[0].height_consumed);
    float height;
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - 9.05f) < .001f);
    REQUIRE(draw(surface) && height_matches(height));
    uint64_t revision = surface->parts[0].dynamic_revision;
    REQUIRE(height_surface((GlobalTransform2D){ .position = { 2, 0 } }) == surface);
    REQUIRE(surface->parts[0].dynamic_revision == revision);

    height_material(2, false);
    REQUIRE(height_surface((GlobalTransform2D){ 0 }) == surface);
    REQUIRE(surface->parts[0].dynamic_revision > revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - 10.05f) < .001f);
    REQUIRE(draw(surface) && height_matches(height));
    revision = surface->parts[0].dynamic_revision;
    height_material(0, true);
    REQUIRE(!height_surface((GlobalTransform2D){ 0 }));
    REQUIRE(strstr(diagnostic, "Time"));
    REQUIRE(surface->parts[0].dynamic_revision == revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - 10.05f) < .001f);
    return true;
}

static bool set_gain(const char *parameters)
{
    Shader *binding = Shader_get(world, entity);
    Shader proposed = *binding;
    proposed.parameters = parameters;
    shader_error error = { 0 };
    if (!authoring_shader_binding_prepare(&proposed, &error)) {
        fprintf(stderr, "Height fixture parameter preparation: %s\n", error.message);
        return false;
    }
    binding->parameters = parameters;
    return ecs_component_modified(world, entity, COMPONENT_Shader.id) == ECS_OK;
}

TEST_CASE(material_height_cache_tracks_parameters_image_reload_and_sampling)
{
    for (int i = 0; i < 8; i++) {
        height_pixels[i] = i < 4 ? 64 : 128;
    }
    height_pixels[3] = height_pixels[7] = 255;
    test_mount_images(&height_image, 1);
    image_height_material(SHADER_IMAGE_LINEAR, SHADER_IMAGE_NEAREST);
    Shader binding = { .material = "Mesh height parity", .parameters = "Gain 1.0\n" };
    shader_error prepared = { 0 };
    REQUIRE(authoring_shader_binding_prepare(&binding, &prepared));
    REQUIRE(ecs_world_record_add(world, entity, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    const render_mesh_surface *surface = height_surface((GlobalTransform2D){ 0 });
    REQUIRE(surface);
    float height;
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - (8 + 64 / 255.f)) < .001f);
    REQUIRE(draw(surface) && height_matches(height));
    uint64_t revision = surface->parts[0].dynamic_revision;
    REQUIRE(height_surface((GlobalTransform2D){ 0 }) == surface);
    REQUIRE(surface->parts[0].dynamic_revision == revision);
    REQUIRE(draw(surface));
    REQUIRE(render_material_raster_mesh_work(raster).mesh_uploads == 0);

    REQUIRE(set_gain("Gain 2.0\n"));
    REQUIRE(height_surface((GlobalTransform2D){ 0 }) == surface);
    REQUIRE(surface->parts[0].dynamic_revision > revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - (8 + 128 / 255.f)) < .001f);
    REQUIRE(draw(surface) && height_matches(height));
    revision = surface->parts[0].dynamic_revision;

    height_pixels[0] = height_pixels[1] = height_pixels[2] = 192;
    REQUIRE(render_image_reload(height_image.key, diagnostic, sizeof diagnostic));
    REQUIRE(height_surface((GlobalTransform2D){ 0 }) == surface);
    REQUIRE(surface->parts[0].dynamic_revision > revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - (8 + 384 / 255.f)) < .001f);
    REQUIRE(draw(surface) && height_matches(height));
    revision = surface->parts[0].dynamic_revision;

    image_height_material(SHADER_IMAGE_LINEAR, SHADER_IMAGE_BILINEAR);
    REQUIRE(height_surface((GlobalTransform2D){ 0 }) == surface);
    REQUIRE(surface->parts[0].dynamic_revision > revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    float expected = 8 + 2 * (192 * .52f + 128 * .48f) / 255.f;
    REQUIRE(fabsf(height - expected) < .001f);
    REQUIRE(draw(surface) && height_matches(height));
    revision = surface->parts[0].dynamic_revision;

    REQUIRE(set_gain("Gain 20.0\n"));
    REQUIRE(!height_surface((GlobalTransform2D){ 0 }));
    REQUIRE(surface->parts[0].dynamic_revision == revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - expected) < .001f);
    REQUIRE(render_material_raster_mesh_work(raster).dynamic_meshes == 1);
    REQUIRE(set_gain("Gain 2.0\n"));
    image_height_material(SHADER_IMAGE_SRGB, SHADER_IMAGE_BILINEAR);
    REQUIRE(!height_surface((GlobalTransform2D){ 0 }));
    REQUIRE(strstr(diagnostic, "Linear"));
    REQUIRE(surface->parts[0].dynamic_revision == revision);
    REQUIRE(render_mesh_surface_pick(surface, (CF_V2){ .5f, -.5f }, &height));
    REQUIRE(fabsf(height - expected) < .001f);
    REQUIRE(render_material_raster_mesh_work(raster).dynamic_meshes == 1);
    return true;
}

void test_material_mesh_cache()
{
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    test_graph_packs_install();
    test_open_hidden_app("material-mesh-cache", SIDE, SIDE);
    cf_app_set_canvas_size(SIDE, SIDE);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(mask_mesh_reuses_uploads_and_retires_replaced_geometry);
    RUN_TEST_CASE(surface_height_displaces_geometry_once_and_rejects_dynamic_roots);
    RUN_TEST_CASE(material_height_cache_tracks_parameters_image_reload_and_sampling);
    pu_clear_setup();
    cf_destroy_app();
    test_graph_packs_restore();
    type_catalog_set_project(NULL);
}
