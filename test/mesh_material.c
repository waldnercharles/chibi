#include "authoring/image_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/mesh_projection.h"
#include "render/mesh_surface.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_asset.h"
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

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31, mantissa = value & 1023;
    if (exponent == 31) {
        return mantissa ? NAN : INFINITY;
    }
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

static bool read_channels(CF_Texture texture, int width, int height, int channels, float **result)
{
    int count = width * height * channels, bytes = count * (int)sizeof(uint16_t);
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
        (*result)[i] = half_value(pixels[i]);
        ok &= isfinite((*result)[i]);
    }
    if (readback.id) {
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    free(pixels);
    return ok;
}

static bool read_target(CF_Texture texture, int width, int height, float **result)
{
    return read_channels(texture, width, height, 4, result);
}

static bool draw(
    render_material_raster *raster,
    ecs_world *world,
    render_mesh_material_actor actor,
    const render_material_entity *layers,
    int count,
    render_material_view view
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(raster, layers, count, view, world, &actor, 1, 0);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "Mesh material draw: %s\n", render_material_raster_error(raster));
    }
    return ok;
}

static bool close_value(float actual, float expected, const char *channel)
{
    if (fabsf(actual - expected) < .025f) {
        return true;
    }
    fprintf(stderr, "Mesh material %s: got %.5f, expected %.5f\n", channel, actual, expected);
    return false;
}

static bool inspect(
    render_material_raster *raster,
    int width,
    int height,
    int x,
    int y,
    float red,
    float blue,
    float nx,
    float ny,
    float elevation
)
{
    render_material_textures textures = render_material_raster_textures(raster);
    CF_Texture targets[] = { textures.scene,
                             textures.source,
                             textures.albedo,
                             textures.normal,
                             textures.visibility };
    float *data[5] = { 0 };
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        ok &= read_target(targets[i], width, height, &data[i]);
    }
    float *cover = NULL;
    ok &= read_channels(textures.cover, width, height, 4, &cover);
    free(cover);
    int p = (y * width + x) * 4;
    if (ok) {
        ok &= close_value(data[0][p], red, "scene red") &&
              close_value(data[0][p + 2], blue, "scene blue");
        ok &= close_value(data[0][p + 3], 1, "coverage");
        ok &= close_value(data[3][p], nx, "normal X") &&
              close_value(data[3][p + 1], ny, "normal Y");
        ok &= close_value(data[2][p + 3], elevation, "elevation");
    }
    for (int i = 0; i < 5; i++) {
        free(data[i]);
    }
    return ok;
}

static bool inspect_source(render_material_raster *raster, int x, int y, float red, float blue, float depth)
{
    float *source = NULL;
    bool ok = read_target(render_material_raster_textures(raster).source, WIDTH, HEIGHT, &source);
    int p = (y * WIDTH + x) * 4;
    if (ok) {
        ok = close_value(source[p], red, "visible actor red source") &&
             close_value(source[p + 2], blue, "visible actor blue source") &&
             close_value(source[p + 3], depth, "visible actor optical depth");
    }
    free(source);
    return ok;
}

