#include "render/scene3d_gi.h"

#include "platform/cute.h"
#include "render/fullscreen.h"
#include "render/scene3d_terrain.h"
#include "render/shader_program.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* A solid's row: world-to-shape rows, albedo, emission, sphere and the
     * drawn box's edges. */
    SOLID_TEXELS = 9,
    /* Terrain proxies are counted against the same limit. */
    SOLID_LIMIT = RENDER_TERRAIN_GI_PROXY_LIMIT,
    /* The BVH over the solids: the most solids in a leaf, each node's texels
     * and the nodes in a row of their texture, as scene3d_gi_trace.frag
     * reads them, and the deepest tree its stack holds. */
    LEAF_SOLIDS = 4,
    NODE_TEXELS = 2,
    NODES_PER_ROW = 64,
    BVH_DEPTH = 32,
    /* Each probe's tiles, with a texel of border around each side. */
    IRRADIANCE_TEXELS = 8,
    DISTANCE_TEXELS = 16,
    TILES_PER_ROW = 64,
    /* The rays canvas's width, as scene3d_gi_pass.shd's GI_RAY_ROW: the rays
     * of as many probes as fit in a row, within the 4096 texels every
     * backend allows. */
    RAY_ROW = 4096,
    /* The most frames of light counted, well past the twenty after which a
     * texel keeps its full share. */
    SETTLED = 100
};

/* Each quality's most probes and its rays per probe. */
typedef struct
{
    int probes, rays;
} probe_quality;

static const probe_quality qualities[] = {
    [GLOBAL_ILLUMINATION_LOW] = { 512, 32 },
    [GLOBAL_ILLUMINATION_MEDIUM] = { 2048, 64 },
    [GLOBAL_ILLUMINATION_HIGH] = { 4096, 128 },
};

/* The share of last frame's light that each frame keeps, which averages the
 * rays' noise over about twenty frames. */
static const float keep = .95f;
/* Room left around the solids when the grid is fit, as a share of their
 * extent, so that solids moving a little keep the grid. */
static const float margin = .125f;
/* A refit grid whose spacing would shrink below this share of the current
 * one, as when the solids gather into a small part of it, replaces it. */
static const float coarsest = .5f;
/* The widest grid the shaders' float math holds: they square distances
 * across it, which must stay within float's range. Wider scenes take their
 * light without global illumination. */
static const float widest_grid = 1e18f;

/* A grid of probes: the first one, the spacing between them and their count
 * along each axis. */
typedef struct
{
    CF_V3 origin;
    float spacing;
    int counts[3];
} probe_grid;

/* A solid by where its center lies along an axis. */
typedef struct
{
    float key;
    int solid;
} keyed;

struct render_scene3d_gi
{
    GlobalIllumination quality;
    /* The solids in their BVH leaves' order, a row each, and the BVH's
     * nodes. */
    CF_Texture solids, nodes;
    int solid_rows, node_rows;
    CF_V4 *texels, *node_texels;
    int *order;
    keyed *keys;
    /* A texel per ray, as many probes' to a row as fit. */
    CF_Canvas rays;
    /* The irradiance tiles' rows, then the depth tiles', for the quality's
     * most probes. Each frame reads one and writes the other. */
    CF_Canvas atlas[2];
    int current;             /* The atlas that holds the probes' light. */
    GlobalIllumination made; /* The quality the targets are for, if any. */
    CF_Texture dark;
    CF_Shader trace, update;
    CF_Material material;
    CF_Mesh triangle;
    /* This frame's probes, fit to the solids for a budget of probes, and
     * those whose light atlas[current] holds. */
    probe_grid grid, lit;
    int budget;
    /* Frames of light the atlas holds, and of depths seen from the current
     * grid's probes. */
    int frames, depth_frames;
    bool shown; /* Whether the lit pass reads the probes this frame. */
    unsigned frame;
};

render_scene3d_gi *render_scene3d_gi_new()
{
    return xcalloc(1, sizeof(render_scene3d_gi));
}

