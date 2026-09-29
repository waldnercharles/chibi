/* HRC resource/dispatch core adapted from Neon-Swarm src/lighting.c.
 * Original algorithm and shader comments retained. */
#include "render/radiance.h"

#include "diagnostics/tracy.h"
#include "render/draw_textures.h"
#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Color's grid may be 8 times coarser than the lowest resolution. */
#define HRC_MIN_SCALE RENDER_VIEW_2D_MIN_SCALE
#define HRC_MAX_SCALE 1.f
#define HRC_MIN_RESOLUTION .1f
#define HRC_MIN_TRACE_LEVELS 1
#define HRC_MAX_TRACE_LEVELS 5
#define HRC_MAX_CASCADES 16
/* Adaptive resolution steps between rungs: frames averaged per decision,
 * frames skipped after a step while history refills, windows over budget
 * before stepping down, windows with room before stepping up, and the share
 * of the budget a step up must fit. */
#define HRC_MAX_RUNGS 16
#define HRC_ADAPT_WINDOW 30
#define HRC_ADAPT_SETTLE 30
#define HRC_ADAPT_FALL 2
#define HRC_ADAPT_RISE 4
#define HRC_ADAPT_ROOM .8
/* COLOR solves RGB light and owns every presentation pass. LUMINANCE solves
 * luminance on a finer grid than color, pairing each direction with its
 * opposite in one texel. */
typedef enum
{
    HRC_COLOR,
    HRC_LUMINANCE,
} hrc_role;
struct render_radiance
{
    hrc_role role;
    int canvas_w, canvas_h, radiance_w, radiance_h, work_w, work_h, world_w,
        world_h, N;
    float falloff, bounce_intensity, bounce_hysteresis, radiance_hysteresis;
    int upscale_mode, radiance_ping, frame_count;
    CF_Aabb screen;
    int capacity_w, capacity_h;
    float upscale_threshold;
    GameHRC2D lighting;
    bool authored_lighting;
    bool work_resources_ready;
    int allocated_levels, allocated_stored_from;
    /* Split lighting: the luminance transport's grid is 2^luminance_shift
     * times finer than color's; zero without one. Set on both hierarchies. */
    struct render_radiance *luminance;
    int luminance_shift;
    /* Luminance role: each quadrant's cascade-0 luminance in one image. */
    CF_Canvas luminance_image;
    float scale, reconstruction_scale;
    int trace_levels;
    bool feedback;
    /* Adaptive resolution: the latest frame's scene GPU time, the rung in use,
     * frames left to settle after a step, the window being averaged, windows
     * in a row over budget and with room to step up, each rung's cost
     * relative to the one below it, the lowest cost of the current windows
     * over budget, and the cost that the last step down left. */
    double gpu_ms;
    int rung, settle, window_count, over, rise;
    double window_sum, rung_ratio[HRC_MAX_RUNGS], over_cost, cost_above;
    /* Borrowed material targets; see render_material_textures. */
    CF_Texture scene, source, albedo, black;
    /* Gather's targets: received light, the far light bounce reflects, and
     * per-quadrant arriving luminance; see gather.frag. */
    CF_Canvas gather_canvas;
    /* Each canvas pixel's near-field step, which gather reads when its work
     * grid is coarser than the canvas; see near.frag. */
    CF_Canvas near_canvas;
    /* Filtered received light, alternating with the previous frame's. */
    CF_Canvas radiance_canvas[2];
    CF_Canvas bounce_canvas;
    /* Transport input at the work grid: RGB emission and A optical depth, and
     * reflected light scaled by bounce intensity. */
    CF_Canvas source_canvas;
    /* Hierarchy levels are render targets; see hrc/hrc_pass.shd. Quadrants
     * run sequentially; only their final frusta survive for gather. Luminance
     * solves opposite quadrants together, so it runs two pairs and keeps two
     * frusta. */
    CF_Canvas t_level[HRC_MAX_CASCADES], r_level[HRC_MAX_CASCADES];
    render_view_2d history_view;
    CF_V2 history_offset;
    bool has_history, history_valid;
    CF_Canvas frustum[4];
    /* Row n: each level-n direction's upper and lower sub-cone shares. */
    CF_Texture merge_weights;
    /* Each quadrant's cascade-0 fluence in world orientation, for gather. */
    CF_Canvas quadrant_image[4];
    CF_Shader surface_seed_shd, surface_reduce_shd, surface_blur_shd, trace_shd,
        extend_shd, merge_shd, resolve_shd, prefilter_shd, downsample_shd,
        near_shd, gather_shd, filter_shd, bounce_shd, composite_shd;
    /* Bounce blends into its image; every other pass overwrites. */
    CF_Material pass_material, bounce_material;
    CF_Mesh pass_mesh;
    /* Received light around solids at block resolution: [0] holds the result,
     * [1] the intermediate blur. RGB is light times openness, A openness; the
     * direction textures hold per-quadrant luminance times openness. */
    CF_Canvas surface_canvas[2];
    /* Blocks of 4 pixels or more first seed 2-pixel blocks here. */
    CF_Canvas surface_half;
    int surface_w, surface_h, surface_block;
    /* This frame's surface light blur in blocks, or zero when it is off. */
    float surface_sigma;
};
typedef render_radiance HRC;
#define HRC_LEVEL_NAMES(phase)                                                 \
    { phase " L0",  phase " L1",  phase " L2",  phase " L3",  phase " L4",     \
      phase " L5",  phase " L6",  phase " L7",  phase " L8",  phase " L9",     \
      phase " L10", phase " L11", phase " L12", phase " L13", phase " L14" }
/* Pass timing detail names, one per cascade level. */
static const char *const s_trace_names[HRC_MAX_CASCADES - 1] = HRC_LEVEL_NAMES("HRC trace");
static const char *const s_extend_names[HRC_MAX_CASCADES - 1] = HRC_LEVEL_NAMES("HRC extend");
static const char *const s_merge_names[HRC_MAX_CASCADES - 1] = HRC_LEVEL_NAMES("HRC merge");
static const char *const s_luminance_trace_names[HRC_MAX_CASCADES - 1] = HRC_LEVEL_NAMES(
    "HRC luminance trace"
);
static const char *const s_luminance_extend_names[HRC_MAX_CASCADES - 1] = HRC_LEVEL_NAMES(
    "HRC luminance extend"
);
static const char *const s_luminance_merge_names[HRC_MAX_CASCADES - 1] = HRC_LEVEL_NAMES(
    "HRC luminance merge"
);
static int s_ceil_div(int x, int y)
{
    return (x + y - 1) / y;
}

static int s_probe_count(int width, int level)
{
    int step = 1 << level;
    return s_ceil_div(width, step);
}

static int s_t_width(int width, int level)
{
    return s_probe_count(width, level) * ((1 << level) + 1);
}

static int s_cascade_count(int work_w, int work_h)
{
    int span = work_w > work_h ? work_w : work_h;
    return cf_min((int)ceil(log2((double)span)), HRC_MAX_CASCADES - 1);
}

/* The lowest level whose intervals are stored. The first extension reads the
 * last traced level; merges read levels from 1. A level-0 interval samples
 * its probe's cell alone, so merges trace it rather than read it. Tracing
 * level 1 there too saves memory but costs more than storing it. */
static int s_stored_from(const HRC *hrc, int N)
{
    int last_traced = cf_min(hrc->trace_levels, N) - 1;
    return cf_max(cf_min(last_traced, 1), 0);
}

static int s_r_width(int width, int level)
{
    return s_probe_count(width, level) * (1 << level);
}

static float s_angular_span(int n, float i)
{
    float pow2n = (float)(1 << n);
    float left = atan2f(2.0f * (i - 0.5f) - pow2n, pow2n);
    float right = atan2f(2.0f * (i + 0.5f) - pow2n, pow2n);
    return right - left;
}

static CF_Texture s_make_merge_weights(int levels)
{
    int width = 1 << (levels - 1);
    CF_TextureParams params = cf_texture_defaults(width, levels);
    params.pixel_format = CF_PIXEL_FORMAT_R32G32_FLOAT;
    params.filter = CF_FILTER_NEAREST;
    CF_Texture texture = cf_make_texture(params);
    float *weights = xcalloc((size_t)width * (size_t)levels * 2, sizeof *weights);
    for (int level = 0; level < levels; ++level) {
        for (int i = 0; i < 1 << level; ++i) {
            float parent = s_angular_span(level, (float)i + 0.5f);
            float *w = weights + ((size_t)level * (size_t)width + (size_t)i) * 2;
            w[0] = s_angular_span(level + 1, 2.0f * (float)i + 1.5f) / parent;
            w[1] = s_angular_span(level + 1, 2.0f * (float)i + 0.5f) / parent;
        }
    }
    if (texture.id) {
        cf_texture_update(texture, weights, width * levels * 2 * (int)sizeof *weights);
    }
    free(weights);
    return texture;
}

