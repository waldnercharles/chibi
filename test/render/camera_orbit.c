#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "render/material_raster.h"
#include "render/mesh_projection.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
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
    HEIGHT = 80
};

/* Independent scalar basis oracle. U points toward the eye; D points down
 * the image, so the zero-angle frame is world X/Y/Z respectively. */
static CF_V3 camera_coordinates(render_material_view v, CF_V3 p)
{
    float cy = cosf(v.yaw), sy = sinf(v.yaw), cp = cosf(v.pitch), sp = sinf(v.pitch);
    float x = p.x - v.center_x, y = p.y - v.target_elevation, z = p.z + v.center_y;
    float right = cy * x - sy * z;
    float down = sy * cp * x - sp * y + cy * cp * z;
    float cr = cosf(v.roll), sr = sinf(v.roll);
    return (CF_V3){ cr * right + sr * down,
                    sy * sp * x + cp * y + cy * sp * z,
                    -sr * right + cr * down };
}
static void projected_oracle(render_material_view v, CF_V3 p, CF_V2 *lo, CF_V2 *hi)
{
    float scale = v.focal_distance > 0
                    ? v.focal_distance /
                          fmaxf(RENDER_MESH_NEAR, v.focal_distance - p.y)
                    : 1;
    CF_V2 q = { v.center_x + p.x * scale, -v.center_y + p.z * scale };
    lo->x = fminf(lo->x, q.x);
    lo->y = fminf(lo->y, q.y);
    hi->x = fmaxf(hi->x, q.x);
    hi->y = fmaxf(hi->y, q.y);
}
static bool expected_bounds(render_material_view v, CF_V3 min, CF_V3 max, CF_V2 *lo, CF_V2 *hi)
{
    CF_V3 corners[8];
    *lo = (CF_V2){ INFINITY, INFINITY };
    *hi = (CF_V2){ -INFINITY, -INFINITY };
    float near_height = v.focal_distance - RENDER_MESH_NEAR;
    bool any = false;
    for (int i = 0; i < 8; i++) {
        corners[i] = camera_coordinates(
            v,
            (CF_V3){ i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z }
        );
        if (v.focal_distance <= 0 || corners[i].y <= near_height) {
            projected_oracle(v, corners[i], lo, hi);
            any = true;
        }
    }
    if (v.focal_distance <= 0) {
        return any;
    }
    for (int i = 0; i < 8; i++) {
        for (int axis = 1; axis <= 4; axis *= 2) {
            int j = i ^ axis;
            if (j <= i) {
                continue;
            }
            CF_V3 a = corners[i], b = corners[j];
            if ((a.y < near_height && b.y > near_height) ||
                (b.y < near_height && a.y > near_height)) {
                float t = (near_height - a.y) / (b.y - a.y);
                CF_V3 p = { a.x + t * (b.x - a.x), near_height, a.z + t * (b.z - a.z) };
                projected_oracle(v, p, lo, hi);
                any = true;
            }
        }
    }
    return any;
}
/* An eye circling the source image's vertical world-Z axis keeps its basis,
 * radius and centered target, by a scalar oracle independent of the camera
 * basis implementation. */
TEST_CASE(centered_circles_keep_their_basis_and_radius)
{
    const CF_V3 target = { 13, 7, -9 };
    const float radius = 700;
    for (int step = 0; step <= 8; step++) {
        float theta = step * PI * .25f;
        render_material_view v = { .center_x = target.x,
                                   .center_y = -target.z,
                                   .target_elevation = target.y,
                                   .focal_distance = radius,
                                   .yaw = PI * .5f,
                                   .roll = PI * .5f,
                                   .pitch = theta };
        CF_V3 basis[3], eye = render_mesh_camera_eye(v);
        render_mesh_camera_basis(v, basis);
        CF_V3 expected[] = { { cosf(theta), -sinf(theta), 0 },
                             { sinf(theta), cosf(theta), 0 },
                             { 0, 0, 1 } };
        for (int axis = 0; axis < 3; axis++) {
            REQUIRE(fabsf(basis[axis].x - expected[axis].x) < 1e-6f);
            REQUIRE(fabsf(basis[axis].y - expected[axis].y) < 1e-6f);
            REQUIRE(fabsf(basis[axis].z - expected[axis].z) < 1e-6f);
        }
        REQUIRE(fabsf(eye.x - target.x - radius * sinf(theta)) < .001f);
        REQUIRE(fabsf(eye.y - target.y - radius * cosf(theta)) < .001f);
        REQUIRE(fabsf(eye.z - target.z) < .001f);
        REQUIRE(fabsf(cf_len_v3(cf_sub_v3(eye, target)) - radius) < .001f);
        CF_V3 centered = render_mesh_world_to_camera(v, target);
        REQUIRE(fabsf(centered.x - v.center_x) < 1e-6f && centered.y == 0);
        REQUIRE(fabsf(centered.z + v.center_y) < 1e-6f);
        /* The initial basis matches the source top-down image exactly within
         * trigonometric float precision, without rotating the authored scene. */
        if (!step) {
            CF_V3 p = { 100, 20, -70 };
            CF_V3 aligned = render_mesh_world_to_camera(v, p);
            REQUIRE(fabsf(aligned.x - p.x) < .0001f);
            REQUIRE(fabsf(aligned.y - (p.y - target.y)) < .0001f);
            REQUIRE(fabsf(aligned.z - p.z) < .0001f);
        }
    }
    return true;
}

