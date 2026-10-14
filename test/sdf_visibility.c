#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/fullscreen.h"
#include "render/material_raster.h"
#include "render/mesh_material.h"
#include "render/mesh_surface.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    WIDTH = 96,
    HEIGHT = 80,
    TARGETS = 5
};
typedef struct
{
    int width, height;
    float *data[TARGETS];
} snapshot;

static node *op(graph *g, const char *name)
{
    return graph_add_node(g, registry_node_by_name(name));
}
static void wire(graph *g, node *a, node *b, int input)
{
    assert(!graph_connect_data(g, a, 0, b, input));
}
static void number(graph *g, node *n, int input, float x)
{
    pin_value value = { .type = PIN_F32, .f = x };
    assert(!graph_set_pin_default(g, n, 0, input, &value));
}
static behavior_asset *shape(const char *name, bool ring)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_SHAPE;
    asset->bounds[0] = asset->bounds[1] = 12;
    graph *g = asset->graph;
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
        number(g, major, 1, 8);
        wire(g, major, cross, 0);
        wire(g, y, cross, 1);
        wire(g, cross, length, 0);
    } else {
        wire(g, p, length, 0);
    }
    node *subtract = op(g, "Shader Subtract");
    wire(g, length, subtract, 0);
    number(g, subtract, 1, ring ? 2 : 10);
    wire(g, subtract, shader_graph_add_output(g, true), 0);
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    return asset;
}
static behavior_asset *cutout()
{
    behavior_asset *asset = behavior_asset_new("Visibility center cutout");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *p = op(g, "Shader ObjectPosition"), *x = op(g, "Shader X"),
         *absolute = op(g, "Shader Abs"), *step = op(g, "Shader Step"),
         *out = shader_graph_add_output(g, false);
    wire(g, p, x, 0);
    wire(g, x, absolute, 0);
    wire(g, absolute, step, 0);
    number(g, step, 1, 3);
    wire(g, step, out, 5);
    shader_error error = { 0 };
    assert(render_shader_upsert(asset, &error));
    return asset;
}
static ecs_entity actor(ecs_world *world, const char *shape_name, float elevation)
{
    ecs_entity e = ecs_world_create_entity(world);
    Geometry3D geometry = { .primitive = PRIMITIVE_BOX,
                            .width = 24,
                            .depth = 24,
                            .height = 24,
                            .elevation = elevation };
    VisualTransform transform = { .scale = 1 };
    Orientation orientation = { 0 };
    Material material = { .scene = shape_name ? 0xff0000ff : 0x0000ffff,
                          .albedo = shape_name ? 0xff0000ff : 0x0000ffff,
                          .absorption = 1,
                          .edge_light = 1 };
    Shader shader = { .shape = shape_name };
    SdfSurface3D sdf = { .enabled = shape_name != NULL };
    MeshSurface2D mesh = { .enabled = shape_name == NULL };
#define ADD(type, value)                                                       \
    assert(ecs_world_record_add(world, e, COMPONENT_##type.id, &(value), 0) == ECS_OK)
    ADD(Geometry3D, geometry);
    ADD(VisualTransform, transform);
    ADD(Orientation, orientation);
    ADD(Material, material);
    ADD(Shader, shader);
    ADD(SdfSurface3D, sdf);
    ADD(MeshSurface2D, mesh);
#undef ADD
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return e;
}
/* A mesh asset of the generated box, one vertex per distinct corner, read
 * back from its file bytes so drawing it gathers corners through indices. */
static void publish_indexed_box(const char *source)
{
    render_mesh_surface_vertex *box = NULL;
    char error[128];
    assert(render_mesh_surface_vertices(PRIMITIVE_BOX, &box, error, sizeof error));
    render_mesh_asset asset = { .min = { -.5f, -.5f, -.5f }, .max = { .5f, .5f, .5f } };
    for (int i = 0; i < acount(box); i++) {
        int found = 0;
        while (found < acount(asset.vertices) &&
               memcmp(&asset.vertices[found], &box[i], sizeof *box)) {
            found++;
        }
        if (found == acount(asset.vertices)) {
            apush(asset.vertices, box[i]);
        }
        apush(asset.indices, found);
    }
    afree(box);
    assert(acount(asset.vertices) < acount(asset.indices) && acount(asset.indices) % 9 == 0);
    /* Three material groups, each a third of the triangles, all inheriting the
     * entity's material, so each group draws as its own slice. */
    for (int i = 0; i < 3; i++) {
        int count = acount(asset.indices) / 3;
        render_mesh_group group = { .first = i * count,
                                    .count = count,
                                    .part = -1,
                                    .asset = xstrdup("Box"),
                                    .name = xstrdup("Faces"),
                                    .path = xstrdup("Box/Faces"),
                                    .material = xstrdup(""),
                                    .material_scope = xstrdup("") };
        apush(asset.groups, group);
    }
    render_mesh_asset_tangents(&asset);
    unsigned char *bytes;
    size_t length, budget = SIZE_MAX;
    render_mesh_asset file = { 0 };
    assert(render_mesh_asset_prepared_write(&asset, &bytes, &length, error, sizeof error));
    assert(
        render_mesh_asset_prepared_read_budget(bytes, length, &budget, &file, error, sizeof error)
    );
    free(bytes);
    render_mesh_asset_free(&asset);
    render_mesh_asset_publish(source, &file);
}
static ecs_entity indexed_box(ecs_world *world)
{
    publish_indexed_box("content/meshes/box.mesh");
    ecs_entity e = actor(world, NULL, 24);
    MeshAsset imported = { .source = "content/meshes/box.mesh" };
    assert(ecs_world_record_add(world, e, COMPONENT_MeshAsset.id, &imported, 0) == ECS_OK);
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
static bool read(CF_Texture texture, int width, int height, int active_width, int active_height, float **out)
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
    int bytes = width * height * 4 * (int)sizeof(uint16_t);
    uint16_t *pixels = xmalloc(bytes);
    bool ok = rb.id && cf_readback_ready(rb) && cf_readback_size(rb) == bytes &&
              cf_readback_data(rb, pixels, bytes) == bytes;
    *out = xmalloc(active_width * active_height * 4 * sizeof(float));
    for (int y = 0; y < active_height && ok; y++) {
        for (int x = 0; x < active_width * 4; x++) {
            float f = half(pixels[y * width * 4 + x]);
            (*out)[y * active_width * 4 + x] = f;
            ok &= isfinite(f);
        }
    }
    free(pixels);
    if (rb.id) {
        cf_destroy_readback(rb);
    }
    cf_destroy_canvas(canvas);
    return ok;
}
static void release(snapshot *s)
{
    for (int i = 0; i < TARGETS; i++) {
        free(s->data[i]);
    }
    *s = (snapshot){ 0 };
}
static bool raw(render_mesh_material *pass, snapshot *s)
{
    render_mesh_material_textures t = render_mesh_material_get_textures(pass);
    CF_Texture targets[] = { t.scene, t.emission, t.absorption, t.albedo, t.normal };
    int active[2], capacity[2];
    render_mesh_material_extent(pass, active, capacity);
    s->width = active[0];
    s->height = active[1];
    bool ok = true;
    for (int i = 0; i < TARGETS; i++) {
        ok &= read(targets[i], capacity[0], capacity[1], active[0], active[1], &s->data[i]);
    }
    return ok;
}
static bool flattened(render_material_raster *r, snapshot *s)
{
    render_material_textures t = render_material_raster_textures(r);
    CF_Texture targets[] = { t.scene, t.source, t.albedo, t.normal, t.cover };
    s->width = WIDTH;
    s->height = HEIGHT;
    bool ok = true;
    for (int i = 0; i < TARGETS; i++) {
        ok &= read(targets[i], WIDTH, HEIGHT, WIDTH, HEIGHT, &s->data[i]);
    }
    return ok;
}
static bool matches(snapshot *a, snapshot *b, const char *label)
{
    if (a->width != b->width || a->height != b->height) {
        return false;
    }
    int covered = 0;
    float maximum_delta = 0;
    for (int i = 3; i < a->width * a->height * 4; i += 4) {
        covered += a->data[0][i] > .5f;
        if (a->data[0][i] != b->data[0][i]) {
            fprintf(
                stderr,
                "Visibility %s coverage pixel %d: %g != %g\n",
                label,
                i / 4,
                a->data[0][i],
                b->data[0][i]
            );
            return false;
        }
    }
    if (!covered) {
        fprintf(stderr, "Visibility %s has no reference coverage\n", label);
        return false;
    }
    for (int t = 0; t < TARGETS; t++) {
        for (int i = 0; i < a->width * a->height * 4; i++) {
            maximum_delta = fmaxf(maximum_delta, fabsf(a->data[t][i] - b->data[t][i]));
            if (fabsf(a->data[t][i] - b->data[t][i]) <=
                .003f + fabsf(a->data[t][i]) * .002f) {
                continue;
            }
            fprintf(
                stderr,
                "Visibility %s target %d pixel %d channel %d: %g != %g\n",
                label,
                t,
                i / 4,
                i % 4,
                a->data[t][i],
                b->data[t][i]
            );
            return false;
        }
    }
    printf("SDF visibility parity %s: covered %d, max delta %.8g\n", label, covered, maximum_delta);
    return true;
}
static bool raw_draw(
    render_mesh_material *pass,
    ecs_world *world,
    render_mesh_material_actor *actors,
    int count,
    render_material_view view,
    bool enabled,
    snapshot *out
)
{
    render_mesh_material_set_hidden_rejection(pass, enabled);
    cf_app_update(NULL);
    bool ok = render_mesh_material_draw(pass, world, actors, count, view, 0);
    cf_app_draw_onto_screen(false);
    return ok && raw(pass, out);
}
/* An indexed mesh asset of the box draws, bounds and picks like the box. */
static bool indexed_copy(
    render_mesh_material *pass,
    ecs_world *world,
    ecs_entity box,
    ecs_entity copy,
    render_material_view view
)
{
    *Geometry3D_get(world, copy) = *Geometry3D_get(world, box);
    render_mesh_material_actor actors[] = { { box, { 0, 0 } }, { copy, { 0, 0 } } };
    snapshot a = { 0 }, b = { 0 };
    bool ok = raw_draw(pass, world, actors, 1, view, false, &a) &&
              raw_draw(pass, world, actors + 1, 1, view, false, &b) &&
              matches(&a, &b, "indexed mesh asset");
    release(&a);
    release(&b);
    render_mesh_surface surfaces[2] = { 0 };
    char error[128];
    for (int i = 0; i < 2; i++) {
        ok = render_mesh_surface_build(
                 world,
                 actors[i].entity,
                 &actors[i].position,
                 &surfaces[i],
                 error,
                 sizeof error
             ) &&
             ok;
    }
    /* Each material group draws its own slice of the indices. */
    ok = ok && acount(surfaces[1].parts) == 3 &&
         surfaces[1].parts[2].first_index == 24 &&
         render_mesh_surface_corner_count(&surfaces[1].parts[2]) == 12;
    ok = ok && !memcmp(&surfaces[0].min, &surfaces[1].min, sizeof surfaces[0].min) &&
         !memcmp(&surfaces[0].max, &surfaces[1].max, sizeof surfaces[0].max);
    const CF_V2 points[] = { { 0, 0 }, { 25, -12 }, { 40, 0 } };
    for (int i = 0; i < 3; i++) {
        float heights[2] = { 0, 0 };
        bool hit = render_mesh_surface_pick(&surfaces[0], points[i], &heights[0]);
        ok = ok && hit == (i < 2) &&
             render_mesh_surface_pick(&surfaces[1], points[i], &heights[1]) == hit &&
             heights[0] == heights[1];
    }
    render_mesh_surface_free(&surfaces[0]);
    render_mesh_surface_free(&surfaces[1]);
    printf("SDF visibility indexed mesh asset: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
static bool flat_draw(
    render_material_raster *r,
    ecs_world *world,
    render_mesh_material_actor *actors,
    int count,
    render_material_view view,
    bool enabled,
    bool mixed,
    snapshot *out
)
{
    render_material_entity layers[3] = { { .shape_type = RENDER_SHAPE_MESH },
                                         { .shape_type = RENDER_SHAPE_MESH },
                                         { .shape_type = RENDER_SHAPE_MESH } };
    layers[1].points[0] = 1;
    layers[2].points[0] = 1;
    int layers_count = count;
    if (mixed && count == 2) {
        layers[1] = (render_material_entity){ .shape_type = RENDER_SHAPE_BOX,
                                              .inv_rot = { 1, 0, 0, 1 },
                                              .pos = { 0, 0 },
                                              .extent = { 6, 6 },
                                              .points = { 6, 6 },
                                              .scene = { 0, .5f, 0, .5f },
                                              .albedo = { 0, .5f, 0, .5f },
                                              .absorption = { .5f, .5f, .5f, .5f } };
        layers_count = 3;
    }
    render_material_raster_set_hidden_rejection(r, enabled);
    cf_app_update(NULL);
    bool ok = render_material_raster_draw_mixed(r, layers, layers_count, view, world, actors, count, 0);
    cf_app_draw_onto_screen(false);
    return ok && flattened(r, out);
}
static bool pair(
    render_mesh_material *pass,
    render_material_raster *r,
    ecs_world *world,
    render_mesh_material_actor actors[2],
    int count,
    render_material_view view,
    bool mixed,
    const char *label
)
{
    snapshot a = { 0 }, b = { 0 };
    bool ok = raw_draw(pass, world, actors, count, view, false, &a);
    render_mesh_material_work off = render_mesh_material_last_work(pass);
    ok = ok && !off.depth_copies && !off.depth_copy_pixels &&
         raw_draw(pass, world, actors, count, view, true, &b) &&
         matches(&a, &b, label);
    render_mesh_material_work on = render_mesh_material_last_work(pass);
    if (count == 2 && SdfSurface3D_get(world, actors[1].entity)->enabled &&
        on.depth_copies < 1) {
        fprintf(stderr, "Visibility %s did not submit depth copy\n", label);
        ok = false;
    }
    if (on.depth_copies && !on.depth_copy_pixels) {
        ok = false;
    }
    release(&a);
    release(&b);
    ok = flat_draw(r, world, actors, count, view, false, mixed, &a) &&
         flat_draw(r, world, actors, count, view, true, mixed, &b) &&
         matches(&a, &b, label) && ok;
    off = render_material_raster_mesh_work(r);
    printf(
        "SDF visibility %s: %s; raw copies %d pixels %zu; flattened copies "
        "%d\n",
        label,
        ok ? "PASS" : "FAIL",
        on.depth_copies,
        on.depth_copy_pixels,
        off.depth_copies
    );
    release(&a);
    release(&b);
    return ok;
}
static bool same_graph_pair(
    render_mesh_material *pass,
    render_material_raster *r,
    ecs_world *world,
    render_mesh_material_actor actors[2],
    render_material_view view,
    const char *label
)
{
    snapshot baseline = { 0 }, optimized = { 0 };
    bool ok = raw_draw(pass, world, actors, 2, view, false, &baseline);
    render_mesh_material_work off = render_mesh_material_last_work(pass);
    ok = raw_draw(pass, world, actors, 2, view, true, &optimized) && ok;
    render_mesh_material_work on = render_mesh_material_last_work(pass);
    ok = ok && matches(&baseline, &optimized, label) && off.sdf_draw_calls == 1 &&
         !off.depth_copies && on.sdf_draw_calls == 2 && on.depth_copies == 1;
    printf(
        "SDF visibility %s: %s; compatible draws %d -> %d, copies %d -> %d\n",
        label,
        ok ? "PASS" : "FAIL",
        off.sdf_draw_calls,
        on.sdf_draw_calls,
        off.depth_copies,
        on.depth_copies
    );
    release(&baseline);
    release(&optimized);
    return pair(pass, r, world, actors, 2, view, false, label) && ok;
}
static bool diagnostic(
    render_mesh_material *pass,
    ecs_world *world,
    render_mesh_material_actor actors[2],
    bool hole
)
{
    snapshot s = { 0 };
    render_mesh_material_set_sdf_trace_debug(pass, 2);
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    bool ok = raw_draw(pass, world, actors, 2, view, true, &s);
    float rect[4];
    render_mesh_material_rect(pass, rect);
    int x = 48 - (int)rect[0], y = 40 - (int)rect[1];
    if (x < 0 || y < 0 || x >= s.width || y >= s.height) {
        ok = false;
    }
    if (ok) {
        int i = (y * s.width + x) * 4;
        if (hole) {
            ok &= s.data[0][i] > 0 && s.data[0][i + 1] == 1;
        } else {
            ok &= s.data[0][i] == 0 && s.data[0][i + 1] == 1 && s.data[0][i + 2] == 1;
        }
        printf(
            "SDF visibility diagnostic %s: trace calls %.0f, green %g, blue "
            "%g\n",
            hole ? "hole" : "hidden",
            s.data[0][i] * 128,
            s.data[0][i + 1],
            s.data[0][i + 2]
        );
        if (!ok) {
            fprintf(
                stderr,
                "Visibility diagnostic %s: %g %g %g\n",
                hole ? "hole" : "hidden",
                s.data[0][i],
                s.data[0][i + 1],
                s.data[0][i + 2]
            );
        }
    }
    release(&s);
    render_mesh_material_set_sdf_trace_debug(pass, 0);
    return ok;
}
/* Copies run while a cropped source is bound, then the ordinary draw contract
 * explicitly rebinds its canvas and crop before redrawing the source. */
static bool depth_snapshot_backend()
{
    enum
    {
        SIDE = 8
    };
    const char *vertex = "layout(location=0) in vec2 in_pos;\n"
                         "void main(){gl_Position=vec4(in_pos,0.0,1.0);}\n";
    const char
        *seed_source = "layout(location=0) out vec4 out_value;\n"
                       "layout(set=3,binding=0) uniform uniform_block {vec4 "
                       "seed;};\n"
                       "void main(){gl_FragDepth=seed.x;out_value=seed;}\n";
    const char *decode_source = "layout(location=0) out vec4 out_value;\n"
                                "layout(set=2,binding=0) uniform sampler2D "
                                "depth_image;\n"
                                "layout(set=2,binding=1) uniform sampler2D "
                                "color_image;\n"
                                "void main(){ivec2 p=ivec2(gl_FragCoord.xy);"
                                "float d=texelFetch(depth_image,p,0).r;"
                                "float c=texelFetch(color_image,p,0).r;"
                                "out_value=vec4(c,d,0,1);}\n";
    CF_Shader seed = cf_make_shader_from_source(vertex, seed_source);
    CF_Shader decode = cf_make_shader_from_source(vertex, decode_source);
    CF_Mesh mesh = render_make_pass_mesh();
    CF_Material seed_material = cf_make_material(),
                decode_material = cf_make_material();
    CF_RenderState state = cf_render_state_defaults();
    state.depth_write_enabled = true;
    state.depth_compare = CF_COMPARE_FUNCTION_ALWAYS;
    cf_material_set_render_state(seed_material, state);
    CF_CanvasParams cp = cf_canvas_defaults(SIDE, SIDE);
    cp.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    cp.depth_stencil_enable = true;
    cp.depth_stencil_target.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    cp.depth_stencil_target.filter = CF_FILTER_NEAREST;
    cp.depth_stencil_target.mip_filter = CF_MIP_FILTER_NEAREST;
    CF_Canvas source = cf_make_canvas(cp), destination = cf_make_canvas(cp),
              output = cf_make_canvas(cp);
    bool ok = seed.id && decode.id && mesh.id && source.id && destination.id &&
              output.id;
    if (ok) {
        /* Isolate the copy fixture from the observed Zink endpoint-clear issue.
         * The full .25 seed overwrites this; see docs/testing.md (depth snapshot). */
        cf_canvas_set_clear_depth_stencil(source, .75f, 0);
        cf_app_update(NULL);
        cf_apply_canvas(source, true);
        cf_apply_mesh(mesh);
        float value[4] = { .25f };
        cf_material_set_uniform_fs(seed_material, "seed", value, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_apply_shader(seed, seed_material);
        cf_apply_viewport(0, 0, SIDE, SIDE);
        cf_apply_scissor(0, 0, SIDE, SIDE);
        cf_draw_elements();
        value[0] = .5f;
        cf_material_set_uniform_fs(seed_material, "seed", value, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_apply_canvas(source, false);
        cf_apply_mesh(mesh);
        cf_apply_shader(seed, seed_material);
        cf_apply_viewport(0, 0, SIDE, SIDE);
        cf_apply_scissor(3, 3, 2, 2);
        cf_draw_elements();

        /* Confirm the source contains the crop before testing the depth blit.
         * Decode into a distinct canvas to avoid sampling an attached texture. */
        cf_material_set_texture_fs(decode_material, "depth_image", cf_canvas_get_depth_stencil_target(source));
        cf_material_set_texture_fs(decode_material, "color_image", cf_canvas_get_target(source));
        cf_apply_canvas(output, true);
        cf_apply_mesh(mesh);
        cf_apply_shader(decode, decode_material);
        cf_apply_viewport(0, 0, SIDE, SIDE);
        cf_apply_scissor(0, 0, SIDE, SIDE);
        cf_draw_elements();
        cf_app_draw_onto_screen(false);
        float *source_pixels = NULL;
        bool source_read = read(cf_canvas_get_target(output), SIDE, SIDE, SIDE, SIDE, &source_pixels);
        ok &= source_read;
        for (int y = 0; y < SIDE && source_read && source_pixels; y++) {
            for (int x = 0; x < SIDE; x++) {
                bool cropped = x >= 3 && x < 5 && y >= 3 && y < 5;
                float expected = cropped ? .5f : .25f;
                int i = (y * SIDE + x) * 4;
                float color = source_pixels[i];
                float depth = source_pixels[i + 1];
                if (fabsf(color - expected) > .001f || fabsf(depth - expected) > .001f) {
                    fprintf(
                        stderr,
                        "Depth snapshot source before copy pixel %d,%d: color "
                        "%g != %g; depth %g != %g\n",
                        x,
                        y,
                        color,
                        expected,
                        depth,
                        expected
                    );
                    ok = false;
                }
            }
        }
        free(source_pixels);
        /* The readback draw ended the current SDL_GPU command buffer. */
        cf_app_update(NULL);

        /* Leave a real cropped draw as the last backend state before blitting. */
        cf_apply_canvas(source, false);
        cf_apply_mesh(mesh);
        cf_material_set_uniform_fs(seed_material, "seed", value, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_apply_shader(seed, seed_material);
        cf_apply_viewport(0, 0, SIDE, SIDE);
        cf_apply_scissor(3, 3, 2, 2);
        cf_draw_elements();
        cf_canvas_copy_depth(destination, source);
        value[0] = .125f;
        cf_apply_canvas(source, false);
        cf_apply_mesh(mesh);
        cf_material_set_uniform_fs(seed_material, "seed", value, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_apply_shader(seed, seed_material);
        cf_apply_viewport(0, 0, SIDE, SIDE);
        cf_apply_scissor(3, 3, 2, 2);
        cf_draw_elements();
        CF_Canvas inputs[] = { destination, source };
        for (int input = 0; input < 2; input++) {
            cf_material_set_texture_fs(
                decode_material,
                "depth_image",
                cf_canvas_get_depth_stencil_target(inputs[input])
            );
            cf_material_set_texture_fs(decode_material, "color_image", cf_canvas_get_target(source));
            cf_apply_canvas(output, true);
            cf_apply_mesh(mesh);
            cf_apply_shader(decode, decode_material);
            cf_apply_viewport(0, 0, SIDE, SIDE);
            cf_apply_scissor(0, 0, SIDE, SIDE);
            cf_draw_elements();
            cf_app_draw_onto_screen(false);
            float *pixels = NULL;
            bool output_read = read(cf_canvas_get_target(output), SIDE, SIDE, SIDE, SIDE, &pixels);
            ok &= output_read;
            for (int y = 0; y < SIDE && output_read && pixels; y++) {
                for (int x = 0; x < SIDE; x++) {
                    bool cropped = x >= 3 && x < 5 && y >= 3 && y < 5;
                    float expected_depth = cropped ? (input ? .125f : .5f) : .25f;
                    float expected_color = cropped ? .125f : .25f;
                    int i = (y * SIDE + x) * 4;
                    float color = pixels[i];
                    float depth = pixels[i + 1];
                    if (fabsf(color - expected_color) > .001f ||
                        fabsf(depth - expected_depth) > .001f) {
                        fprintf(
                            stderr,
                            "Depth snapshot backend %s pixel %d,%d: color %g "
                            "!= %g; depth %g != %g\n",
                            input ? "source redraw" : "full copy",
                            x,
                            y,
                            color,
                            expected_color,
                            depth,
                            expected_depth
                        );
                        ok = false;
                    }
                }
            }
            free(pixels);
            if (input == 0) {
                cf_app_update(NULL);
            }
        }
    }
    cf_destroy_canvas(source);
    cf_destroy_canvas(destination);
    cf_destroy_canvas(output);
    cf_destroy_material(seed_material);
    cf_destroy_material(decode_material);
    cf_destroy_mesh(mesh);
    cf_destroy_shader(seed);
    cf_destroy_shader(decode);
    puts(ok ? "Depth snapshot backend PASS: full copy ignores cropped draw; cropped source redraw preserved" : "Depth snapshot backend FAIL");
    return ok;
}
bool test_sdf_visibility()
{
    behavior_asset *sphere = shape("Visibility rear sphere", false),
                   *front = shape("Visibility front sphere", false),
                   *ring = shape("Visibility ring", true), *material = cutout();
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = {
        &COMPONENT_Geometry3D,    &COMPONENT_VisualTransform,
        &COMPONENT_Orientation,   &COMPONENT_Material,
        &COMPONENT_Shader,        &COMPONENT_SdfSurface3D,
        &COMPONENT_MeshSurface2D, &COMPONENT_MeshParts,
        &COMPONENT_MeshAsset
    };
    for (int i = 0; i < (int)(sizeof components / sizeof *components); i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    ecs_entity mesh = actor(world, NULL, 24), rear = actor(world, sphere->name, 0),
               solid = actor(world, front->name, 24),
               torus = actor(world, ring->name, 24), copy = indexed_box(world);
    Geometry3D *bounds = Geometry3D_get(world, mesh);
    bounds->width = bounds->depth = 60;
    bounds->height = 4;
    render_mesh_material *pass = render_mesh_material_new();
    render_material_raster *r = render_material_raster_new();
    SurfaceQuality quality = { .resolution = SURFACE_FULL,
                               .sdf_normal_mode = SDF_NORMAL_TETRAHEDRAL };
    render_mesh_material_set_quality(pass, quality);
    render_material_raster_set_quality(r, quality);
    bool backend_ok = depth_snapshot_backend();
    bool ok = render_mesh_material_resize(pass, WIDTH, HEIGHT) &&
              render_material_raster_resize(r, WIDTH, HEIGHT);
    render_material_view view = { -48, 40, WIDTH, HEIGHT };
    render_mesh_material_actor actors[2] = { { mesh, { 0, 0 } }, { rear, { 0, 0 } } };
    ok &= pair(pass, r, world, actors, 2, view, false, "mesh fully hides sphere");
    ok &= diagnostic(pass, world, actors, false);
    ok &= indexed_copy(pass, world, mesh, copy, view);
    Shader_get(world, mesh)->material = material->name;
    ok &= pair(pass, r, world, actors, 2, view, false, "material cutout");
    ok &= diagnostic(pass, world, actors, true);
    Shader_get(world, mesh)->material = NULL;
    actors[0].entity = solid;
    ok &= pair(pass, r, world, actors, 2, view, false, "SDF occluder");
    ok &= diagnostic(pass, world, actors, false);
    Shader_get(world, solid)->shape = sphere->name;
    Geometry3D_get(world, solid)->elevation = 28;
    ok &= same_graph_pair(pass, r, world, actors, view, "same graph hidden SDF");
    ok &= diagnostic(pass, world, actors, false);
    actors[0].entity = rear;
    actors[1].entity = solid;
    ok &= same_graph_pair(pass, r, world, actors, view, "same graph reversed hidden SDF");
    ok &= diagnostic(pass, world, actors, false);
    Shader_get(world, solid)->shape = front->name;
    Geometry3D_get(world, solid)->elevation = 24;
    actors[1].entity = rear;
    actors[0].entity = torus;
    ok &= pair(pass, r, world, actors, 2, view, false, "SDF hole");
    actors[0].entity = mesh;
    actors[0].position.x = 28;
    ok &= pair(pass, r, world, actors, 2, view, false, "partial visibility");
    actors[0].position.x = 0;
    actors[0].entity = rear;
    actors[1].entity = mesh;
    ok &= pair(pass, r, world, actors, 2, view, false, "back to front order");
    actors[0].entity = solid;
    actors[1].entity = rear;
    Geometry3D_get(world, solid)->elevation = 0;
    Material_get(world, solid)->scene = Material_get(world, solid)->albedo = 0x0000ffff;
    ok &= pair(pass, r, world, actors, 2, view, false, "equal depth caller order");
    Material_get(world, solid)->scene = Material_get(world, solid)->albedo = 0x0000ffff;
    actors[0].entity = rear;
    actors[1].entity = solid;
    ok &= pair(pass, r, world, actors, 2, view, false, "equal depth reversed order");
    actors[0].entity = mesh;
    actors[1].entity = rear;
    VisualTransform_get(world, mesh)->pitch = .3f;
    VisualTransform_get(world, rear)->scale = .8f;
    VisualTransform_get(world, rear)->yaw = .4f;
    actors[1].position = (Transform){ 3, -2 };
    view.focal_distance = 64;
    ok &= pair(pass, r, world, actors, 2, view, false, "perspective transformed surfaces");
    view.focal_distance = 32;
    bounds->elevation = 31;
    ok &= pair(pass, r, world, actors, 2, view, false, "near clipping");
    view.focal_distance = 0;
    bounds->elevation = 24;
    VisualTransform_get(world, mesh)->pitch = 0;
    VisualTransform_get(world, rear)->scale = 1;
    actors[1].position = (Transform){ 0 };
    ok &= pair(pass, r, world, actors, 2, view, true, "mixed 2D layer");
    /* Same retained pass after a frame with an occluder must clear sampled depth. */
    actors[0] = actors[1];
    ok &= pair(pass, r, world, actors, 1, view, false, "later frame occluder removed");
    render_mesh_material_free(pass);
    render_material_raster_free(r);
    ecs_world_free(world);
    render_mesh_assets_clear();
    behavior_asset_free(sphere);
    behavior_asset_free(front);
    behavior_asset_free(ring);
    behavior_asset_free(material);
    return ok && backend_ok;
}
TEST_CASE(meshes_and_sdfs_hide_each_other)
{
    REQUIRE(test_sdf_visibility());
    return true;
}

void test_render_sdf_visibility()
{
#ifdef __linux__
    /* Linux agents may have no display, and the offscreen driver still reaches
     * Vulkan. Metal and D3D12 need the platform's own window. A caller's
     * driver wins, under either of SDL's names. */
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    /* The tools binary is not installed beside the engine's resources, and the
     * check must cover this tree's shaders whatever the caller exported. */
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 1);
    CF_Result made = cf_make_app(
        "sdf-visibility",
        0,
        0,
        0,
        WIDTH,
        HEIGHT,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        NULL
    );
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
    RUN_TEST_CASE(meshes_and_sdfs_hide_each_other);
    registry_set_packs(NULL, 0);
    cf_destroy_app();
}