static int s_rot_world_w(const HRC *hrc, int rot)
{
    return (rot & 1) ? hrc->world_h : hrc->world_w;
}

static int s_rot_world_h(const HRC *hrc, int rot)
{
    return (rot & 1) ? hrc->world_w : hrc->world_h;
}

static void s_push_gpu_level_label(const char *phase, int level)
{
    char label[32];
    snprintf(label, sizeof label, "%s L%d", phase, level);
    cf_push_gpu_label(label);
}

static void s_push_hrc_label(const HRC *hrc, const char *phase)
{
    char label[48];
    snprintf(label, sizeof label, "%s%s", hrc->role == HRC_LUMINANCE ? "HRC luminance " : "HRC ", phase);
    cf_push_gpu_label(label);
}

/* A hierarchy level: a render target that fragment passes write and later
 * passes sample. Apple GPUs compress it losslessly only without compute
 * storage usage, which Metal maps to ShaderWrite; keep that usage off. */
static CF_Canvas s_make_level(int w, int h, CF_PixelFormat format)
{
    CF_CanvasParams params = cf_canvas_defaults(w, h);
    params.target.pixel_format = format;
    params.target.filter = CF_FILTER_NEAREST;
    params.target.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    return cf_make_canvas(params);
}

/* A world-space image of received light that gather filters. */
static CF_Canvas s_make_image(int w, int h)
{
    CF_CanvasParams params = cf_canvas_defaults(w, h);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.target.filter = CF_FILTER_LINEAR;
    params.target.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    return cf_make_canvas(params);
}

/* RGBA16F targets that one fragment pass writes together. */
static CF_Canvas s_make_targets(int w, int h, int count, CF_Filter filter)
{
    CF_CanvasParams params = cf_canvas_defaults(w, h);
    params.target_count = count;
    for (int i = 0; i < count; i++) {
        params.targets[i].pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        params.targets[i].filter = filter;
        params.targets[i].wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
        params.targets[i].wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    }
    return cf_make_canvas(params);
}

static void s_destroy_level(CF_Canvas *level)
{
    if (level->id) {
        cf_destroy_canvas(*level);
    }
    *level = (CF_Canvas){ 0 };
}

static void s_destroy_canvas(CF_Canvas *canvas)
{
    if (canvas->id) {
        cf_destroy_canvas(*canvas);
    }
    *canvas = (CF_Canvas){ 0 };
}

static void s_destroy_work_resources(HRC *hrc)
{
    hrc->work_resources_ready = false;
    hrc->capacity_w = hrc->capacity_h = 0;
    hrc->allocated_levels = hrc->allocated_stored_from = 0;
    s_destroy_canvas(&hrc->source_canvas);
    for (int i = 0; i < HRC_MAX_CASCADES; ++i) {
        s_destroy_level(&hrc->t_level[i]);
        s_destroy_level(&hrc->r_level[i]);
    }
    if (hrc->merge_weights.id) {
        cf_destroy_texture(hrc->merge_weights);
    }
    hrc->merge_weights = (CF_Texture){ 0 };
    s_destroy_level(&hrc->luminance_image);
    for (int i = 0; i < 4; ++i) {
        s_destroy_level(&hrc->frustum[i]);
        s_destroy_level(&hrc->quadrant_image[i]);
    }
}

/* Every quadrant shares each level's texture: quadrants 1 and 3 are stored
 * transposed, so a level holding n elements of the rotated width w per row
 * needs n(work_w) x n(work_h) texels. */
static bool s_create_work_resources(HRC *hrc, int work_w, int work_h)
{
    int N = s_cascade_count(work_w, work_h);
    int levels = N;
    int stored_from = s_stored_from(hrc, N);
    bool reuse = work_w <= hrc->capacity_w && work_h <= hrc->capacity_h &&
                 levels <= hrc->allocated_levels &&
                 stored_from == hrc->allocated_stored_from;
    if (!reuse) {
        s_destroy_work_resources(hrc);
        hrc->source_canvas = s_make_targets(work_w, work_h, 2, CF_FILTER_LINEAR);
        if (!hrc->source_canvas.id) {
            goto fail;
        }
    }

    hrc->world_w = work_w;
    hrc->world_h = work_h;
    hrc->N = N;
    if (reuse) {
        hrc->work_w = work_w;
        hrc->work_h = work_h;
        hrc->work_resources_ready = true;
        return true;
    }

    /* An RGB interval texel holds (light, depth), a luminance one both
     * directions' (light, depth); fluence is RGB or two brightnesses. */
    bool luminance = hrc->role == HRC_LUMINANCE;
    CF_PixelFormat t_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_PixelFormat r_format = luminance ? CF_PIXEL_FORMAT_R16G16_FLOAT : t_format;
    for (int i = 0; i < levels; ++i) {
        if (i >= stored_from) {
            hrc->t_level[i] = s_make_level(s_t_width(work_w, i), s_t_width(work_h, i), t_format);
        }
        hrc->r_level[i] = s_make_level(s_r_width(work_w, i), s_r_width(work_h, i), r_format);
        if ((i >= stored_from && !hrc->t_level[i].id) || !hrc->r_level[i].id) {
            goto fail;
        }
    }
    hrc->merge_weights = s_make_merge_weights(N);
    if (!hrc->merge_weights.id) {
        goto fail;
    }
    if (luminance) {
        hrc->luminance_image = s_make_image(work_w, work_h);
        if (!hrc->luminance_image.id) {
            goto fail;
        }
    }
    /* Color and luminance keep each quadrant's cascade 0 for gather. */
    for (int i = 0; i < (luminance ? 2 : 4); ++i) {
        hrc->frustum[i] = s_make_level(work_w, work_h, r_format);
        if (!hrc->frustum[i].id) {
            goto fail;
        }
        if (hrc->role == HRC_COLOR) {
            hrc->quadrant_image[i] = s_make_image(work_w, work_h);
            if (!hrc->quadrant_image[i].id) {
                goto fail;
            }
        }
    }

    hrc->work_w = hrc->capacity_w = work_w;
    hrc->work_h = hrc->capacity_h = work_h;
    hrc->allocated_levels = levels;
    hrc->allocated_stored_from = stored_from;
    hrc->work_resources_ready = true;
    return true;

fail:
    fprintf(stderr, "[lighting] work resource creation failed for work=%dx%d.\n", work_w, work_h);
    s_destroy_work_resources(hrc);
    return false;
}

/* Draws a fragment pass over the top-left w x h texels of target. */
static void s_draw_region(HRC *hrc, CF_Shader shader, CF_Canvas target, int w, int h)
{
    cf_apply_canvas(target, true);
    cf_apply_mesh(hrc->pass_mesh);
    cf_apply_shader(shader, hrc->pass_material);
    cf_apply_viewport(0, 0, w, h);
    cf_apply_scissor(0, 0, w, h);
    cf_draw_elements();
}

/* Draws a fragment pass over the elements x rows block of a level, which
 * quadrants 1 and 3 store transposed. */
static void s_draw_pass(HRC *hrc, CF_Shader shader, CF_Canvas target, int elements, int rows, int quadrant)
{
    bool transposed = quadrant & 1;
    int flag = transposed;
    cf_material_set_uniform_fs(hrc->pass_material, "u_transposed", &flag, CF_UNIFORM_TYPE_INT, 1);
    s_draw_region(hrc, shader, target, transposed ? rows : elements, transposed ? elements : rows);
}

static void s_set_ints(CF_Material mat, const char *const *names, const int *values, int count)
{
    for (int i = 0; i < count; i++) {
        cf_material_set_uniform_fs(mat, names[i], (void *)(values + i), CF_UNIFORM_TYPE_INT, 1);
    }
}

/* The ray march inputs of trace passes and of merges that trace levels. */
static void s_trace_inputs(HRC *hrc, int quadrant)
{
    CF_Material mat = hrc->pass_material;
    cf_material_set_texture_fs(mat, "u_source", cf_canvas_get_target2(hrc->source_canvas, 0));
    cf_material_set_texture_fs(mat, "u_bounce", cf_canvas_get_target2(hrc->source_canvas, 1));
    int params[3] = { quadrant, hrc->work_w, hrc->work_h };
    const char *names[3] = { "u_rotate", "u_work_w", "u_work_h" };
    s_set_ints(mat, names, params, 3);
    float step_scale = (float)hrc->canvas_w / (float)hrc->work_w;
    cf_material_set_uniform_fs(mat, "u_step_scale", &step_scale, CF_UNIFORM_TYPE_FLOAT, 1);
}

