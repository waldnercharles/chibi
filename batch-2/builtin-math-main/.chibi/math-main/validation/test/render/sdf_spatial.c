#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "render/material_raster.h"
#include "render/mesh_projection.h"
#include "render/shader_catalog.h"
#include "render/spatial_groups.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    WIDTH = 96,
    HEIGHT = 80,
    VALUES = WIDTH * HEIGHT * 4
};

static float half(uint16_t v)
{
    int e = (v >> 10) & 31, m = v & 1023;
    float f = e == 31 ? (m ? NAN : INFINITY)
            : e       ? ldexpf(1 + m / 1024.0f, e - 15)
                      : ldexpf((float)m, -24);
    return v & 0x8000 ? -f : f;
}

static bool read_sized_pixels(CF_Texture texture, int width, int height, float **result)
{
    int count = width * height * 4, bytes = count * (int)sizeof(uint16_t);
    uint16_t *pixels = xmalloc(bytes);
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (readback.id && !cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    bool ok = readback.id && cf_readback_ready(readback) &&
              cf_readback_size(readback) == bytes &&
              cf_readback_data(readback, pixels, bytes) == bytes;
    *result = xmalloc(count * sizeof(float));
    for (int i = 0; i < count && ok; i++) {
        (*result)[i] = half(pixels[i]);
        ok &= isfinite((*result)[i]);
    }
    if (readback.id) {
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    free(pixels);
    return ok;
}

static bool read_pixels(CF_Texture texture, float **result)
{
    return read_sized_pixels(texture, WIDTH, HEIGHT, result);
}

static ecs_entity actor(ecs_world *world, bool sdf, const char *name)
{
    ecs_entity entity = ecs_world_create_entity(world);
    Geometry3D bounds = { .primitive = PRIMITIVE_BOX, .width = 24, .depth = 24, .height = 24 };
    VisualTransform transform = { .scale = .25f };
    Orientation orientation = { 0 };
    Material material = { .scene = sdf ? 0xff0000ff : 0x0000ffff,
                          .albedo = sdf ? 0xff0000ff : 0x0000ffff,
                          .absorption = 1,
                          .edge_light = 1 };
    Shader shader = { .shape = name };
    SdfSurface3D sdf_surface = { true };
    MeshSurface2D mesh_surface = { true };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, entity, COMPONENT_##type.id, &(value), 0) == ECS_OK)
    ADD(Geometry3D, bounds);
    ADD(VisualTransform, transform);
    ADD(Orientation, orientation);
    ADD(Material, material);
    if (sdf) {
        ADD(Shader, shader);
        ADD(SdfSurface3D, sdf_surface);
    } else {
        ADD(MeshSurface2D, mesh_surface);
    }
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static behavior_asset *sphere()
{
    behavior_asset *asset = behavior_asset_new("Spatial scratch sphere");
    asset->domain = BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 12;
    graph *g = asset->graph;
    node *position = graph_add_node(g, registry_node_by_name("Shader ObjectPosition"));
    node *length = graph_add_node(g, registry_node_by_name("Shader Length"));
    node *distance = graph_add_node(g, registry_node_by_name("Shader Subtract"));
    assert(!graph_connect_data(g, position, 0, length, 0));
    assert(!graph_connect_data(g, length, 0, distance, 0));
    pin_value radius = { .type = PIN_F32, .f = 10 };
    assert(!graph_set_pin_default(g, distance, 0, 1, &radius));
    assert(!graph_connect_data(g, distance, 0, shader_graph_add_output(g, true), 0));
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    return asset;
}

/* Overlapping actors share a scratch group, distant ones split, a crowd
 * stays within the group limit, and a near-plane crop stays on the target. */
TEST_CASE(spatial_partitions_group_nearby_actors)
{
    render_spatial_actor actors[] = { { { 0, 0, 12, 12 }, 0, 0 },
                                      { { 8, 8, 20, 20 }, 1, 0 },
                                      { { 80, 0, 92, 12 }, 2, 0 },
                                      { { 0, 60, 12, 72 }, 3, 0 } };
    int groups = render_spatial_partition(actors, 4, 0), membership[4];
    for (int i = 0; i < 4; i++) {
        membership[actors[i].actor] = actors[i].group;
    }
    render_spatial_actor *crowd = xmalloc(1000 * sizeof *crowd);
    for (int i = 0; i < 1000; i++) {
        int cluster = i % 16, local = i / 16;
        int x = (cluster % 4) * 512 + (local % 8) * 6;
        int y = (cluster / 4) * 512 + (local / 8) * 6;
        crowd[i] = (render_spatial_actor){ { x, y, x + 8, y + 8 }, i, 0 };
    }
    int crowd_groups = render_spatial_partition(crowd, 1000, 0);
    free(crowd);
    REQUIRE(groups == 3 && membership[0] == membership[1]);
    REQUIRE(membership[2] != membership[0] && membership[3] != membership[0]);
    REQUIRE(crowd_groups >= 2 && crowd_groups <= RENDER_SCRATCH_GROUP_MAX);
    render_view_2d view = { -48, 40, WIDTH, HEIGHT, .focal_distance = 32 };
    CF_V2 lo, hi;
    REQUIRE(render_mesh_project_bounds(
        view,
        (CF_V3){ -.01f, 30.98f, -.01f },
        (CF_V3){ .01f, 31.02f, .01f },
        &lo,
        &hi
    ));
    render_pixel_rect clipped = render_mesh_pixel_bounds(view, lo, hi, WIDTH, HEIGHT);
    REQUIRE(clipped.x >= 0 && clipped.y >= 0 && clipped.right <= WIDTH && clipped.bottom <= HEIGHT);
    REQUIRE(clipped.right > clipped.x && clipped.bottom > clipped.y);
    return true;
}

static bool snapshot(
    render_material_raster *r,
    ecs_world *world,
    const render_mesh_material_actor actors[3],
    const render_material_entity *layers,
    int count,
    render_material_view view,
    float *data[5]
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(r, layers, count, view, world, actors, 3, 0);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "Spatial scratch draw: %s\n", render_material_raster_error(r));
        return false;
    }
    render_material_textures t = render_material_raster_textures(r);
    CF_Texture textures[] = { t.scene, t.source, t.albedo, t.normal, t.cover };
    for (int i = 0; i < 5 && ok; i++) {
        ok = read_pixels(textures[i], &data[i]);
    }
    return ok;
}

/* Three SDF actors, two of them far from the third, draw the same pixels
 * through two scratch groups as through one, with less surface and
 * composition work, orthographic and in perspective, with and without a
 * prior 2D layer. */
TEST_CASE(spatial_scratch_matches_one_scratch)
{
    behavior_asset *shape = sphere();
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Geometry3D,   &COMPONENT_VisualTransform,
        &COMPONENT_Orientation,  &COMPONENT_Material,
        &COMPONENT_Shader,       &COMPONENT_SdfSurface3D,
        &COMPONENT_MeshSurface2D
    };
    for (int i = 0; i < 7; i++) {
        REQUIRE(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    render_mesh_material_actor actors[3];
    for (int i = 0; i < 3; i++) {
        actors[i] = (render_mesh_material_actor){ actor(world, true, shape->name),
                                                  { i ? 35 : -35, 0 } };
        if (i == 2) {
            Material *m = Material_get(world, actors[i].entity);
            m->scene = m->albedo = 0x0000ffff;
        }
    }
    render_material_entity layers[] = {
        { .shape_type = RENDER_SHAPE_BOX,
          .inv_rot = { 1, 0, 0, 1 },
          .extent = { 60, 50 },
          .points = { 60, 50 },
          .layer = -1,
          .scene = { 0, .2f, 0, 1 },
          .albedo = { 0, .2f, 0, 1 },
          .absorption = { 1, 1, 1, 1 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 0 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 2 } },
        { .shape_type = RENDER_SHAPE_BOX,
          .inv_rot = { 1, 0, 0, 1 },
          .extent = { 4, 4 },
          .points = { 4, 4 },
          .layer = 1,
          .scene = { .3f, .3f, 0, 1 },
          .albedo = { .3f, .3f, 0, 1 },
          .absorption = { 1, 1, 1, 1 } }
    };
    render_material_raster *r = render_material_raster_new();
    REQUIRE(render_material_raster_resize(r, WIDTH, HEIGHT));
    for (int perspective = 0; perspective < 2; perspective++) {
        for (int prior = 0; prior < 2; prior++) {
            render_material_view view = { -48,
                                          40,
                                          WIDTH,
                                          HEIGHT,
                                          .focal_distance = perspective ? 40 : 0 };
            float *reference[5] = { 0 }, *split[5] = { 0 };
            render_material_raster_set_spatial_scratch(r, false);
            REQUIRE(snapshot(r, world, actors, layers + !prior, 4 + prior, view, reference));
            render_mesh_material_work before = render_material_raster_mesh_work(r);
            render_material_raster_set_spatial_scratch(r, true);
            REQUIRE(snapshot(r, world, actors, layers + !prior, 4 + prior, view, split));
            render_mesh_material_work after = render_material_raster_mesh_work(r);
            /* The lone actor's island shows its own color. */
            int island = (40 * WIDTH + 83) * 4;
            REQUIRE(reference[0][island + 3] > .5f);
            REQUIRE(reference[0][island] > .5f || reference[0][island + 2] > .5f);
            float maximum = 0;
            for (int target = 0; target < 5; target++) {
                for (int i = 0; i < VALUES; i++) {
                    maximum = fmaxf(maximum, fabsf(reference[target][i] - split[target][i]));
                }
            }
            for (int i = 0; i < 5; i++) {
                free(reference[i]);
                free(split[i]);
            }
            REQUIRE(maximum <= .015f);
            REQUIRE(after.scratch_groups == 2 && before.scratch_groups == 1);
            REQUIRE(after.surface_pixels < before.surface_pixels / 2);
            REQUIRE(after.composition_pixels < (size_t)(prior + 1) * WIDTH * HEIGHT);
            REQUIRE(prior || after.composition_pixels < before.composition_pixels / 2);
        }
    }
    render_material_raster_free(r);
    ecs_world_free(world);
    behavior_asset_free(shape);
    return true;
}

static void teardown()
{
    authoring_shader_catalog_clear();
}

/* Runs in an open hidden app with the graph packs installed. */
void test_sdf_spatial()
{
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(spatial_partitions_group_nearby_actors);
    RUN_TEST_CASE(spatial_scratch_matches_one_scratch);
    pu_clear_setup();
}
