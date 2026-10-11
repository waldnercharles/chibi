#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846f
enum
{
    WIDTH = 512,
    HEIGHT = 256
};
static behavior_asset *sphere()
{
    behavior_asset *a = behavior_asset_new("Orbit emissive radius six sphere");
    a->domain = BEHAVIOR_DOMAIN_SHAPE;
    a->bounds[0] = a->bounds[1] = 8;
    graph *g = a->graph;
    node *p = graph_add_node(g, registry_node_by_name("Shader ObjectPosition"));
    node *length = graph_add_node(g, registry_node_by_name("Shader Length"));
    node *distance = graph_add_node(g, registry_node_by_name("Shader Subtract"));
    node *out = shader_graph_add_output(g, true);
    assert(!graph_connect_data(g, p, 0, length, 0));
    assert(!graph_connect_data(g, length, 0, distance, 0));
    pin_value radius = { .type = PIN_F32, .f = 6 };
    assert(!graph_set_pin_default(g, distance, 0, 1, &radius));
    assert(!graph_connect_data(g, distance, 0, out, 0));
    shader_error error = { 0 };
    assert(render_shader_upsert(a, &error));
    return a;
}
static ecs_entity actor(ecs_world *world, const char *shape, uint32_t color)
{
    ecs_entity e = ecs_world_create_entity(world);
    Geometry3D geometry = { .primitive = PRIMITIVE_BOX,
                            .width = shape ? 16 : 20,
                            .depth = shape ? 16 : 20,
                            .height = shape ? 16 : 20 };
    VisualTransform pose = { .scale = 1 };
    Orientation orientation = { 0 };
    Material material = { .scene = color,
                          .albedo = color,
                          .emission_color = color,
                          .emission_intensity = shape ? 4 : 0,
                          .emission_multiplier = 1,
                          .edge_light = 1,
                          .absorption = shape ? 0 : 1 };
    Shader shader = { .shape = shape };
    SdfSurface3D sdf = { .enabled = shape != NULL };
    MeshSurface2D mesh = { .enabled = shape == NULL };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, e, COMPONENT_##type.id, &(value), 0) == ECS_OK)
    ADD(Geometry3D, geometry);
    ADD(VisualTransform, pose);
    ADD(Orientation, orientation);
    ADD(Material, material);
    ADD(Shader, shader);
    ADD(SdfSurface3D, sdf);
    ADD(MeshSurface2D, mesh);
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
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
static bool read_source(render_material_raster *r, float **out)
{
    CF_CanvasParams cp = cf_canvas_defaults(0, 0);
    cp.attach_target = render_material_raster_textures(r).source;
    CF_Canvas canvas = cf_make_canvas(cp);
    CF_Readback rb = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (rb.id && !cf_readback_ready(rb) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    int count = WIDTH * HEIGHT * 4, bytes = count * sizeof(uint16_t);
    uint16_t *pixels = xmalloc(bytes);
    bool ok = rb.id && cf_readback_ready(rb) && cf_readback_size(rb) == bytes &&
              cf_readback_data(rb, pixels, bytes) == bytes;
    *out = xmalloc(count * sizeof(float));
    for (int i = 0; i < count && ok; i++) {
        (*out)[i] = half(pixels[i]);
        ok &= isfinite((*out)[i]);
    }
    free(pixels);
    if (rb.id) {
        cf_destroy_readback(rb);
    }
    cf_destroy_canvas(canvas);
    return ok;
}
static bool capture(
    render_material_raster *r,
    ecs_world *world,
    render_mesh_material_actor *actors,
    int count,
    float angle,
    float **source
)
{
    render_material_view view = { .min_x = -512,
                                  .max_y = 256,
                                  .width = 1024,
                                  .height = 512,
                                  .focal_distance = 700,
                                  .yaw = PI * .5f,
                                  .roll = PI * .5f,
                                  .pitch = angle };
    render_material_entity layers[3] = { 0 };
    for (int i = 0; i < count; i++) {
        layers[i].shape_type = RENDER_SHAPE_MESH;
        layers[i].points[0] = i;
    }
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(r, layers, count, view, world, actors, count, 0);
    cf_app_draw_onto_screen(false);
    return ok && read_source(r, source);
}
typedef struct
{
    double sum, x, y, area;
} moments;
static moments emission_moments(const float *pixels, int channel)
{
    moments m = { 0 };
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            float emission = pixels[(y * WIDTH + x) * 4 + channel];
            if (emission <= 0) {
                continue;
            }
            m.sum += emission;
            m.x += (x + .5) * emission;
            m.y += (y + .5) * emission;
            m.area += emission / 4;
        }
    }
    if (m.sum > 0) {
        m.x /= m.sum;
        m.y /= m.sum;
    }
    return m;
}
/* Whether `channel`'s emission has the centroid and area of a radius-six
 * sphere at `world` seen from `angle` around the view's center, by a
 * closed-form projection independent of the renderer's helpers. Scene
 * transforms map gameplay Y directly to world Z. */