/* Projected bounds of a box under oblique, rolled and near-plane cameras
 * hold every corner an independent clip keeps, and match the box itself
 * without a camera. */
TEST_CASE(orbit_bounds_hold_every_clipped_corner)
{
    const CF_V3 min = { -7, -2, -5 }, max = { 9, 8, 6 };
    const float angles[][3] = { { 0, 0, 0 },
                                { .7f, .5f, 0 },
                                { -1.3f, -.4f, 0 },
                                { PI * .5f, PI * .5f, 0 },
                                { PI * .5f, .9f, PI * .5f },
                                { 0, 0, .4f } };
    for (int perspective = 0; perspective < 2; perspective++) {
        for (int i = 0; i < 6; i++) {
            render_material_view v = { .center_x = 2,
                                       .center_y = -1,
                                       .target_elevation = 3,
                                       .focal_distance = perspective ? 64 : 0,
                                       .yaw = angles[i][0],
                                       .pitch = angles[i][1],
                                       .roll = angles[i][2] };
            CF_V2 lo, hi, want_lo, want_hi;
            bool visible = render_mesh_project_bounds(v, min, max, &lo, &hi);
            REQUIRE(visible == expected_bounds(v, min, max, &want_lo, &want_hi));
            /* A camera-aligned enclosing AABB may conservatively enlarge an oblique
             * world box, but must never omit the independently clipped box corners. */
            REQUIRE(lo.x <= want_lo.x + .001f && lo.y <= want_lo.y + .001f);
            REQUIRE(hi.x >= want_hi.x - .001f && hi.y >= want_hi.y - .001f);
        }
    }
    render_material_view legacy = { 0 };
    CF_V2 lo, hi;
    REQUIRE(render_mesh_project_bounds(legacy, min, max, &lo, &hi));
    REQUIRE(lo.x == min.x && lo.y == min.z && hi.x == max.x && hi.y == max.z);
    render_material_view near = { .focal_distance = 8,
                                  .yaw = .7f,
                                  .pitch = .5f,
                                  .target_elevation = 3 };
    CF_V2 want_lo, want_hi;
    bool visible = render_mesh_project_bounds(near, min, max, &lo, &hi);
    REQUIRE(visible == expected_bounds(near, min, max, &want_lo, &want_hi));
    REQUIRE(lo.x <= want_lo.x + .001f && lo.y <= want_lo.y + .001f);
    REQUIRE(hi.x >= want_hi.x - .001f && hi.y >= want_hi.y - .001f);
    return true;
}
static node *op(graph *g, const char *name)
{
    return graph_add_node(g, registry_node_by_name(name));
}
static behavior_asset *sphere()
{
    behavior_asset *a = behavior_asset_new("Orbit depth sphere");
    a->domain = BEHAVIOR_DOMAIN_SHAPE;
    a->bounds[0] = a->bounds[1] = 5;
    graph *g = a->graph;
    node *p = op(g, "Shader ObjectPosition"), *length = op(g, "Shader Length"),
         *distance = op(g, "Shader Subtract"),
         *out = shader_graph_add_output(g, true);
    assert(!graph_connect_data(g, p, 0, length, 0));
    assert(!graph_connect_data(g, length, 0, distance, 0));
    pin_value radius = { .type = PIN_F32, .f = 4 };
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
                            .width = shape ? 10 : 8,
                            .depth = shape ? 10 : 8,
                            .height = shape ? 10 : 8,
                            .elevation = 3 };
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
/* Whether the four center pixels show the blue mesh's or the red SDF's
 * scene and albedo, fully covered. */