static void s_dispatch_trace(HRC *hrc, int quadrant, int level)
{
    s_trace_inputs(hrc, quadrant);
    cf_material_set_uniform_fs(hrc->pass_material, "u_cascade", &level, CF_UNIFORM_TYPE_INT, 1);
    s_draw_pass(
        hrc,
        hrc->trace_shd,
        hrc->t_level[level],
        s_t_width(s_rot_world_w(hrc, quadrant), level),
        s_rot_world_h(hrc, quadrant),
        quadrant
    );
}

static void s_dispatch_extend(HRC *hrc, int quadrant, int level)
{
    CF_Material mat = hrc->pass_material;
    int rot_w = s_rot_world_w(hrc, quadrant);
    int rot_h = s_rot_world_h(hrc, quadrant);
    cf_material_set_texture_fs(mat, "u_prev", cf_canvas_get_target(hrc->t_level[level - 1]));
    int params[3] = { level, rot_w, rot_h };
    const char *names[3] = { "u_cascade", "u_world_w", "u_world_h" };
    s_set_ints(mat, names, params, 3);
    s_draw_pass(hrc, hrc->extend_shd, hrc->t_level[level], s_t_width(rot_w, level), rot_h, quadrant);
}

/* T_N and R_N are implicit zero boundaries. The shader skips those loads,
 * so the terminal merge binds existing levels in their place. */
static void s_dispatch_merge(HRC *hrc, int quadrant, int level, CF_Texture r_prev, CF_Canvas r_curr)
{
    CF_Material mat = hrc->pass_material;
    int rot_w = s_rot_world_w(hrc, quadrant);
    int rot_h = s_rot_world_h(hrc, quadrant);
    /* Levels the merge traces, or that do not exist, bind a stand-in. */
    CF_Canvas t_curr = hrc->t_level[level];
    CF_Canvas t_next = level + 1 < hrc->N ? hrc->t_level[level + 1]
                                          : (CF_Canvas){ 0 };
    cf_material_set_texture_fs(mat, "u_t_curr", t_curr.id ? cf_canvas_get_target(t_curr) : hrc->merge_weights);
    cf_material_set_texture_fs(mat, "u_t_next", t_next.id ? cf_canvas_get_target(t_next) : hrc->merge_weights);
    cf_material_set_texture_fs(mat, "u_r_prev", r_prev);
    cf_material_set_texture_fs(mat, "u_weights", hrc->merge_weights);
    s_trace_inputs(hrc, quadrant);
    int params[5] = { level, rot_w, rot_h, hrc->N, hrc->allocated_stored_from };
    const char *names[5] = { "u_cascade", "u_world_w", "u_world_h", "u_cascade_count", "u_stored_from" };
    s_set_ints(mat, names, params, 5);
    float ww = (float)hrc->work_w, wh = (float)hrc->work_h;
    float diag = sqrtf(ww * ww + wh * wh);
    float falloff = diag > 0 ? hrc->falloff * 2.0f / diag : 0.0f;
    cf_material_set_uniform_fs(mat, "u_falloff", &falloff, CF_UNIFORM_TYPE_FLOAT, 1);
    s_draw_pass(hrc, hrc->merge_shd, r_curr, s_r_width(rot_w, level), rot_h, quadrant);
}

/* Box-averages fine's prefiltered inputs into coarse's grid, factor times
 * coarser. */
static void s_dispatch_downsample(HRC *fine, HRC *coarse, int factor)
{
    CF_Material mat = fine->pass_material;
    cf_material_set_texture_fs(mat, "u_source", cf_canvas_get_target2(fine->source_canvas, 0));
    cf_material_set_texture_fs(mat, "u_bounce", cf_canvas_get_target2(fine->source_canvas, 1));
    int params[3] = { fine->work_w, fine->work_h, factor };
    const char *names[3] = { "u_fine_w", "u_fine_h", "u_factor" };
    s_set_ints(mat, names, params, 3);
    /* A coarse cell's length in canvas pixels, the unit of optical depth. */
    float length = (float)fine->canvas_w / (float)fine->work_w * (float)factor;
    cf_material_set_uniform_fs(mat, "u_coarse_step", &length, CF_UNIFORM_TYPE_FLOAT, 1);
    s_draw_region(
        fine,
        fine->downsample_shd,
        coarse->source_canvas,
        coarse->work_w,
        coarse->work_h
    );
}

static void s_dispatch_resolve(HRC *hrc, int quadrant)
{
    CF_Material mat = hrc->pass_material;
    cf_material_set_texture_fs(mat, "u_frustum", cf_canvas_get_target(hrc->frustum[quadrant]));
    int params[3] = { quadrant, hrc->work_w, hrc->work_h };
    const char *names[3] = { "u_rotate", "u_work_w", "u_work_h" };
    s_set_ints(mat, names, params, 3);
    s_draw_region(hrc, hrc->resolve_shd, hrc->quadrant_image[quadrant], hrc->work_w, hrc->work_h);
}

static void s_dispatch_luminance_resolve(HRC *hrc)
{
    CF_Material mat = hrc->pass_material;
    int params[2] = { hrc->work_w, hrc->work_h };
    const char *names[2] = { "u_work_w", "u_work_h" };
    s_set_ints(mat, names, params, 2);
    cf_material_set_texture_fs(mat, "u_pair0", cf_canvas_get_target(hrc->frustum[0]));
    cf_material_set_texture_fs(mat, "u_pair1", cf_canvas_get_target(hrc->frustum[1]));
    s_draw_region(hrc, hrc->resolve_shd, hrc->luminance_image, hrc->work_w, hrc->work_h);
}

static void s_begin_view(HRC *hrc, render_view_2d view)
{
    hrc->history_valid = hrc->has_history && hrc->history_view.width == view.width &&
                         hrc->history_view.height == view.height;
    hrc->history_offset = hrc->history_valid
                            ? (CF_V2){ (view.min_x - hrc->history_view.min_x) / view.width,
                                       (hrc->history_view.max_y - view.max_y) / view.height }
                            : (CF_V2){ 0 };
    hrc->history_view = view;
    hrc->has_history = true;
}

/* Prefilters color's canvas inputs onto target's work grid: color's own, or
 * the luminance transport's, which color then box-averages. */
