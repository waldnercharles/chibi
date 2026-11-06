#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "render/shader_program.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDF surfaces the material raster traces: heights and normals against
 * analytic shapes at every quality, thin geometry, planes and creases, trace
 * work, and analytic gradients against numeric ones. Each case draws one SDF
 * actor, a sphere unless it changes its shape. */

enum
{
    WIDTH = 96,
    HEIGHT = 80,
    CENTER = (40 * WIDTH + 48) * 4
};
static behavior_asset *sphere, *ring, *tilted, *creased, *noisy;
static ecs_world *world;
static ecs_entity e;
static render_material_raster *r;

static node *op(graph *g, const char *name)
{
    return graph_add_node(g, registry_node_by_name(name));
}
static void wire(graph *g, node *a, node *b, int input)
{
    assert(!graph_connect_data(g, a, 0, b, input));
}
static void constant(graph *g, node *n, int input, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(g, n, 0, input, &v));
}
static behavior_asset *shape(bool ring)
{
    behavior_asset *a = behavior_asset_new(ring ? "Quality thin ring" : "Quality changing sphere");
    a->domain = BEHAVIOR_DOMAIN_SHAPE;
    a->bounds[0] = a->bounds[1] = 12;
    graph *g = a->graph;
    node *p = op(g, "Shader ObjectPosition"), *length = op(g, "Shader Length");
    if (ring) {
        node *x = op(g, "Shader X"), *y = op(g, "Shader Y"), *z = op(g, "Shader Z"),
             *xz = op(g, "Shader Vector2"), *radial = op(g, "Shader Length"),
             *major = op(g, "Shader Subtract"), *cross = op(g, "Shader Vector2");
        wire(g, p, x, 0);
        wire(g, p, y, 0);
        wire(g, p, z, 0);
        wire(g, x, xz, 0);
        wire(g, z, xz, 1);
        wire(g, xz, radial, 0);
        wire(g, radial, major, 0);
        constant(g, major, 1, 8);
        wire(g, major, cross, 0);
        wire(g, y, cross, 1);
        wire(g, cross, length, 0);
    } else {
        wire(g, p, length, 0);
    }
    node *subtract = op(g, "Shader Subtract");
    wire(g, length, subtract, 0);
    if (ring) {
        constant(g, subtract, 1, .75f);
    } else {
        node *time = op(g, "Shader Time"), *rate = op(g, "Shader Multiply"),
             *radius = op(g, "Shader Add");
        wire(g, time, rate, 0);
        constant(g, rate, 1, .5f);
        wire(g, rate, radius, 0);
        constant(g, radius, 1, 10);
        wire(g, radius, subtract, 1);
    }
    wire(g, subtract, shader_graph_add_output(g, true), 0);
    shader_error error = { 0 };
    assert(render_shader_upsert(a, &error));
    return a;
}
static behavior_asset *plane(bool crease)
{
    behavior_asset *a = behavior_asset_new(
        crease ? "Quality sharp adjacent planes" : "Quality tilted plane"
    );
    a->domain = BEHAVIOR_DOMAIN_SHAPE;
    a->bounds[0] = a->bounds[1] = 12;
    graph *g = a->graph;
    node *p = op(g, "Shader ObjectPosition"), *x = op(g, "Shader X"),
         *y = op(g, "Shader Y");
    wire(g, p, x, 0);
    wire(g, p, y, 0);
    if (crease) {
        node *shift = op(g, "Shader Subtract"), *absolute = op(g, "Shader Abs");
        wire(g, x, shift, 0);
        constant(g, shift, 1, .5f);
        wire(g, shift, absolute, 0);
        x = absolute;
    }
    node *slope = op(g, "Shader Multiply"), *height = op(g, "Shader Add"),
         *distance = op(g, "Shader Subtract"), *normalized = op(g, "Shader Divide");
    wire(g, x, slope, 0);
    constant(g, slope, 1, .5f);
    wire(g, slope, height, 0);
    constant(g, height, 1, 2);
    wire(g, y, distance, 0);
    wire(g, height, distance, 1);
    wire(g, distance, normalized, 0);
    constant(g, normalized, 1, sqrtf(1.25f));
    wire(g, normalized, shader_graph_add_output(g, true), 0);
    shader_error error = { 0 };
    assert(render_shader_upsert(a, &error));
    return a;
}
static behavior_asset *noise_shape()
{
    behavior_asset *a = behavior_asset_new("Quality unsupported gradient noise");
    a->domain = BEHAVIOR_DOMAIN_SHAPE;
    a->bounds[0] = a->bounds[1] = 12;
    graph *g = a->graph;
    node *p = op(g, "Shader ObjectPosition"), *length = op(g, "Shader Length"),
         *noise = op(g, "Shader Noise"), *strength = op(g, "Shader Multiply"),
         *sum = op(g, "Shader Add"), *distance = op(g, "Shader Subtract");
    wire(g, p, length, 0);
    wire(g, p, noise, 0);
    constant(g, noise, 1, .15f);
    wire(g, noise, strength, 0);
    constant(g, strength, 1, .05f);
    wire(g, length, sum, 0);
    wire(g, strength, sum, 1);
    wire(g, sum, distance, 0);
    constant(g, distance, 1, 9);
    wire(g, distance, shader_graph_add_output(g, true), 0);
    shader_error error = { 0 };
    assert(render_shader_upsert(a, &error));
    return a;
}
static float half(uint16_t v)
{
    int exponent = (v >> 10) & 31, m = v & 1023;
    float f = exponent == 31 ? (m ? NAN : INFINITY)
            : exponent       ? ldexpf(1 + m / 1024.0f, exponent - 15)
                             : ldexpf((float)m, -24);
    return v & 0x8000 ? -f : f;
}
static bool read_texture(CF_Texture texture, float **out)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback r = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (r.id && !cf_readback_ready(r) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    int count = WIDTH * HEIGHT * 4, bytes = count * (int)sizeof(uint16_t);
    uint16_t *data = xmalloc(bytes);
    *out = xmalloc(count * sizeof(float));
    bool ok = r.id && cf_readback_ready(r) && cf_readback_size(r) == bytes &&
              cf_readback_data(r, data, bytes) == bytes;
    if (ok) {
        for (int i = 0; i < count; i++) {
            (*out)[i] = half(data[i]);
            if (!isfinite((*out)[i])) {
                ok = false;
            }
        }
    }
    if (r.id) {
        cf_destroy_readback(r);
    }
    cf_destroy_canvas(canvas);
    free(data);
    return ok;
}
static bool capture(
    render_material_raster *r,
    ecs_world *w,
    ecs_entity e,
    SurfaceQuality quality,
    float time,
    float focal,
    float **scene,
    float **albedo,
    float **normal
)
{
    render_material_raster_set_quality(r, quality);
    render_mesh_material_actor actor = { e, { 0, 0 } };
    render_material_entity layer = { .shape_type = RENDER_SHAPE_MESH };
    render_material_view view = { -48, 40, WIDTH, HEIGHT, .focal_distance = focal };
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(r, &layer, 1, view, w, &actor, 1, time);
    cf_app_draw_onto_screen(false);
    render_material_textures t = render_material_raster_textures(r);
    return ok && read_texture(t.scene, scene) &&
           read_texture(t.albedo, albedo) && read_texture(t.normal, normal);
}
/* Whether a sphere of `radius` at the view's center covers its pixels with
 * its analytic height and normal, through a lens of `focal` length or
 * orthographic at 0. */