/* Releases the targets and the light they hold. */
static void free_targets(render_scene3d_gi *gi)
{
    gi->frames = 0;
    if (gi->made == GLOBAL_ILLUMINATION_OFF) {
        return;
    }
    cf_destroy_canvas(gi->rays);
    cf_destroy_canvas(gi->atlas[0]);
    cf_destroy_canvas(gi->atlas[1]);
    gi->made = GLOBAL_ILLUMINATION_OFF;
}

void render_scene3d_gi_free(render_scene3d_gi *gi)
{
    free_targets(gi);
    if (gi->solids.id) {
        cf_destroy_texture(gi->solids);
    }
    if (gi->nodes.id) {
        cf_destroy_texture(gi->nodes);
    }
    if (gi->dark.id) {
        cf_destroy_texture(gi->dark);
    }
    if (gi->trace.id) {
        cf_destroy_shader(gi->trace);
        cf_destroy_shader(gi->update);
        cf_destroy_material(gi->material);
        cf_destroy_mesh(gi->triangle);
    }
    afree(gi->texels);
    afree(gi->node_texels);
    afree(gi->order);
    afree(gi->keys);
    free(gi);
}

void render_scene3d_gi_reset(render_scene3d_gi *gi)
{
    gi->frames = 0;
}

bool render_scene3d_gi_prepare(render_scene3d_gi *gi, const Environment *environment, char *error, size_t size)
{
    GlobalIllumination quality = environment->global_illumination;
    gi->quality = GLOBAL_ILLUMINATION_OFF;
    gi->shown = false;
    if ((unsigned)quality > GLOBAL_ILLUMINATION_HIGH) {
        snprintf(error, size, "Environment global illumination must be Off, Low, Medium or High.");
        return false;
    }
    gi->quality = quality;
    if (quality == GLOBAL_ILLUMINATION_OFF) {
        free_targets(gi);
    }
    return true;
}

bool render_scene3d_gi_active(const render_scene3d_gi *gi)
{
    return gi->quality != GLOBAL_ILLUMINATION_OFF;
}

/* The unit shape fits the unit cube, but for the flat triangle, which reaches
 * 2/3 along x. */
static float reach_x(int primitive)
{
    return primitive == PRIMITIVE_TRIANGLE ? 2.0f / 3 : .5f;
}

/* The world box around a solid, as its center and half extents. */
static void solid_box(const render_scene3d_gi_solid *solid, CF_V3 *center, CF_V3 *half)
{
    const float *m = solid->frame.elements;
    float x = reach_x(solid->primitive);
    *center = (CF_V3){ m[12], m[13], m[14] };
    *half = (CF_V3){ fabsf(m[0]) * x + (fabsf(m[4]) + fabsf(m[8])) * .5f,
                     fabsf(m[1]) * x + (fabsf(m[5]) + fabsf(m[9])) * .5f,
                     fabsf(m[2]) * x + (fabsf(m[6]) + fabsf(m[10])) * .5f };
}

static int product(const int counts[3])
{
    return counts[0] * counts[1] * counts[2];
}

/* The spacing and counts that cover an extent with at most budget probes,
 * at least two along each axis. */
static float spacing_for(CF_V3 extent, int budget, int counts[3])
{
    float widest = fmaxf(fmaxf(extent.x, extent.y), fmaxf(extent.z, 1e-6f));
    /* Flat extents still hold two layers of probes. The volume of extents a
     * float holds can pass a float's range, but not a double's. */
    float thinnest = widest / 64;
    double volume = (double)fmaxf(extent.x, thinnest) *
                    fmaxf(extent.y, thinnest) * fmaxf(extent.z, thinnest);
    float spacing = (float)cbrt(volume / budget);
    for (;;) {
        const float sides[3] = { extent.x, extent.y, extent.z };
        for (int i = 0; i < 3; i++) {
            counts[i] = (int)ceilf(sides[i] / spacing) + 1;
            counts[i] = counts[i] < 2 ? 2 : counts[i];
        }
        if (product(counts) <= budget) {
            return spacing;
        }
        spacing *= 1.05f;
    }
}

static float longest_side(const probe_grid *grid)
{
    int most = grid->counts[0];
    for (int i = 1; i < 3; i++) {
        most = grid->counts[i] > most ? grid->counts[i] : most;
    }
    return (float)(most - 1) * grid->spacing;
}

