#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/mesh_material.h"
#include "render/mesh_surface.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
    WIDTH = 96,
    HEIGHT = 80
};

static node *operation(graph *g, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(g, desc);
}

static void connect(graph *g, node *from, node *to, int input)
{
    assert(!graph_connect_data(g, from, 0, to, input));
}

static void scalar(graph *g, node *n, int input, float value)
{
    pin_value v = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(g, n, 0, input, &v));
}

/* Independently authored distance fields: a sphere and an XZ-plane torus.
 * They exercise every spatial axis without adding native primitive shortcuts. */
static behavior_asset *shape(bool torus)
{
    behavior_asset *asset = behavior_asset_new(
        torus ? "SDF regression torus" : "SDF regression sphere"
    );
    asset->domain = BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 12;
    graph *g = asset->graph;
    node *p = operation(g, "Shader ObjectPosition");
    node *distance = operation(g, "Shader Length");
    if (torus) {
        node *x = operation(g, "Shader X"), *y = operation(g, "Shader Y"),
             *z = operation(g, "Shader Z"), *xz = operation(g, "Shader Vector2"),
             *radial = operation(g, "Shader Length"),
             *major = operation(g, "Shader Subtract"),
             *cross = operation(g, "Shader Vector2");
        connect(g, p, x, 0);
        connect(g, p, y, 0);
        connect(g, p, z, 0);
        connect(g, x, xz, 0);
        connect(g, z, xz, 1);
        connect(g, xz, radial, 0);
        connect(g, radial, major, 0);
        scalar(g, major, 1, 8);
        connect(g, major, cross, 0);
        connect(g, y, cross, 1);
        connect(g, cross, distance, 0);
    } else {
        connect(g, p, distance, 0);
    }
    node *radius = operation(g, "Shader Subtract");
    connect(g, distance, radius, 0);
    scalar(g, radius, 1, torus ? 3 : 10);
    connect(g, radius, shader_graph_add_output(g, true), 0);
    shader_error error = { 0 };
    bool valid = render_shader_upsert(asset, &error);
    if (!valid) {
        fprintf(
            stderr,
            "SDF fixture %s, node %llu: %s\n",
            asset->name,
            (unsigned long long)error.node,
            error.message
        );
    }
    assert(valid);
    return asset;
}

static behavior_asset *surface()
{
    behavior_asset *asset = behavior_asset_new("SDF hit-position material regression");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *position = operation(g, "Shader ObjectPosition"),
         *height = operation(g, "Shader Y"),
         *factor = operation(g, "Shader Divide"),
         *color = operation(g, "Shader Vector3");
    connect(g, position, height, 0);
    connect(g, height, factor, 0);
    scalar(g, factor, 1, 20);
    connect(g, factor, color, 0);
    connect(g, color, shader_graph_add_output(g, false), 0);
    shader_error error = { 0 };
    bool valid = render_shader_upsert(asset, &error);
    if (!valid) {
        fprintf(stderr, "SDF surface fixture: %s\n", error.message);
    }
    assert(valid);
    return asset;
}

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

static bool near(float actual, float expected, float tolerance, const char *label)
{
    if (fabsf(actual - expected) <= tolerance) {
        return true;
    }
    fprintf(stderr, "SDF %s: got %g, expected %g (+/- %g)\n", label, actual, expected, tolerance);
    return false;
}

static bool draw(
    render_material_raster *raster,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int count,
    const render_material_entity *layers,
    render_material_view view
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(raster, layers, count, view, world, actors, count, 0);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "SDF draw: %s\n", render_material_raster_error(raster));
    }
    return ok;
}

static bool pixel(
    render_material_raster *raster,
    int x,
    int y,
    float red,
    float blue,
    float height,
    float nx,
    float ny
)
{
    render_material_textures t = render_material_raster_textures(raster);
    float *scene = NULL, *albedo = NULL, *normal = NULL;
    bool ok = read_pixels(t.scene, &scene) && read_pixels(t.albedo, &albedo) &&
              read_pixels(t.normal, &normal);
    int p = (y * WIDTH + x) * 4;
    if (ok) {
        ok = near(scene[p], red, .03f, "red") &&
             near(scene[p + 2], blue, .03f, "blue") &&
             near(scene[p + 3], 1, .03f, "coverage") &&
             near(albedo[p + 3], height, .15f, "height") &&
             near(normal[p], nx, .04f, "normal X") &&
             near(normal[p + 1], ny, .04f, "normal Y");
    }
    free(scene);
    free(albedo);
    free(normal);
    return ok;
}

static bool coverage(render_material_raster *raster, int x, int y, float expected)
{
    float *pixels = NULL;
    bool ok = read_pixels(render_material_raster_textures(raster).scene, &pixels);
    if (ok) {
        ok = near(pixels[(y * WIDTH + x) * 4 + 3], expected, .03f, "silhouette/hole");
    }
    free(pixels);
    return ok;
}

static bool area(render_material_raster *raster, float expected)
{
    float *pixels = NULL;
    bool ok = read_pixels(render_material_raster_textures(raster).scene, &pixels);
    float sum = 0;
    if (ok) {
        for (int p = 0; p < WIDTH * HEIGHT; p++) {
            sum += pixels[p * 4 + 3];
        }
        ok = near(sum, expected, expected * .06f + 2, "analytic projected sphere area");
    }
    free(pixels);
    return ok;
}