static void s_dispatch_prefilter(HRC *hrc, HRC *target)
{
    CF_Material mat = hrc->pass_material;
    render_view_2d view = hrc->history_view;
    float crop[] = { view.crop_min_u, view.crop_min_v, view.crop_max_u, view.crop_max_v };
    int valid = hrc->history_valid;
    cf_material_set_texture_fs(mat, "u_source", hrc->source);
    cf_material_set_texture_fs(mat, "u_bounce", cf_canvas_get_target(hrc->bounce_canvas));
    cf_material_set_uniform_fs(mat, "u_history_offset", &hrc->history_offset, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(mat, "u_crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
    int params[4] = { hrc->canvas_w, hrc->canvas_h, target->work_w, target->work_h };
    const char *names[4] = { "u_canvas_w", "u_canvas_h", "u_work_w", "u_work_h" };
    s_set_ints(mat, names, params, 4);
    cf_material_set_uniform_fs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    s_draw_region(
        hrc,
        hrc->prefilter_shd,
        target->source_canvas,
        target->work_w,
        target->work_h
    );
}

static CF_Texture s_radiance(const HRC *hrc)
{
    return cf_canvas_get_target(hrc->radiance_canvas[hrc->radiance_ping]);
}

static void s_dispatch_gather(HRC *hrc)
{
    CF_Material mat = hrc->pass_material;
    render_view_2d view = hrc->history_view;
    float crop[] = { view.crop_min_u, view.crop_min_v, view.crop_max_u, view.crop_max_v };
    int valid = hrc->history_valid;
    cf_material_set_uniform_fs(mat, "u_history_offset", &hrc->history_offset, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(mat, "u_crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);

    /* Split lighting reconstructs on the luminance grid, with color from this
     * grid; see gather.frag. */
    HRC *detail = hrc->luminance ? hrc->luminance : hrc;
    cf_material_set_texture_fs(mat, "u_source", hrc->source);
    cf_material_set_texture_fs(
        mat,
        "u_source_filtered",
        cf_canvas_get_target2(detail->source_canvas, 0)
    );
    cf_material_set_texture_fs(mat, "u_bounce", cf_canvas_get_target(hrc->bounce_canvas));
    cf_material_set_texture_fs(
        mat,
        "u_luminance",
        hrc->luminance ? cf_canvas_get_target(hrc->luminance->luminance_image) : hrc->black
    );
    const char *quadrant_names[4] = { "u_f0", "u_f1", "u_f2", "u_f3" };
    for (int q = 0; q < 4; q++) {
        cf_material_set_texture_fs(mat, quadrant_names[q], cf_canvas_get_target(hrc->quadrant_image[q]));
    }

    int params[10] = {
        hrc->canvas_w,    hrc->canvas_h,   detail->work_w,
        detail->work_h,   hrc->work_w,     hrc->work_h,
        hrc->radiance_w,  hrc->radiance_h, hrc->luminance != NULL,
        hrc->upscale_mode
    };
    const char *names[10] = { "u_canvas_w",    "u_canvas_h", "u_work_w",
                              "u_work_h",      "u_color_w",  "u_color_h",
                              "u_out_w",       "u_out_h",    "u_split",
                              "u_upscale_mode" };
    s_set_ints(mat, names, params, 10);
    cf_material_set_uniform_fs(mat, "u_upscale_threshold", &hrc->upscale_threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    bool near = hrc->canvas_w != detail->work_w || hrc->canvas_h != detail->work_h;
    if (near) {
        s_draw_region(hrc, hrc->near_shd, hrc->near_canvas, hrc->canvas_w, hrc->canvas_h);
        render_gpu_timing_detail("HRC near field");
    }
    cf_material_set_texture_fs(mat, "u_near", near ? cf_canvas_get_target(hrc->near_canvas) : hrc->black);
    s_draw_region(hrc, hrc->gather_shd, hrc->gather_canvas, hrc->radiance_w, hrc->radiance_h);
}

/* Filters gather's received light and blends it with the previous frame's. */
static void s_dispatch_filter(HRC *hrc)
{
    CF_Material mat = hrc->pass_material;
    int prev = hrc->radiance_ping;
    int curr = 1 - prev;
    cf_material_set_texture_fs(mat, "u_in_radiance", cf_canvas_get_target2(hrc->gather_canvas, 0));
    cf_material_set_texture_fs(mat, "u_source", hrc->source);
    cf_material_set_texture_fs(mat, "u_prev_radiance", cf_canvas_get_target(hrc->radiance_canvas[prev]));
    float texel_size[2] = { 1.0f / hrc->radiance_w, 1.0f / hrc->radiance_h };
    float threshold = 0.1f;
    float hysteresis = fminf(fmaxf(hrc->radiance_hysteresis, 0.01f), 1.0f);
    int valid = hrc->history_valid;
    cf_material_set_uniform_fs(mat, "u_texel_size", texel_size, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_threshold", &threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_history_offset", &hrc->history_offset, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    s_draw_region(hrc, hrc->filter_shd, hrc->radiance_canvas[curr], hrc->radiance_w, hrc->radiance_h);
    hrc->radiance_ping = curr;
}

static void s_destroy_surface(HRC *hrc)
{
    s_destroy_canvas(&hrc->surface_canvas[0]);
    s_destroy_canvas(&hrc->surface_canvas[1]);
    s_destroy_canvas(&hrc->surface_half);
    hrc->surface_w = hrc->surface_h = hrc->surface_block = 0;
}

static bool s_surface_resources(HRC *hrc, int width, int height, int block)
{
    if (hrc->surface_w == width && hrc->surface_h == height &&
        hrc->surface_block == block) {
        return true;
    }
    s_destroy_surface(hrc);
    for (int i = 0; i < 2; i++) {
        hrc->surface_canvas[i] = s_make_targets(width, height, 2, CF_FILTER_LINEAR);
        if (!hrc->surface_canvas[i].id) {
            s_destroy_surface(hrc);
            return false;
        }
    }
    if (block >= 4) {
        int half = block / 2;
        hrc->surface_half = s_make_targets(width * half, height * half, 2, CF_FILTER_LINEAR);
        if (!hrc->surface_half.id) {
            s_destroy_surface(hrc);
            return false;
        }
    }
    hrc->surface_w = width;
    hrc->surface_h = height;
    hrc->surface_block = block;
    return true;
}

/* Blurs surface_canvas[from] along one axis into the other canvas. */
static void s_dispatch_surface_blur(HRC *hrc, int from, bool vertical, float sigma)
{
    CF_Material mat = hrc->pass_material;
    cf_material_set_texture_fs(mat, "u_light", cf_canvas_get_target2(hrc->surface_canvas[from], 0));
    cf_material_set_texture_fs(mat, "u_dirs", cf_canvas_get_target2(hrc->surface_canvas[from], 1));
    int params[3] = { hrc->surface_w, hrc->surface_h, vertical };
    const char *names[3] = { "u_width", "u_height", "u_vertical" };
    s_set_ints(mat, names, params, 3);
    cf_material_set_uniform_fs(mat, "u_sigma", &sigma, CF_UNIFORM_TYPE_FLOAT, 1);
    s_draw_region(hrc, hrc->surface_blur_shd, hrc->surface_canvas[1 - from], hrc->surface_w, hrc->surface_h);
}

/* Blurs received light over open space at the coarsest block size, up to 8
 * pixels, that keeps the Gaussian at least two texels wide; 16-pixel blocks
 * measured several times slower on Metal. Returns the blur width in texels,
 * or zero when surface lighting is off. */
static float s_dispatch_surface(HRC *hrc, render_view_2d view, int *block)
{
    float radius = hrc->authored_lighting ? hrc->lighting.surface_radius : 0;
    if (radius <= 0 || !hrc->surface_seed_shd.id || view.width <= 0) {
        return 0;
    }
    float sigma = .5f * radius * (float)hrc->canvas_w / view.width;
    *block = 1;
    while (*block < 8 && sigma / (float)(*block * 2) >= 2) {
        *block *= 2;
    }
    int width = s_ceil_div(hrc->canvas_w, *block);
    int height = s_ceil_div(hrc->canvas_h, *block);
    if (!s_surface_resources(hrc, width, height, *block)) {
        return 0;
    }
    /* A seed fragment fetches its block's taps in turn. Blocks of 4 pixels or
     * more give too few fragments to hide that latency, so they seed 2-pixel
     * blocks, which have one tap each, and average those. */
    int seed = *block >= 4 ? 2 : *block;
    CF_Canvas seeded = seed < *block ? hrc->surface_half : hrc->surface_canvas[0];
    CF_Material mat = hrc->pass_material;
    cf_material_set_texture_fs(mat, "u_fluence", s_radiance(hrc));
    cf_material_set_texture_fs(mat, "u_scene", hrc->scene);
    cf_material_set_texture_fs(mat, "u_dirs", cf_canvas_get_target2(hrc->gather_canvas, 2));
    int params[3] = { hrc->canvas_w, hrc->canvas_h, seed };
    const char *names[3] = { "u_canvas_w", "u_canvas_h", "u_block" };
    s_set_ints(mat, names, params, 3);
    s_draw_region(hrc, hrc->surface_seed_shd, seeded, width * (*block / seed), height * (*block / seed));
    if (seed < *block) {
        cf_material_set_texture_fs(mat, "u_light", cf_canvas_get_target2(hrc->surface_half, 0));
        cf_material_set_texture_fs(mat, "u_dirs", cf_canvas_get_target2(hrc->surface_half, 1));
        int reduce = *block / seed;
        cf_material_set_uniform_fs(mat, "u_block", &reduce, CF_UNIFORM_TYPE_INT, 1);
        s_draw_region(hrc, hrc->surface_reduce_shd, hrc->surface_canvas[0], width, height);
    }
    render_gpu_timing_detail("Surface seed");
    float level_sigma = sigma / (float)*block;
    s_dispatch_surface_blur(hrc, 0, false, level_sigma);
    s_dispatch_surface_blur(hrc, 1, true, level_sigma);
    return level_sigma;
}

static void s_solve_trace(HRC *hrc, int j, const char *const *names)
{
    int cutoff = cf_min(hrc->trace_levels, hrc->N);
    for (int i = hrc->allocated_stored_from; i < cutoff; ++i) {
        s_push_gpu_level_label("Trace", i);
        s_dispatch_trace(hrc, j, i);
        cf_pop_gpu_label();
        render_gpu_timing_detail(names[i]);
    }
}

static void s_solve_extend(HRC *hrc, int j, const char *const *names)
{
    for (int i = cf_min(hrc->trace_levels, hrc->N); i < hrc->N; ++i) {
        s_push_gpu_level_label("Extend", i);
        s_dispatch_extend(hrc, j, i);
        cf_pop_gpu_label();
        render_gpu_timing_detail(names[i]);
    }
}

/* Merges levels from the top down into output. The top level reads the
 * implicit zero R_N. */
static void s_solve_merge(HRC *hrc, int j, CF_Canvas output, const char *const *names)
{
    /* Above the top level nothing is read; any other texture stands in. */
    CF_Texture prev = hrc->merge_weights;
    for (int i = hrc->N - 1; i >= 0; --i) {
        CF_Canvas r_out = i == 0 ? output : hrc->r_level[i];
        s_push_gpu_level_label("Merge", i);
        s_dispatch_merge(hrc, j, i, prev, r_out);
        cf_pop_gpu_label();
        render_gpu_timing_detail(names[i]);
        prev = cf_canvas_get_target(r_out);
    }
}

/* Reflects gather's far light for the next frame. Alpha blending with the
 * hysteresis as alpha mixes it into the previous bounce in place. */
static void s_render_bounce(HRC *hrc)
{
    if (!hrc->feedback) {
        return;
    }
    float hysteresis = hrc->bounce_hysteresis;
    /* Transport samples the old bounce in world coordinates. Blending in place
     * cannot move it, so replace it when the view moves. */
    if (!hrc->history_valid || hrc->history_offset.x || hrc->history_offset.y) {
        hysteresis = 1;
    }
    hysteresis = fminf(fmaxf(hysteresis, 0.01f), 1.0f);
    CF_Material mat = hrc->bounce_material;
    cf_material_set_texture_fs(mat, "u_radiance", cf_canvas_get_target2(hrc->gather_canvas, 1));
    cf_material_set_texture_fs(mat, "u_albedo", hrc->albedo);
    cf_material_set_uniform_fs(mat, "u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_apply_canvas(hrc->bounce_canvas, false);
    cf_apply_mesh(hrc->pass_mesh);
    cf_apply_shader(hrc->bounce_shd, mat);
    cf_apply_viewport(0, 0, hrc->radiance_w, hrc->radiance_h);
    cf_apply_scissor(0, 0, hrc->radiance_w, hrc->radiance_h);
    cf_draw_elements();
}

static void destroy_images(HRC *hrc)
{
    s_destroy_canvas(&hrc->bounce_canvas);
    s_destroy_canvas(&hrc->gather_canvas);
    s_destroy_canvas(&hrc->near_canvas);
    s_destroy_canvas(&hrc->radiance_canvas[0]);
    s_destroy_canvas(&hrc->radiance_canvas[1]);
    hrc->canvas_w = hrc->canvas_h = 0;
    hrc->radiance_w = hrc->radiance_h = 0;
}

static CF_Shader s_make_pass_shader(const char *fragment)
{
    char path[128];
    snprintf(path, sizeof path, "/chibi-render/%s", fragment);
    return cf_make_shader("/chibi-render/hrc/pass.vert", path);
}

/* One triangle covering the viewport of a fragment pass. */

static render_radiance *s_new(hrc_role role)
{
    HRC *hrc = xmalloc(sizeof *hrc);
    memset(hrc, 0, sizeof *hrc);
    hrc->role = role;
    hrc->pass_material = cf_make_material();
    hrc->pass_mesh = render_make_pass_mesh();
    bool scalar = role == HRC_LUMINANCE;
    hrc->trace_shd = s_make_pass_shader(scalar ? "hrc/trace_luminance.frag" : "hrc/trace.frag");
    hrc->extend_shd = s_make_pass_shader(scalar ? "hrc/extend_luminance.frag" : "hrc/extend.frag");
    hrc->merge_shd = s_make_pass_shader(scalar ? "hrc/merge_luminance.frag" : "hrc/merge.frag");
    bool ok = hrc->pass_material.id && hrc->pass_mesh.id && hrc->trace_shd.id &&
              hrc->extend_shd.id && hrc->merge_shd.id;
    hrc->downsample_shd = s_make_pass_shader("hrc/downsample.frag");
    hrc->resolve_shd = s_make_pass_shader(
        scalar ? "hrc/resolve_luminance.frag" : "hrc/resolve.frag"
    );
    ok = ok && hrc->downsample_shd.id && hrc->resolve_shd.id;
    if (role == HRC_COLOR) {
        hrc->prefilter_shd = s_make_pass_shader("hrc/prefilter.frag");
        hrc->near_shd = s_make_pass_shader("hrc/near.frag");
        hrc->gather_shd = s_make_pass_shader("hrc/gather.frag");
        hrc->filter_shd = s_make_pass_shader("hrc/filter.frag");
        hrc->surface_seed_shd = s_make_pass_shader("hrc/surface_seed.frag");
        hrc->surface_reduce_shd = s_make_pass_shader("hrc/surface_reduce.frag");
        hrc->surface_blur_shd = s_make_pass_shader("hrc/surface_blur.frag");
        hrc->bounce_shd = s_make_pass_shader("hrc/bounce.frag");
        hrc->bounce_material = cf_make_material();
        CF_RenderState blend = cf_render_state_defaults();
        blend.blend.rgb_src_blend_factor = CF_BLENDFACTOR_SRC_ALPHA;
        blend.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        cf_material_set_render_state(hrc->bounce_material, blend);
        hrc->composite_shd = cf_make_draw_shader("hrc/composite.shd");
        hrc->black = cf_make_texture(cf_texture_defaults(1, 1));
        unsigned black = 0;
        if (hrc->black.id) {
            cf_texture_update(hrc->black, &black, sizeof black);
        }
        ok = ok && hrc->prefilter_shd.id && hrc->near_shd.id &&
             hrc->gather_shd.id && hrc->filter_shd.id &&
             hrc->surface_seed_shd.id && hrc->surface_reduce_shd.id &&
             hrc->surface_blur_shd.id && hrc->bounce_shd.id &&
             hrc->bounce_material.id && hrc->composite_shd.id && hrc->black.id;
    }
    hrc->scale = .5f;
    hrc->reconstruction_scale = 1;
    hrc->gpu_ms = -1;
    hrc->trace_levels = 3;
    hrc->feedback = true;
    hrc->falloff = hrc->bounce_intensity = 1;
    hrc->bounce_hysteresis = hrc->radiance_hysteresis = 0.8f;
    hrc->upscale_mode = 1;
    hrc->upscale_threshold = 0.1f;
    if (!ok) {
        render_radiance_free(hrc);
        return NULL;
    }
    return hrc;
}

render_radiance *render_radiance_new()
{
    return s_new(HRC_COLOR);
}

void render_radiance_free(render_radiance *hrc)
{
    if (!hrc) {
        return;
    }
    render_radiance_free(hrc->luminance);
    hrc->luminance = NULL;
    s_destroy_work_resources(hrc);
    destroy_images(hrc);
    s_destroy_surface(hrc);
    CF_Shader passes[] = { hrc->trace_shd,        hrc->extend_shd,
                           hrc->merge_shd,        hrc->resolve_shd,
                           hrc->prefilter_shd,    hrc->downsample_shd,
                           hrc->near_shd,         hrc->gather_shd,
                           hrc->filter_shd,       hrc->bounce_shd,
                           hrc->surface_seed_shd, hrc->surface_reduce_shd,
                           hrc->surface_blur_shd, hrc->composite_shd };
    for (int i = 0; i < (int)(sizeof passes / sizeof *passes); i++) {
        if (passes[i].id) {
            cf_destroy_shader(passes[i]);
        }
    }
    if (hrc->pass_mesh.id) {
        cf_destroy_mesh(hrc->pass_mesh);
    }
    CF_Material materials[] = { hrc->pass_material, hrc->bounce_material };
    for (int i = 0; i < 2; i++) {
        if (materials[i].id) {
            cf_destroy_material(materials[i]);
        }
    }
    if (hrc->black.id) {
        cf_destroy_texture(hrc->black);
    }
    free(hrc);
}

void render_radiance_reset(render_radiance *hrc)
{
    assert(hrc);
    hrc->frame_count = 0;
    hrc->radiance_ping = 0;
    hrc->has_history = hrc->history_valid = false;
    if (!hrc->canvas_w) {
        return;
    }
    CF_Canvas history[] = { hrc->radiance_canvas[0],
                            hrc->radiance_canvas[1],
                            hrc->bounce_canvas };
    for (int i = 0; i < 3; i++) {
        cf_canvas_set_clear_color(history[i], cf_make_color_rgba_f(0, 0, 0, 0));
        cf_render_to(history[i], true);
    }
}

/* Creates, resizes or frees the luminance transport after color's work
 * resources match its current grid. Its cells split color's evenly. */
static bool s_luminance_resize(HRC *hrc)
{
    if (!hrc->luminance_shift) {
        render_radiance_free(hrc->luminance);
        hrc->luminance = NULL;
        return true;
    }
    if (!hrc->luminance) {
        hrc->luminance = s_new(HRC_LUMINANCE);
        if (!hrc->luminance) {
            return false;
        }
    }
    HRC *luminance = hrc->luminance;
    luminance->canvas_w = hrc->canvas_w;
    luminance->canvas_h = hrc->canvas_h;
    luminance->trace_levels = hrc->trace_levels;
    luminance->luminance_shift = hrc->luminance_shift;
    return s_create_work_resources(
        luminance,
        hrc->work_w << hrc->luminance_shift,
        hrc->work_h << hrc->luminance_shift
    );
}

bool render_radiance_resize(render_radiance *hrc, int width, int height)
{
    assert(hrc);
    /* Bound shifts and level sizes, including a highly elongated viewport. */
    if (width < 2 || height < 2 || width > 4096 || height > 4096) {
        return false;
    }
    int radiance_w = (int)fmaxf(2, ceilf(width * hrc->reconstruction_scale));
    int radiance_h = (int)fmaxf(2, ceilf(height * hrc->reconstruction_scale));
    int work_w = (int)fmaxf(2, ceilf(width * hrc->scale));
    int work_h = (int)fmaxf(2, ceilf(height * hrc->scale));
    int N = s_cascade_count(work_w, work_h);
    bool interval_layout_ready = N <= hrc->allocated_levels &&
                                 s_stored_from(hrc, N) == hrc->allocated_stored_from;
    bool luminance_ready = (hrc->luminance != NULL) == (hrc->luminance_shift > 0);
    if (hrc->luminance && luminance_ready) {
        const HRC *luminance = hrc->luminance;
        luminance_ready = luminance->work_w == work_w << hrc->luminance_shift &&
                          luminance->work_h == work_h << hrc->luminance_shift &&
                          luminance->luminance_shift == hrc->luminance_shift &&
                          luminance->trace_levels == hrc->trace_levels &&
                          luminance->work_resources_ready;
    }
    if (hrc->canvas_w == width && hrc->canvas_h == height &&
        hrc->radiance_w == radiance_w && hrc->radiance_h == radiance_h &&
        hrc->work_w == work_w && hrc->work_h == work_h && luminance_ready &&
        hrc->work_resources_ready && interval_layout_ready) {
        return true;
    }
    bool images_changed = hrc->canvas_w != width || hrc->canvas_h != height ||
                          hrc->radiance_w != radiance_w ||
                          hrc->radiance_h != radiance_h;
    bool ok = true;
    if (images_changed) {
        destroy_images(hrc);
        hrc->bounce_canvas = s_make_image(radiance_w, radiance_h);
        hrc->radiance_canvas[0] = s_make_image(radiance_w, radiance_h);
        hrc->radiance_canvas[1] = s_make_image(radiance_w, radiance_h);
        /* Surface lighting filters gather's per-quadrant luminance. */
        hrc->gather_canvas = s_make_targets(radiance_w, radiance_h, 3, CF_FILTER_LINEAR);
        hrc->near_canvas = s_make_level(width, height, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
        ok = hrc->bounce_canvas.id && hrc->radiance_canvas[0].id &&
             hrc->radiance_canvas[1].id && hrc->gather_canvas.id &&
             hrc->near_canvas.id;
    }
    if (!ok || !s_create_work_resources(hrc, work_w, work_h)) {
        s_destroy_work_resources(hrc);
        destroy_images(hrc);
        return false;
    }
    hrc->canvas_w = width;
    hrc->canvas_h = height;
    hrc->radiance_w = radiance_w;
    hrc->radiance_h = radiance_h;
    if (!s_luminance_resize(hrc)) {
        s_destroy_work_resources(hrc);
        destroy_images(hrc);
        return false;
    }
    if (images_changed) {
        render_radiance_reset(hrc);
    }
    return true;
}

/* One step of adaptive resolution: the finest grid, color's grid relative to
 * it, and the received-light image's resolution. */
typedef struct
{
    float resolution;
    GameHRCColor color;
    bool gather_half;
} hrc_rung;

/* Adaptive halves gather first when it is Auto. */
static int s_gather_steps(GameHRC2D lighting)
{
    return lighting.adaptive && lighting.gather_resolution == GAME_HRC_GATHER_AUTO;
}

/* Then it coarsens color up to Quarter at the authored resolution, which
 * costs less than halving the resolution and changes the image less. Below
 * that resolution the transport is a small part of the frame, so halvings
 * keep the authored color. */
static int s_color_steps(GameHRC2D lighting)
{
    return lighting.adaptive
             ? cf_max(GAME_HRC_COLOR_QUARTER - (int)lighting.color_resolution, 0)
             : 0;
}

/* Rung 0 is the authored lighting; each later one is cheaper. */
static int s_rung_count(GameHRC2D lighting)
{
    if (!lighting.adaptive) {
        return 1;
    }
    int steps = s_gather_steps(lighting) + s_color_steps(lighting);
    int halvings = 0;
    while (steps + halvings + 2 < HRC_MAX_RUNGS &&
           lighting.resolution / (float)(2 << halvings) >=
               lighting.min_resolution - 1e-4f) {
        halvings++;
    }
    return 1 + steps + halvings;
}

static hrc_rung s_rung(GameHRC2D lighting, int rung)
{
    int gather = s_gather_steps(lighting);
    int colors = s_color_steps(lighting);
    int coarser = cf_min(cf_max(rung - gather, 0), colors);
    int halvings = cf_max(rung - gather - colors, 0);
    return (hrc_rung){
        .resolution = lighting.resolution / (float)(1 << halvings),
        .color = lighting.color_resolution + (halvings ? 0 : coarser),
        .gather_half = lighting.gather_resolution == GAME_HRC_GATHER_HALF ||
                       (gather && rung >= gather),
    };
}

/* Color's coarsest grid over the rungs, which the view reserves room for. */
static float s_min_scale(GameHRC2D lighting)
{
    float scale = HRC_MAX_SCALE;
    for (int i = 0; i < s_rung_count(lighting); i++) {
        hrc_rung rung = s_rung(lighting, i);
        scale = fminf(scale, rung.resolution / (float)(1 << rung.color));
    }
    return scale;
}

render_radiance_settings render_radiance_get_settings(const render_radiance *radiance)
{
    if (!radiance) {
        return (render_radiance_settings){ 0 };
    }
    return (render_radiance_settings){
        .scale = radiance->scale,
        .min_scale = radiance->authored_lighting ? s_min_scale(radiance->lighting)
                                                 : radiance->scale,
        .trace_levels = radiance->trace_levels,
        .feedback = radiance->feedback,
        .available = true,
    };
}

bool render_radiance_set_settings(render_radiance *radiance, render_radiance_settings settings)
{
    if (!radiance || !isfinite(settings.scale) || settings.scale < HRC_MIN_SCALE ||
        settings.scale > HRC_MAX_SCALE || settings.trace_levels < HRC_MIN_TRACE_LEVELS ||
        settings.trace_levels > HRC_MAX_TRACE_LEVELS) {
        return false;
    }
    /* Which interval levels are stored depends on the traced levels. */
    bool layout_changed = radiance->scale != settings.scale ||
                          radiance->trace_levels != settings.trace_levels;
    radiance->scale = settings.scale;
    radiance->trace_levels = settings.trace_levels;
    bool resized = true;
    if (layout_changed && radiance->canvas_w && radiance->canvas_h) {
        resized = render_radiance_resize(radiance, radiance->canvas_w, radiance->canvas_h);
    }
    if (radiance->feedback != settings.feedback) {
        radiance->feedback = settings.feedback;
        render_radiance_reset(radiance);
    }
    return resized;
}

typedef struct
{
    size_t offset;
    float low, high;
    const char *name;
} s_float_range;

typedef struct
{
    size_t offset;
    int low, high;
    const char *name;
} s_int_range;

/* The ranges each independent GameHRC2D field supports. */
static const s_float_range s_float_ranges[] = {
    { offsetof(GameHRC2D, resolution), HRC_MIN_RESOLUTION, HRC_MAX_SCALE, "resolution" },
    { offsetof(GameHRC2D, target_fps), 1, 1000, "target_fps" },
    { offsetof(GameHRC2D, min_resolution), HRC_MIN_RESOLUTION, HRC_MAX_SCALE, "min_resolution" },
    { offsetof(GameHRC2D, ambient), 0, 100, "ambient" },
    { offsetof(GameHRC2D, intensity), 0, 100, "intensity" },
    { offsetof(GameHRC2D, fog), 0, 100, "fog" },
    { offsetof(GameHRC2D, shadow_floor), 0, 100, "shadow_floor" },
    { offsetof(GameHRC2D, falloff), 0, 100, "falloff" },
    { offsetof(GameHRC2D, lighting_margin), 0, 256, "lighting_margin" },
    { offsetof(GameHRC2D, bounce_intensity), 0, 100, "bounce_intensity" },
    { offsetof(GameHRC2D, bounce_hysteresis), 0, 1, "bounce_hysteresis" },
    { offsetof(GameHRC2D, radiance_hysteresis), 0, 1, "radiance_hysteresis" },
    { offsetof(GameHRC2D, upscale_threshold), 0, 100, "upscale_threshold" },
    { offsetof(GameHRC2D, particle_focal_length), 1, 100000, "particle_focal_length" },
    { offsetof(GameHRC2D, surface_radius), 0, 1024, "surface_radius" },
    { offsetof(GameHRC2D, surface_relief), 0, 100, "surface_relief" },
    { offsetof(GameHRC2D, surface_bevel), 0, 100, "surface_bevel" },
    { offsetof(GameHRC2D, specular), 0, 100, "specular" },
    { offsetof(GameHRC2D, exposure), -16, 16, "exposure" },
    { offsetof(GameHRC2D, bloom), 0, 1, "bloom" },
};

static const s_int_range s_int_ranges[] = {
    { offsetof(GameHRC2D, color_resolution),
      GAME_HRC_COLOR_FULL,
      GAME_HRC_COLOR_EIGHTH,
      "color_resolution" },
    { offsetof(GameHRC2D, gather_resolution),
      GAME_HRC_GATHER_FULL,
      GAME_HRC_GATHER_AUTO,
      "gather_resolution" },
    { offsetof(GameHRC2D, upscale_mode), 0, 3, "upscale_mode" },
    { offsetof(GameHRC2D, debug_view), 0, 9, "debug_view" },
    { offsetof(GameHRC2D, trace_levels), HRC_MIN_TRACE_LEVELS, HRC_MAX_TRACE_LEVELS, "trace_levels" },
    { offsetof(GameHRC2D, tone_mapping), 0, 1, "tone_mapping" },
};

static void s_note(char *notes, size_t size, const char *name, double from, double to)
{
    size_t used = strlen(notes);
    if (used + 1 < size) {
        snprintf(notes + used, size - used, "%s%s %g -> %g", used ? ", " : "", name, from, to);
    }
}

GameHRC2D render_radiance_normalize(GameHRC2D lighting, char *notes, size_t size)
{
    assert(size > 0);
    notes[0] = '\0';
    GameHRC2D defaults = game_hrc2d_defaults();
    for (size_t i = 0; i < sizeof s_float_ranges / sizeof *s_float_ranges; i++) {
        const s_float_range *range = &s_float_ranges[i];
        float *value = (float *)((char *)&lighting + range->offset);
        float fallback = *(const float *)((const char *)&defaults + range->offset);
        float fitted = isfinite(*value)
                         ? fminf(fmaxf(*value, range->low), range->high)
                         : fallback;
        if (fitted != *value) {
            s_note(notes, size, range->name, *value, fitted);
            *value = fitted;
        }
    }
    for (size_t i = 0; i < sizeof s_int_ranges / sizeof *s_int_ranges; i++) {
        const s_int_range *range = &s_int_ranges[i];
        int *value = (int *)((char *)&lighting + range->offset);
        int fitted = cf_min(cf_max(*value, range->low), range->high);
        if (fitted != *value) {
            s_note(notes, size, range->name, *value, fitted);
            *value = fitted;
        }
    }
    return lighting;
}

/* Solves color at the rung's resolution over 2^color and, when that is
 * coarser, brightness at its resolution. */
static bool s_apply_rung(HRC *hrc)
{
    const GameHRC2D *lighting = &hrc->lighting;
    hrc_rung rung = s_rung(*lighting, hrc->rung);
    float reconstruction = rung.gather_half ? .5f : 1;
    int shift = rung.color;
    bool layout_changed = hrc->reconstruction_scale != reconstruction ||
                          hrc->luminance_shift != shift;
    hrc->reconstruction_scale = reconstruction;
    hrc->luminance_shift = shift;
    render_radiance_settings settings = {
        .scale = rung.resolution / (float)(1 << shift),
        .trace_levels = lighting->trace_levels,
        .feedback = lighting->feedback,
    };
    if (!render_radiance_set_settings(hrc, settings)) {
        return false;
    }
    return !layout_changed || !hrc->canvas_w ||
           render_radiance_resize(hrc, hrc->canvas_w, hrc->canvas_h);
}

static bool s_ladder_changed(GameHRC2D a, GameHRC2D b)
{
    return a.resolution != b.resolution || a.color_resolution != b.color_resolution ||
           a.gather_resolution != b.gather_resolution ||
           a.adaptive != b.adaptive || a.min_resolution != b.min_resolution;
}

bool render_radiance_set_lighting(render_radiance *radiance, GameHRC2D lighting)
{
    assert(radiance);
    char notes[256];
    render_radiance_normalize(lighting, notes, sizeof notes);
    if (notes[0]) {
        return false;
    }
    if (!radiance->authored_lighting || s_ladder_changed(radiance->lighting, lighting)) {
        radiance->rung = radiance->settle = radiance->window_count = 0;
        radiance->over = radiance->rise = 0;
        radiance->window_sum = radiance->cost_above = 0;
        memset(radiance->rung_ratio, 0, sizeof radiance->rung_ratio);
    }
    radiance->lighting = lighting;
    radiance->authored_lighting = true;
    radiance->falloff = lighting.falloff;
    radiance->bounce_intensity = lighting.feedback ? lighting.bounce_intensity : 0;
    radiance->bounce_hysteresis = lighting.bounce_hysteresis;
    radiance->radiance_hysteresis = lighting.radiance_hysteresis;
    radiance->upscale_mode = lighting.upscale_mode;
    radiance->upscale_threshold = lighting.upscale_threshold;
    return s_apply_rung(radiance);
}

void render_radiance_frame_time(render_radiance *hrc, double gpu_ms)
{
    assert(hrc);
    hrc->gpu_ms = gpu_ms;
    if (!hrc->authored_lighting || s_rung_count(hrc->lighting) < 2 ||
        !isfinite(gpu_ms) || gpu_ms < 0) {
        return;
    }
    if (hrc->settle > 0) {
        hrc->settle--;
        return;
    }
    hrc->window_sum += gpu_ms;
    if (++hrc->window_count < HRC_ADAPT_WINDOW) {
        return;
    }
    double average = hrc->window_sum / hrc->window_count;
    hrc->window_sum = 0;
    hrc->window_count = 0;
    if (hrc->cost_above > 0) {
        hrc->rung_ratio[hrc->rung] = hrc->cost_above / fmax(average, 1e-3);
        hrc->cost_above = 0;
    }
    /* Sustained load steps down, and records the lower cost so that one
     * spike does not keep lighting down. */
    double budget = 1000 / hrc->lighting.target_fps;
    double ratio = hrc->rung_ratio[hrc->rung];
    bool over = average > budget && hrc->rung + 1 < s_rung_count(hrc->lighting);
    bool room = hrc->rung > 0 && ratio > 0 && average * ratio < budget * HRC_ADAPT_ROOM;
    hrc->over_cost = !over     ? 0
                   : hrc->over ? fmin(hrc->over_cost, average)
                               : average;
    hrc->over = over ? hrc->over + 1 : 0;
    hrc->rise = room ? hrc->rise + 1 : 0;
    int step = hrc->over >= HRC_ADAPT_FALL ? 1
             : hrc->rise >= HRC_ADAPT_RISE ? -1
                                           : 0;
    if (!step) {
        return;
    }
    if (step > 0) {
        hrc->cost_above = hrc->over_cost;
    }
    hrc->over = hrc->rise = 0;
    hrc->rung += step;
    hrc->settle = HRC_ADAPT_SETTLE;
    bool applied = s_apply_rung(hrc);
    assert(applied);
}

render_radiance_status render_radiance_get_status(const render_radiance *radiance)
{
    assert(radiance);
    GameHRC2D lighting = radiance->authored_lighting ? radiance->lighting
                                                     : game_hrc2d_defaults();
    hrc_rung rung = s_rung(lighting, radiance->rung);
    return (render_radiance_status){
        .resolution = rung.resolution,
        .color = rung.color,
        .gather_half = rung.gather_half,
        .gpu_ms = render_gpu_span_available() ? radiance->gpu_ms : -1,
        .budget_ms = 1000 / lighting.target_fps,
        .adaptive = lighting.adaptive,
        .rung = radiance->rung,
        .rungs = s_rung_count(lighting),
    };
}

static void s_solve_color_quadrant(HRC *hrc, int q)
{
    cf_push_gpu_label("Trace");
    s_solve_trace(hrc, q, s_trace_names);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC trace");
    cf_push_gpu_label("Extend");
    s_solve_extend(hrc, q, s_extend_names);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC extend");
    cf_push_gpu_label("Merge");
    s_solve_merge(hrc, q, hrc->frustum[q], s_merge_names);
    s_dispatch_resolve(hrc, q);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC merge");
}

/* Solves luminance quadrants pair and pair + 2 together. */
static void s_solve_luminance_pair(HRC *luminance, int pair)
{
    s_solve_trace(luminance, pair, s_luminance_trace_names);
    s_solve_extend(luminance, pair, s_luminance_extend_names);
    s_solve_merge(luminance, pair, luminance->frustum[pair], s_luminance_merge_names);
}

static bool s_dispatch_transport(HRC *hrc, render_material_textures material, render_view_2d view)
{
    if (!hrc->canvas_w) {
        return false;
    }
    s_begin_view(hrc, view);
    hrc->screen = (CF_Aabb){ .min = { view.min_x + view.width * view.crop_min_u,
                                      view.max_y - view.height * view.crop_max_v },
                             .max = { view.min_x + view.width * view.crop_max_u,
                                      view.max_y - view.height * view.crop_min_v } };
    hrc->scene = material.scene;
    hrc->source = material.source;
    hrc->albedo = material.albedo;
    /* Luminance prefilters the canvas once; color box-averages it. */
    HRC *luminance = hrc->luminance;
    s_push_hrc_label(hrc, "prefilter");
    if (luminance) {
        s_dispatch_prefilter(hrc, luminance);
        s_dispatch_downsample(luminance, hrc, 1 << hrc->luminance_shift);
    } else {
        s_dispatch_prefilter(hrc, hrc);
    }
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC prefilter");
    if (luminance) {
        luminance->falloff = hrc->falloff;
    }
    const char *quadrants[] = { "+X", "+Y", "-X", "-Y" };
    for (int q = 0; q < 4; q++) {
        s_push_hrc_label(hrc, quadrants[q]);
        s_solve_color_quadrant(hrc, q);
        cf_pop_gpu_label();
    }
    const char *pairs[] = { "+-X", "+-Y" };
    for (int pair = 0; luminance && pair < 2; pair++) {
        s_push_hrc_label(luminance, pairs[pair]);
        s_solve_luminance_pair(luminance, pair);
        cf_pop_gpu_label();
        render_gpu_timing_mark("HRC luminance");
    }
    if (luminance) {
        s_push_hrc_label(luminance, "resolve");
        s_dispatch_luminance_resolve(luminance);
        cf_pop_gpu_label();
        render_gpu_timing_mark("HRC luminance");
    }
    s_push_hrc_label(hrc, "gather");
    s_dispatch_gather(hrc);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC gather");
    s_push_hrc_label(hrc, "filter");
    s_dispatch_filter(hrc);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC filter");
    s_push_hrc_label(hrc, "bounce");
    s_render_bounce(hrc);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC bounce");
    return true;
}

bool render_radiance_solve(HRC *hrc, render_material_textures material, render_view_2d view)
{
    assert(hrc);
    if (!hrc->canvas_w || !material.scene.id || !material.source.id ||
        !material.albedo.id) {
        return false;
    }
    CHIBI_TRACE_ZONE_BEGIN(zone, "HRC submission");
    /* Prefilter averages full-resolution inputs directly; only gather at a
     * reduced reconstruction resolution samples emission mipmaps. */
    if (hrc->radiance_w != hrc->canvas_w || hrc->radiance_h != hrc->canvas_h) {
        cf_generate_mipmaps(material.source);
        render_gpu_timing_mark("Emission mipmaps");
    }
    bool solved = s_dispatch_transport(hrc, material, view);
    if (solved) {
        s_push_hrc_label(hrc, "surface");
        int block = 1;
        hrc->surface_sigma = s_dispatch_surface(hrc, view, &block);
        cf_pop_gpu_label();
        render_gpu_timing_mark("Surface light");
    }
    CHIBI_TRACE_ZONE_END(zone);
    return solved;
}

render_radiance_light render_radiance_received_light(const HRC *hrc)
{
    if (hrc->surface_sigma <= 0) {
        return (render_radiance_light){ .texture = s_radiance(hrc), .map = { 1, 1 } };
    }
    return (render_radiance_light){
        .texture = cf_canvas_get_target2(hrc->surface_canvas[0], 0),
        .map = { (float)hrc->canvas_w / (float)(hrc->surface_w * hrc->surface_block),
                 (float)hrc->canvas_h / (float)(hrc->surface_h * hrc->surface_block) },
        .openness = true,
    };
}

bool render_radiance_draw(HRC *hrc, render_radiance_inputs inputs, render_view_2d view, CF_Canvas output)
{
    return render_radiance_solve(hrc, inputs.material, view) &&
           render_radiance_composite(hrc, inputs, view, output);
}

bool render_radiance_composite(HRC *hrc, render_radiance_inputs inputs, render_view_2d view, CF_Canvas output)
{
    assert(hrc && output.id);
    GameHRC2D lighting = hrc->authored_lighting ? hrc->lighting
                                                : game_hrc2d_defaults();
    int block = hrc->surface_block;
    float surface_sigma = hrc->surface_sigma;
    float ambient = lighting.ambient;
    float intensity = lighting.intensity;
    float shadow_floor = lighting.shadow_floor;
    float fog = lighting.fog;
    float debug = (float)lighting.debug_view;
    float phase = (float)(hrc->frame_count & 63);
    float crop[] = { view.crop_min_u, view.crop_min_v, view.crop_max_u, view.crop_max_v };
    cf_push_gpu_label("HRC composite");
    cf_draw_push_shader(hrc->composite_shd);
    cf_draw_set_texture("scene_tex", hrc->scene);
    cf_draw_set_texture("source_tex", hrc->source);
    cf_draw_set_texture("albedo_tex", hrc->albedo);
    cf_draw_set_texture("fluence_tex", s_radiance(hrc));
    cf_draw_set_texture("normal_tex", inputs.material.normal);
    cf_draw_set_texture("background_tex", inputs.background.id ? inputs.background : hrc->black);
    cf_draw_set_texture("layers_tex", inputs.layers.id ? inputs.layers : hrc->black);
    cf_draw_set_texture("visibility_tex", inputs.material.visibility);
    cf_draw_set_texture("cover_tex", inputs.material.cover);
    cf_draw_set_texture(
        "surface_tex",
        surface_sigma > 0 ? cf_canvas_get_target2(hrc->surface_canvas[0], 0) : hrc->black
    );
    cf_draw_set_texture(
        "surface_dir_tex",
        surface_sigma > 0 ? cf_canvas_get_target2(hrc->surface_canvas[0], 1) : hrc->black
    );
    float world_per_px = view.width / (float)hrc->canvas_w;
    cf_draw_set_uniform("world_per_px", &world_per_px, CF_UNIFORM_TYPE_FLOAT, 1);
    float reconstruction_threshold = lighting.upscale_threshold;
    cf_draw_set_uniform("reconstruction_threshold", &reconstruction_threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    float surface_params[4] = { surface_sigma > 0,
                                lighting.surface_relief,
                                lighting.surface_bevel,
                                lighting.specular };
    float surface_map[4] = { 1, 1, 0, 0 };
    if (surface_sigma > 0) {
        surface_map[0] = (float)hrc->canvas_w / (float)(hrc->surface_w * block);
        surface_map[1] = (float)hrc->canvas_h / (float)(hrc->surface_h * block);
        surface_map[2] = 1.0f / (float)hrc->surface_w;
        surface_map[3] = 1.0f / (float)hrc->surface_h;
    }
    /* Tone-mapped scenes are presented later from a linear HDR target. */
    int linear = lighting.tone_mapping > 0;
    cf_draw_set_uniform("linear_output", &linear, CF_UNIFORM_TYPE_INT, 1);
    cf_draw_set_uniform("surface_sigma", &surface_sigma, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("surface_params", surface_params, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("surface_map", surface_map, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("ambient", &ambient, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("intensity", &intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("shadow_floor", &shadow_floor, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("fog", &fog, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("debug_view", &debug, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("dither_phase", &phase, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_push_filter(CF_DRAW_FILTER_LINEAR);
    render_draw_fullscreen(hrc->bounce_canvas);
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    cf_canvas_set_clear_color(output, cf_make_color_rgba_f(0, 0, 0, 1));
    cf_render_to(output, true);
    RENDER_DRAW_UNBIND_TEXTURES(
        "scene_tex",
        "source_tex",
        "albedo_tex",
        "fluence_tex",
        "normal_tex",
        "background_tex",
        "layers_tex",
        "visibility_tex",
        "cover_tex",
        "surface_tex",
        "surface_dir_tex"
    );
    cf_pop_gpu_label();
    render_gpu_timing_mark("Composite");
    hrc->frame_count = (hrc->frame_count + 1) & 63;
    return true;
}