/* Whether a grid still covers the solids' box without being much coarser
 * than one fit to it would be. */
static bool covers(const probe_grid *grid, CF_V3 low, CF_V3 high, int budget)
{
    if (!grid->spacing) {
        return false;
    }
    const float lows[3] = { low.x, low.y, low.z },
                highs[3] = { high.x, high.y, high.z };
    const float first[3] = { grid->origin.x, grid->origin.y, grid->origin.z };
    for (int i = 0; i < 3; i++) {
        float last = first[i] + (float)(grid->counts[i] - 1) * grid->spacing;
        if (lows[i] < first[i] || highs[i] > last) {
            return false;
        }
    }
    int counts[3];
    return spacing_for(cf_sub_v3(high, low), budget, counts) >= grid->spacing * coarsest;
}

/* Fits the grid around the solids, keeping the current one while it still
 * covers them. A new grid's probes see the scene from new places, so the
 * depths they gather start over. */
static void fit(render_scene3d_gi *gi, const render_scene3d_gi_solid *solids, int count, int budget)
{
    CF_V3 low = { INFINITY, INFINITY, INFINITY },
          high = { -INFINITY, -INFINITY, -INFINITY };
    for (int i = 0; i < count; i++) {
        CF_V3 center, half;
        solid_box(&solids[i], &center, &half);
        low = cf_min_v3(low, cf_sub_v3(center, half));
        high = cf_max_v3(high, cf_add_v3(center, half));
    }
    if (gi->budget == budget && covers(&gi->grid, low, high, budget)) {
        return;
    }
    CF_V3 room = cf_mul_v3_f(cf_sub_v3(high, low), margin);
    low = cf_sub_v3(low, room);
    high = cf_add_v3(high, room);
    probe_grid *grid = &gi->grid;
    grid->spacing = spacing_for(cf_sub_v3(high, low), budget, grid->counts);
    CF_V3 span = { (float)(grid->counts[0] - 1) * grid->spacing,
                   (float)(grid->counts[1] - 1) * grid->spacing,
                   (float)(grid->counts[2] - 1) * grid->spacing };
    grid->origin = cf_sub_v3(cf_mul_v3_f(cf_add_v3(low, high), .5f), cf_mul_v3_f(span, .5f));
    gi->budget = budget;
    gi->depth_frames = 0;
}

static int tiles_per_row(int probes)
{
    return probes < TILES_PER_ROW ? probes : TILES_PER_ROW;
}

static int tile_rows(int probes)
{
    int row = tiles_per_row(probes);
    return (probes + row - 1) / row;
}

static CF_Canvas make_target(int width, int height, CF_Filter filter)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.filter = filter;
    params.target.wrap_u = params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.depth_stencil_enable = false;
    return cf_make_canvas(params);
}

/* Targets for the quality's most probes and its rays, which another quality
 * replaces, light and all. */
static void resize(render_scene3d_gi *gi)
{
    if (gi->made == gi->quality) {
        return;
    }
    free_targets(gi);
    const probe_quality *quality = &qualities[gi->quality];
    int per_row = RAY_ROW / quality->rays;
    int width = tiles_per_row(quality->probes) * DISTANCE_TEXELS;
    int height = tile_rows(quality->probes) * (IRRADIANCE_TEXELS + DISTANCE_TEXELS);
    gi->rays = make_target(RAY_ROW, (quality->probes + per_row - 1) / per_row, CF_FILTER_NEAREST);
    gi->atlas[0] = make_target(width, height, CF_FILTER_LINEAR);
    gi->atlas[1] = make_target(width, height, CF_FILTER_LINEAR);
    gi->made = gi->quality;
}

static bool load_passes(render_scene3d_gi *gi, char *error, size_t size)
{
    if (gi->trace.id) {
        return true;
    }
    CF_Shader trace = render_pass_shader("scene3d_gi_trace.frag", error, size);
    if (!trace.id) {
        return false;
    }
    CF_Shader update = render_pass_shader("scene3d_gi_update.frag", error, size);
    if (!update.id) {
        cf_destroy_shader(trace);
        return false;
    }
    gi->trace = trace;
    gi->update = update;
    gi->material = cf_make_material();
    gi->triangle = render_make_pass_mesh();
    return true;
}