static ecs_entity actor(ecs_world *world, bool sdf, const char *name)
{
    ecs_entity entity = ecs_world_create_entity(world);
    GameGeometry3D bounds = { .primitive = GAME_PRIMITIVE_BOX,
                              .width = 24,
                              .depth = 24,
                              .height = 24 };
    GameVisualTransform transform = { .scale = 1 };
    GameOrientation orientation = { 0 };
    GameMaterial material = { .scene = sdf ? 0xff0000ff : 0x0000ffff,
                              .albedo = sdf ? 0xff0000ff : 0x0000ffff,
                              .absorption = 1,
                              .edge_light = 1 };
    GameShader shader = { .shape = name };
    GameSdfSurface3D sdf_surface = { true };
    GameMeshSurface2D mesh_surface = { true };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, entity, GAME_COMPONENT_##type.id, &(value), 0) == ECS_OK)
    ADD(GameGeometry3D, bounds);
    ADD(GameVisualTransform, transform);
    ADD(GameOrientation, orientation);
    ADD(GameMaterial, material);
    if (sdf) {
        ADD(GameShader, shader);
        ADD(GameSdfSurface3D, sdf_surface);
    } else {
        ADD(GameMeshSurface2D, mesh_surface);
    }
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static node *parameter(graph *g, const char *name, int slot, int width, float value)
{
    node *n = operation(g, "Shader Parameter");
    assert(!graph_set_node_reference(g, n, slot, name));
    if (width == 3) {
        assert(!graph_set_pin_type(g, n, 1, 0, PIN_STRUCT, "ShaderVec3"));
        float rgb[] = { value, value, value };
        pin_value v = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = rgb };
        assert(!graph_set_pin_default(g, n, 1, 0, &v));
        return n;
    }
    pin_value v = { .type = PIN_F32, .f = value };
    assert(!graph_set_pin_default(g, n, 1, 0, &v));
    return n;
}

static behavior_asset *batch_graph(bool material)
{
    behavior_asset *asset = behavior_asset_new(
        material ? "SDF batch parameter material" : "SDF batch parameter shape"
    );
    asset->domain = material ? BEHAVIOR_DOMAIN_MATERIAL : BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 12;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, !material);
    if (material) {
        node *tint = parameter(g, "Tint", 0, 3, .5f),
             *glow = parameter(g, "Glow", 1, 1, 2),
             *roughness = parameter(g, "Roughness", 2, 1, .5f),
             *emission = operation(g, "Shader Multiply");
        connect(g, tint, out, 0);
        connect(g, tint, emission, 0);
        connect(g, glow, emission, 1);
        connect(g, emission, out, 1);
        connect(g, roughness, out, 2);
    } else {
        node *p = operation(g, "Shader ObjectPosition"),
             *length = operation(g, "Shader Length"),
             *radius = parameter(g, "Radius", 0, 1, 6),
             *distance = operation(g, "Shader Subtract");
        connect(g, p, length, 0);
        connect(g, length, distance, 0);
        connect(g, radius, distance, 1);
        connect(g, distance, out, 0);
    }
    shader_error error = { 0 };
    bool valid = render_shader_upsert(asset, &error);
    if (!valid) {
        fprintf(stderr, "SDF batch graph: %s\n", error.message);
    }
    assert(valid);
    return asset;
}

typedef struct
{
    float *data[5];
    int width, height;
} sdf_snapshot;

static void snapshot_free(sdf_snapshot *snapshot)
{
    for (int i = 0; i < 5; i++) {
        free(snapshot->data[i]);
    }
    memset(snapshot, 0, sizeof *snapshot);
}

static bool snapshot_read(render_mesh_material *pass, sdf_snapshot *snapshot)
{
    render_mesh_material_textures t = render_mesh_material_get_textures(pass);
    CF_Texture textures[] = { t.scene, t.emission, t.absorption, t.albedo, t.normal };
    int active[2], capacity[2];
    render_mesh_material_extent(pass, active, capacity);
    snapshot->width = active[0];
    snapshot->height = active[1];
    bool ok = true;
    for (int i = 0; i < 5 && ok; i++) {
        float *pixels = NULL;
        ok = read_sized_pixels(textures[i], capacity[0], capacity[1], &pixels);
        snapshot->data[i] = xmalloc((size_t)active[0] * active[1] * 4 * sizeof(float));
        for (int y = 0; y < active[1] && ok; y++) {
            memcpy(
                snapshot->data[i] + (size_t)y * active[0] * 4,
                pixels + (size_t)y * capacity[0] * 4,
                active[0] * 4 * sizeof(float)
            );
        }
        free(pixels);
    }
    return ok;
}

static bool snapshot_matches(const sdf_snapshot *reference, const sdf_snapshot *actual, const char *label)
{
    if (reference->width != actual->width || reference->height != actual->height) {
        fprintf(stderr, "SDF batch %s: target dimensions changed\n", label);
        return false;
    }
    const char *channels[] = { "scene", "emission", "absorption", "albedo/height", "normal/gloss" };
    int count = reference->width * reference->height * 4;
    for (int target = 0; target < 5; target++) {
        for (int i = 0; i < count; i++) {
            float a = reference->data[target][i], b = actual->data[target][i];
            if (fabsf(a - b) <= .003f + fabsf(a) * .002f) {
                continue;
            }
            fprintf(
                stderr,
                "SDF batch %s %s pixel%d channel%d: reference%g actual%g\n",
                label,
                channels[target],
                i / 4,
                i % 4,
                a,
                b
            );
            return false;
        }
    }
    return true;
}

static bool batch_draw(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int count,
    render_material_view view,
    int limit,
    sdf_snapshot *snapshot,
    render_mesh_material_work *work
)
{
    render_mesh_material_set_sdf_batch_limit(pass, limit);
    cf_app_update(NULL);
    bool ok = render_mesh_material_draw(pass, world, actors, count, view, .375f);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "SDF batch draw: %s\n", render_mesh_material_error(pass));
        return false;
    }
    *work = render_mesh_material_last_work(pass);
    return snapshot_read(pass, snapshot);
}

