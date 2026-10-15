#include "render/material_raster.h"

#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    WIDTH = 96,
    HEIGHT = 64
};

static node *parameter(graph *g, const char *name, int slot, bool vector)
{
    node *n = graph_add_node(g, registry_node_by_name("Shader Parameter"));
    assert(!graph_set_node_reference(g, n, slot, name));
    if (vector) {
        assert(!graph_set_pin_type(g, n, 1, 0, PIN_STRUCT, "ShaderVec3"));
    }
    return n;
}

static behavior_asset *graph_fixture(bool material)
{
    behavior_asset *asset = behavior_asset_new(
        material ? "Raster upload material" : "Raster upload shape"
    );
    asset->domain = material ? BEHAVIOR_DOMAIN_MATERIAL : BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 12;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, !material);
    if (material) {
        const char *names[] = { "Tint", "Glow", "Roughness", "Height" };
        const int pins[] = { 0, 1, 2, 7 };
        for (int i = 0; i < 4; i++) {
            node *n = parameter(g, names[i], i, i < 2);
            assert(!graph_connect_data(g, n, 0, out, pins[i]));
        }
    } else {
        node *p = graph_add_node(g, registry_node_by_name("Shader Position"));
        node *circle = graph_add_node(g, registry_node_by_name("Shader Circle"));
        node *radius = parameter(g, "Radius", 0, false);
        assert(!graph_connect_data(g, p, 0, circle, 0));
        assert(!graph_connect_data(g, radius, 0, circle, 1));
        assert(!graph_connect_data(g, circle, 0, out, 0));
    }
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    return asset;
}

static ecs_entity mesh_actor(ecs_world *world)
{
    ecs_entity e = ecs_world_create_entity(world);
    Geometry3D geometry = { .primitive = PRIMITIVE_BOX, .width = 12, .depth = 12, .height = 2 };
    VisualTransform visual = { .scale = 1 };
    Orientation orientation = { 0 };
    MeshSurface2D enabled = { true };
    Material material = { .scene = 0x0000ffff,
                          .albedo = 0x0000ffff,
                          .absorption = 1,
                          .edge_light = 1 };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, e, COMPONENT_##type.id, &(value), 0) == ECS_OK)
    ADD(Geometry3D, geometry);
    ADD(VisualTransform, visual);
    ADD(Orientation, orientation);
    ADD(MeshSurface2D, enabled);
    ADD(Material, material);
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return e;
}

static render_material_entity box(float x, float r, float g, float b)
{
    return (render_material_entity){ .inv_rot = { 1, 0, 0, 1 },
                                     .pos = { x, 0 },
                                     .extent = { 13, 13 },
                                     .shape_type = RENDER_SHAPE_BOX,
                                     .aa = 1,
                                     .points = { 10, 10 },
                                     .scene = { r, g, b, 1 },
                                     .albedo = { r, g, b, 1 },
                                     .absorption = { 1, 1, 1, 1 } };
}

static render_material_entity graph_entity(float x, int material, int shape, const char *values)
{
    render_material_entity e = box(x, 1, 1, 1);
    e.reserved[0] = (float)material;
    e.reserved[1] = (float)shape;
    render_shader_parameters(material, NULL, (ecs_entity){ 0 }, values, e.material_parameters);
    render_shader_parameters(shape, NULL, (ecs_entity){ 0 }, values, e.shape_parameters);
    return e;
}

static float half(uint16_t v)
{
    int e = (v >> 10) & 31, m = v & 1023;
    float f = e == 31 ? (m ? NAN : INFINITY)
            : e       ? ldexpf(1 + m / 1024.0f, e - 15)
                      : ldexpf((float)m, -24);
    return v & 0x8000 ? -f : f;
}

static bool sample(CF_Texture texture, int x, int y, const float expected[4], const char *label)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = cf_canvas_readback(canvas);
    uint16_t pixels[WIDTH * HEIGHT * 4];
    uint64_t start = cf_get_ticks();
    while (readback.id && !cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    bool ok = readback.id && cf_readback_ready(readback) &&
              cf_readback_size(readback) == sizeof pixels &&
              cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
    for (int c = 0; c < 4 && ok; c++) {
        float actual = half(pixels[4 * (y * WIDTH + x) + c]);
        if (!isfinite(actual) || fabsf(actual - expected[c]) > .03f) {
            fprintf(stderr, "material upload %s channel %d: %g expected %g\n", label, c, actual, expected[c]);
            ok = false;
        }
    }
    if (readback.id) {
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

static bool draw(
    render_material_raster *r,
    const render_material_entity *layers,
    int count,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int actor_count
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(
        r,
        layers,
        count,
        (render_material_view){ .min_x = -48, .max_y = 32, .width = WIDTH, .height = HEIGHT },
        world,
        actors,
        actor_count,
        0
    );
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "material upload draw: %s\n", render_material_raster_error(r));
    }
    return ok;
}

static bool work_matches(render_material_raster *r, int entities, int parameters)
{
    render_material_upload_work work = render_material_raster_upload_work(r);
    bool ok = work.entity_records == entities && work.parameter_blocks == parameters &&
              work.entity_bytes == (size_t)entities * 336 &&
              work.parameter_bytes == (size_t)parameters * 1024 &&
              work.parameter_buffer_bytes >= work.parameter_bytes;
    if (!ok) {
        fprintf(
            stderr,
            "material upload work: records %d blocks %d bytes %zu/%zu capacity "
            "%zu\n",
            work.entity_records,
            work.parameter_blocks,
            work.entity_bytes,
            work.parameter_bytes,
            work.parameter_buffer_bytes
        );
    }
    return ok;
}

