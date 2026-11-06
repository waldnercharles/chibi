#include "authoring/parts_prepare.h"
#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "hidden_app.h"
#include "memory_images.h"
#include "render/material_raster.h"
#include "render/mesh_projection.h"
#include "render/mesh_surface.h"
#include "render/scene3d_parts.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_asset.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Meshes drawn by the material raster: depth between meshes and their parts,
 * ordered 2D layers, planes, coverage, padded views, imported meshes and
 * perspective. Each case draws a fresh box actor. */

enum
{
    WIDTH = 96,
    HEIGHT = 80
};

static const render_material_view home = { -48, 40, WIDTH, HEIGHT };
static const render_material_entity mesh = { .shape_type = RENDER_SHAPE_MESH };
static ecs_world *world;
static ecs_entity box;
static render_material_raster *raster;
static behavior_asset *ramp_asset;

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

/* Draws `count` layers whose mesh layers draw `actors`, by the index in their
 * first point. */
static bool draw_all(
    const render_material_entity *layers,
    int count,
    const render_mesh_material_actor *actors,
    int actor_count,
    render_material_view view
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(raster, layers, count, view, world, actors, actor_count, 0);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "Mesh material draw: %s\n", render_material_raster_error(raster));
    }
    return ok;
}

/* Draws the one mesh layer of `actor`. */
static bool draw(render_mesh_material_actor actor, render_material_view view)
{
    return draw_all(&mesh, 1, &actor, 1, view);
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

/* Gives the box `text`'s parts, prepared as project preparation would. */
static bool set_parts(const char *text)
{
    char error[256];
    MeshParts_get(world, box)->parts = text;
    return !text || authoring_parts_prepare(text, error, sizeof error);
}

static void add(ecs_entity entity, const ecs_component_desc *component, const void *value)
{
    assert(ecs_world_record_add(world, entity, component->id, value, 0) == ECS_OK);
}

/* A mesh actor of `geometry`, opaque and emissive in `color`. */
static ecs_entity mesh_box(Geometry3D geometry, uint32_t color, float emission)
{
    ecs_entity entity = ecs_world_create_entity(world);
    VisualTransform visual = { .scale = 1 };
    MeshSurface2D enabled = { true };
    Material material = { .scene = color,
                          .albedo = color,
                          .emission_color = color,
                          .emission_intensity = emission,
                          .emission_multiplier = 1,
                          .absorption = 1,
                          .edge_light = 1 };
    add(entity, &COMPONENT_Geometry3D, &geometry);
    add(entity, &COMPONENT_VisualTransform, &visual);
    add(entity, &COMPONENT_MeshSurface2D, &enabled);
    add(entity, &COMPONENT_Material, &material);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static void setup()
{
    /* Other catalog materials contribute graph helpers to the mesh program.
     * Their ramp uniforms must be declared before those helper functions,
     * even when an actor uses only fixed coefficients or an image. */
    ramp_asset = behavior_asset_new("Mesh catalog ramp regression");
    ramp_asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(ramp_asset->graph, false);
    node *ramp = graph_add_node(ramp_asset->graph, registry_node_by_name("Shader Color Ramp"));
    shader_ramp_stop stops[] = { { .position = 0, .color = { 0, 0, 0, 1 } },
                                 { .position = 1, .color = { 1, 1, 1, 1 } } };
    assert(shader_node_ramp_set(ramp_asset->graph, ramp, stops, 2));
    assert(!graph_connect_data(ramp_asset->graph, ramp, 0, out, 0));
    shader_error error;
    assert(render_shader_upsert(ramp_asset, &error));
    world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Geometry3D,  &COMPONENT_VisualTransform,
        &COMPONENT_Orientation, &COMPONENT_MeshParts,
        &COMPONENT_Material,    &COMPONENT_MeshSurface2D,
        &COMPONENT_MeshAsset,   &COMPONENT_Shader
    };
    for (int i = 0; i < 8; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    box = mesh_box(
        (Geometry3D){ .primitive = PRIMITIVE_BOX, .width = 20, .depth = 16, .height = 4 },
        0xff0000ff,
        2
    );
    Orientation heading = { 0 };
    MeshParts parts = { 0 };
    add(box, &COMPONENT_Orientation, &heading);
    add(box, &COMPONENT_MeshParts, &parts);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    raster = render_material_raster_new();
    assert(render_material_raster_resize(raster, WIDTH, HEIGHT));
}

static void teardown()
{
    render_material_raster_free(raster);
    ecs_world_free(world);
    behavior_asset_free(ramp_asset);
    authoring_shader_catalog_clear();
    render_scene3d_parts_clear();
    render_mesh_assets_clear();
    render_images_clear();
}

/* A box draws its material, relief and emission; a hidden part adds nothing,
 * and a pixel half on a raised part keeps a real face's height. */
TEST_CASE(boxes_draw_their_visible_faces)
{
    render_mesh_material_actor actor = { box, { 0, 0 } };
    REQUIRE(draw(actor, home));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 2));
    float front_coverage, front_emission;
    bool partial;
    REQUIRE(energy(raster, WIDTH, HEIGHT, &front_coverage, &front_emission, &partial));
    /* A smaller emissive box fully behind the front box adds neither emission
     * nor coverage. Parts are in reverse order to exercise depth. */
    REQUIRE(set_parts(
        "(array)- {\n"
        "    - {\n"
        "        primitive 0\n"
        "        (array)size 12 2 10\n"
        "        (array)position 0 -6 0\n"
        "    }\n"
        "    - {\n"
        "        primitive 0\n"
        "        (array)size 20 4 16\n"
        "    }\n"
        "}\n"
    ));
    float coverage, emission;
    REQUIRE(draw(actor, home));
    REQUIRE(energy(raster, WIDTH, HEIGHT, &coverage, &emission, &partial));
    REQUIRE(close_value(coverage, front_coverage, "hidden part coverage"));
    REQUIRE(close_value(emission, front_emission, "hidden part emission"));
    /* Half the pixel sees the raised face, half the underlying hull. Relief
     * describes a real face rather than their nonexistent middle height. Both
     * subpixel placements keep the step inside this output pixel. */
    REQUIRE(set_parts(
        "(array)- {\n"
        "    - {\n"
        "        primitive 0\n"
        "        (array)size 20 4 16\n"
        "    }\n"
        "    - {\n"
        "        primitive 0\n"
        "        (array)size 4 2 8\n"
        "        (array)position 2.5 8 0\n"
        "    }\n"
        "}\n"
    ));
    for (int negative = 0; negative < 2; negative++) {
        float elevation = negative ? -20 : 0;
        Geometry3D_get(world, box)->elevation = elevation;
        for (int phase = 0; phase < 2; phase++) {
            actor.position.x = phase ? .125f : -.125f;
            REQUIRE(draw(actor, home));
            REQUIRE(stepped_height(raster, elevation + 9));
        }
    }
    return true;
}