static bool batch_parity(
    render_mesh_material *pass,
    ecs_world *world,
    const render_mesh_material_actor *actors,
    int count,
    int sdf_count,
    render_material_view view,
    int limit,
    const char *label,
    bool require_reduction
)
{
    sdf_snapshot reference = { 0 }, batched = { 0 };
    render_mesh_material_work before = { 0 }, after = { 0 };
    bool ok = batch_draw(pass, world, actors, count, view, 1, &reference, &before) &&
              batch_draw(pass, world, actors, count, view, limit, &batched, &after) &&
              snapshot_matches(&reference, &batched, label);
    if (ok) {
        float coverage = 0;
        for (int p = 0; p < reference.width * reference.height; p++) {
            coverage += reference.data[0][p * 4 + 3];
        }
        ok = coverage > 20 && before.sdf_instances == sdf_count &&
             after.sdf_instances == sdf_count &&
             before.sdf_draw_calls == sdf_count && after.sdf_draw_calls > 0 &&
             before.sdf_uploads == 1 && after.sdf_uploads == 1 &&
             before.sdf_upload_bytes == after.sdf_upload_bytes &&
             after.sdf_upload_bytes > 0 && before.sdf_parameter_uploads == 1 &&
             after.sdf_parameter_uploads == 1 &&
             before.sdf_parameter_upload_bytes == after.sdf_parameter_upload_bytes &&
             after.sdf_parameter_upload_bytes ==
                 (size_t)after.sdf_parameter_rows * 1024 &&
             after.sdf_pair_probes >= (size_t)sdf_count &&
             after.sdf_pair_probes < (size_t)sdf_count * 4;
        if (require_reduction) {
            ok &= after.sdf_draw_calls < before.sdf_draw_calls / 2;
        }
        if (!ok) {
            fprintf(
                stderr,
                "SDF batch %s work/coverage failed: covered%g instances%d/%d "
                "draws%d/%d uploads%d bytes%zu\n",
                label,
                coverage,
                before.sdf_instances,
                after.sdf_instances,
                before.sdf_draw_calls,
                after.sdf_draw_calls,
                after.sdf_uploads,
                after.sdf_upload_bytes
            );
        }
    }
    printf(
        "SDF batch %s %s: %d -> %d SDF draws, %d instances; five targets\n",
        label,
        ok ? "PASS" : "FAIL",
        before.sdf_draw_calls,
        after.sdf_draw_calls,
        after.sdf_instances
    );
    snapshot_free(&reference);
    snapshot_free(&batched);
    return ok;
}

static bool raster_snapshot(
    render_material_raster *raster,
    ecs_world *world,
    const render_material_entity *layers,
    int layer_count,
    const render_mesh_material_actor *actors,
    int actor_count,
    int limit,
    sdf_snapshot *snapshot
)
{
    render_material_raster_set_sdf_batch_limit(raster, limit);
    cf_app_update(NULL);
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    bool ok = render_material_raster_draw_mixed(raster, layers, layer_count, view, world, actors, actor_count, 0);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "SDF layer parity: %s\n", render_material_raster_error(raster));
        return false;
    }
    render_material_textures t = render_material_raster_textures(raster);
    CF_Texture textures[] = { t.scene, t.source, t.albedo, t.normal, t.cover };
    snapshot->width = WIDTH;
    snapshot->height = HEIGHT;
    for (int i = 0; i < 5 && ok; i++) {
        ok = read_pixels(textures[i], &snapshot->data[i]);
    }
    return ok;
}