static bool winners(render_material_raster *r, bool blue)
{
    render_material_textures t = render_material_raster_textures(r);
    CF_Texture targets[] = { t.scene, t.source, t.albedo, t.normal, t.cover };
    float *pixels[5] = { 0 };
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        ok &= read(targets[i], &pixels[i]);
    }
    for (int y = 39; y <= 40 && ok; y++) {
        for (int x = 47; x <= 48; x++) {
            int p = (y * WIDTH + x) * 4;
            ok &= fabsf(pixels[0][p] - (blue ? 0 : 1)) < .005f &&
                  fabsf(pixels[0][p + 2] - (blue ? 1 : 0)) < .005f &&
                  pixels[0][p + 3] == 1;
            ok &= fabsf(pixels[2][p] - (blue ? 0 : 1)) < .005f &&
                  fabsf(pixels[2][p + 2] - (blue ? 1 : 0)) < .005f;
            ok &= pixels[4][p + 1] == 1 && pixels[4][p + 2] == 1;
        }
    }
    for (int i = 0; i < 5; i++) {
        free(pixels[i]);
    }
    return ok;
}
/* A mesh and an SDF swap which one is in front as the camera turns half way
 * around, orthographic and in perspective, whatever their order or hidden
 * surface rejection. */
TEST_CASE(orbits_draw_the_frontmost_surface)
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
    ecs_entity mesh = actor(world, NULL, 0x0000ffff),
               sdf = actor(world, shape->name, 0xff0000ff);
    render_material_raster *r = render_material_raster_new();
    REQUIRE(render_material_raster_resize(r, WIDTH, HEIGHT));
    render_material_raster_set_quality(r, (SurfaceQuality){ .resolution = SURFACE_FULL });
    for (int perspective = 0; perspective < 2; perspective++) {
        for (int angle = 0; angle < 2; angle++) {
            for (int order = 0; order < 2; order++) {
                for (int hidden = 0; hidden < 2; hidden++) {
                    render_material_view view = { .min_x = -48,
                                                  .max_y = 40,
                                                  .width = WIDTH,
                                                  .height = HEIGHT,
                                                  .focal_distance = perspective ? 64 : 0,
                                                  .yaw = angle ? PI * 1.5f : PI * .5f,
                                                  .pitch = PI * .5f,
                                                  .target_elevation = 3 };
                    render_mesh_material_actor actors[] = { { mesh, { 6, 0 } },
                                                            { sdf, { -6, 0 } } };
                    if (order) {
                        render_mesh_material_actor swap = actors[0];
                        actors[0] = actors[1];
                        actors[1] = swap;
                    }
                    render_material_entity layers[] = {
                        { .shape_type = RENDER_SHAPE_MESH },
                        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 } }
                    };
                    /* The independent camera oracle predicts a strictly reversed eye depth. */
                    float mesh_depth = camera_coordinates(view, (CF_V3){ 6, 3, 0 })
                                           .y;
                    float sdf_depth = camera_coordinates(view, (CF_V3){ -6, 3, 0 })
                                          .y;
                    bool blue = mesh_depth > sdf_depth;
                    REQUIRE(blue == !angle);
                    render_material_raster_set_hidden_rejection(r, hidden);
                    cf_app_update(NULL);
                    bool drawn = render_material_raster_draw_mixed(r, layers, 2, view, world, actors, 2, 0);
                    cf_app_draw_onto_screen(false);
                    REQUIRE(drawn);
                    REQUIRE(winners(r, blue));
                }
            }
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

void test_camera_plane();
void test_camera_centered_orbit();
void test_camera_emitter_orbit();

void test_material_cameras()
{
    test_open_hidden_app("material-cameras", WIDTH, HEIGHT);
    test_graph_packs_install();
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(orbit_bounds_hold_every_clipped_corner);
    RUN_TEST_CASE(centered_circles_keep_their_basis_and_radius);
    RUN_TEST_CASE(orbits_draw_the_frontmost_surface);
    test_camera_plane();
    test_camera_centered_orbit();
    test_camera_emitter_orbit();
    pu_clear_setup();
    test_graph_packs_restore();
    cf_destroy_app();
}