/* Grows a texture of float texels, doubling its rows, to hold `texels` in
 * rows of `width`, pads them to its size and uploads them. */
static void stream(CF_Texture *texture, int *rows, int width, CF_V4 **texels)
{
    int used = acount(*texels);
    int needed = (used + width - 1) / width;
    if (needed > *rows) {
        if (texture->id) {
            cf_destroy_texture(*texture);
        }
        int grown = *rows ? *rows : 1;
        while (grown < needed) {
            grown *= 2;
        }
        CF_TextureParams params = cf_texture_defaults(width, grown);
        params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        params.filter = CF_FILTER_NEAREST;
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
        params.stream = true;
        *texture = cf_make_texture(params);
        *rows = grown;
    }
    asetlen(*texels, *rows * width);
    memset(*texels + used, 0, (size_t)(acount(*texels) - used) * sizeof **texels);
    cf_texture_update(*texture, *texels, acount(*texels) * (int)sizeof **texels);
}

static int compare_keys(const void *a, const void *b)
{
    const keyed *x = a, *y = b;
    if (x->key != y->key) {
        return x->key < y->key ? -1 : 1;
    }
    return x->solid - y->solid;
}

static int widest(CF_V3 spread)
{
    if (spread.x >= spread.y && spread.x >= spread.z) {
        return 0;
    }
    return spread.y >= spread.z ? 1 : 2;
}

/* Sorts solids order[first, first + count) by their centers along an axis. */
static void sort_along(
    render_scene3d_gi *gi,
    const render_scene3d_gi_solid *solids,
    int first,
    int count,
    int axis
)
{
    asetlen(gi->keys, count);
    for (int i = 0; i < count; i++) {
        int solid = gi->order[first + i];
        gi->keys[i] = (keyed){ solids[solid].frame.elements[12 + axis], solid };
    }
    qsort(gi->keys, (size_t)count, sizeof *gi->keys, compare_keys);
    for (int i = 0; i < count; i++) {
        gi->order[first + i] = gi->keys[i].solid;
    }
}

/* Adds the BVH node over solids order[first, first + count), then its
 * subtree depth first, halving the solids along the axis their centers
 * spread widest on. Each node holds its box, then for an inner node the
 * node of its second child, the first following it, or for a leaf its
 * first solid and their count. */
static void build(render_scene3d_gi *gi, const render_scene3d_gi_solid *solids, int first, int count, int depth)
{
    assert(depth < BVH_DEPTH);
    CF_V3 low = { INFINITY, INFINITY, INFINITY },
          high = { -INFINITY, -INFINITY, -INFINITY };
    CF_V3 nearest = low, farthest = high;
    for (int i = first; i < first + count; i++) {
        CF_V3 center, half;
        solid_box(&solids[gi->order[i]], &center, &half);
        low = cf_min_v3(low, cf_sub_v3(center, half));
        high = cf_max_v3(high, cf_add_v3(center, half));
        nearest = cf_min_v3(nearest, center);
        farthest = cf_max_v3(farthest, center);
    }
    int texel = acount(gi->node_texels);
    bool leaf = count <= LEAF_SOLIDS;
    apush(gi->node_texels, ((CF_V4){ low.x, low.y, low.z, (float)first }));
    apush(gi->node_texels, ((CF_V4){ high.x, high.y, high.z, leaf ? (float)count : 0 }));
    if (leaf) {
        return;
    }
    sort_along(gi, solids, first, count, widest(cf_sub_v3(farthest, nearest)));
    int half = count / 2;
    build(gi, solids, first, half, depth + 1);
    gi->node_texels[texel].w = (float)(acount(gi->node_texels) / NODE_TEXELS);
    build(gi, solids, first + half, count - half, depth + 1);
}

/* The rows of a frame's inverse, which take world points into its unit
 * shape. They are worked in double: a general inverse multiplies three of
 * the frame's entries, which passes float's range for frames over about
 * 7e12 units. Frames that draw are never flat (scene3d_visual.c). */