/* The visible emitter, and its source color, follow geometric depth rather
 * than actor order, unless an authored layer overrides it. */
TEST_CASE(meshes_win_by_depth_not_order)
{
    ecs_entity other = mesh_box(
        (Geometry3D){ .primitive = PRIMITIVE_BOX,
                      .width = 16,
                      .depth = 12,
                      .height = 2,
                      .elevation = -6 },
        0x0000ffff,
        4
    );
    render_mesh_material_actor actors[] = { { box, { 0, 0 } }, { other, { 0, 0 } } };
    render_material_entity layers[] = { mesh, mesh };
    for (int negative = 0; negative < 2; negative++) {
        float base = negative ? -20 : 0;
        Geometry3D_get(world, box)->elevation = base;
        for (int near = 0; near < 2; near++) {
            Geometry3D_get(world, other)->elevation = base + (near ? 6 : -6);
            for (int reverse = 0; reverse < 2; reverse++) {
                layers[0].points[0] = reverse ? 1 : 0;
                layers[1].points[0] = reverse ? 0 : 1;
                REQUIRE(draw_all(layers, 2, actors, 2, home));
                REQUIRE(
                    inspect(raster, WIDTH, HEIGHT, 48, 40, !near, near, 0, 0, base + (near ? 7 : 2))
                );
                REQUIRE(inspect_source(raster, 48, 40, near ? 0 : 2, near ? 4 : 0, 1));
            }
        }
    }
    Geometry3D_get(world, box)->elevation = 0;
    /* Intersecting planes need a different winning actor on either side. */
    const float angle = .35f;
    VisualTransform_get(world, box)->pitch = angle;
    Geometry3D_get(world, other)->elevation = 1;
    for (int reverse = 0; reverse < 2; reverse++) {
        layers[0].points[0] = reverse ? 1 : 0;
        layers[1].points[0] = reverse ? 0 : 1;
        REQUIRE(draw_all(layers, 2, actors, 2, home));
        REQUIRE(inspect(raster, WIDTH, HEIGHT, 42, 40, 0, 1, 0, 0, 2));
        REQUIRE(
            inspect(raster, WIDTH, HEIGHT, 54, 40, 1, 0, -sinf(angle), 0, 2 / cosf(angle) + 6.5f * tanf(angle))
        );
    }
    VisualTransform_get(world, box)->pitch = 0;
    /* A higher authored layer remains an intentional depth override. */
    Geometry3D_get(world, other)->elevation = -6;
    layers[0].points[0] = 0;
    layers[1].points[0] = 1;
    layers[1].layer = 1;
    REQUIRE(draw_all(layers, 2, actors, 2, home));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 0, 1, 0, 0, -5));
    /* A mesh that misses this pixel cannot assign its layer to an earlier face. */
    render_mesh_material_actor gap_actors[] = { actors[0],
                                                { other, { 30, 0 } },
                                                actors[1] };
    render_material_entity gap_layers[] = {
        mesh,
        { .shape_type = RENDER_SHAPE_MESH, .points = { 1 }, .layer = 1 },
        { .shape_type = RENDER_SHAPE_MESH, .points = { 2 }, .layer = 1 }
    };
    REQUIRE(draw_all(gap_layers, 3, gap_actors, 3, home));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 0, 1, 0, 0, -5));
    /* Half a pixel sees the nearer red silhouette and half the rear blue mesh.
     * Both actor orders fill its optical coverage and keep a real plane. */
    Geometry3D_get(world, other)->width = 40;
    Geometry3D_get(world, other)->depth = 30;
    actors[0].position.x = .5f;
    layers[1].layer = 0;
    for (int reverse = 0; reverse < 2; reverse++) {
        layers[0].points[0] = reverse ? 1 : 0;
        layers[1].points[0] = reverse ? 0 : 1;
        REQUIRE(draw_all(layers, 2, actors, 2, home));
        REQUIRE(inspect(raster, WIDTH, HEIGHT, 58, 40, .5f, .5f, 0, 0, 2));
        REQUIRE(inspect_source(raster, 58, 40, 1, 2, 1));
    }
    return true;
}