static bool batch_layers(render_material_raster *raster, ecs_world *world, const char *shape_name)
{
    render_mesh_material_actor actors[4];
    for (int i = 0; i < 4; i++) {
        ecs_entity entity = actor(world, i != 2, shape_name);
        actors[i] = (render_mesh_material_actor){ entity,
                                                  { i == 0   ? -10
                                                    : i == 1 ? 10
                                                             : 0,
                                                    0 } };
        GAME_GameMaterial_get(world, entity)
            ->scene = GAME_GameMaterial_get(world, entity)->albedo = i == 3 ? 0xff00ffff
                                                                   : i == 2
                                                                       ? 0x0000ffff
                                                                       : 0xff0000ff;
        if (i == 2) {
            GAME_GameGeometry3D_get(world, entity)->height = 2;
            GAME_GameGeometry3D_get(world, entity)->elevation = -12;
        }
        if (i == 3) {
            GAME_GameGeometry3D_get(world, entity)->elevation = 12;
        }
    }
    render_material_entity layers[] = {
        { .shape_type = RENDER_SHAPE_MESH, .points = { 0 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 2 } },
        { .shape_type = RENDER_SHAPE_BOX,
          .inv_rot = { 1, 0, 0, 1 },
          .extent = { 18, 18 },
          .points = { 14, 14 },
          .wobble = { 0, 0, 0, .5f },
          .scene = { 0, 1, 0, 1 },
          .albedo = { 0, 1, 0, 1 },
          .absorption = { 1, 1, 1, 1 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 3 }, .wobble = { 0, 0, 0, 1 } }
    };
    sdf_snapshot reference = { 0 }, batched = { 0 };
    bool ok = raster_snapshot(raster, world, layers, 5, actors, 4, 1, &reference) &&
              raster_snapshot(raster, world, layers, 5, actors, 4, 256, &batched) &&
              snapshot_matches(&reference, &batched, "authored layers and mixed mesh");
    if (ok) {
        render_mesh_material_work work = render_material_raster_mesh_work(raster);
        int center = (40 * WIDTH + 48) * 4, side = (40 * WIDTH + 35) * 4;
        ok = work.sdf_uploads == 2 && work.sdf_instances == 3 &&
             near(batched.data[0][center], 1, .03f, "higher SDF layer red") &&
             near(batched.data[0][center + 2], 1, .03f, "higher SDF layer blue") &&
             near(batched.data[0][side], 0, .03f, "2D layer red") &&
             near(batched.data[0][side + 1], 1, .03f, "2D layer green") &&
             near(batched.data[2][side + 3], 0, .03f, "2D layer replaces height");
    }
    snapshot_free(&reference);
    snapshot_free(&batched);
    render_material_raster_set_sdf_batch_limit(raster, 0);
    printf("SDF batch authored layers %s: five flattened targets and independent layer winners\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool test_sdf_batches(ecs_world *world)
{
    enum
    {
        COUNT = 270,
        VISIBLE = 269,
        PARAMETERS = 18
    };
    behavior_asset *shape_asset = batch_graph(false),
                   *material_asset = batch_graph(true);
    render_mesh_material_actor *actors = xmalloc((COUNT + 2) * sizeof *actors);
    for (int i = 0; i < COUNT; i++) {
        ecs_entity entity = actor(world, true, shape_asset->name);
        actors[i] = (render_mesh_material_actor){ entity,
                                                  { -38 + (i % 20) * 4.0f,
                                                    -26 + (i / 20) * 4.0f } };
        *GAME_GameVisualTransform_get(world, entity) = (GameVisualTransform){
            .scale = i == 267 ? 0 : .3f + (i % 5) * .04f,
            .pitch = i * .13f,
            .yaw = i * .19f,
            .roll = i * .07f
        };
        GAME_GameOrientation_get(world, entity)->radians = i * .11f;
        *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){
            .width = 16 + i % 4,
            .depth = 18 + i % 5,
            .height = 20 + i % 3,
            .elevation = (i % 7) * .25f - .75f
        };
        uint32_t color = ((uint32_t)(64 + i * 31 % 192) << 24) |
                         ((uint32_t)(64 + i * 19 % 192) << 16) |
                         ((uint32_t)(64 + i * 43 % 192) << 8) | 255;
        GameMaterial *m = GAME_GameMaterial_get(world, entity);
        m->scene = m->albedo = m->emission_color = color;
        m->emission_intensity = .5f + i % 3;
        m->emission_multiplier = 1;
        m->absorption = .2f + (i % 4) * .2f;
        m->edge_light = .4f + (i % 3) * .2f;
    }
    render_mesh_material *pass = render_mesh_material_new();
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    bool ok = pass && render_mesh_material_resize(pass, WIDTH, HEIGHT);
    // More than one default256-instance chunk, all five raw MRT targets.
    if (ok) {
        ok = batch_parity(pass, world, actors, COUNT, VISIBLE, view, 256, "default chunks", true);
    }
    if (ok) {
        ok = batch_parity(pass, world, actors, COUNT, VISIBLE, view, 7, "small chunks", true);
    }
    // Interleaving ordinary meshes must flush without changing geometric order.
    memmove(actors + 101, actors + 100, (COUNT - 100) * sizeof *actors);
    ecs_entity mesh = actor(world, false, NULL);
    GAME_GameGeometry3D_get(world, mesh)->height = 2;
    GAME_GameGeometry3D_get(world, mesh)->elevation = 5;
    actors[100] = (render_mesh_material_actor){ mesh, { -10, 0 } };
    if (ok) {
        ok = batch_parity(pass, world, actors, COUNT + 1, VISIBLE, view, 256, "ordinary mesh interleave", true);
    }
    // Different resolved parameters now share a graph-pair batch. Each value
    // must still affect its actor rather than leaking a neighbor's table row.
    char parameters[PARAMETERS][160];
    for (int i = 0; i < PARAMETERS; i++) {
        ecs_entity entity = actor(world, true, shape_asset->name);
        actors[i] = (render_mesh_material_actor){ entity,
                                                  { -30 + (i % 6) * 12.0f,
                                                    -16 + (i / 6) * 16.0f } };
        int group = i / 3;
        snprintf(
            parameters[i],
            sizeof parameters[i],
            "Radius %g; (array)Tint %g %g %g; Glow %g; Roughness %g;",
            3.0f + group * .3f,
            .15f + group * .1f,
            .7f - group * .08f,
            .2f + group * .04f,
            1.0f + group * .2f,
            .1f + group * .12f
        );
        GameShader *binding = GAME_GameShader_get(world, entity);
        binding->material = material_asset->name;
        binding->parameters = parameters[i];
    }
    if (ok) {
        ok = batch_parity(pass, world, actors, PARAMETERS, PARAMETERS, view, 256, "entity shape/material parameters", true);
    }
    if (ok) {
        sdf_snapshot values = { 0 };
        render_mesh_material_work work;
        ok = batch_draw(pass, world, actors, PARAMETERS, view, 256, &values, &work);
        ok = ok && work.sdf_draw_calls == 1 &&
             work.sdf_parameter_rows == PARAMETERS / 3 &&
             work.sdf_parameter_upload_bytes == (size_t)(PARAMETERS / 3) * 1024;
        float rect[4];
        render_mesh_material_rect(pass, rect);
        for (int i = 0; i < PARAMETERS && ok; i++) {
            int group = i / 3;
            int x = (int)(2 * (actors[i].position.x - view.min_x - rect[0])) + 1;
            int y = (int)(2 * (view.max_y + actors[i].position.y - rect[1])) + 1;
            int p = (y * values.width + x) * 4;
            float tint[] = { .15f + group * .1f, .7f - group * .08f, .2f + group * .04f };
            for (int c = 0; c < 3 && ok; c++) {
                ok = near(values.data[0][p + c], tint[c], .003f, "entity material Tint") &&
                     near(values.data[1][p + c], tint[c] * (1 + group * .2f), .005f, "entity material Glow");
            }
            if (ok) {
                ok = near(values.data[4][p + 3], .9f - group * .12f, .003f, "entity material roughness");
            }
            float covered = 0;
            for (int dy = -10; dy <= 10; dy++) {
                for (int dx = -10; dx <= 10; dx++) {
                    covered += values.data[0][((y + dy) * values.width + x + dx) * 4 + 3];
                }
            }
            float radius = 3 + group * .3f;
            if (ok) {
                ok = near(covered, 4 * 3.14159265f * radius * radius, 4 + radius * radius * .65f, "entity shape Radius area");
            }
        }
        if (ok) {
            // The selected detail graph must keep entity-bound Radius/Tint values,
            // even when the primary shape graph is completely different.
            for (int i = 0; i < PARAMETERS; i++) {
                GAME_GameShader_get(world, actors[i].entity)
                    ->shape = "SDF regression torus";
                actors[i].sdf_shape = shape_asset->name;
            }
            sdf_snapshot selected = { 0 };
            ok = batch_draw(pass, world, actors, PARAMETERS, view, 256, &selected, &work) &&
                 snapshot_matches(&values, &selected, "detail graph live parameters") &&
                 work.sdf_lod_instances == PARAMETERS &&
                 work.sdf_program_prepares == 1;
            snapshot_free(&selected);
            for (int i = 0; i < PARAMETERS; i++) {
                GAME_GameShader_get(world, actors[i].entity)
                    ->shape = shape_asset->name;
                actors[i].sdf_shape = NULL;
            }
        }
        snapshot_free(&values);
    }
    // Reuse the pass after a mesh-only frame, then change record order, offsets,
    // transforms and visibility. Old staged slices must not survive the frame.
    if (ok) {
        render_mesh_material_actor mesh_only = { mesh, { 0, 0 } };
        sdf_snapshot ordinary = { 0 };
        render_mesh_material_work work;
        ok = batch_draw(pass, world, &mesh_only, 1, view, 256, &ordinary, &work) &&
             work.sdf_instances == 0 && work.sdf_draw_calls == 0 &&
             work.sdf_uploads == 0 && work.sdf_upload_bytes == 0;
        snapshot_free(&ordinary);
        int order[] = { 17, 0, 9, 3, 12, 6 };
        render_mesh_material_actor changed[7];
        for (int i = 0; i < 6; i++) {
            changed[i + (i >= 2)] = actors[order[i]];
            changed[i + (i >= 2)].position.x += i % 2 ? 5 : -4;
            changed[i + (i >= 2)].position.y += i - 2;
            GameVisualTransform *
                transform = GAME_GameVisualTransform_get(world, actors[order[i]].entity);
            transform->scale = i == 3 ? 0 : .7f + .05f * i;
            transform->yaw = .23f * i;
        }
        changed[2] = mesh_only;
        if (ok) {
            ok = batch_parity(pass, world, changed, 7, 5, view, 256, "reused frame slices", false);
        }
    }
    // Identical surfaces at identical depth: LESS preserves the first caller.
    for (int i = 0; i < 2; i++) {
        ecs_entity entity = actor(world, true, shape_asset->name);
        actors[i] = (render_mesh_material_actor){ entity, { 0, 0 } };
        GAME_GameMaterial_get(world, entity)
            ->scene = GAME_GameMaterial_get(world, entity)->albedo = i ? 0x0000ffff
                                                                       : 0xff0000ff;
    }
    for (int reverse = 0; reverse < 2 && ok; reverse++) {
        if (reverse) {
            render_mesh_material_actor first = actors[0];
            actors[0] = actors[1];
            actors[1] = first;
        }
        ok = batch_parity(pass, world, actors, 2, 2, view, 256, "equal depth tie", false);
        sdf_snapshot tie = { 0 };
        render_mesh_material_work work;
        if (ok) {
            ok = batch_draw(pass, world, actors, 2, view, 256, &tie, &work);
        }
        float red = 0, blue = 0;
        if (ok) {
            for (int p = 0; p < tie.width * tie.height; p++) {
                red += tie.data[0][p * 4];
                blue += tie.data[0][p * 4 + 2];
            }
            ok = reverse ? blue > 20 && red < .01f : red > 20 && blue < .01f;
            if (!ok) {
                fprintf(stderr, "SDF batch tie lost first-caller material: reverse%d red%g blue%g\n", reverse, red, blue);
            }
        }
        snapshot_free(&tie);
    }
    render_mesh_material_free(pass);
    behavior_asset_free(shape_asset);
    behavior_asset_free(material_asset);
    free(actors);
    return ok;
}

static bool active_scratch_reuse(ecs_world *world, ecs_entity entity)
{
    render_material_raster *reused = render_material_raster_new(),
                           *fresh = render_material_raster_new();
    bool ok = reused && fresh &&
              render_material_raster_resize(reused, WIDTH, HEIGHT) &&
              render_material_raster_resize(fresh, WIDTH, HEIGHT);
    /* Keep one scratch group so this fixture exercises retained large capacity;
     * disjoint-group allocation has separate spatial-scratch coverage. */
    render_material_raster_set_spatial_scratch(reused, false);
    render_material_raster_set_spatial_scratch(fresh, false);
    render_mesh_material_actor actors[] = { { entity, { -30, 0 } },
                                            { entity, { 30, 0 } } };
    render_material_entity layers[] = {
        { .shape_type = RENDER_SHAPE_MESH, .points = { 0 } },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 } }
    };
    sdf_snapshot large = { 0 }, actual = { 0 }, reference = { 0 };
    render_mesh_material_work before = { 0 }, after = { 0 }, clean = { 0 };
    if (ok) {
        ok = raster_snapshot(reused, world, layers, 2, actors, 2, 0, &large);
        before = render_material_raster_mesh_work(reused);
        actors[0].position = (GameTransform){ 0 };
        ok = ok && raster_snapshot(reused, world, layers, 1, actors, 1, 0, &actual) &&
             raster_snapshot(fresh, world, layers, 1, actors, 1, 0, &reference);
        after = render_material_raster_mesh_work(reused);
        clean = render_material_raster_mesh_work(fresh);
        ok = ok &&
             snapshot_matches(&reference, &actual, "large then small active scratch") &&
             after.surface_pixels == clean.surface_pixels &&
             after.surface_pixels < before.surface_pixels &&
             after.surface_capacity_pixels == before.surface_capacity_pixels &&
             after.surface_capacity_pixels > clean.surface_capacity_pixels;
    }
    if (!ok) {
        fprintf(
            stderr,
            "SDF active scratch reuse failed: active%d/%d/%d "
            "capacity%d/%d/%d\n",
            before.surface_pixels,
            after.surface_pixels,
            clean.surface_pixels,
            before.surface_capacity_pixels,
            after.surface_capacity_pixels,
            clean.surface_capacity_pixels
        );
    }
    snapshot_free(&large);
    snapshot_free(&actual);
    snapshot_free(&reference);
    render_material_raster_free(reused);
    render_material_raster_free(fresh);
    return ok;
}