/* Marker records split ordered raster batches but never enter their generic
 * storage buffers. Distinct graph bindings on either side catch index remaps. */
bool test_material_raster_uploads()
{
    behavior_asset *material = graph_fixture(true), *shape = graph_fixture(false);
    int material_id = render_shader_find(material->name),
        shape_id = render_shader_find(shape->name);
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = { &COMPONENT_Geometry3D,
                                               &COMPONENT_VisualTransform,
                                               &COMPONENT_Orientation,
                                               &COMPONENT_Material,
                                               &COMPONENT_MeshSurface2D };
    for (int i = 0; i < 5; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    render_mesh_material_actor actors[] = { { mesh_actor(world), { -12, 0 } },
                                            { mesh_actor(world), { 16, 0 } } };
    render_material_entity layers[] = {
        box(-32, 1, 0, 0),
        graph_entity(
            -12,
            material_id,
            shape_id,
            "(array)Tint 0 1 0\n"
            "(array)Glow 0.1 0.2 0.3\n"
            "Roughness 0.2\n"
            "Height 2\n"
            "Radius 10\n"
        ),
        { .shape_type = RENDER_SHAPE_MESH, .points = { 0 } },
        graph_entity(
            0,
            material_id,
            shape_id,
            "(array)Tint 0.7 0 0.6\n"
            "(array)Glow 0.4 0.5 0.6\n"
            "Roughness 0.4\n"
            "Height 4\n"
            "Radius 10\n"
        ),
        box(16, 1, 1, 0),
        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 } },
        graph_entity(
            24,
            material_id,
            shape_id,
            "(array)Tint 0 0.5 1\n"
            "(array)Glow 0.7 0.8 0.9\n"
            "Roughness 0.6\n"
            "Height 6\n"
            "Radius 6\n"
        )
    };
    render_material_raster *r = render_material_raster_new();
    bool ok = r && render_material_raster_resize(r, WIDTH, HEIGHT) &&
              draw(r, layers, 7, world, actors, 2) && work_matches(r, 5, 3);
    render_material_textures t = render_material_raster_textures(r);
    if (ok) {
        ok = sample(t.scene, 16, 32, (float[4]){ 1, 0, 0, 1 }, "plain before marker") &&
             sample(t.scene, 32, 32, (float[4]){ 0, 0, 1, 1 }, "mesh over graph") &&
             sample(t.scene, 64, 24, (float[4]){ 1, 1, 0, 1 }, "plain after marker") &&
             sample(t.scene, 64, 32, (float[4]){ 0, 0, 1, 1 }, "second mesh");
    }
    const int xs[] = { 28, 40, 68 };
    const float colors[][4] = { { 0, 1, 0, 1 }, { .7f, 0, .6f, 1 }, { 0, .5f, 1, 1 } };
    const float glows[][4] = { { .1f, .2f, .3f, 1 },
                               { .4f, .5f, .6f, 1 },
                               { .7f, .8f, .9f, 1 } };
    for (int i = 0; i < 3 && ok; i++) {
        float albedo[] = { colors[i][0], colors[i][1], colors[i][2], 2.0f + 2 * i };
        float normal[] = { 0, 0, 0, .8f - .2f * i };
        ok = sample(t.scene, xs[i], 32, colors[i], "indexed graph color") &&
             sample(t.source, xs[i], 32, glows[i], "indexed graph emission") &&
             sample(t.albedo, xs[i], 32, albedo, "indexed graph height") &&
             sample(t.normal, xs[i], 32, normal, "indexed graph roughness") &&
             sample(t.cover, xs[i], 32, (float[4]){ 1, 1, 0, 0 }, "graph geometry coverage");
    }
    /* A second draw reverses the graph/mesh overlap at the first actor. */
    render_material_entity swapped = layers[1];
    layers[1] = layers[2];
    layers[2] = swapped;
    if (ok) {
        ok = draw(r, layers, 7, world, actors, 2) && work_matches(r, 5, 3) &&
             sample(render_material_raster_textures(r).scene, 36, 32, colors[0], "graph over mesh");
    }
    render_material_entity leading[] = { layers[1], layers[2] };
    leading[1].shape_parameters[0][0] = 3;
    if (ok) {
        ok = draw(r, leading, 2, world, actors, 2) && work_matches(r, 1, 1) &&
             sample(
                 render_material_raster_textures(r).scene,
                 31,
                 32,
                 (float[4]){ 0, 0, 1, 1 },
                 "leading mesh retained outside graph"
             ) &&
             sample(render_material_raster_textures(r).scene, 36, 32, colors[0], "first compact graph over leading mesh");
    }
    render_material_entity markers[] = { layers[1], layers[5] };
    if (ok) {
        ok = draw(r, markers, 2, world, actors, 2) && work_matches(r, 0, 0) &&
             sample(render_material_raster_textures(r).scene, 36, 32, (float[4]){ 0, 0, 1, 1 }, "mesh-only draw");
    }
    if (ok) {
        ok = draw(r, layers, 1, world, actors, 2) && work_matches(r, 1, 0) &&
             sample(render_material_raster_textures(r).scene, 16, 32, (float[4]){ 1, 0, 0, 1 }, "plain-only draw");
    }
    if (ok) {
        ok = draw(r, NULL, 0, world, actors, 2) && work_matches(r, 0, 0);
    }
    printf(
        "material raster compact uploads %s: mixed order, graph slots, five "
        "channels, marker/plain/empty transitions\n",
        ok ? "PASS" : "FAIL"
    );
    render_material_raster_free(r);
    ecs_world_free(world);
    behavior_asset_free(shape);
    behavior_asset_free(material);
    return ok;
}