static void unit_rows(CF_M4x4 frame, CF_V4 rows[3])
{
    const float *m = frame.elements;
    double cofactors[3][3];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (c + 1) % 3,
                c2 = (c + 2) % 3;
            /* Row r of column c is m[4 * c + r]. */
            cofactors[r][c] = (double)m[4 * c1 + r1] * m[4 * c2 + r2] -
                              (double)m[4 * c2 + r1] * m[4 * c1 + r2];
        }
    }
    double determinant = m[0] * cofactors[0][0] + m[4] * cofactors[0][1] +
                         m[8] * cofactors[0][2];
    assert(determinant != 0);
    for (int r = 0; r < 3; r++) {
        const double inverse[3] = { cofactors[0][r] / determinant,
                                    cofactors[1][r] / determinant,
                                    cofactors[2][r] / determinant };
        double offset = inverse[0] * m[12] + inverse[1] * m[13] + inverse[2] * m[14];
        rows[r] = (CF_V4){ (float)inverse[0],
                           (float)inverse[1],
                           (float)inverse[2],
                           (float)-offset };
    }
}

/* Each solid's row, in its BVH leaf's order: the rows taking world points
 * into its shape's space, where a capsule's caps are round; its albedo and
 * primitive; its emission and whether it is a mesh's box; the world sphere
 * around it, which rays test first; and the edges of its drawn box. */
static void upload_solids(render_scene3d_gi *gi, const render_scene3d_gi_solid *solids, int count)
{
    asetlen(gi->texels, count * SOLID_TEXELS);
    for (int i = 0; i < count; i++) {
        const render_scene3d_gi_solid *solid = &solids[gi->order[i]];
        CF_M4x4 shape = solid->frame;
        if (solid->primitive == PRIMITIVE_CAPSULE) {
            shape = cf_mul_m4(shape, cf_m4_scale((CF_V3){ 2, 1, 2 }));
        }
        CF_V4 *row = &gi->texels[i * SOLID_TEXELS];
        unit_rows(shape, row);
        row[3] = (CF_V4){ solid->albedo.x,
                          solid->albedo.y,
                          solid->albedo.z,
                          (float)solid->primitive };
        row[4] = (CF_V4){ solid->emission.x,
                          solid->emission.y,
                          solid->emission.z,
                          solid->bounds };
        CF_V3 center, half;
        solid_box(solid, &center, &half);
        row[5] = (CF_V4){ center.x, center.y, center.z, cf_len_v3(half) };
        const float *box = solid->box.elements;
        for (int axis = 0; axis < 3; axis++) {
            CF_V3 edge = { box[4 * axis], box[4 * axis + 1], box[4 * axis + 2] };
            CF_V3 middle = { box[12], box[13], box[14] };
            row[6 + axis] = (CF_V4){ edge.x, edge.y, edge.z, cf_dot_v3(edge, middle) };
        }
    }
    stream(&gi->solids, &gi->solid_rows, SOLID_TEXELS, &gi->texels);
}

/* Builds the BVH over the solids and uploads it with them. Returns its
 * nodes. */
static int upload_bvh(render_scene3d_gi *gi, const render_scene3d_gi_solid *solids, int count)
{
    asetlen(gi->order, count);
    for (int i = 0; i < count; i++) {
        gi->order[i] = i;
    }
    asetlen(gi->node_texels, 0);
    build(gi, solids, 0, count, 0);
    int nodes = acount(gi->node_texels) / NODE_TEXELS;
    upload_solids(gi, solids, count);
    stream(&gi->nodes, &gi->node_rows, NODES_PER_ROW * NODE_TEXELS, &gi->node_texels);
    return nodes;
}

static float fraction(double x)
{
    return (float)(x - floor(x));
}

/* This frame's turn of every probe's rays, as matrix columns: a uniformly
 * random rotation from points of an evenly spread sequence. */