static bool sphere_pixels(float *scene, float *height, float *normal, float radius, float focal, bool supersampled)
{
    for (int y = 32; y < 48; y++) {
        for (int x = 40; x < 56; x++) {
            float px = x + .5f - 48, pz = y + .5f - 40, square = px * px + pz * pz;
            if (square > (radius - 1) * (radius - 1)) {
                continue;
            }
            int i = (y * WIDTH + x) * 4;
            if (supersampled) {
                // Curved surfaces retain the actual frontmost of four samples,
                // rather than extrapolating an unstable tangent to the center.
                px -= copysignf(.25f, px);
                pz -= copysignf(.25f, pz);
                square = px * px + pz * pz;
            }
            float expected = sqrtf(radius * radius - square);
            if (focal > 0) {
                float norm = sqrtf(square + focal * focal), dy = -focal / norm;
                float b = focal * dy,
                      t = -b - sqrtf(b * b - focal * focal + radius * radius);
                expected = focal + dy * t;
                px *= t / norm;
                pz *= t / norm;
            }
            if (fabsf(scene[i + 3] - 1) > .01f ||
                fabsf(height[i + 3] - expected) > .04f ||
                fabsf(normal[i] - px / radius) > .01f ||
                fabsf(normal[i + 1] + pz / radius) > .01f) {
                fprintf(
                    stderr,
                    "Quality sphere pixel(%d,%d) radius%g focal%g: coverage%g "
                    "height%g expected%g normal(%g,%g) expected(%g,%g)\n",
                    x,
                    y,
                    radius,
                    focal,
                    scene[i + 3],
                    height[i + 3],
                    expected,
                    normal[i],
                    normal[i + 1],
                    px / radius,
                    -pz / radius
                );
                return false;
            }
        }
    }
    return true;
}
/* Draws the actor at `quality`, reading its scene, albedo and normal
 * targets into arrays the caller frees. */