static bool inspect(const float *source, CF_V3 world, float angle, int channel)
{
    double x = cos(angle) * world.x - sin(angle) * world.y;
    double depth = sin(angle) * world.x + cos(angle) * world.y;
    double distance = 700 - depth, scale = 700 / distance;
    double px = WIDTH * .5 + x * scale * .5, py = HEIGHT * .5 + world.z * scale * .5;
    double radius = 6 * scale * .5;
    double area = PI * radius * radius *
                  sqrt(1 + (x * x + world.z * world.z) / (distance * distance));
    moments m = emission_moments(source, channel);
    return m.sum > 0 && fabs(m.x - px) < .8 && fabs(m.y - py) < .8 &&
           fabs(m.area - area) < 3 + area * .25;
}
/* Emissive SDF spheres project their emission where an orbiting eye sees
 * them, raised or lowered, and an opaque mesh in front hides one emitter's
 * light completely without changing it once it moves aside. */
TEST_CASE(emitters_follow_an_orbiting_camera)
{
    behavior_asset *shape = sphere();
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Geometry3D,    &COMPONENT_VisualTransform,
        &COMPONENT_Orientation,   &COMPONENT_Material,
        &COMPONENT_Shader,        &COMPONENT_SdfSurface3D,
        &COMPONENT_MeshSurface2D, &COMPONENT_MeshParts,
        &COMPONENT_MeshAsset
    };
    for (int i = 0; i < (int)(sizeof components / sizeof *components); i++) {
        REQUIRE(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    ecs_entity cyan = actor(world, shape->name, 0x00ffffff),
               amber = actor(world, shape->name, 0xff6600ff),
               blocker = actor(world, NULL, 0x0000ffff);
    render_mesh_material_actor actors[] = { { cyan, { -215, -45 } },
                                            { amber, { 215, 45 } },
                                            { blocker, { 245, 45 * 455.0f / 485 } } };
    render_material_raster *r = render_material_raster_new();
    REQUIRE(render_material_raster_resize(r, WIDTH, HEIGHT));
    render_material_raster_set_quality(r, (SurfaceQuality){ .resolution = SURFACE_FULL });
    const float angles[] = { 0, PI * .25f, PI * .5f, PI, PI * 1.5f };
    for (int elevated = 0; elevated < 2; elevated++) {
        Geometry3D_get(world, cyan)->elevation = elevated ? 25 : 0;
        Geometry3D_get(world, amber)->elevation = elevated ? -15 : 0;
        for (int i = 0; i < 5; i++) {
            float *source = NULL;
            REQUIRE(capture(r, world, actors, 2, angles[i], &source));
            bool seen = inspect(source, (CF_V3){ -215, elevated ? 25 : 0, -45 }, angles[i], 2) &&
                        inspect(source, (CF_V3){ 215, elevated ? -15 : 0, 45 }, angles[i], 0);
            free(source);
            REQUIRE(seen);
        }
    }
    Geometry3D_get(world, cyan)->elevation = Geometry3D_get(world, amber)->elevation = 0;
    float *reference = NULL, *hidden = NULL, *front = NULL, *restored = NULL;
    REQUIRE(capture(r, world, actors, 2, PI * .5f, &reference));
    REQUIRE(capture(r, world, actors, 3, PI * .5f, &hidden));
    moments baseline = emission_moments(reference, 0),
            occluded = emission_moments(hidden, 0);
    REQUIRE(baseline.sum > 0 && occluded.sum == 0);
    REQUIRE(inspect(hidden, (CF_V3){ -215, 0, -45 }, PI * .5f, 2));
    actors[2].position = (Transform){ 185, 45 * 515.0f / 485 };
    REQUIRE(capture(r, world, actors, 3, PI * .5f, &front));
    REQUIRE(capture(r, world, actors, 2, PI * .5f, &restored));
    REQUIRE(fabs(emission_moments(front, 0).sum - baseline.sum) < .01);
    REQUIRE(fabs(emission_moments(restored, 0).sum - baseline.sum) < .01);
    free(reference);
    free(hidden);
    free(front);
    free(restored);
    render_material_raster_free(r);
    ecs_world_free(world);
    behavior_asset_free(shape);
    return true;
}

/* Runs in an open hidden app with the graph packs installed. */
void test_camera_emitter_orbit()
{
    RUN_TEST_CASE(emitters_follow_an_orbiting_camera);
}