/* Ordered 2D layers win or lose by authored layer, whatever the mesh's own
 * depth, and replace its visible normal and height. */
TEST_CASE(ordered_layers_override_mesh_depth)
{
    render_mesh_material_actor actor = { box, { 0, 0 } };
    render_material_entity layers[] = { { .inv_rot = { 1, 0, 0, 1 },
                                          .shape_type = RENDER_SHAPE_BOX,
                                          .extent = { 32, 32 },
                                          .points = { 30, 30 },
                                          .scene = { 0, 0, 1, 1 },
                                          .albedo = { 0, 0, 1, 1 },
                                          .absorption = { 1, 1, 1, 1 },
                                          .layer = -1 },
                                        mesh };
    REQUIRE(draw_all(layers, 2, &actor, 1, home));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 2));
    layers[0].layer = 1;
    REQUIRE(draw_all(layers, 2, &actor, 1, home));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 0, 1, 0, 0, 0));
    return true;
}

/* Pitched, rolled and turned boxes draw their known planes' normals and
 * heights. */
TEST_CASE(tilted_meshes_draw_their_planes)
{
    render_mesh_material_actor actor = { box, { 0, 0 } };
    const float angle = .5235987756f;
    VisualTransform *transform = VisualTransform_get(world, box);
    transform->pitch = angle;
    REQUIRE(draw(actor, home));
    REQUIRE(
        inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, -.5f, 0, 2 / cosf(angle) + .5f * tanf(angle))
    );
    transform->pitch = 0;
    transform->roll = angle;
    REQUIRE(draw(actor, home));
    REQUIRE(
        inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, -.5f, 2 / cosf(angle) - .5f * tanf(angle))
    );
    transform->roll = 0;
    transform->scale = 2;
    Orientation_get(world, box)->radians = 1.5707963268f;
    REQUIRE(draw(actor, home));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 4));
    return true;
}

/* A turned box's coverage stays within 3% across eight subpixel phases, some
 * of which cover pixels partly. */