static bool draw(SurfaceQuality quality, float time, float focal, float **scene, float **albedo, float **normal)
{
    return capture(r, world, e, quality, time, focal, scene, albedo, normal);
}

static void setup()
{
    sphere = shape(false);
    ring = shape(true);
    tilted = plane(false);
    creased = plane(true);
    noisy = noise_shape();
    world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Geometry3D,   &COMPONENT_VisualTransform,
        &COMPONENT_Orientation,  &COMPONENT_Material,
        &COMPONENT_Shader,       &COMPONENT_SdfSurface3D,
        &COMPONENT_MeshSurface2D
    };
    for (int i = 0; i < 7; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    e = ecs_world_create_entity(world);
    Geometry3D bounds = { .primitive = PRIMITIVE_BOX, .width = 24, .height = 24, .depth = 24 };
    VisualTransform transform = { .scale = 1 };
    Orientation orientation = { 0 };
    Material material = { .scene = 0xff0000ff,
                          .albedo = 0xff0000ff,
                          .absorption = 1,
                          .edge_light = 1 };
    Shader shader = { .shape = sphere->name };
    SdfSurface3D enabled = { true };
    MeshSurface2D mesh = { false };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, e, COMPONENT_##type.id, &value, 0) == ECS_OK)
    ADD(Geometry3D, bounds);
    ADD(VisualTransform, transform);
    ADD(Orientation, orientation);
    ADD(Material, material);
    ADD(Shader, shader);
    ADD(SdfSurface3D, enabled);
    ADD(MeshSurface2D, mesh);
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
    r = render_material_raster_new();
    assert(render_material_raster_resize(r, WIDTH, HEIGHT));
}

static void teardown()
{
    render_material_raster_free(r);
    ecs_world_free(world);
    behavior_asset_free(noisy);
    behavior_asset_free(creased);
    behavior_asset_free(tilted);
    behavior_asset_free(ring);
    behavior_asset_free(sphere);
    authoring_shader_catalog_clear();
}

/* A program specialized for one shape leaves out other shapes' code, and
 * knows whether its shape is an analytic sphere or has analytic gradients. */
TEST_CASE(programs_specialize_for_their_shape)
{
    render_shader_set selected = { .shape = render_shader_find(sphere->name) };
    char *source = render_shader_glsl(&selected, 2, 0);
    char excluded[64];
    snprintf(excluded, sizeof excluded, "void sg_asset_%d(", render_shader_find(ring->name));
    bool sphere_only = strstr(source, "#define SG_ANALYTIC_SPHERE 1") &&
                       !strstr(source, excluded);
    free(source);
    REQUIRE(sphere_only);
    selected.shape = render_shader_find(ring->name);
    source = render_shader_glsl(&selected, 2, 0);
    bool gradient = strstr(source, "#define SG_ANALYTIC_SPHERE 0") &&
                    strstr(source, "#define SG_ANALYTIC_GRADIENT 1");
    free(source);
    REQUIRE(gradient);
    return true;
}

/* A sphere whose radius grows with time draws its analytic heights and
 * normals at every resolution, pixel error and normal mode, and through
 * perspective at two instance scales. */