static bool indexed_graph_pairs(ecs_world *world)
{
    enum
    {
        PAIRS = 16,
        COUNT = 64
    };
    char names[PAIRS][48];
    for (int i = 0; i < PAIRS; i++) {
        snprintf(names[i], sizeof names[i], "SDF indexed pair %d", i);
        behavior_asset *asset = shape(false);
        char *original = asset->name;
        asset->name = names[i];
        shader_error error = { 0 };
        assert(render_shader_upsert(asset, &error));
        asset->name = original;
        behavior_asset_free(asset);
    }
    render_mesh_material_actor actors[COUNT];
    for (int i = 0; i < COUNT; i++) {
        ecs_entity entity = actor(world, true, names[i % PAIRS]);
        GAME_GameVisualTransform_get(world, entity)->scale = .25f;
        actors[i] = (render_mesh_material_actor){ entity,
                                                  { -35 + (i % 8) * 10,
                                                    -30 + (i / 8) * 8 } };
    }
    render_mesh_material *pass = render_mesh_material_new();
    bool ok = pass && render_mesh_material_resize(pass, WIDTH, HEIGHT);
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    sdf_snapshot reference = { 0 }, actual = { 0 };
    render_mesh_material_work before = { 0 }, after = { 0 };
    ok = ok && batch_draw(pass, world, actors, COUNT, view, 1, &reference, &before) &&
         batch_draw(pass, world, actors, COUNT, view, 0, &actual, &after) &&
         snapshot_matches(&reference, &actual, "indexed interleaved graph pairs") &&
         after.sdf_program_prepares == PAIRS && after.sdf_pair_probes >= COUNT &&
         after.sdf_pair_probes < COUNT * 4 && after.sdf_parameter_rows == 1 &&
         after.sdf_parameter_upload_bytes == 1024 && after.sdf_draw_calls == COUNT;
    if (!ok) {
        fprintf(
            stderr,
            "SDF indexed graph pairs failed: prepares%d probes%zu rows%d\n",
            after.sdf_program_prepares,
            after.sdf_pair_probes,
            after.sdf_parameter_rows
        );
    }
    snapshot_free(&reference);
    snapshot_free(&actual);
    render_mesh_material_free(pass);
    return ok;
}