static bool separate_meshes(render_material_raster *raster, ecs_world *world, ecs_entity front)
{
    ecs_entity other = ecs_world_create_entity(world);
    GameGeometry3D geometry = { .primitive = GAME_PRIMITIVE_BOX,
                                .width = 16,
                                .depth = 12,
                                .height = 2,
                                .elevation = -6 };
    GameVisualTransform visual = { .scale = 1 };
    GameMeshSurface2D enabled = { true };
    GameMaterial material = { .scene = 0x0000ffff,
                              .albedo = 0x0000ffff,
                              .emission_color = 0x0000ffff,
                              .emission_intensity = 4,
                              .emission_multiplier = 1,
                              .absorption = 1,
                              .edge_light = 1 };
    assert(
        ecs_world_record_add(world, other, GAME_COMPONENT_GameGeometry3D.id, &geometry, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, other, GAME_COMPONENT_GameVisualTransform.id, &visual, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, other, GAME_COMPONENT_GameMeshSurface2D.id, &enabled, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, other, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    render_mesh_material_actor actors[] = { { front, { 0, 0 } }, { other, { 0, 0 } } };
    render_material_entity layers[] = { { .shape_type = RENDER_SHAPE_MESH },
                                        { .shape_type = RENDER_SHAPE_MESH } };
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    bool ok = true;
    // Changing elevation and caller order must change the visible emitter by
    // geometric depth, including its source color, rather than actor order.
    for (int negative = 0; negative < 2 && ok; negative++) {
        float base = negative ? -20 : 0;
        GAME_GameGeometry3D_get(world, front)->elevation = base;
        for (int near = 0; near < 2 && ok; near++) {
            GAME_GameGeometry3D_get(world, other)->elevation = base + (near ? 6 : -6);
            for (int reverse = 0; reverse < 2 && ok; reverse++) {
                layers[0].points[0] = reverse ? 1 : 0;
                layers[1].points[0] = reverse ? 0 : 1;
                cf_app_update(NULL);
                ok = render_material_raster_draw_mixed(raster, layers, 2, view, world, actors, 2, 0);
                cf_app_draw_onto_screen(false);
                ok &= inspect(
                          raster,
                          WIDTH,
                          HEIGHT,
                          48,
                          40,
                          near ? 0 : 1,
                          near ? 1 : 0,
                          0,
                          0,
                          base + (near ? 7 : 2)
                      ) &&
                      inspect_source(raster, 48, 40, near ? 0 : 2, near ? 4 : 0, 1);
            }
        }
    }
    GAME_GameGeometry3D_get(world, front)->elevation = 0;
    // Intersecting planes require a different winning actor on either side.
    const float angle = .35f;
    GAME_GameVisualTransform_get(world, front)->pitch = angle;
    GAME_GameGeometry3D_get(world, other)->elevation = 1;
    for (int reverse = 0; reverse < 2 && ok; reverse++) {
        layers[0].points[0] = reverse ? 1 : 0;
        layers[1].points[0] = reverse ? 0 : 1;
        cf_app_update(NULL);
        ok = render_material_raster_draw_mixed(raster, layers, 2, view, world, actors, 2, 0);
        cf_app_draw_onto_screen(false);
        ok &= inspect(raster, WIDTH, HEIGHT, 42, 40, 0, 1, 0, 0, 2) &&
              inspect(raster, WIDTH, HEIGHT, 54, 40, 1, 0, -sinf(angle), 0, 2 / cosf(angle) + 6.5f * tanf(angle));
    }
    GAME_GameVisualTransform_get(world, front)->pitch = 0;
    // An explicit higher authored layer remains an intentional depth override.
    GAME_GameGeometry3D_get(world, other)->elevation = -6;
    layers[0].points[0] = 0;
    layers[1].points[0] = 1;
    layers[1].layer = 1;
    cf_app_update(NULL);
    ok &= render_material_raster_draw_mixed(raster, layers, 2, view, world, actors, 2, 0);
    cf_app_draw_onto_screen(false);
    ok &= inspect(raster, WIDTH, HEIGHT, 48, 40, 0, 1, 0, 0, -5);
    // A mesh that misses this pixel cannot assign its layer to an earlier face.
    render_mesh_material_actor gap_actors[] = { actors[0],
                                                { other, { 30, 0 } },
                                                actors[1] };
    render_material_entity gap_layers[] = {
        { .shape_type = RENDER_SHAPE_MESH },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 }, .layer = 1 },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 2 }, .layer = 1 }
    };
    cf_app_update(NULL);
    ok &= render_material_raster_draw_mixed(raster, gap_layers, 3, view, world, gap_actors, 3, 0);
    cf_app_draw_onto_screen(false);
    ok &= inspect(raster, WIDTH, HEIGHT, 48, 40, 0, 1, 0, 0, -5);
    // Half a pixel sees the nearer red silhouette and half the rear blue mesh.
    // Both actor orders must fill its optical coverage and keep a real plane.
    GAME_GameGeometry3D_get(world, other)->width = 40;
    GAME_GameGeometry3D_get(world, other)->depth = 30;
    actors[0].position.x = .5f;
    layers[1].layer = 0;
    for (int reverse = 0; reverse < 2 && ok; reverse++) {
        layers[0].points[0] = reverse ? 1 : 0;
        layers[1].points[0] = reverse ? 0 : 1;
        cf_app_update(NULL);
        ok = render_material_raster_draw_mixed(raster, layers, 2, view, world, actors, 2, 0);
        cf_app_draw_onto_screen(false);
        ok &= inspect(raster, WIDTH, HEIGHT, 58, 40, .5f, .5f, 0, 0, 2) &&
              inspect_source(raster, 58, 40, 1, 2, 1);
    }
    return ok;
}