TEST_CASE(spheres_draw_their_analytic_surface)
{
    for (int mode = 0; mode < 12; mode++) {
        for (int frame = 0; frame < 2; frame++) {
            SurfaceQuality q = { .resolution = mode & 1 ? SURFACE_FULL : SURFACE_SUPERSAMPLED,
                                 .sdf_pixel_error = mode & 2 ? .25f : 0,
                                 .sdf_normal_mode = mode >= 8 ? SDF_NORMAL_ANALYTIC
                                                  : mode & 4 ? SDF_NORMAL_TETRAHEDRAL
                                                             : SDF_NORMAL_CENTRAL };
            float *scene = NULL, *height = NULL, *normal = NULL;
            bool drawn = draw(q, frame, 0, &scene, &height, &normal) &&
                         sphere_pixels(scene, height, normal, 10 + .5f * frame, 0, q.resolution == SURFACE_SUPERSAMPLED);
            free(scene);
            free(height);
            free(normal);
            REQUIRE(drawn);
        }
    }
    /* Projection and inverse instance scale both change the ray footprint. */
    for (int scale = 1; scale <= 2; scale++) {
        VisualTransform_get(world, e)->scale = scale;
        SurfaceQuality q = { .resolution = SURFACE_FULL,
                             .sdf_pixel_error = .25f,
                             .sdf_normal_mode = SDF_NORMAL_ANALYTIC };
        float *scene = NULL, *height = NULL, *normal = NULL;
        bool drawn = draw(q, 0, 40, &scene, &height, &normal) &&
                     sphere_pixels(scene, height, normal, 10 * scale, 40, false);
        free(scene);
        free(height);
        free(normal);
        REQUIRE(drawn);
    }
    return true;
}

/* A thin ring keeps the reference's coverage, its central hole and heights
 * within its tube radius at every candidate quality. */
TEST_CASE(thin_rings_keep_their_coverage_and_hole)
{
    Shader_get(world, e)->shape = ring->name;
    for (int mode = 0; mode < 4; mode++) {
        int resolution = mode & 1;
        float *a = NULL, *ah = NULL, *an = NULL, *b = NULL, *bh = NULL, *bn = NULL;
        SurfaceQuality reference = { .resolution = resolution };
        SurfaceQuality candidate = { .resolution = resolution,
                                     .sdf_pixel_error = mode & 2 ? 0 : .25f,
                                     .sdf_normal_mode = mode & 2
                                                          ? SDF_NORMAL_ANALYTIC
                                                          : SDF_NORMAL_TETRAHEDRAL };
        REQUIRE(draw(reference, 0, 0, &a, &ah, &an));
        REQUIRE(draw(candidate, 0, 0, &b, &bh, &bn));
        int mismatches = 0, covered = 0, partial = 0;
        float max_height_delta = 0, max_normal_delta = 0, max_height = 0;
        for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
            mismatches += fabsf(a[i + 3] - b[i + 3]) > .01f;
            partial += b[i + 3] > 0 && b[i + 3] < .9f;
            covered += b[i + 3] > .9f;
            if (a[i + 3] > .9f && b[i + 3] > .9f) {
                max_height_delta = fmaxf(max_height_delta, fabsf(ah[i + 3] - bh[i + 3]));
                max_normal_delta = fmaxf(
                    max_normal_delta,
                    fmaxf(fabsf(an[i] - bn[i]), fabsf(an[i + 1] - bn[i + 1]))
                );
            }
            /* The torus tube radius bounds every stored height. */
            max_height = fmaxf(max_height, b[i + 3] > 0 ? bh[i + 3] : 0);
            max_height = fmaxf(max_height, a[i + 3] > 0 ? ah[i + 3] : 0);
        }
        float hole = b[CENTER + 3];
        free(a);
        free(ah);
        free(an);
        free(b);
        free(bh);
        free(bn);
        REQUIRE(max_height <= .76f && max_height_delta <= .12f);
        /* Thin geometry and the central hole survive, with no added silhouette. */
        REQUIRE(mismatches == 0 && covered > 20 && hole < .01f);
        REQUIRE(resolution == 1 || partial > 0);
        REQUIRE(!(mode & 2) || (max_height_delta == 0 && max_normal_delta < .015f));
    }
    return true;
}

/* A tilted plane resolves its height along a row, and a crease between two
 * planes keeps one real subpixel face rather than their average. */