static bool surface_storage_lifetime(ecs_world *world, ecs_entity entity)
{
    char root[] = "/tmp/chibi-sdf-surface-XXXXXX", path[256], source[128],
         error[256] = { 0 };
    assert(mkdtemp(root));
    const char *leaf = strrchr(root, '/') + 1;
    snprintf(path, sizeof path, "%s/%s.mesh.json", root, leaf);
    snprintf(source, sizeof source, "content/%s.mesh.json", leaf);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("{\"version\":1,\"vertices\":[[-0.5,0,-0.5,0,1,0,0,0],[0.5,0,-0.5,0,1,0,1,0],[0,0,0.5,0,1,0,0.5,1]]}", file);
    assert(!fclose(file));
    assert(!cf_is_error(cf_fs_mount(root, "/content", true)));
    GameSdfSurface3D saved_sdf = *GAME_GameSdfSurface3D_get(world, entity);
    GameVisualTransform saved_pose = *GAME_GameVisualTransform_get(world, entity);
    GameMeshAsset imported = { .source = source };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMeshAsset.id, &imported, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    render_mesh_surface surface = { 0 };
    GameTransform position = { 0 };
    GAME_GameSdfSurface3D_get(world, entity)->enabled = false;
    bool ok = render_mesh_surface_build(world, entity, &position, &surface, error, sizeof error);
    render_mesh_surface_part *storage = surface.parts;
    const render_mesh_asset *asset = render_mesh_asset_load(source, error, sizeof error);
    ok = ok && asset && surface.parts[0].borrowed &&
         surface.parts[0].vertices == asset->vertices;
    GAME_GameMeshAsset_get(world, entity)->source = NULL;
    ok = ok &&
         render_mesh_surface_build(world, entity, &position, &surface, error, sizeof error) &&
         surface.parts == storage && !surface.parts[0].borrowed &&
         render_mesh_surface_vertex_count(&surface.parts[0]) == 36;
    GAME_GameSdfSurface3D_get(world, entity)->enabled = true;
    ok = ok &&
         render_mesh_surface_build(world, entity, &position, &surface, error, sizeof error) &&
         surface.parts == storage && surface.parts[0].borrowed &&
         surface.parts[0].vertex_count == 36;
    GAME_GameVisualTransform_get(world, entity)->scale = 0;
    ok = ok &&
         render_mesh_surface_build(world, entity, &position, &surface, error, sizeof error) &&
         surface.parts == storage && !acount(surface.parts);
    *GAME_GameVisualTransform_get(world, entity) = saved_pose;
    *GAME_GameSdfSurface3D_get(world, entity) = saved_sdf;
    ok = ok &&
         render_mesh_surface_build(world, entity, &position, &surface, error, sizeof error) &&
         surface.parts == storage && surface.parts[0].borrowed &&
         acount(asset->vertices) == 3 &&
         near(asset->vertices[0].position.x, -.5f, 0, "borrowed imported storage survives");
    render_mesh_surface_free(&surface);
    assert(!cf_is_error(cf_fs_dismount(root)));
    assert(!unlink(path));
    assert(!rmdir(root));
    if (!ok) {
        fprintf(stderr, "SDF surface storage switching failed: %s\n", error);
    }
    return ok;
}