TEST_CASE(subpixel_coverage_stays_even)
{
    Orientation_get(world, box)->radians = .3f;
    float minimum = INFINITY, maximum = 0;
    bool any_partial = false;
    for (int phase = 0; phase < 8; phase++) {
        render_mesh_material_actor actor = { box, { phase / 8.0f, phase / 16.0f } };
        float coverage, emission;
        bool partial;
        REQUIRE(draw(actor, home));
        REQUIRE(energy(raster, WIDTH, HEIGHT, &coverage, &emission, &partial));
        minimum = fminf(minimum, coverage);
        maximum = fmaxf(maximum, coverage);
        any_partial |= partial;
    }
    REQUIRE(any_partial && maximum > 0 && (maximum - minimum) / maximum < .03f);
    return true;
}

/* Moving both the actor and the padded view keeps the same pixel's material,
 * including after the target resizes. */
TEST_CASE(padded_views_keep_material_coordinates)
{
    render_mesh_material_actor actor = { box, { 200, -120 } };
    render_material_view view = home;
    view.min_x += 200;
    view.max_y += 120;
    REQUIRE(draw(actor, view));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 2));
    REQUIRE(render_material_raster_resize(raster, WIDTH * 2, HEIGHT * 2));
    REQUIRE(draw(actor, view));
    REQUIRE(inspect(raster, WIDTH * 2, HEIGHT * 2, 96, 80, 1, 0, 0, 0, 2));
    return true;
}

/* Publishes a mesh of two unit quads facing up at heights .25 and -.25. The
 * front quad's UVs reverse both axes of a planar projection, or sample one
 * green texel when `alternate`; the back quad samples blue everywhere and must
 * never show. */
static void publish_quads(const char *source, bool alternate)
{
    static const float corners[6][2] = { { -.5f, -.5f }, { -.5f, .5f },
                                         { .5f, .5f },   { -.5f, -.5f },
                                         { .5f, .5f },   { .5f, -.5f } };
    render_mesh_asset asset = { .min = { -.5f, -.25f, -.5f },
                                .max = { .5f, .25f, .5f } };
    for (int back = 0; back < 2; back++) {
        for (int i = 0; i < 6; i++) {
            float x = corners[i][0], z = corners[i][1];
            render_mesh_surface_vertex vertex = {
                .position = { x, back ? -.25f : .25f, z },
                .normal = { 0, 1, 0 },
                .uv = { back        ? .25f
                        : alternate ? .75f
                                    : .5f - x,
                        back        ? .75f
                        : alternate ? .25f
                                    : .5f - z },
            };
            apush(asset.indices, acount(asset.vertices));
            apush(asset.vertices, vertex);
        }
    }
    render_mesh_asset_tangents(&asset);
    render_mesh_asset_publish(source, &asset);
}

/* Imported meshes sample their own UVs, hide their back faces by depth and
 * fit the box's geometry, through either of two assets. */