TEST_CASE(planes_and_creases_keep_real_faces)
{
    for (int crease = 0; crease < 2; crease++) {
        Shader_get(world, e)->shape = crease ? creased->name : tilted->name;
        float *scene = NULL, *height = NULL, *normal = NULL;
        REQUIRE(draw((SurfaceQuality){ 0 }, 0, 0, &scene, &height, &normal));
        float expected = crease ? 2.125f : 2.25f;
        bool faces = fabsf(height[CENTER + 3] - expected) < .03f &&
                     fabsf(fabsf(normal[CENTER]) - .5f / sqrtf(1.25f)) < .015f;
        for (int x = 42; !crease && x < 55; x++) {
            int i = (40 * WIDTH + x) * 4;
            faces &= fabsf(height[i + 3] - (2 + .5f * (x + .5f - 48))) <= .03f;
        }
        free(scene);
        free(height);
        free(normal);
        REQUIRE(faces);
    }
    return true;
}

/* A rasterized tilted box top, thin and raised, matches the same plane. */
TEST_CASE(tilted_raster_meshes_match_their_plane)
{
    SdfSurface3D_get(world, e)->enabled = false;
    ((MeshSurface2D *)ecs_component_data(world, e, COMPONENT_MeshSurface2D.id))->enabled = true;
    Geometry3D_get(world, e)->height = .02f;
    Geometry3D_get(world, e)->elevation = 2;
    VisualTransform_get(world, e)->pitch = atanf(.5f);
    float *scene = NULL, *height = NULL, *normal = NULL;
    REQUIRE(draw((SurfaceQuality){ 0 }, 0, 0, &scene, &height, &normal));
    bool plane_drawn = true;
    for (int x = 42; x < 55; x++) {
        int i = (40 * WIDTH + x) * 4;
        float expected = 2 + .5f * (x + .5f - 48) + .01f * sqrtf(1.25f);
        plane_drawn &= scene[i + 3] >= .99f &&
                       fabsf(height[i + 3] - expected) <= .03f &&
                       fabsf(normal[i] + .5f / sqrtf(1.25f)) <= .015f;
    }
    free(scene);
    free(height);
    free(normal);
    REQUIRE(plane_drawn);
    return true;
}

/* With trace debugging, each pixel counts its distance and normal
 * evaluations: adaptive steps never trace fewer than one per hit, central
 * normals take six, tetrahedral four and analytic at most one. */
static bool trace_work()
{
    render_material_raster_set_sdf_trace_debug(r, true);
    bool counted = true;
    for (int candidate = 0; counted && candidate < 5; candidate++) {
        SurfaceQuality q = { .resolution = SURFACE_FULL,
                             .sdf_pixel_error = candidate & 1 ? .25f : 0,
                             .sdf_normal_mode = candidate == 4 ? SDF_NORMAL_ANALYTIC
                                              : candidate & 2 ? SDF_NORMAL_TETRAHEDRAL
                                                              : SDF_NORMAL_CENTRAL };
        float *scene = NULL, *height = NULL, *normal = NULL;
        counted = draw(q, 0, 0, &scene, &height, &normal);
        double evaluations = 0, hits = 0, normal_evaluations = 0;
        /* RGBA16F stores these small integers divided by 128 exactly. */
        for (int i = 0; counted && i < WIDTH * HEIGHT * 4; i += 4) {
            evaluations += scene[i] * 128;
            hits += scene[i + 1];
            normal_evaluations += scene[i + 2] * 8;
        }
        counted = counted && hits > 0 && evaluations >= hits &&
                  (candidate == 4
                       ? normal_evaluations <= hits
                       : normal_evaluations == hits * (candidate & 2 ? 4 : 6));
        free(scene);
        free(height);
        free(normal);
    }
    render_material_raster_set_sdf_trace_debug(r, false);
    return counted;
}

TEST_CASE(traces_count_their_evaluations)
{
    Shader_get(world, e)->shape = ring->name;
    REQUIRE(trace_work());
    Shader_get(world, e)->shape = sphere->name;
    REQUIRE(trace_work());
    return true;
}

/* A ring's analytic gradient draws the same coverage and heights as its
 * numeric normals, and nearly the same normals, under a scaled, turned and
 * raised transform, orthographic and in perspective. */
