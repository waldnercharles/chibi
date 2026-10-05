#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    WIDTH = 96,
    HEIGHT = 80
};
static float maximum_height_error, maximum_normal_error;

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
    GameSurfaceQuality quality,
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
static bool sphere_pixels(float *scene, float *height, float *normal, float radius, float focal, bool supersampled)
{
    bool ok = true;
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
            maximum_height_error = fmaxf(maximum_height_error, fabsf(height[i + 3] - expected));
            maximum_normal_error = fmaxf(
                maximum_normal_error,
                fmaxf(fabsf(normal[i] - px / radius), fabsf(normal[i + 1] + pz / radius))
            );
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
    return ok;
}
static bool trace_work(render_material_raster *r, ecs_world *world, ecs_entity e, const char *shape_name)
{
    render_material_raster_set_sdf_trace_debug(r, true);
    bool ok = true;
    for (int candidate = 0; ok && candidate < 5; candidate++) {
        GameSurfaceQuality q = { .resolution = GAME_SURFACE_FULL,
                                 .sdf_pixel_error = candidate & 1 ? .25f : 0,
                                 .sdf_normal_mode = candidate == 4 ? GAME_SDF_NORMAL_ANALYTIC
                                                  : candidate & 2
                                                      ? GAME_SDF_NORMAL_TETRAHEDRAL
                                                      : GAME_SDF_NORMAL_CENTRAL };
        float *scene = NULL, *height = NULL, *normal = NULL;
        ok = capture(r, world, e, q, 0, 0, &scene, &height, &normal);
        double evaluations = 0, hits = 0, normal_evaluations = 0;
        if (ok) {
            for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
                evaluations += scene[i] * 128;
                hits += scene[i + 1];
                normal_evaluations += scene[i + 2] * 8;
            }
        }
        // RGBA16F stores exact small integer /128 values in this fixture.
        if (ok) {
            printf(
                "SDF quality work %s %s: visible_hits=%.0f "
                "trace_evaluations=%.0f normal_evaluations=%.0f\n",
                shape_name,
                (
                    const char *[]
                ){ "reference", "adaptive", "normal4", "combined", "analytic" }[candidate],
                hits,
                evaluations,
                normal_evaluations
            );
        }
        ok = ok && hits > 0 && evaluations >= hits;
        if (candidate == 4) {
            ok = ok && normal_evaluations <= hits;
        } else {
            ok = ok && normal_evaluations == hits * (candidate & 2 ? 4 : 6);
        }
        free(scene);
        free(height);
        free(normal);
    }
    render_material_raster_set_sdf_trace_debug(r, false);
    return ok;
}
static bool gradient_fallback(render_material_raster *r, ecs_world *world, ecs_entity e, bool singular)
{
    GameSurfaceQuality q = { .resolution = GAME_SURFACE_FULL,
                             .sdf_normal_mode = GAME_SDF_NORMAL_TETRAHEDRAL };
    float *a = NULL, *ah = NULL, *an = NULL, *b = NULL, *bh = NULL, *bn = NULL;
    render_material_raster_set_sdf_trace_debug(r, true);
    bool ok = capture(r, world, e, q, 0, 0, &a, &ah, &an);
    q.sdf_normal_mode = GAME_SDF_NORMAL_ANALYTIC;
    ok = ok && capture(r, world, e, q, 0, 0, &b, &bh, &bn);
    int checked = 0;
    for (int i = 0; ok && i < WIDTH * HEIGHT * 4; i += 4) {
        if (a[i + 1] == 0 || (singular && i != (40 * WIDTH + 48) * 4)) {
            continue;
        }
        checked++;
        // Unsupported graphs use exactly four distance calls. A singular
        // derivative attempts one gradient, then uses those same four calls.
        ok = a[i + 2] * 8 == 4 && b[i + 2] * 8 == (singular ? 5 : 4) &&
             a[i] == b[i] && a[i + 1] == b[i + 1];
        for (int c = 0; ok && c < 4; c++) {
            ok = ah[i + c] == bh[i + c] && an[i + c] == bn[i + c];
        }
    }
    printf(
        "SDF gradient %s fallback: checked%d %s\n",
        singular ? "singular" : "unsupported",
        checked,
        ok && checked ? "PASS" : "FAIL"
    );
    free(a);
    free(ah);
    free(an);
    free(b);
    free(bh);
    free(bn);
    render_material_raster_set_sdf_trace_debug(r, false);
    return ok && checked;
}
static bool transformed_gradient(render_material_raster *r, ecs_world *world, ecs_entity e)
{
    GameVisualTransform *transform = GAME_GameVisualTransform_get(world, e);
    *transform = (GameVisualTransform){ .scale = 1.4f,
                                        .pitch = .35f,
                                        .yaw = .7f,
                                        .roll = -.2f };
    GAME_GameGeometry3D_get(world, e)->elevation = 3;
    bool ok = true;
    for (int perspective = 0; ok && perspective < 2; perspective++) {
        float *a = NULL, *ah = NULL, *an = NULL, *b = NULL, *bh = NULL, *bn = NULL;
        GameSurfaceQuality q = { .resolution = GAME_SURFACE_FULL };
        float focal = perspective ? 40 : 0;
        ok = capture(r, world, e, q, 0, focal, &a, &ah, &an);
        q.sdf_normal_mode = GAME_SDF_NORMAL_ANALYTIC;
        ok = ok && capture(r, world, e, q, 0, focal, &b, &bh, &bn);
        int covered = 0;
        float max_delta = 0;
        for (int i = 0; ok && i < WIDTH * HEIGHT * 4; i += 4) {
            ok = a[i + 3] == b[i + 3] && ah[i + 3] == bh[i + 3];
            if (a[i + 3] < .99f) {
                continue;
            }
            covered++;
            max_delta = fmaxf(
                max_delta,
                fmaxf(fabsf(an[i] - bn[i]), fabsf(an[i + 1] - bn[i + 1]))
            );
        }
        ok = ok && covered > 20 && max_delta < .02f;
        printf(
            "SDF transformed gradient perspective%d: covered%d "
            "max_normal_delta%g %s\n",
            perspective,
            covered,
            max_delta,
            ok ? "PASS" : "FAIL"
        );
        free(a);
        free(ah);
        free(an);
        free(b);
        free(bh);
        free(bn);
    }
    *transform = (GameVisualTransform){ .scale = 1 };
    GAME_GameGeometry3D_get(world, e)->elevation = 0;
    return ok;
}
bool test_sdf_quality()
{
    maximum_height_error = maximum_normal_error = 0;
    behavior_asset *sphere = shape(false), *ring = shape(true),
                   *tilted = plane(false), *creased = plane(true),
                   *noisy = noise_shape();
    // Pair specialization must exclude unrelated image-compatible shapes.
    render_shader_set selected = { .shape = render_shader_find(sphere->name) };
    char *source = render_shader_glsl(&selected, 2, 0);
    char excluded[64];
    snprintf(excluded, sizeof excluded, "void sg_asset_%d(", render_shader_find(ring->name));
    assert(strstr(source, "#define SG_ANALYTIC_SPHERE 1"));
    assert(!strstr(source, excluded));
    free(source);
    selected.shape = render_shader_find(ring->name);
    source = render_shader_glsl(&selected, 2, 0);
    assert(strstr(source, "#define SG_ANALYTIC_SPHERE 0"));
    assert(strstr(source, "#define SG_ANALYTIC_GRADIENT 1"));
    free(source);
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &GAME_COMPONENT_GameGeometry3D,   &GAME_COMPONENT_GameVisualTransform,
        &GAME_COMPONENT_GameOrientation,  &GAME_COMPONENT_GameMaterial,
        &GAME_COMPONENT_GameShader,       &GAME_COMPONENT_GameSdfSurface3D,
        &GAME_COMPONENT_GameMeshSurface2D
    };
    for (int i = 0; i < 7; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    ecs_entity e = ecs_world_create_entity(world);
    GameGeometry3D bounds = { .primitive = GAME_PRIMITIVE_BOX,
                              .width = 24,
                              .height = 24,
                              .depth = 24 };
    GameVisualTransform transform = { .scale = 1 };
    GameOrientation orientation = { 0 };
    GameMaterial material = { .scene = 0xff0000ff,
                              .albedo = 0xff0000ff,
                              .absorption = 1,
                              .edge_light = 1 };
    GameShader shader = { .shape = sphere->name };
    GameSdfSurface3D enabled = { true };
    GameMeshSurface2D mesh = { false };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, e, GAME_COMPONENT_##type.id, &value, 0) == ECS_OK)
    ADD(GameGeometry3D, bounds);
    ADD(GameVisualTransform, transform);
    ADD(GameOrientation, orientation);
    ADD(GameMaterial, material);
    ADD(GameShader, shader);
    ADD(GameSdfSurface3D, enabled);
    ADD(GameMeshSurface2D, mesh);
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
    render_material_raster *r = render_material_raster_new();
    bool ok = r && render_material_raster_resize(r, WIDTH, HEIGHT);
    for (int mode = 0; ok && mode < 12; mode++) {
        for (int frame = 0; ok && frame < 2; frame++) {
            GameSurfaceQuality q = {
                .resolution = mode & 1 ? GAME_SURFACE_FULL : GAME_SURFACE_SUPERSAMPLED,
                .sdf_pixel_error = mode & 2 ? .25f : 0,
                .sdf_normal_mode = mode >= 8 ? GAME_SDF_NORMAL_ANALYTIC
                                 : mode & 4  ? GAME_SDF_NORMAL_TETRAHEDRAL
                                             : GAME_SDF_NORMAL_CENTRAL
            };
            float *scene = NULL, *height = NULL, *normal = NULL;
            ok = capture(r, world, e, q, frame, 0, &scene, &height, &normal) &&
                 sphere_pixels(scene, height, normal, 10 + .5f * frame, 0, q.resolution == GAME_SURFACE_SUPERSAMPLED);
            free(scene);
            free(height);
            free(normal);
        }
    }
    // Projection and inverse instance scale both change the ray footprint.
    for (int scale = 1; ok && scale <= 2; scale++) {
        GAME_GameVisualTransform_get(world, e)->scale = scale;
        GameSurfaceQuality q = { .resolution = GAME_SURFACE_FULL,
                                 .sdf_pixel_error = .25f,
                                 .sdf_normal_mode = GAME_SDF_NORMAL_ANALYTIC };
        float *scene = NULL, *height = NULL, *normal = NULL;
        ok = capture(r, world, e, q, 0, 40, &scene, &height, &normal) &&
             sphere_pixels(scene, height, normal, 10 * scale, 40, false);
        free(scene);
        free(height);
        free(normal);
    }
    GAME_GameVisualTransform_get(world, e)->scale = 1;
    GAME_GameShader_get(world, e)->shape = ring->name;
    for (int mode = 0; ok && mode < 4; mode++) {
        int resolution = mode & 1;
        float *a = NULL, *ah = NULL, *an = NULL, *b = NULL, *bh = NULL, *bn = NULL;
        GameSurfaceQuality reference = { .resolution = resolution };
        GameSurfaceQuality candidate = {
            .resolution = resolution,
            .sdf_pixel_error = mode & 2 ? 0 : .25f,
            .sdf_normal_mode = mode & 2 ? GAME_SDF_NORMAL_ANALYTIC : GAME_SDF_NORMAL_TETRAHEDRAL
        };
        ok = capture(r, world, e, reference, 0, 0, &a, &ah, &an) &&
             capture(r, world, e, candidate, 0, 0, &b, &bh, &bn);
        int mismatches = 0, covered = 0, partial = 0;
        float max_height_delta = 0, max_normal_delta = 0, max_reference_height = 0;
        if (ok) {
            for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
                if (fabsf(a[i + 3] - b[i + 3]) > .01f) {
                    mismatches++;
                }
                if (b[i + 3] > 0 && b[i + 3] < .9f) {
                    partial++;
                }
                if (b[i + 3] > .9f) {
                    covered++;
                }
                if (a[i + 3] > .9f && b[i + 3] > .9f) {
                    max_height_delta = fmaxf(max_height_delta, fabsf(ah[i + 3] - bh[i + 3]));
                    max_normal_delta = fmaxf(
                        max_normal_delta,
                        fmaxf(fabsf(an[i] - bn[i]), fabsf(an[i + 1] - bn[i + 1]))
                    );
                    max_reference_height = fmaxf(max_reference_height, ah[i + 3]);
                }
                if (a[i + 3] > .9f && b[i + 3] > .9f &&
                    fabsf(ah[i + 3] - bh[i + 3]) > .12f) {
                    fprintf(
                        stderr,
                        "Quality ring height(%d,%d): reference%g candidate%g\n",
                        i / 4 % WIDTH,
                        i / 4 / WIDTH,
                        ah[i + 3],
                        bh[i + 3]
                    );
                    ok = false;
                }
            }
        }
        if (!ok || mismatches || covered <= 20 || b[(40 * WIDTH + 48) * 4 + 3] >= .01f) {
            fprintf(
                stderr,
                "Quality ring resolution%d coverage mismatches%d covered%d "
                "hole%g\n",
                resolution,
                mismatches,
                covered,
                b[(40 * WIDTH + 48) * 4 + 3]
            );
        }
        printf(
            "SDF quality ring mode%d: coverage_mismatches%d "
            "max_height_delta%g max_normal_delta%g max_reference_height%g "
            "analytic_tube_height_bound0.75\n",
            mode,
            mismatches,
            max_height_delta,
            max_normal_delta,
            max_reference_height
        );
        // Independently known torus tube radius bounds every stored height.
        ok = ok && max_reference_height <= .76f;
        for (int i = 0; ok && i < WIDTH * HEIGHT * 4; i += 4) {
            if ((b[i + 3] > 0 && bh[i + 3] > .76f) ||
                (a[i + 3] > 0 && ah[i + 3] > .76f)) {
                ok = false;
            }
        }
        // Thin geometry and central hole must survive, with no added silhouette.
        ok = ok && (resolution == 1 || partial > 0) && mismatches == 0 &&
             covered > 20 && b[(40 * WIDTH + 48) * 4 + 3] < .01f;
        if (mode & 2) {
            ok = ok && max_height_delta == 0 && max_normal_delta < .015f;
        }
        free(a);
        free(ah);
        free(an);
        free(b);
        free(bh);
        free(bn);
    }
    for (int crease = 0; ok && crease < 2; crease++) {
        GAME_GameShader_get(world, e)->shape = crease ? creased->name : tilted->name;
        float *scene = NULL, *height = NULL, *normal = NULL;
        GameSurfaceQuality q = { 0 };
        ok = capture(r, world, e, q, 0, 0, &scene, &height, &normal);
        if (ok) {
            int center = (40 * WIDTH + 48) * 4;
            // A plane resolves to center x=.5; a crease spans opposite normals
            // and retains one actual subpixel face with height2.125.
            float expected = crease ? 2.125f : 2.25f;
            ok = fabsf(height[center + 3] - expected) < .03f &&
                 fabsf(fabsf(normal[center]) - .5f / sqrtf(1.25f)) < .015f;
            if (!crease) {
                for (int x = 42; x < 55; x++) {
                    int i = (40 * WIDTH + x) * 4;
                    if (fabsf(height[i + 3] - (2 + .5f * (x + .5f - 48))) > .03f) {
                        ok = false;
                    }
                }
            }
            printf(
                "SDF quality %s: centerheight%g expected%g normalX%g %s\n",
                crease ? "sharp adjacent planes" : "tilted plane",
                height[center + 3],
                expected,
                normal[center],
                ok ? "PASS" : "FAIL"
            );
        }
        free(scene);
        free(height);
        free(normal);
    }
    if (ok) {
        // A genuine rasterized tilted box top independently validates plane
        // reconstruction, including a nonzero thickness and elevation.
        GAME_GameSdfSurface3D_get(world, e)->enabled = false;
        ((GameMeshSurface2D *)
             ecs_component_data(world, e, GAME_COMPONENT_GameMeshSurface2D.id))
            ->enabled = true;
        GAME_GameGeometry3D_get(world, e)->height = .02f;
        GAME_GameGeometry3D_get(world, e)->elevation = 2;
        GAME_GameVisualTransform_get(world, e)->pitch = atanf(.5f);
        GameSurfaceQuality q = { 0 };
        float *scene = NULL, *height = NULL, *normal = NULL;
        ok = capture(r, world, e, q, 0, 0, &scene, &height, &normal);
        float max_error = 0;
        if (ok) {
            for (int x = 42; x < 55; x++) {
                int i = (40 * WIDTH + x) * 4;
                float expected = 2 + .5f * (x + .5f - 48) + .01f * sqrtf(1.25f);
                max_error = fmaxf(max_error, fabsf(height[i + 3] - expected));
                if (scene[i + 3] < .99f || fabsf(height[i + 3] - expected) > .03f ||
                    fabsf(normal[i] + .5f / sqrtf(1.25f)) > .015f) {
                    ok = false;
                }
            }
        }
        printf("SDF quality tilted raster mesh: max_height_error%g %s\n", max_error, ok ? "PASS" : "FAIL");
        free(scene);
        free(height);
        free(normal);
        GAME_GameSdfSurface3D_get(world, e)->enabled = true;
        ((GameMeshSurface2D *)
             ecs_component_data(world, e, GAME_COMPONENT_GameMeshSurface2D.id))
            ->enabled = false;
        GAME_GameGeometry3D_get(world, e)->height = 24;
        GAME_GameGeometry3D_get(world, e)->elevation = 0;
        GAME_GameVisualTransform_get(world, e)->pitch = 0;
    }
    GAME_GameShader_get(world, e)->shape = ring->name;
    printf("SDF quality analytic sphere: max_height_error%g max_normal_component_error%g\n", maximum_height_error, maximum_normal_error);
    if (ok) {
        ok = trace_work(r, world, e, "thin-ring");
    }
    ok = ok && transformed_gradient(r, world, e);
    GAME_GameShader_get(world, e)->shape = sphere->name;
    if (ok) {
        ok = trace_work(r, world, e, "sphere");
    }
    GAME_GameShader_get(world, e)->shape = noisy->name;
    ok = ok && gradient_fallback(r, world, e, false);
    GAME_GameShader_get(world, e)->shape = creased->name;
    ok = ok && gradient_fallback(r, world, e, true);
    if (!ok) {
        fprintf(stderr, "SDF quality analytic height, normal, time or thin-ring comparison failed\n");
    }
    render_material_raster_free(r);
    behavior_asset_free(noisy);
    ecs_world_free(world);
    behavior_asset_free(tilted);
    behavior_asset_free(creased);
    behavior_asset_free(ring);
    behavior_asset_free(sphere);
    return ok;
}
int sdf_quality_run(const char *program, bool gles)
{
    CF_Result result = cf_make_app(
        "SDF quality regression",
        0,
        0,
        0,
        WIDTH,
        HEIGHT,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT |
            (gles ? CF_APP_OPTIONS_GFX_OPENGL_BIT : 0),
        program
    );
    if (cf_is_error(result)) {
        return 1;
    }
    app_scene_renderer_initialize();
    bool ok = test_sdf_quality();
    cf_destroy_app();
    return !ok;
}