static bool stepped_height(render_material_raster *raster, float expected)
{
    render_material_textures textures = render_material_raster_textures(raster);
    float *albedo = NULL, *cover = NULL, *normal = NULL;
    bool ok = read_target(textures.albedo, WIDTH, HEIGHT, &albedo) &&
              read_target(textures.cover, WIDTH, HEIGHT, &cover) &&
              read_target(textures.normal, WIDTH, HEIGHT, &normal);
    int p = (40 * WIDTH + 48) * 4;
    if (ok) {
        ok = close_value(cover[p + 1], 1, "stepped geometry coverage") &&
             close_value(cover[p + 2], 1, "stepped mesh coverage") &&
             close_value(albedo[p + 3] / cover[p + 1], expected, "selected face height") &&
             close_value(normal[p], 0, "selected face normal X") &&
             close_value(normal[p + 1], 0, "selected face normal Y");
    }
    free(albedo);
    free(cover);
    free(normal);
    return ok;
}

static bool energy(
    render_material_raster *raster,
    int width,
    int height,
    float *coverage,
    float *emission,
    bool *partial
)
{
    render_material_textures textures = render_material_raster_textures(raster);
    float *scene = NULL, *source = NULL;
    bool ok = read_target(textures.scene, width, height, &scene) &&
              read_target(textures.source, width, height, &source);
    *coverage = *emission = 0;
    *partial = false;
    for (int p = 0; p < width * height && ok; p++) {
        float a = scene[p * 4 + 3];
        *coverage += a;
        *emission += source[p * 4];
        *partial |= a > .01f && a < .99f;
        ok &= a >= 0 && a <= 1.001f;
    }
    free(scene);
    free(source);
    return ok;
}

/* Independent flat cards isolate projection from Suzanne's mesh/material.
 * Their analytic visible areas are 8x8, 16x16 and 4x4 at known distances. */