static bool prepared_surfaces(ecs_world *world, ecs_entity entity)
{
    render_mesh_surface first = { 0 }, second = { 0 };
    char error[256] = { 0 };
    GameSdfSurface3D detail = { .enabled = true,
                                .low_detail_shape = "test detail",
                                .low_detail_pixels = 32 };
    bool thresholds = render_mesh_surface_lod_shape(&detail, 31, false) ==
                          detail.low_detail_shape &&
                      render_mesh_surface_lod_shape(&detail, 32, false) ==
                          detail.low_detail_shape &&
                      !render_mesh_surface_lod_shape(&detail, 33, false) &&
                      !render_mesh_surface_lod_shape(&detail, 16, true);
    GameVisualTransform saved = *GAME_GameVisualTransform_get(world, entity);
    GameGeometry3D bounds = *GAME_GameGeometry3D_get(world, entity);
    *GAME_GameVisualTransform_get(world, entity) = (GameVisualTransform){
        .scale = 1.7f,
        .pitch = .43f,
        .yaw = -.71f,
        .roll = .29f
    };
    *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){ .width = 24,
                                                                .height = 28,
                                                                .depth = 32,
                                                                .elevation = 7 };
    GameTransform position = { 3, -4 };
    bool ok = surface_storage_lifetime(world, entity) && thresholds &&
              render_mesh_surface_build(world, entity, &position, &first, error, sizeof error) &&
              render_mesh_surface_build(world, entity, &position, &second, error, sizeof error);
    if (ok) {
        const render_mesh_surface_part *part = &first.parts[0];
        CF_M4x4 model = render_arena_visual_model(&part->visual);
        CF_V3 center = cf_m4_transform_point(model, (CF_V3){ 0 });
        CF_V3 extent = { 0 };
        for (int axis = 0; axis < 3; axis++) {
            CF_V3 unit = { axis == 0, axis == 1, axis == 2 };
            CF_V3 edge = cf_sub_v3(cf_m4_transform_point(model, unit), center);
            extent.x += fabsf(edge.x) * .5f;
            extent.y += fabsf(edge.y) * .5f;
            extent.z += fabsf(edge.z) * .5f;
        }
        ok = part->borrowed && part->vertices == second.parts[0].vertices &&
             render_mesh_surface_vertex_count(part) == 36 &&
             near(first.min.x, center.x - extent.x, .0001f, "rotated SDF min X") &&
             near(first.min.y, center.y - extent.y, .0001f, "rotated SDF min height") &&
             near(first.min.z, center.z - extent.z, .0001f, "rotated SDF min Z") &&
             near(first.max.x, center.x + extent.x, .0001f, "rotated SDF max X") &&
             near(first.max.y, center.y + extent.y, .0001f, "rotated SDF max height") &&
             near(first.max.z, center.z + extent.z, .0001f, "rotated SDF max Z");
    }
    render_mesh_surface_free(&first);
    *GAME_GameVisualTransform_get(world, entity) = saved;
    *GAME_GameGeometry3D_get(world, entity) = bounds;
    render_mesh_material *pass = render_mesh_material_new();
    ok = ok && pass && render_mesh_material_resize(pass, WIDTH, HEIGHT);
    render_mesh_material_actor actors[] = { { entity, { 0, 0 } },
                                            { entity, { 9, -3 } } };
    sdf_snapshot direct = { 0 }, retained = { 0 };
    render_mesh_material_work before = { 0 }, after = { 0 };
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    if (ok) {
        ok = batch_draw(pass, world, actors, 2, view, 0, &direct, &before) &&
             render_mesh_surface_build(world, entity, &actors[0].position, &first, error, sizeof error) &&
             render_mesh_surface_build(world, entity, &actors[1].position, &second, error, sizeof error);
        actors[0].surface = &first;
        actors[1].surface = &second;
        ok = ok && batch_draw(pass, world, actors, 2, view, 0, &retained, &after) &&
             snapshot_matches(&direct, &retained, "retained CPU preparation") &&
             before.surface_builds == 2 && before.surface_reuses == 0 &&
             after.surface_builds == 0 && after.surface_reuses == 2 &&
             before.sdf_program_prepares == 1 && after.sdf_program_prepares == 1;
    }
    snapshot_free(&direct);
    snapshot_free(&retained);
    render_mesh_material_free(pass);
    render_mesh_surface_free(&first);
    render_mesh_surface_free(&second);
    if (!ok) {
        fprintf(stderr, "SDF retained CPU preparation failed: %s\n", error);
    }
    return ok;
}