static void turn(unsigned frame, CF_V4 columns[3])
{
    float u1 = fraction(.5 + frame * 0.8191725133961645);
    float u2 = fraction(.5 + frame * 0.6710436067037893);
    float u3 = fraction(.5 + frame * 0.5497004779019703);
    float a = sqrtf(1 - u1), b = sqrtf(u1);
    float two_pi = 6.2831853f;
    float x = a * sinf(two_pi * u2), y = a * cosf(two_pi * u2);
    float z = b * sinf(two_pi * u3), w = b * cosf(two_pi * u3);
    columns[0] = (CF_V4){ 1 - 2 * (y * y + z * z),
                          2 * (x * y + w * z),
                          2 * (x * z - w * y),
                          0 };
    columns[1] = (CF_V4){ 2 * (x * y - w * z),
                          1 - 2 * (x * x + z * z),
                          2 * (y * z + w * x),
                          0 };
    columns[2] = (CF_V4){ 2 * (x * z + w * y),
                          2 * (y * z - w * x),
                          1 - 2 * (x * x + y * y),
                          0 };
}

/* Depths are measured in 1.5 probe diagonals, past which a probe's view of
 * a point no longer matters. */
static float depth_unit(const probe_grid *grid)
{
    return 1.5f * sqrtf(3) * grid->spacing;
}

/* The grid of the atlas's probes as the shaders read it, scene_gi_grid,
 * with whether the atlas holds their light. */
static void atlas_grid(const render_scene3d_gi *gi, const probe_grid *grid, bool held, CF_V4 out[3])
{
    int probes = qualities[gi->made].probes;
    out[0] = (CF_V4){ grid->origin.x, grid->origin.y, grid->origin.z, grid->spacing };
    out[1] = (CF_V4){ (float)grid->counts[0],
                      (float)grid->counts[1],
                      (float)grid->counts[2],
                      held };
    out[2] = (CF_V4){ (float)tiles_per_row(probes),
                      (float)(tile_rows(probes) * IRRADIANCE_TEXELS),
                      depth_unit(grid),
                      0 };
}

/* The share of the atlas's light that a texel keeps after `frames` frames of
 * it: an even average of the frames until that reaches keep. */
static float kept_share(int frames)
{
    return fminf(keep, (float)frames / (float)(frames + 1));
}

/* Counts another frame, up to SETTLED. */
static int counted(int frames)
{
    return frames < SETTLED ? frames + 1 : frames;
}

