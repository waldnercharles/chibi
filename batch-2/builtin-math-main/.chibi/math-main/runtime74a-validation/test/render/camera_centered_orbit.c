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
    WIDTH = 96,
    HEIGHT = 80,
    TARGETS = 5
};

static behavior_asset *sphere()
{
    behavior_asset *a = behavior_asset_new("Centered camera orbit sphere");
    a->domain = BEHAVIOR_DOMAIN_SHAPE;
    a->bounds[0] = a->bounds[1] = 4;
    graph *g = a->graph;
    node *p = graph_add_node(g, registry_node_by_name("Shader ObjectPosition"));
    node *length = graph_add_node(g, registry_node_by_name("Shader Length"));
    node *distance = graph_add_node(g, registry_node_by_name("Shader Subtract"));
    node *out = shader_graph_add_output(g, true);
    assert(!graph_connect_data(g, p, 0, length, 0));
    assert(!graph_connect_data(g, length, 0, distance, 0));
    pin_value radius = { .type = PIN_F32, .f = 3 };
    assert(!graph_set_pin_default(g, distance, 0, 1, &radius));
    assert(!graph_connect_data(g, distance, 0, out, 0));
    shader_error error = { 0 };
    assert(render_shader_upsert(a, &error));
    return a;
}
static ecs_entity actor(ecs_world *world, const char *shape, uint32_t color, float elevation)
{
    ecs_entity e = ecs_world_create_entity(world);
    Geometry3D geometry = { .primitive = PRIMITIVE_BOX,
                            .width = shape ? 8 : 6,
                            .depth = shape ? 8 : 6,
                            .height = shape ? 8 : 6,
                            .elevation = elevation };
    VisualTransform pose = { .scale = 1 };
    Orientation orientation = { 0 };
    Material material = { .scene = color, .albedo = color, .absorption = 1, .edge_light = 1 };
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
static bool read(CF_Texture texture, float **out)
{
    CF_CanvasParams cp = cf_canvas_defaults(0, 0);
    cp.attach_target = texture;
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
static void release(float *pixels[TARGETS])
{
    for (int i = 0; i < TARGETS; i++) {
        free(pixels[i]);
    }
}
static bool capture(
    render_material_raster *r,
    ecs_world *world,
    render_mesh_material_actor actors[4],
    render_material_view view,
    float *pixels[TARGETS]
)
{
    render_material_entity layers[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        layers[i].shape_type = RENDER_SHAPE_MESH;
        layers[i].points[0] = i;
    }
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(r, layers, 4, view, world, actors, 4, 0);
    cf_app_draw_onto_screen(false);
    render_material_textures t = render_material_raster_textures(r);
    CF_Texture targets[] = { t.scene, t.source, t.albedo, t.normal, t.cover };
    for (int i = 0; i < TARGETS; i++) {
        ok &= read(targets[i], &pixels[i]);
    }
    return ok;
}
static bool winner(float *pixels[TARGETS], const float color[3], const char *label)
{
    bool ok = true;
    for (int y = 39; y <= 40; y++) {
        for (int x = 47; x <= 48; x++) {
            int p = (y * WIDTH + x) * 4;
            for (int c = 0; c < 3; c++) {
                ok &= fabsf(pixels[0][p + c] - color[c]) < .005f;
                ok &= fabsf(pixels[2][p + c] - color[c]) < .005f;
            }
            ok &= pixels[0][p + 3] == 1 && pixels[4][p + 1] == 1 &&
                  pixels[4][p + 2] == 1;
            /* These fixed actors sit eight units toward the eye at cardinal views;
             * their visible front faces must be above depth eight, not the rear. */
            ok &= pixels[2][p + 3] > 10 && pixels[2][p + 3] < 12;
        }
    }
    if (!ok) {
        int p = (40 * WIDTH + 48) * 4;
        fprintf(
            stderr,
            "Centered orbit %s wrong winner/depth: scene %g %g %g %g; height "
            "%g\n",
            label,
            pixels[0][p],
            pixels[0][p + 1],
            pixels[0][p + 2],
            pixels[0][p + 3],
            pixels[2][p + 3]
        );
    }
    return ok;
}
/* Four actors make a cross around the view's center: an eye circling it sees
 * blue, green, red and yellow in turn, whatever their order or hidden-surface
 * rejection, and a full turn draws what the unrotated camera does. */
TEST_CASE(centered_orbits_see_each_side_in_turn)
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
    /* Fixed cross in the world's XY plane. The source view sees blue in front;
     * an eye travelling around Z must successively see green, red and yellow. */
    ecs_entity blue = actor(world, NULL, 0x0000ffff, 8);
    ecs_entity green = actor(world, shape->name, 0x00ff00ff, 0);
    ecs_entity red = actor(world, shape->name, 0xff0000ff, -8);
    ecs_entity yellow = actor(world, NULL, 0xffff00ff, 0);
    render_mesh_material_actor fixed[] = { { blue, { 0, 0 } },
                                           { green, { 8, 0 } },
                                           { red, { 0, 0 } },
                                           { yellow, { -8, 0 } } };
    const float colors[][3] = { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 1, 1, 0 } };
    render_material_raster *r = render_material_raster_new();
    REQUIRE(render_material_raster_resize(r, WIDTH, HEIGHT));
    render_material_raster_set_quality(r, (SurfaceQuality){ .resolution = SURFACE_FULL });
    render_material_view base = { .min_x = -48,
                                  .max_y = 40,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .focal_distance = 700 };
    float *legacy[TARGETS] = { 0 };
    REQUIRE(capture(r, world, fixed, base, legacy));
    REQUIRE(winner(legacy, colors[0], "legacy front"));
    for (int angle = 0; angle <= 4; angle++) {
        for (int order = 0; order < 2; order++) {
            for (int hidden = 0; hidden < 2; hidden++) {
                render_material_view view = base;
                view.yaw = PI * .5f;
                view.roll = PI * .5f;
                view.pitch = angle * PI * .5f;
                render_mesh_material_actor actors[4];
                for (int i = 0; i < 4; i++) {
                    actors[i] = fixed[order ? 3 - i : i];
                }
                render_material_raster_set_hidden_rejection(r, hidden);
                float *pixels[TARGETS] = { 0 };
                char label[80];
                snprintf(label, sizeof label, "angle%d order%d hidden%d", angle, order, hidden);
                REQUIRE(capture(r, world, actors, view, pixels));
                bool passed = winner(pixels, colors[angle % 4], label);
                for (int t = 0; (angle == 0 || angle == 4) && t < TARGETS; t++) {
                    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) {
                        float delta = fabsf(legacy[t][i] - pixels[t][i]);
                        passed &= delta <= .003f + .002f * fabsf(legacy[t][i]);
                    }
                }
                release(pixels);
                REQUIRE(passed);
            }
        }
    }
    release(legacy);
    render_material_raster_free(r);
    ecs_world_free(world);
    behavior_asset_free(shape);
    return true;
}

/* Runs in an open hidden app with the graph packs installed. */
void test_camera_centered_orbit()
{
    RUN_TEST_CASE(centered_orbits_see_each_side_in_turn);
}