TEST_CASE(analytic_gradients_follow_transforms)
{
    Shader_get(world, e)->shape = ring->name;
    *VisualTransform_get(world, e) = (VisualTransform){ .scale = 1.4f,
                                                        .pitch = .35f,
                                                        .yaw = .7f,
                                                        .roll = -.2f };
    Geometry3D_get(world, e)->elevation = 3;
    for (int perspective = 0; perspective < 2; perspective++) {
        float *a = NULL, *ah = NULL, *an = NULL, *b = NULL, *bh = NULL, *bn = NULL;
        SurfaceQuality q = { .resolution = SURFACE_FULL };
        float focal = perspective ? 40 : 0;
        REQUIRE(draw(q, 0, focal, &a, &ah, &an));
        q.sdf_normal_mode = SDF_NORMAL_ANALYTIC;
        REQUIRE(draw(q, 0, focal, &b, &bh, &bn));
        bool same = true;
        int covered = 0;
        float max_delta = 0;
        for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
            same &= a[i + 3] == b[i + 3] && ah[i + 3] == bh[i + 3];
            if (a[i + 3] < .99f) {
                continue;
            }
            covered++;
            max_delta = fmaxf(
                max_delta,
                fmaxf(fabsf(an[i] - bn[i]), fabsf(an[i + 1] - bn[i + 1]))
            );
        }
        free(a);
        free(ah);
        free(an);
        free(b);
        free(bh);
        free(bn);
        REQUIRE(same && covered > 20 && max_delta < .02f);
    }
    return true;
}

/* Whether analytic normals of the actor's shape fall back to the four
 * distance calls tetrahedral normals make, with the same pixels. A singular
 * derivative first attempts one gradient; only the center pixel is
 * checked for it. */
static bool falls_back(bool singular)
{
    SurfaceQuality q = { .resolution = SURFACE_FULL,
                         .sdf_normal_mode = SDF_NORMAL_TETRAHEDRAL };
    float *a = NULL, *ah = NULL, *an = NULL, *b = NULL, *bh = NULL, *bn = NULL;
    render_material_raster_set_sdf_trace_debug(r, true);
    bool same = draw(q, 0, 0, &a, &ah, &an);
    q.sdf_normal_mode = SDF_NORMAL_ANALYTIC;
    same = same && draw(q, 0, 0, &b, &bh, &bn);
    int checked = 0;
    for (int i = 0; same && i < WIDTH * HEIGHT * 4; i += 4) {
        if (a[i + 1] == 0 || (singular && i != CENTER)) {
            continue;
        }
        checked++;
        same = a[i + 2] * 8 == 4 && b[i + 2] * 8 == (singular ? 5 : 4) &&
               a[i] == b[i] && a[i + 1] == b[i + 1];
        for (int c = 0; same && c < 4; c++) {
            same = ah[i + c] == bh[i + c] && an[i + c] == bn[i + c];
        }
    }
    free(a);
    free(ah);
    free(an);
    free(b);
    free(bh);
    free(bn);
    render_material_raster_set_sdf_trace_debug(r, false);
    return same && checked;
}

TEST_CASE(unsupported_gradients_fall_back)
{
    Shader_get(world, e)->shape = noisy->name;
    REQUIRE(falls_back(false));
    Shader_get(world, e)->shape = creased->name;
    REQUIRE(falls_back(true));
    return true;
}

void test_sdf_spatial();

void test_sdf_quality()
{
    test_open_hidden_app("sdf-quality", WIDTH, HEIGHT);
    test_graph_packs_install();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(programs_specialize_for_their_shape);
    RUN_TEST_CASE(spheres_draw_their_analytic_surface);
    RUN_TEST_CASE(thin_rings_keep_their_coverage_and_hole);
    RUN_TEST_CASE(planes_and_creases_keep_real_faces);
    RUN_TEST_CASE(tilted_raster_meshes_match_their_plane);
    RUN_TEST_CASE(traces_count_their_evaluations);
    RUN_TEST_CASE(analytic_gradients_follow_transforms);
    RUN_TEST_CASE(unsupported_gradients_fall_back);
    pu_clear_setup();
    test_sdf_spatial();
    test_graph_packs_restore();
    cf_destroy_app();
}