bool test_sdf_material()
{
    behavior_asset *sphere = shape(false), *torus = shape(true),
                   *material = surface();
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &GAME_COMPONENT_GameGeometry3D,    &GAME_COMPONENT_GameVisualTransform,
        &GAME_COMPONENT_GameOrientation,   &GAME_COMPONENT_GameMaterial,
        &GAME_COMPONENT_GameShader,        &GAME_COMPONENT_GameSdfSurface3D,
        &GAME_COMPONENT_GameMeshSurface2D, &GAME_COMPONENT_GameMeshParts,
        &GAME_COMPONENT_GameMeshAsset
    };
    for (int i = 0; i < (int)(sizeof components / sizeof *components); i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    ecs_entity solid = actor(world, true, sphere->name);
    render_mesh_material_actor actors[] = { { solid, { 0, 0 } } };
    render_material_entity layers[] = { { .shape_type = RENDER_SHAPE_MESH } };
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    render_material_raster *raster = render_material_raster_new();
    bool ok = raster && render_material_raster_resize(raster, WIDTH, HEIGHT) &&
              prepared_surfaces(world, solid);
    if (ok) {
        // Curved geometry retains the frontmost actual quarter-pixel sample.
        // Shaded color still averages all four samples independently.
        ok = draw(raster, world, actors, 1, layers, view) &&
             pixel(raster, 48, 40, 1, 0, sqrtf(99.875f), .025f, -.025f) &&
             pixel(raster, 54, 40, 1, 0, sqrtf(60.875f), .625f, -.025f) &&
             coverage(raster, 59, 40, 0) && area(raster, 3.14159265f * 100);
        // The material must sample the first hit, rather than its bounding proxy.
        GAME_GameShader_get(world, solid)->material = material->name;
        if (ok) {
            ok = draw(raster, world, actors, 1, layers, view) &&
                 pixel(raster, 48, 40, sqrtf(99.5f) / 20, 0, sqrtf(99.875f), .025f, -.025f) &&
                 pixel(raster, 54, 40, sqrtf(57.5f) / 20, 0, sqrtf(60.875f), .625f, -.025f);
        }
        GAME_GameShader_get(world, solid)->material = NULL;
    }
    // The torus must reveal the background through its actual 3D hole.
    GAME_GameShader_get(world, solid)->shape = torus->name;
    if (ok) {
        ok = draw(raster, world, actors, 1, layers, view) &&
             coverage(raster, 48, 40, 0) && coverage(raster, 56, 40, 1) &&
             coverage(raster, 61, 40, 0);
    }
    // Rotate its thin axis into the screen: a face-on ring becomes edge-on.
    GAME_GameVisualTransform_get(world, solid)->pitch = 1.5707963268f;
    if (ok) {
        ok = draw(raster, world, actors, 1, layers, view) &&
             coverage(raster, 48, 40, 1) && coverage(raster, 54, 40, 0) &&
             coverage(raster, 48, 48, 1);
    }
    GAME_GameVisualTransform_get(world, solid)->pitch = 0;
    GAME_GameShader_get(world, solid)->shape = sphere->name;
    // Translated/padded crop and a scaled bound must preserve all sphere edges.
    GAME_GameVisualTransform_get(world, solid)->scale = 1.5f;
    GAME_GameGeometry3D_get(world, solid)->elevation = -12;
    actors[0].position = (GameTransform){ 200, -120 };
    view.min_x += 200;
    view.max_y += 120;
    if (ok) {
        ok = draw(raster, world, actors, 1, layers, view) &&
             pixel(raster, 48, 40, 1, 0, -12 + sqrtf(224.875f), .25f / 15, -.25f / 15) &&
             coverage(raster, 61, 40, 1) && coverage(raster, 64, 40, 0);
    }
    *GAME_GameVisualTransform_get(world, solid) = (GameVisualTransform){ .scale = 1 };
    actors[0].position = (GameTransform){ 0, 0 };
    view = (render_material_view){ -48, 40, WIDTH, HEIGHT, .focal_distance = 40 };
    GAME_GameGeometry3D_get(world, solid)->elevation = 0;
    if (ok) {
        ok = draw(raster, world, actors, 1, layers, view) &&
             area(raster, 3.14159265f * 100 * 1600 / 1500) &&
             coverage(raster, 57, 40, 1);
    }
    GAME_GameGeometry3D_get(world, solid)->elevation = 15;
    if (ok) {
        ok = draw(raster, world, actors, 1, layers, view) &&
             area(raster, 3.14159265f * 100 * 1600 / 525) &&
             coverage(raster, 63, 40, 1);
    }
    // Mesh and SDF first hits must share geometric depth in either caller order.
    ecs_entity mesh = actor(world, false, NULL);
    GameGeometry3D *box = GAME_GameGeometry3D_get(world, mesh);
    box->height = 2;
    render_mesh_material_actor mixed[] = { actors[0], { mesh, { 0, 0 } } };
    render_material_entity mixed_layers[] = { layers[0],
                                              { .shape_type = RENDER_SHAPE_MESH,
                                                .points = { 1 } } };
    view = (render_material_view){ -48, 40, WIDTH, HEIGHT };
    GAME_GameGeometry3D_get(world, solid)->elevation = 0;
    for (int front = 0; front < 2 && ok; front++) {
        box->elevation = front ? 12 : -12;
        for (int reverse = 0; reverse < 2 && ok; reverse++) {
            mixed_layers[0] = (render_material_entity){ .shape_type = RENDER_SHAPE_MESH,
                                                        .points = { reverse ? 1 : 0 } };
            mixed_layers[1] = (render_material_entity){ .shape_type = RENDER_SHAPE_MESH,
                                                        .points = { reverse ? 0 : 1 } };
            ok = draw(raster, world, mixed, 2, mixed_layers, view) &&
                 pixel(
                     raster,
                     48,
                     40,
                     front ? 0 : 1,
                     front ? 1 : 0,
                     front ? 13 : sqrtf(99.5f),
                     front ? 0 : .05f,
                     front ? 0 : -.05f
                 );
        }
    }
    // A missing graph must report an error instead of silently drawing its bound.
    GAME_GameShader_get(world, solid)
        ->shape = "SDF nonexistent regression shape";
    if (ok) {
        cf_app_update(NULL);
        bool accepted = render_material_raster_draw_mixed(raster, layers, 1, view, world, actors, 1, 0);
        cf_app_draw_onto_screen(false);
        ok = !accepted && render_material_raster_error(raster)[0];
        if (!ok) {
            fprintf(stderr, "SDF missing shape did not report an error\n");
        }
    }
    GAME_GameShader_get(world, solid)->shape = sphere->name;
    if (ok) {
        ok = draw(raster, world, actors, 1, layers, view) &&
             coverage(raster, 48, 40, 1);
    }
    if (ok) {
        ok = active_scratch_reuse(world, solid) && test_sdf_batches(world) &&
             indexed_graph_pairs(world) && batch_layers(raster, world, sphere->name);
    }
    render_material_raster_free(raster);
    ecs_world_free(world);
    behavior_asset_free(sphere);
    behavior_asset_free(torus);
    behavior_asset_free(material);
    printf(
        "SDF material %s: sphere silhouette/height/normal, torus hole, "
        "transformed bounds, perspective, shared mesh depth\n",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

int sdf_material_run(const char *program)
{
    CF_Result result = cf_make_app(
        "SDF material regression",
        0,
        0,
        0,
        WIDTH,
        HEIGHT,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "%s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    bool ok = test_sdf_material();
    cf_destroy_app();
    return !ok;
}
