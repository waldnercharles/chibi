#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "render/mesh_material.h"
#include "runtime/component_catalog.h"
#include "test/graph/packs.h"
#include "test/render/hidden_app.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SIDE = 64,
    CASES = 6,
    ACTORS = CASES * 2,
    CHANNELS = SIDE * SIDE * 4
};
static ecs_world *world;
static render_mesh_material *pass;
static FILE *evidence;
static render_mesh_material_actor actors[ACTORS];
static uint16_t scene[CHANNELS], absorption[CHANNELS];
static const char *const names[] = { "Surface transport full",
                                     "Surface transport half",
                                     "Surface transport cutout" };
static const float opacity[] = { 1, .5f, 0 };
static const char *const cases[] = { "no-material",  "clear-material",
                                     "absorber",     "filtered-alpha",
                                     "half-opacity", "cutout" };

static void add(ecs_entity entity, const ecs_component_desc *desc, const void *data)
{
    assert(ecs_world_record_add(world, entity, desc->id, data, 0) == ECS_OK);
}
static void constant(graph *g, node *n, int input, float value)
{
    pin_value pin = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(g, n, 0, input, &pin));
}
static void register_material(int index)
{
    behavior_asset *asset = behavior_asset_new(names[index]);
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(asset->graph, false);
    float color[] = { .4f, .6f, .8f };
    pin_value base = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = color };
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    assert(!graph_set_pin_default(asset->graph, out, 0, SHADER_SURFACE_BASE_COLOR, &base));
    assert(!graph_set_pin_default(asset->graph, out, 0, SHADER_SURFACE_UNLIT, &unlit));
    constant(asset->graph, out, SHADER_SURFACE_OPACITY, opacity[index]);
    shader_error diagnostic = { 0 };
    bool valid = render_shader_upsert(asset, &diagnostic);
    if (!valid) {
        fprintf(stderr, "surface-transport material: %s\n", diagnostic.message);
    }
    assert(valid);
    behavior_asset_free(asset);
}
static void register_sphere()
{
    behavior_asset *asset = behavior_asset_new("Surface transport sphere");
    asset->domain = BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 3;
    graph *g = asset->graph;
    node *position = graph_add_node(g, registry_node_by_name("Shader ObjectPosition"));
    node *length = graph_add_node(g, registry_node_by_name("Shader Length"));
    node *subtract = graph_add_node(g, registry_node_by_name("Shader Subtract"));
    node *out = shader_graph_add_output(g, true);
    assert(!graph_connect_data(g, position, 0, length, 0));
    assert(!graph_connect_data(g, length, 0, subtract, 0));
    constant(g, subtract, 1, 3);
    assert(!graph_connect_data(g, subtract, 0, out, 0));
    shader_error diagnostic = { 0 };
    bool valid = render_shader_upsert(asset, &diagnostic);
    if (!valid) {
        fprintf(stderr, "surface-transport shape: %s\n", diagnostic.message);
    }
    assert(valid);
    behavior_asset_free(asset);
}
static void setup()
{
    const char *output = getenv("CHIBI_SURFACE_TRANSPORT_EVIDENCE");
    evidence = output ? fopen(output, "w") : stderr;
    assert(evidence);
    world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    for (int i = 0; i < 3; i++) {
        register_material(i);
    }
    register_sphere();
    for (int path = 0; path < 2; path++) {
        for (int index = 0; index < CASES; index++) {
            ecs_entity entity = ecs_world_create_entity(world);
            Geometry3D geometry = { .primitive = PRIMITIVE_BOX,
                                    .width = 6,
                                    .height = 6,
                                    .depth = 6 };
            add(entity, &COMPONENT_Geometry3D, &geometry);
            add(entity, &COMPONENT_VisualTransform, &(VisualTransform){ .scale = 1 });
            add(entity, &COMPONENT_Orientation, &(Orientation){ 0 });
            add(entity, &COMPONENT_Appearance, &(Appearance){ .color = 0xffffffffu });
            add(entity, &COMPONENT_MeshSurface2D, &(MeshSurface2D){ .enabled = !path });
            add(entity, &COMPONENT_SdfSurface3D, &(SdfSurface3D){ .enabled = path });
            int material = index == 4 ? 1 : index == 5 ? 2 : 0;
            add(entity,
                &COMPONENT_Shader,
                &(Shader){ .material = names[material],
                           .shape = path ? "Surface transport sphere" : NULL });
            if (index) {
                Material coefficients = { .scene = 0xffffffffu,
                                          .albedo = 0xffffffffu,
                                          .absorption = index == 1 ? 0 : .8f };
                if (index >= 3) {
                    coefficients.scene = 0xffffff80u;
                    coefficients.filter = 0x0040c0ffu;
                }
                add(entity, &COMPONENT_Material, &coefficients);
            }
            actors[path * CASES + index] = (render_mesh_material_actor){
                .entity = entity,
                .position = { -25 + index * 10, path ? 12 : -12 }
            };
        }
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    pass = render_mesh_material_new();
    assert(render_mesh_material_resize(pass, SIDE, SIDE));
    render_mesh_material_set_quality(
        pass,
        (SurfaceQuality){ .resolution = SURFACE_FULL, .disable_sdf_lod = true }
    );
}
static void teardown()
{
    render_mesh_material_free(pass);
    ecs_world_free(world);
    authoring_shader_catalog_clear();
    if (evidence != stderr) {
        assert(!fclose(evidence));
    }
}
/* Both public attachments share the pass's padded capacity, not the app size. */
static bool read(CF_Texture texture, uint16_t *pixels, int count)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    if (!canvas.id) {
        return false;
    }
    CF_Readback request = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (request.id && !cf_readback_ready(request) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 2) {
        cf_sleep(1);
    }
    int bytes = count * 4 * (int)sizeof(uint16_t);
    bool valid = request.id && cf_readback_ready(request) &&
                 cf_readback_size(request) == bytes &&
                 cf_readback_data(request, pixels, bytes) == bytes;
    if (request.id) {
        cf_destroy_readback(request);
    }
    cf_destroy_canvas(canvas);
    return valid;
}
static bool near(float actual, float expected)
{
    return isfinite(actual) && fabsf(actual - expected) < .003f;
}
TEST_CASE(shader_material_preserves_authored_surface_transport)
{
    for (int i = 0; i < 3; i++) {
        REQUIRE(render_shader_find(names[i]) > 0);
    }
    REQUIRE(render_shader_find("Surface transport sphere") > 0);
    cf_app_update(NULL);
    bool drawn = render_mesh_material_draw(
        pass,
        world,
        actors,
        ACTORS,
        (render_material_view){ -32, 32, SIDE, SIDE },
        0
    );
    cf_app_draw_onto_screen(false);
    if (!drawn) {
        fprintf(stderr, "surface-transport draw: %s\n", render_mesh_material_error(pass));
    }
    REQUIRE(drawn);
    int active[2], capacity[2];
    float rect[4];
    render_mesh_material_extent(pass, active, capacity);
    render_mesh_material_rect(pass, rect);
    REQUIRE(capacity[0] > 0 && capacity[1] > 0 && capacity[0] <= SIDE && capacity[1] <= SIDE);
    REQUIRE(active[0] > 0 && active[1] > 0 && rect[2] > 0 && rect[3] > 0);
    render_mesh_material_textures textures = render_mesh_material_get_textures(pass);
    REQUIRE(read(textures.scene, scene, capacity[0] * capacity[1]));
    REQUIRE(read(textures.absorption, absorption, capacity[0] * capacity[1]));
    fprintf(
        evidence,
        "surface-transport backend=%s frames=1 actors=12 readbacks=2 "
        "capacity=%dx%d active=%dx%d\n",
        cf_backend_type_to_string(cf_query_backend()),
        capacity[0],
        capacity[1],
        active[0],
        active[1]
    );
    for (int path = 0; path < 2; path++) {
        for (int index = 0; index < CASES; index++) {
            Transform position = actors[path * CASES + index].position;
            int x = (int)floorf((32 + position.x - rect[0]) * active[0] / rect[2]);
            int y = (int)floorf((32 + position.y - rect[1]) * active[1] / rect[3]);
            REQUIRE(x >= 0 && y >= 0 && x < active[0] && y < active[1]);
            int pixel = (y * capacity[0] + x) * 4;
            float observed[4], expected[4] = { 0 };
            for (int c = 0; c < 4; c++) {
                observed[c] = test_half(absorption[pixel + c]);
            }
            if (index >= 2 && index < 5) {
                float alpha = index >= 3 ? 128.0f / 255 : 1;
                float coverage = index == 4 ? .5f : 1;
                const int filter[] = { 0, index >= 3 ? 64 : 0, index >= 3 ? 192 : 0 };
                for (int c = 0; c < 3; c++) {
                    expected[c] = .8f * alpha * (1 - filter[c] / 255.0f) * coverage;
                }
                expected[3] = coverage;
            }
            fprintf(
                evidence,
                "surface-transport path=%s case=%s absorption=%.6g,%.6g,%.6g "
                "cover=%.6g expected=%.6g,%.6g,%.6g,%.6g\n",
                path ? "sdf" : "mesh",
                cases[index],
                observed[0],
                observed[1],
                observed[2],
                observed[3],
                expected[0],
                expected[1],
                expected[2],
                expected[3]
            );
            for (int c = 0; c < 4; c++) {
                REQUIRE(near(observed[c], expected[c]));
            }
            const float base[] = { .4f, .6f, .8f, 1 };
            for (int c = 0; c < 4; c++) {
                REQUIRE(near(test_half(scene[pixel + c]), index == 5 ? 0 : base[c]));
            }
        }
    }
    return true;
}
void test_surface_transport()
{
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    test_graph_packs_install();
    test_open_hidden_app("surface-transport", SIDE, SIDE);
    cf_app_set_canvas_size(SIDE, SIDE);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(shader_material_preserves_authored_surface_transport);
    pu_clear_setup();
    cf_destroy_app();
    test_graph_packs_restore();
    type_catalog_set_project(NULL);
}