TEST_CASE(imported_meshes_sample_their_uvs)
{
    static unsigned char quadrants[8 * 8 * 4];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            static const unsigned char colors[4][4] = { { 255, 0, 0, 255 },
                                                        { 0, 255, 0, 255 },
                                                        { 0, 0, 255, 255 },
                                                        { 255, 255, 0, 255 } };
            memcpy(quadrants + (y * 8 + x) * 4, colors[(y >= 4) * 2 + (x >= 4)], 4);
        }
    }
    static const test_image image = { "content/uv.png", 8, 8, quadrants };
    test_mount_images(&image, 1);
    publish_quads("content/a.mesh", false);
    publish_quads("content/b.mesh", true);
    behavior_asset *asset = behavior_asset_new("Imported mesh UV regression");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *out = shader_graph_add_output(asset->graph, false);
    node *texture = graph_add_node(asset->graph, registry_node_by_name("Shader Image Texture"));
    pin_value key = { .type = PIN_STR,
                      .text = &(const pin_string){
                          .bytes = "content/uv.png",
                          .length = sizeof("content/uv.png") - 1 } };
    REQUIRE(!graph_set_pin_default(asset->graph, texture, 0, SHADER_IMAGE_INPUT_IMAGE, &key));
    for (int i = SHADER_IMAGE_INPUT_COLOR_SPACE; i < SHADER_IMAGE_INPUT_COUNT; i++) {
        pin_value value = { .type = PIN_I32,
                            .i = i == SHADER_IMAGE_INPUT_FILTER ? SHADER_IMAGE_NEAREST
                                                                : 1 };
        REQUIRE(!graph_set_pin_default(asset->graph, texture, 0, i, &value));
    }
    REQUIRE(!graph_connect_data(asset->graph, texture, 0, out, 0));
    shader_error shader_error;
    REQUIRE(render_shader_upsert(asset, &shader_error));
    MeshAsset mesh_asset = { .source = "content/a.mesh" };
    Shader binding = { .material = asset->name };
    add(box, &COMPONENT_MeshAsset, &mesh_asset);
    add(box, &COMPONENT_Shader, &binding);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    *Geometry3D_get(world, box) = (Geometry3D){ .width = 20, .height = 8, .depth = 16 };
    render_mesh_material_actor actor = { box, { 0, 0 } };
    render_mesh_surface surface = { 0 };
    char error[256];
    VisualTransform_get(world, box)->roll = 1.5707963268f;
    REQUIRE(render_mesh_surface_build(world, box, &actor.position, &surface, error, sizeof error));
    REQUIRE(fabsf(surface.min.x + 10) < .001f && fabsf(surface.max.x - 10) < .001f);
    REQUIRE(fabsf(surface.min.y + 8) < .001f && fabsf(surface.max.y - 8) < .001f);
    REQUIRE(fabsf(surface.min.z + 2) < .001f && fabsf(surface.max.z - 2) < .001f);
    render_mesh_surface_free(&surface);
    VisualTransform_get(world, box)->roll = 0;
    const int x[] = { 43, 53, 43, 53 }, y[] = { 36, 36, 44, 44 };
    const float colors[4][3] = { { 1, 1, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
    for (int round = 0; round < 3; round++) {
        bool alternate = round == 1;
        MeshAsset_get(world, box)->source = alternate ? "content/b.mesh"
                                                      : "content/a.mesh";
        REQUIRE(draw(actor, home));
        float *scene = NULL, *albedo = NULL;
        REQUIRE(read_target(render_material_raster_textures(raster).scene, WIDTH, HEIGHT, &scene));
        REQUIRE(
            read_target(render_material_raster_textures(raster).albedo, WIDTH, HEIGHT, &albedo)
        );
        bool matched = true;
        for (int sample = 0; sample < 4; sample++) {
            int p = (y[sample] * WIDTH + x[sample]) * 4;
            for (int c = 0; c < 3; c++) {
                float expected = alternate ? (c == 1) : colors[sample][c];
                matched &= close_value(scene[p + c], expected, "imported UV color");
            }
            matched &= close_value(albedo[p + 3], 2, "imported frontmost elevation");
        }
        free(scene);
        free(albedo);
        REQUIRE(matched);
    }
    behavior_asset_free(asset);
    return true;
}

/* Flat cards isolate projection: their visible areas are 8x8, 16x16 and 4x4
 * at known distances, their heights decide which one shows, and side faces
 * and near-plane crops draw. */
TEST_CASE(perspective_meshes_keep_their_size_and_depth)
{
    render_material_view view = { .min_x = -38,
                                  .max_y = 33,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .focal_distance = 32,
                                  .center_x = 10,
                                  .center_y = -7 };
    CF_V2 lo, hi;
    REQUIRE(
        render_mesh_project_bounds(view, (CF_V3){ 8, 16, 5 }, (CF_V3){ 12, 16, 9 }, &lo, &hi)
    );
    REQUIRE(lo.x == 6 && hi.x == 14 && lo.y == 3 && hi.y == 11);
    REQUIRE(
        render_mesh_project_bounds(view, (CF_V3){ 8, 30, 5 }, (CF_V3){ 12, 32, 9 }, &lo, &hi)
    );
    REQUIRE(lo.x == -54 && hi.x == 74 && lo.y == -57 && hi.y == 71);
    /* A small near-plane intersection needs a small crop, not a full canvas. */
    REQUIRE(render_mesh_project_bounds(
        view,
        (CF_V3){ 9.875f, 30, 6.875f },
        (CF_V3){ 10.125f, 32, 7.125f },
        &lo,
        &hi
    ));
    REQUIRE(lo.x == 6 && hi.x == 14 && lo.y == 3 && hi.y == 11);
    /* This whole clipped volume is off-screen even though it crosses near. */
    REQUIRE(
        render_mesh_project_bounds(view, (CF_V3){ 20, 30, 5 }, (CF_V3){ 22, 32, 9 }, &lo, &hi)
    );
    REQUIRE(lo.x > view.min_x + view.width);
    REQUIRE(
        !render_mesh_project_bounds(view, (CF_V3){ 8, 33, 5 }, (CF_V3){ 12, 34, 9 }, &lo, &hi)
    );
    Geometry3D card = { .primitive = PRIMITIVE_BOX, .width = 8, .depth = 8, .height = .002f };
    ecs_entity front = mesh_box(card, 0xff0000ff, 0);
    render_mesh_material_actor actor = { front, { 10, 7 } };
    const float elevations[] = { 0, 16, -32, 33 };
    const float areas[] = { 64, 256, 16, 0 };
    for (int i = 0; i < 4; i++) {
        Geometry3D_get(world, front)->elevation = elevations[i] - .001f;
        float area, emission;
        bool partial;
        REQUIRE(draw(actor, view));
        REQUIRE(energy(raster, WIDTH, HEIGHT, &area, &emission, &partial));
        REQUIRE(fabsf(area - areas[i]) <= 1);
        REQUIRE(i == 3 || inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, elevations[i]));
    }
    /* Two cards share one depth target whatever the caller order. Their
     * elevations, rather than their projected size, decide which material
     * supplies the visible pixel. */
    card.elevation = -32.001f;
    ecs_entity rear = mesh_box(card, 0x0000ffff, 0);
    Geometry3D_get(world, front)->elevation = 15.999f;
    render_mesh_material_actor actors[] = { actor, { rear, { 10, 7 } } };
    render_material_entity layers[] = { mesh, mesh };
    for (int reverse = 0; reverse < 2; reverse++) {
        layers[0].points[0] = reverse;
        layers[1].points[0] = !reverse;
        REQUIRE(draw_all(layers, 2, actors, 2, view));
        REQUIRE(inspect(raster, WIDTH, HEIGHT, 48, 40, 1, 0, 0, 0, 16));
    }
    /* Perspective exposes the left side of an offset box, whose world normal
     * has zero Y. Orthographic back-face rejection would hide it. */
    Geometry3D_get(world, front)->height = 8;
    Geometry3D_get(world, front)->elevation = 0;
    actor.position.x = 26;
    REQUIRE(draw(actor, view));
    float *normals = NULL, *colors = NULL;
    render_material_textures textures = render_material_raster_textures(raster);
    REQUIRE(read_target(textures.normal, WIDTH, HEIGHT, &normals));
    REQUIRE(read_target(textures.scene, WIDTH, HEIGHT, &colors));
    int pixel = (40 * WIDTH + 60) * 4;
    bool side = normals[pixel] < -.95f && colors[pixel + 3] > .99f;
    free(normals);
    free(colors);
    REQUIRE(side);
    /* A narrow volume straddles the near plane. Its visible side face still
     * draws, while material work stays below a full-canvas fallback. */
    actor.position.x = 10.375f;
    Geometry3D *near = Geometry3D_get(world, front);
    near->width = near->depth = near->height = .25f;
    near->elevation = 31;
    REQUIRE(draw(actor, view));
    render_mesh_material_work work = render_material_raster_mesh_work(raster);
    REQUIRE(work.surface_pixels > 0 && work.surface_pixels < WIDTH * HEIGHT / 4);
    float area, emission;
    bool partial;
    REQUIRE(energy(raster, WIDTH, HEIGHT, &area, &emission, &partial));
    REQUIRE(area > 1);
    /* The crop origin moves independently of the eye: the same card stays
     * centered in the shifted padded frame and keeps its height. */
    near->width = near->depth = 8;
    near->height = .002f;
    near->elevation = 15.999f;
    actor.position.x = 10;
    view.min_x -= 8;
    view.max_y += 8;
    REQUIRE(draw(actor, view));
    REQUIRE(inspect(raster, WIDTH, HEIGHT, 56, 48, 1, 0, 0, 0, 16));
    return true;
}

void test_mesh_materials()
{
    test_open_hidden_app("mesh-materials", WIDTH, HEIGHT);
    test_graph_packs_install();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(boxes_draw_their_visible_faces);
    RUN_TEST_CASE(meshes_win_by_depth_not_order);
    RUN_TEST_CASE(ordered_layers_override_mesh_depth);
    RUN_TEST_CASE(tilted_meshes_draw_their_planes);
    RUN_TEST_CASE(subpixel_coverage_stays_even);
    RUN_TEST_CASE(padded_views_keep_material_coordinates);
    RUN_TEST_CASE(imported_meshes_sample_their_uvs);
    RUN_TEST_CASE(perspective_meshes_keep_their_size_and_depth);
    pu_clear_setup();
    test_graph_packs_restore();
    cf_destroy_app();
}