static void set_lighting(CF_Material material, render_scene3d_gi_lighting *lighting)
{
    cf_material_set_uniform_fs(material, "gi_sun_direction", &lighting->sun_direction, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "gi_sun_color", &lighting->sun_color, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "gi_ambient_sky", &lighting->ambient_sky, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "gi_ambient_ground", &lighting->ambient_ground, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "gi_sky_light", &lighting->sky_light, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "gi_sky_irradiance", lighting->sky_irradiance, CF_UNIFORM_TYPE_FLOAT4, 9);
    CF_V4 lights = { (float)lighting->light_count,
                     (float)lighting->shadowed_lights,
                     lighting->sun_shadows,
                     0 };
    cf_material_set_uniform_fs(material, "gi_light_count", &lights, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(
        material,
        "gi_light_positions",
        lighting->light_positions,
        CF_UNIFORM_TYPE_FLOAT4,
        RENDER_SCENE3D_GI_LIGHTS
    );
    cf_material_set_uniform_fs(
        material,
        "gi_light_colors",
        lighting->light_colors,
        CF_UNIFORM_TYPE_FLOAT4,
        RENDER_SCENE3D_GI_LIGHTS
    );
    cf_material_set_uniform_fs(material, "gi_light_cones", lighting->light_cones, CF_UNIFORM_TYPE_FLOAT4, RENDER_SCENE3D_GI_LIGHTS);
}

/* Runs one pass over every texel of a target. */
static void pass(render_scene3d_gi *gi, CF_Canvas target, CF_Shader shader)
{
    int width, height;
    cf_canvas_get_size(target, &width, &height);
    cf_apply_canvas(target, true);
    cf_apply_mesh(gi->triangle);
    cf_apply_shader(shader, gi->material);
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
}

bool render_scene3d_gi_update(
    render_scene3d_gi *gi,
    const render_scene3d_gi_solid *solids,
    int count,
    render_scene3d_gi_lighting *lighting,
    char *error,
    size_t size
)
{
    assert(render_scene3d_gi_active(gi));
    gi->shown = false;
    if (count > SOLID_LIMIT) {
        snprintf(error, size, "Global illumination traces at most %d solids; the scene has %d.", SOLID_LIMIT, count);
        return false;
    }
    /* Without solids there is nothing to bounce light, nor light to keep. */
    if (!count) {
        gi->frames = 0;
        return true;
    }
    if (!load_passes(gi, error, size)) {
        return false;
    }
    resize(gi);
    const probe_quality *quality = &qualities[gi->quality];
    fit(gi, solids, count, quality->probes);
    const probe_grid *grid = &gi->grid;
    if (longest_side(grid) > widest_grid) {
        gi->frames = 0;
        return true;
    }
    int nodes = upload_bvh(gi, solids, count);
    bool held = gi->frames > 0;
    CF_V4 lit[3], columns[3];
    atlas_grid(gi, &gi->lit, held, lit);
    CF_V4 probes[2] = {
        { grid->origin.x, grid->origin.y, grid->origin.z, grid->spacing },
        { (float)grid->counts[0],
          (float)grid->counts[1],
          (float)grid->counts[2],
          depth_unit(grid) },
    };
    turn(gi->frame++, columns);
    /* Rays reach past the grid's far corner from anywhere in it. */
    float reach = 2 * grid->spacing *
                  sqrtf((float)(grid->counts[0] * grid->counts[0] +
                                grid->counts[1] * grid->counts[1] +
                                grid->counts[2] * grid->counts[2]));
    CF_V4 trace = { (float)quality->rays, (float)nodes, reach, 0 };
    /* Depths belong to where their probes stood, so they are no older than
     * the grid. Irradiance outlasts it: refit probes look it up where they
     * now stand. */
    int depths = gi->depth_frames < gi->frames ? gi->depth_frames : gi->frames;
    bool moved = held && memcmp(&gi->lit, grid, sizeof *grid);
    CF_V4 kept = { kept_share(gi->frames), kept_share(depths), moved, 0 };
    CF_Material material = gi->material;
    cf_material_set_uniform_fs(material, "scene_gi_grid", lit, CF_UNIFORM_TYPE_FLOAT4, 3);
    cf_material_set_uniform_fs(material, "gi_probes", probes, CF_UNIFORM_TYPE_FLOAT4, 2);
    cf_material_set_uniform_fs(material, "gi_turn", columns, CF_UNIFORM_TYPE_FLOAT4, 3);
    cf_material_set_uniform_fs(material, "gi_trace", &trace, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(material, "gi_keep", &kept, CF_UNIFORM_TYPE_FLOAT4, 1);
    set_lighting(material, lighting);
    CF_Canvas last = gi->atlas[gi->current], next = gi->atlas[1 - gi->current];
    cf_material_set_texture_fs(material, "scene_gi", cf_canvas_get_target(last));
    cf_material_set_texture_fs(material, "gi_solids", gi->solids);
    cf_material_set_texture_fs(material, "gi_nodes", gi->nodes);
    pass(gi, gi->rays, gi->trace);
    cf_material_set_texture_fs(material, "gi_rays", cf_canvas_get_target(gi->rays));
    pass(gi, next, gi->update);
    gi->current = 1 - gi->current;
    gi->lit = *grid;
    gi->frames = counted(gi->frames);
    gi->depth_frames = counted(gi->depth_frames);
    gi->shown = true;
    return true;
}

void render_scene3d_gi_bind(render_scene3d_gi *gi)
{
    if (!gi->dark.id) {
        CF_TextureParams params = cf_texture_defaults(1, 1);
        params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
        gi->dark = cf_make_texture(params);
        uint16_t texel[4] = { 0 };
        cf_texture_update(gi->dark, texel, sizeof texel);
    }
    CF_V4 shape[3] = { 0 };
    if (gi->shown) {
        atlas_grid(gi, &gi->lit, true, shape);
    }
    cf_draw3d_set_uniform("scene_gi_grid", shape, CF_UNIFORM_TYPE_FLOAT4, 3);
    cf_draw3d_set_texture(
        "scene_gi",
        gi->shown ? cf_canvas_get_target(gi->atlas[gi->current]) : gi->dark
    );
}

void render_scene3d_gi_unbind()
{
    cf_draw3d_set_texture("scene_gi", (CF_Texture){ 0 });
}