static bool perspective_meshes(render_material_raster *raster, ecs_world *world)
{
    render_material_view view = { .min_x = -38,
                                  .max_y = 33,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .focal_distance = 32,
                                  .center_x = 10,
                                  .center_y = -7 };
    CF_V2 lo, hi;
    assert(
        render_mesh_project_bounds(view, (CF_V3){ 8, 16, 5 }, (CF_V3){ 12, 16, 9 }, &lo, &hi)
    );
    assert(lo.x == 6 && hi.x == 14 && lo.y == 3 && hi.y == 11);
    assert(
        render_mesh_project_bounds(view, (CF_V3){ 8, 30, 5 }, (CF_V3){ 12, 32, 9 }, &lo, &hi)
    );
    assert(lo.x == -54 && hi.x == 74 && lo.y == -57 && hi.y == 71);
    /* A small near-plane intersection needs a small crop, not a full canvas. */
    assert(render_mesh_project_bounds(
        view,
        (CF_V3){ 9.875f, 30, 6.875f },
        (CF_V3){ 10.125f, 32, 7.125f },
        &lo,
        &hi
    ));
    assert(lo.x == 6 && hi.x == 14 && lo.y == 3 && hi.y == 11);
    /* This entire clipped volume is off-screen even though it crosses near. */
    assert(
        render_mesh_project_bounds(view, (CF_V3){ 20, 30, 5 }, (CF_V3){ 22, 32, 9 }, &lo, &hi)
    );
    assert(lo.x > view.min_x + view.width);
    assert(
        !render_mesh_project_bounds(view, (CF_V3){ 8, 33, 5 }, (CF_V3){ 12, 34, 9 }, &lo, &hi)
    );
    ecs_entity entity = ecs_world_create_entity(world);
    GameGeometry3D geometry = { .primitive = GAME_PRIMITIVE_BOX,
                                .width = 8,
                                .depth = 8,
                                .height = .002f,
                                .elevation = -.001f };
    GameVisualTransform pose = { .scale = 1 };
    GameMaterial material = { .scene = 0xff0000ff,
                              .albedo = 0xff0000ff,
                              .absorption = 1,
                              .edge_light = 1 };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameGeometry3D.id, &geometry, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameVisualTransform.id, &pose, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    render_mesh_material_actor actor = { entity, { 10, 7 } };
    render_material_entity layer = { .shape_type = RENDER_SHAPE_MESH };
    bool ok = render_material_raster_resize(raster, WIDTH, HEIGHT);
    const float elevations[] = { 0, 16, -32, 33 };
    const float areas[] = { 64, 256, 16, 0 };
    for (int i = 0; i < 4 && ok; i++) {
        GAME_GameGeometry3D_get(world, entity)->elevation = elevations[i] - .001f;
        float area, emission;
        bool partial;
        ok = draw(raster, world, actor, &layer, 1, view) &&
             energy(raster, WIDTH, HEIGHT, &area, &emission, &partial);
        if (ok && fabsf(area - areas[i]) > 1) {
            fprintf(
                stderr,
                "Perspective card area at height %g: got %g, expected %g\n",
                elevations[i],
                area,
                areas[i]
            );
            ok = false;
        }
        if (ok && i < 3) {
            ok = inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, elevations[i]);
        }
    }
    // Two independent cards share one depth target even when caller order
    // changes. Their actual elevations, rather than their projected size,
    // determine which material supplies the visible pixel.
    ecs_entity rear = ecs_world_create_entity(world);
    geometry.elevation = -32.001f;
    material.scene = material.albedo = 0x0000ffff;
    assert(
        ecs_world_record_add(world, rear, GAME_COMPONENT_GameGeometry3D.id, &geometry, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, rear, GAME_COMPONENT_GameVisualTransform.id, &pose, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, rear, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GAME_GameGeometry3D_get(world, entity)->elevation = 15.999f;
    render_mesh_material_actor actors[] = { actor, { rear, { 10, 7 } } };
    render_material_entity layers[] = { layer, layer };
    for (int reverse = 0; reverse < 2 && ok; reverse++) {
        layers[0].points[0] = reverse;
        layers[1].points[0] = !reverse;
        cf_app_update(NULL);
        ok = render_material_raster_draw_mixed(raster, layers, 2, view, world, actors, 2, 0);
        cf_app_draw_onto_screen(false);
        ok &= inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 16);
    }
    // Perspective exposes the left side of an offset box, whose world normal
    // has zero Y. Orthographic back-face rejection would incorrectly hide it.
    GAME_GameGeometry3D_get(world, entity)->height = 8;
    GAME_GameGeometry3D_get(world, entity)->elevation = 0;
    actor.position.x = 26;
    if (ok) {
        ok = draw(raster, world, actor, &layer, 1, view);
        float *normals = NULL, *colors = NULL;
        render_material_textures textures = render_material_raster_textures(raster);
        ok &= read_target(textures.normal, WIDTH, HEIGHT, &normals) &&
              read_target(textures.scene, WIDTH, HEIGHT, &colors);
        int pixel = (40 * WIDTH + 60) * 4;
        if (ok) {
            ok = normals[pixel] < -.95f && colors[pixel + 3] > .99f;
            if (!ok) {
                fprintf(stderr, "Perspective box side face was lost\n");
            }
        }
        free(normals);
        free(colors);
    }
    /* A narrow volume straddles the near plane. Its visible side face still
     * renders, while material work stays below the old full-canvas fallback. */
    actor.position.x = 10.375f;
    GameGeometry3D *near = GAME_GameGeometry3D_get(world, entity);
    near->width = near->depth = .25f;
    near->height = .25f;
    near->elevation = 31;
    if (ok) {
        ok = draw(raster, world, actor, &layer, 1, view);
        render_mesh_material_work work = render_material_raster_mesh_work(raster);
        ok &= work.surface_pixels > 0 && work.surface_pixels < WIDTH * HEIGHT / 4;
        float area, emission;
        bool partial;
        ok &= energy(raster, WIDTH, HEIGHT, &area, &emission, &partial);
        ok &= area > 1;
        if (!ok) {
            fprintf(stderr, "Near-plane crop: surface area %g, active work %d pixels\n", area, work.surface_pixels);
        }
    }
    near->width = near->depth = 8;
    actor.position.x = 10;
    GAME_GameGeometry3D_get(world, entity)->height = .002f;
    // Crop origin changes independently of the eye: the same card must remain
    // centered in the shifted padded frame and retain its actual height.
    GAME_GameGeometry3D_get(world, entity)->elevation = 15.999f;
    view.min_x -= 8;
    view.max_y += 8;
    if (ok) {
        ok = draw(raster, world, actor, &layer, 1, view) &&
             inspect(raster, WIDTH, HEIGHT, 56, 48, 1, 0, 0, 0, 16);
    }
    printf(
        "perspective mesh %s: analytic size, translated eye/crop, shared "
        "depth, side faces, world height and near clipping\n",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static void write_mesh(const char *path, bool alternate)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("{\"version\":1,\"vertices\":[", file);
    static const float positions[6][2] = { { -.5f, -.5f }, { -.5f, .5f },
                                           { .5f, .5f },   { -.5f, -.5f },
                                           { .5f, .5f },   { .5f, -.5f } };
    /* Deliberately reverse both UV axes relative to planar projection. The
       back plane samples blue everywhere and must never leak through. */
    for (int plane = 0; plane < 2; plane++) {
        for (int i = 0; i < 6; i++) {
            float x = positions[i][0], z = positions[i][1];
            float u = plane ? .25f : alternate ? .75f : .5f - x;
            float v = plane ? .75f : alternate ? .25f : .5f - z;
            fprintf(
                file,
                "%s[%g,%g,%g,0,1,0,%g,%g]",
                plane || i ? "," : "",
                x,
                plane ? -.25f : .25f,
                z,
                u,
                v
            );
        }
    }
    fputs("]}", file);
    assert(!fclose(file));
}

static bool imported_meshes(render_material_raster *raster, ecs_world *world, ecs_entity entity)
{
    char root[] = "/tmp/chibi-mesh-material-XXXXXX";
    assert(mkdtemp(root));
    char paths[3][256];
    snprintf(paths[0], sizeof paths[0], "%s/a.mesh.json", root);
    snprintf(paths[1], sizeof paths[1], "%s/b.mesh.json", root);
    snprintf(paths[2], sizeof paths[2], "%s/uv.png", root);
    write_mesh(paths[0], false);
    write_mesh(paths[1], true);
    CF_Pixel pixels[64];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            pixels[y * 8 + x] = y < 4
                                  ? (x < 4 ? (CF_Pixel){ .r = 255, .a = 255 }
                                           : (CF_Pixel){ .g = 255, .a = 255 })
                                  : (x < 4 ? (CF_Pixel){ .b = 255, .a = 255 }
                                           : (CF_Pixel){ .r = 255, .g = 255, .a = 255 });
        }
    }
    CF_Image image = { 8, 8, pixels };
    void *png = NULL;
    int png_size;
    assert(!cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_size)));
    FILE *file = fopen(paths[2], "wb");
    assert(file && fwrite(png, 1, (size_t)png_size, file) == (size_t)png_size);
    assert(!fclose(file));
    cf_free(png);
    char error[256];
    assert(render_image_mount_content(root, error, sizeof error));
    const render_mesh_asset *first = authoring_mesh_prepare("content/a.mesh.json", error, sizeof error);
    assert(first);
    uint64_t first_identity = first->identity;
    const render_mesh_asset *second = authoring_mesh_prepare("content/b.mesh.json", error, sizeof error);
    assert(second && second->identity != first_identity);
    assert(
        authoring_mesh_prepare("content/a.mesh.json", error, sizeof error)->identity == first_identity
    );
    assert(!authoring_mesh_prepare("content/../a.mesh.json", error, sizeof error));
    behavior_asset *asset = behavior_asset_new("Imported mesh UV regression");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(asset->graph, false);
    node *texture = graph_add_node(asset->graph, registry_node_by_name("Shader Image Texture"));
    pin_value key = { .type = PIN_STR, .str = "content/uv.png" };
    assert(!graph_set_pin_default(asset->graph, texture, 0, SHADER_IMAGE_INPUT_IMAGE, &key));
    for (int i = SHADER_IMAGE_INPUT_COLOR_SPACE; i < SHADER_IMAGE_INPUT_COUNT; i++) {
        pin_value value = { .type = PIN_I32,
                            .i = i == SHADER_IMAGE_INPUT_FILTER ? SHADER_IMAGE_NEAREST
                                                                : 1 };
        assert(!graph_set_pin_default(asset->graph, texture, 0, i, &value));
    }
    assert(!graph_connect_data(asset->graph, texture, 0, out, 0));
    shader_error shader_error;
    assert(render_shader_upsert(asset, &shader_error));
    GameMeshAsset mesh_asset = { .source = "content/a.mesh.json" };
    GameShader binding = { .material = asset->name };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMeshAsset.id, &mesh_asset, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameShader.id, &binding, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameVisualTransform_get(world, entity) = (GameVisualTransform){ .scale = 1 };
    GAME_GameOrientation_get(world, entity)->radians = 0;
    GAME_GameMeshParts_get(world, entity)->parts = NULL;
    *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){ .width = 20,
                                                                .height = 8,
                                                                .depth = 16 };
    render_material_entity layer = { .shape_type = RENDER_SHAPE_MESH };
    render_mesh_material_actor actor = { entity, { 0, 0 } };
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    render_mesh_surface surface = { 0 };
    GAME_GameVisualTransform_get(world, entity)->roll = 1.5707963268f;
    assert(render_mesh_surface_build(world, entity, &actor.position, &surface, error, sizeof error));
    assert(fabsf(surface.min.x + 10) < .001f && fabsf(surface.max.x - 10) < .001f);
    assert(fabsf(surface.min.y + 8) < .001f && fabsf(surface.max.y - 8) < .001f);
    assert(fabsf(surface.min.z + 2) < .001f && fabsf(surface.max.z - 2) < .001f);
    render_mesh_surface_free(&surface);
    GAME_GameVisualTransform_get(world, entity)->roll = 0;
    bool ok = render_material_raster_resize(raster, WIDTH, HEIGHT);
    for (int round = 0; round < 3 && ok; round++) {
        bool alternate = round == 1;
        ((GameMeshAsset *)
             ecs_component_data(world, entity, GAME_COMPONENT_GameMeshAsset.id))
            ->source = alternate ? "content/b.mesh.json" : "content/a.mesh.json";
        ok = draw(raster, world, actor, &layer, 1, view);
        float *scene = NULL, *albedo = NULL;
        ok &= read_target(render_material_raster_textures(raster).scene, WIDTH, HEIGHT, &scene);
        ok &= read_target(render_material_raster_textures(raster).albedo, WIDTH, HEIGHT, &albedo);
        const int x[] = { 43, 53, 43, 53 }, y[] = { 36, 36, 44, 44 };
        const float colors[4][3] = { { 1, 1, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
        for (int sample = 0; sample < 4 && ok; sample++) {
            int p = (y[sample] * WIDTH + x[sample]) * 4;
            for (int c = 0; c < 3; c++) {
                float expected = alternate ? (c == 1) : colors[sample][c];
                ok &= close_value(scene[p + c], expected, "imported UV color");
            }
            ok &= close_value(albedo[p + 3], 2, "imported frontmost elevation");
        }
        free(scene);
        free(albedo);
    }
    behavior_asset_free(asset);
    for (int i = 0; i < 3; i++) {
        assert(!unlink(paths[i]));
    }
    assert(!rmdir(root));
    printf("imported mesh %s: asymmetric UV texture, hidden face depth, two cached asset identities\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool test_mesh_material()
{
    /* Other catalog materials contribute graph helpers to the mesh program.
       Their ramp uniforms must be declared before those helper functions,
       even when this actor uses only fixed coefficients or an image. */
    behavior_asset *ramp_asset = behavior_asset_new("Mesh catalog ramp regression");
    ramp_asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *ramp_out = shader_graph_add_output(ramp_asset->graph, false);
    node *ramp = graph_add_node(ramp_asset->graph, registry_node_by_name("Shader Color Ramp"));
    shader_ramp_stop stops[] = { { .position = 0, .color = { 0, 0, 0, 1 } },
                                 { .position = 1, .color = { 1, 1, 1, 1 } } };
    assert(shader_node_ramp_set(ramp_asset->graph, ramp, stops, 2));
    assert(!graph_connect_data(ramp_asset->graph, ramp, 0, ramp_out, 0));
    shader_error ramp_error;
    assert(render_shader_upsert(ramp_asset, &ramp_error));
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &GAME_COMPONENT_GameGeometry3D,  &GAME_COMPONENT_GameVisualTransform,
        &GAME_COMPONENT_GameOrientation, &GAME_COMPONENT_GameMeshParts,
        &GAME_COMPONENT_GameMaterial,    &GAME_COMPONENT_GameMeshSurface2D,
        &GAME_COMPONENT_GameMeshAsset,   &GAME_COMPONENT_GameShader
    };
    bool ok = true;
    for (int i = 0; i < 8; i++) {
        ok &= ecs_world_register_component(world, components[i], NULL) == ECS_OK;
    }
    ecs_entity entity = ecs_world_create_entity(world);
    GameGeometry3D geometry = { .primitive = GAME_PRIMITIVE_BOX,
                                .width = 20,
                                .depth = 16,
                                .height = 4 };
    GameVisualTransform visual = { .scale = 1 };
    GameOrientation heading = { 0 };
    GameMeshParts parts = { 0 };
    GameMeshSurface2D enabled = { true };
    GameMaterial material = { .scene = 0xff0000ff,
                              .albedo = 0xff0000ff,
                              .emission_color = 0xff0000ff,
                              .emission_intensity = 2,
                              .emission_multiplier = 1,
                              .absorption = 1,
                              .edge_light = 1 };
#define ADD(type, value)                                                       \
    ok &= ecs_world_record_add(world, entity, GAME_COMPONENT_##type.id, &(value), 0) == ECS_OK
    ADD(GameGeometry3D, geometry);
    ADD(GameVisualTransform, visual);
    ADD(GameOrientation, heading);
    ADD(GameMeshParts, parts);
    ADD(GameMaterial, material);
    ADD(GameMeshSurface2D, enabled);
#undef ADD
    ok &= ecs_world_apply_commands(world) == ECS_OK;
    render_material_raster *raster = render_material_raster_new();
    ok &= raster && render_material_raster_resize(raster, WIDTH, HEIGHT);
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    render_mesh_material_actor actor = { entity, { 0, 0 } };
    render_material_entity mesh = { .shape_type = RENDER_SHAPE_MESH };
    if (ok) {
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 2);
    }
    float front_coverage = 0, front_emission = 0;
    bool partial = false;
    if (ok) {
        ok = energy(raster, WIDTH, HEIGHT, &front_coverage, &front_emission, &partial);
    }
    /* A smaller emissive box fully behind the front box must add neither
       emission nor coverage. Reverse authored part order to exercise depth. */
    GAME_GameMeshParts_get(world, entity)
        ->parts = "(array)- {\n"
                  "    - {\n"
                  "        primitive 0\n"
                  "        (array)size 12 2 10\n"
                  "        (array)position 0 -6 0\n"
                  "    }\n"
                  "    - {\n"
                  "        primitive 0\n"
                  "        (array)size 20 4 16\n"
                  "    }\n"
                  "}\n";
    float compound_coverage = 0, compound_emission = 0;
    if (ok) {
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             energy(raster, WIDTH, HEIGHT, &compound_coverage, &compound_emission, &partial) &&
             close_value(compound_coverage, front_coverage, "hidden part coverage") &&
             close_value(compound_emission, front_emission, "hidden part emission");
    }
    /* Half the pixel sees the raised face, half the underlying hull. Relief
       must describe a real face rather than their nonexistent middle height.
       Both subpixel placements keep the step inside this output pixel. */
    GAME_GameMeshParts_get(world, entity)
        ->parts = "(array)- {\n"
                  "    - {\n"
                  "        primitive 0\n"
                  "        (array)size 20 4 16\n"
                  "    }\n"
                  "    - {\n"
                  "        primitive 0\n"
                  "        (array)size 4 2 8\n"
                  "        (array)position 2.5 8 0\n"
                  "    }\n"
                  "}\n";
    for (int negative = 0; negative < 2 && ok; negative++) {
        float elevation = negative ? -20 : 0;
        GAME_GameGeometry3D_get(world, entity)->elevation = elevation;
        for (int phase = 0; phase < 2 && ok; phase++) {
            actor.position.x = phase ? .125f : -.125f;
            ok = draw(raster, world, actor, &mesh, 1, view) &&
                 stepped_height(raster, elevation + 9);
        }
    }
    actor.position.x = 0;
    GAME_GameGeometry3D_get(world, entity)->elevation = 0;
    GAME_GameMeshParts_get(world, entity)->parts = NULL;
    if (ok) {
        ok = separate_meshes(raster, world, entity);
    }
    /* Ordered 2D layers must win or lose by authored layer, independently of
       the mesh's internal depth, and replace its visible normal/height. */
    render_material_entity box = { .inv_rot = { 1, 0, 0, 1 },
                                   .shape_type = RENDER_SHAPE_BOX,
                                   .extent = { 32, 32 },
                                   .points = { 30, 30 },
                                   .scene = { 0, 0, 1, 1 },
                                   .albedo = { 0, 0, 1, 1 },
                                   .absorption = { 1, 1, 1, 1 } };
    render_material_entity layers[] = { box, mesh };
    layers[0].layer = -1;
    if (ok) {
        ok = draw(raster, world, actor, layers, 2, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 2);
    }
    layers[0].layer = 1;
    if (ok) {
        ok = draw(raster, world, actor, layers, 2, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 0, 1, 0, 0, 0);
    }
    /* Known planes provide independent normal and elevation expectations. */
    const float angle = .5235987756f;
    GameVisualTransform *transform = GAME_GameVisualTransform_get(world, entity);
    transform->pitch = angle;
    if (ok) {
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, -.5f, 0, 2 / cosf(angle) + .5f * tanf(angle));
    }
    transform->pitch = 0;
    transform->roll = angle;
    if (ok) {
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, -.5f, 2 / cosf(angle) - .5f * tanf(angle));
    }
    transform->roll = 0;
    transform->scale = 2;
    GAME_GameOrientation_get(world, entity)->radians = 1.5707963268f;
    if (ok) {
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 4);
    }
    transform->scale = 1;
    GAME_GameOrientation_get(world, entity)->radians = .3f;
    float minimum = INFINITY, maximum = 0;
    bool any_partial = false;
    for (int phase = 0; phase < 8 && ok; phase++) {
        actor.position.x = phase / 8.0f;
        actor.position.y = phase / 16.0f;
        float coverage = 0, emission = 0;
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             energy(raster, WIDTH, HEIGHT, &coverage, &emission, &partial);
        minimum = fminf(minimum, coverage);
        maximum = fmaxf(maximum, coverage);
        any_partial |= partial;
    }
    ok &= any_partial && maximum > 0 && (maximum - minimum) / maximum < .03f;
    /* Move both actor and padded material view. The same pixel must retain
       material coordinates, including after target resize. */
    actor.position = (GameTransform){ 200, -120 };
    GAME_GameOrientation_get(world, entity)->radians = 0;
    view.min_x += 200;
    view.max_y += 120;
    if (ok) {
        ok = draw(raster, world, actor, &mesh, 1, view) &&
             inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 2);
    }
    if (ok) {
        ok = render_material_raster_resize(raster, WIDTH * 2, HEIGHT * 2) &&
             draw(raster, world, actor, &mesh, 1, view) &&
             inspect(raster, WIDTH * 2, HEIGHT * 2, 96, 80, 1, 0, 0, 0, 2);
    }
    if (ok) {
        ok = imported_meshes(raster, world, entity);
    }
    if (ok) {
        ok = perspective_meshes(raster, world);
    }
    render_material_raster_free(raster);
    ecs_world_free(world);
    behavior_asset_free(ramp_asset);
    printf(
        "mesh material %s: depth, ordered 2D layers, normals/elevation, finite "
        "targets, subpixel coverage, padded view and resize\n",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

int mesh_material_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Mesh material regression",
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
    bool ok = test_mesh_material();
    cf_destroy_app();
    return !ok;
}
