/* HRC resource/dispatch core adapted from Neon-Swarm src/lighting.c.
 * Original algorithm and shader comments retained. */
#include "render/radiance.h"

#include "diagnostics/tracy.h"
#include "render/draw_textures.h"
#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "render/mesh_projection.h"
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
/* Adaptive halves the resolution no lower than this. */
#define HRC_MIN_ADAPTIVE_RESOLUTION .25f
#define SURFACE_EMISSION_LEVELS 6
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
/* A frame interval includes the wait for the display, so a frame that fits
 * measures about the budget. Intervals are judged against this much more,
 * stepping down only once frames are missed and up only when one fits. */
#define HRC_ADAPT_INTERVAL_SLACK 1.25
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
    int radiance_ping, frame_count;
    CF_Aabb screen;
    int capacity_w, capacity_h;
    HRC2D lighting;
    render_radiance_lab lab;
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
    /* Gather: received light, far light for bounce, and four RGB quadrants. */
    CF_Canvas gather_canvas;
    /* Each canvas pixel's near-field step, which gather reads when its work
     * grid is coarser than the canvas; see near.frag. */
    CF_Canvas near_canvas;
    /* Filtered received light, alternating with the previous frame's. */
    CF_Canvas radiance_canvas[2];
    CF_Canvas bounce_canvas;
    CF_Canvas spot_canvas;
    CF_Shader spot_shd;
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
    CF_Shader surface_seed_shd, surface_reduce_shd, surface_blur_shd,
        surface_filter_shd, surface_gather_shd, surface_depth_shd,
        surface_emission_shd, surface_depth_filter_shd,
        surface_depth_temporal_shd, trace_shd, extend_shd, merge_shd,
        resolve_shd, prefilter_shd, downsample_shd, near_shd, gather_shd,
        filter_shd, bounce_shd, composite_shd, relief_pack_shd, horizon_shd,
        near_relief_shd, relief_dirty_shd, dirty_expand_shd, relief_reduce_shd,
        horizon_hierarchy_shd, horizon4_shd, horizon_hierarchy4_shd;
    /* Bounce blends into its image; every other pass overwrites. */
    CF_Material pass_material, bounce_material;
    CF_Mesh pass_mesh;
    /* Four RGB quadrants around solids at block resolution, each with
     * openness in alpha. [0] holds the result, [1] the intermediate blur. */
    CF_Canvas surface_canvas[2];
    /* Blocks of 4 pixels or more first seed 2-pixel blocks here. */
    CF_Canvas surface_half, surface_history[2], depth_history[2];
    render_view_2d depth_view;
    int depth_ping;
    bool depth_has_history;
    CF_Canvas surface_emission[SURFACE_EMISSION_LEVELS];
    int surface_w, surface_h, surface_block, surface_ping, surface_emission_block;
    HRCSurfaceMode surface_resource_mode;
    bool surface_has_history;
    render_surface_work surface_work;
    CF_Canvas relief_canvas[2], horizon_canvas[2], dirty_canvas[2], relief_levels[3];
    int horizon_w, horizon_h, horizon_ping, horizon_options, horizon_directions;
    bool horizon_history, horizon_packed;
    render_view_2d horizon_view;
    render_material_textures horizon_inputs;
    render_horizon_work horizon_work;
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

/* Pass materials keep each texture a pass bound until its name is bound
 * again, and WebGL validates every one on each draw: forget them before
 * the canvases they name go. Each pass binds what it samples. */
static void s_forget_textures(HRC *hrc)
{
    CF_Material materials[] = { hrc->pass_material, hrc->bounce_material };
    for (int i = 0; i < 2; i++) {
        if (materials[i].id) {
            cf_material_clear_textures(materials[i]);
        }
    }
    if (hrc->luminance) {
        s_forget_textures(hrc->luminance);
    }
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

/* Passes bind every texture they consume. Do not retain unused handles across
 * input replacement or owned-target destruction: GLES marks material textures
 * as read after a draw, including values belonging to earlier shaders. */
static void s_clear_texture_bindings(HRC *hrc)
{
    cf_material_clear_textures(hrc->pass_material);
    if (hrc->bounce_material.id) {
        cf_material_clear_textures(hrc->bounce_material);
    }
}

static void s_destroy_work_resources(HRC *hrc)
{
    s_clear_texture_bindings(hrc);
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

static bool s_same_projection(render_view_2d a, render_view_2d b)
{
    return a.focal_distance == b.focal_distance && a.yaw == b.yaw &&
           a.pitch == b.pitch && a.roll == b.roll &&
           a.target_elevation == b.target_elevation;
}

static void s_begin_view(HRC *hrc, render_view_2d view)
{
    hrc->history_valid = hrc->has_history && hrc->history_view.width == view.width &&
                         hrc->history_view.height == view.height &&
                         s_same_projection(hrc->history_view, view);
    hrc->history_offset = hrc->history_valid
                            ? (CF_V2){ (view.min_x - hrc->history_view.min_x) / view.width,
                                       (hrc->history_view.max_y - view.max_y) / view.height }
                            : (CF_V2){ 0 };
    if ((view.yaw != 0 || view.pitch != 0 || view.roll != 0 || view.focal_distance > 0) &&
        (hrc->history_offset.x || hrc->history_offset.y ||
         view.center_x != hrc->history_view.center_x ||
         view.center_y != hrc->history_view.center_y)) {
        hrc->history_valid = false;
        hrc->history_offset = (CF_V2){ 0 };
    }
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

/* The sky's light as linear RGB; black until lighting is authored. */
static CF_V3 s_sky(const HRC *hrc)
{
    unsigned color = hrc->authored_lighting ? hrc->lighting.sky : 0;
    return (CF_V3){ (float)(color >> 24) / 255.0f,
                    (float)((color >> 16) & 255u) / 255.0f,
                    (float)((color >> 8) & 255u) / 255.0f };
}

static CF_Texture s_radiance(const HRC *hrc)
{
    return cf_canvas_get_target(hrc->radiance_canvas[hrc->radiance_ping]);
}

static void s_relief_inputs(HRC *hrc);

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

    int params[9] = { hrc->canvas_w,   hrc->canvas_h,   detail->work_w,
                      detail->work_h,  hrc->work_w,     hrc->work_h,
                      hrc->radiance_w, hrc->radiance_h, hrc->luminance != NULL };
    const char *names[9] = { "u_canvas_w", "u_canvas_h", "u_work_w",
                             "u_work_h",   "u_color_w",  "u_color_h",
                             "u_out_w",    "u_out_h",    "u_split" };
    s_set_ints(mat, names, params, 9);
    cf_material_set_uniform_fs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    CF_V3 sky = s_sky(hrc);
    cf_material_set_uniform_fs(mat, "u_sky", &sky, CF_UNIFORM_TYPE_FLOAT3, 1);
    bool near = hrc->canvas_w != detail->work_w || hrc->canvas_h != detail->work_h;
    if (near) {
        if (hrc->horizon_w && hrc->lab.horizon_fuse_relief) {
            s_relief_inputs(hrc);
            s_draw_region(
                hrc,
                hrc->near_relief_shd,
                hrc->relief_canvas[hrc->horizon_ping],
                hrc->canvas_w,
                hrc->canvas_h
            );
            hrc->horizon_packed = true;
            hrc->horizon_work.fused_packs = 1;
        } else {
            s_draw_region(hrc, hrc->near_shd, hrc->near_canvas, hrc->canvas_w, hrc->canvas_h);
        }
        render_gpu_timing_detail("HRC near field");
    }
    cf_material_set_texture_fs(
        mat,
        "u_near",
        near ? (hrc->horizon_packed
                    ? cf_canvas_get_target2(hrc->relief_canvas[hrc->horizon_ping], 1)
                    : cf_canvas_get_target(hrc->near_canvas))
             : hrc->black
    );
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

enum
{
    HORIZON_TILE = 16,
    HORIZON_CACHE = 1,
    HORIZON_FUSE = 2,
    HORIZON_HIERARCHY = 4
};

static void s_destroy_horizon(HRC *hrc)
{
    s_clear_texture_bindings(hrc);
    for (int i = 0; i < 2; i++) {
        s_destroy_canvas(&hrc->relief_canvas[i]);
        s_destroy_canvas(&hrc->horizon_canvas[i]);
        s_destroy_canvas(&hrc->dirty_canvas[i]);
    }
    for (int i = 0; i < 3; i++) {
        s_destroy_canvas(&hrc->relief_levels[i]);
    }
    hrc->horizon_w = hrc->horizon_h = 0;
    hrc->horizon_history = false;
    hrc->horizon_work = (render_horizon_work){ 0 };
}

static size_t s_horizon_bytes(const HRC *hrc)
{
    int copies = hrc->horizon_options & HORIZON_CACHE ? 2 : 1;
    int targets = 1 + hrc->horizon_directions / 4 +
                  ((hrc->horizon_options & HORIZON_FUSE) != 0);
    size_t pixels = (size_t)hrc->horizon_w * hrc->horizon_h;
    size_t bytes = pixels * copies * targets * 8;
    if (hrc->horizon_options & HORIZON_CACHE) {
        bytes += (size_t)s_ceil_div(hrc->horizon_w, HORIZON_TILE) *
                 s_ceil_div(hrc->horizon_h, HORIZON_TILE) * 16;
    }
    if (hrc->horizon_options & HORIZON_HIERARCHY) {
        int w = hrc->horizon_w, h = hrc->horizon_h;
        for (int i = 0; i < 3; i++) {
            w = s_ceil_div(w, 2);
            h = s_ceil_div(h, 2);
            bytes += (size_t)w * h * 8;
        }
    }
    return bytes;
}

static bool s_prepare_horizon(HRC *hrc, render_material_textures inputs, int directions)
{
    hrc->horizon_work = (render_horizon_work){ 0 };
    hrc->horizon_packed = false;
    hrc->horizon_inputs = inputs;
    if (!directions) {
        s_destroy_horizon(hrc);
        return true;
    }
    render_radiance_lab lab = hrc->lab;
    int options = (lab.horizon_cache ? HORIZON_CACHE : 0) |
                  (lab.horizon_fuse_relief ? HORIZON_FUSE : 0) |
                  (lab.horizon_hierarchy ? HORIZON_HIERARCHY : 0);
    if (hrc->horizon_w != hrc->canvas_w || hrc->horizon_h != hrc->canvas_h ||
        options != hrc->horizon_options || directions != hrc->horizon_directions) {
        s_destroy_horizon(hrc);
        hrc->horizon_options = options;
        hrc->horizon_directions = directions;
        int copies = options & HORIZON_CACHE ? 2 : 1;
        for (int i = 0; i < copies; i++) {
            hrc->relief_canvas[i] = s_make_targets(
                hrc->canvas_w,
                hrc->canvas_h,
                options & HORIZON_FUSE ? 2 : 1,
                CF_FILTER_NEAREST
            );
            hrc->horizon_canvas[i] = s_make_targets(hrc->canvas_w, hrc->canvas_h, directions / 4, CF_FILTER_NEAREST);
            if (!hrc->relief_canvas[i].id || !hrc->horizon_canvas[i].id) {
                s_destroy_horizon(hrc);
                return false;
            }
        }
        if (options & HORIZON_CACHE) {
            int w = s_ceil_div(hrc->canvas_w, HORIZON_TILE),
                h = s_ceil_div(hrc->canvas_h, HORIZON_TILE);
            for (int i = 0; i < 2; i++) {
                hrc->dirty_canvas[i] = s_make_targets(w, h, 1, CF_FILTER_NEAREST);
                if (!hrc->dirty_canvas[i].id) {
                    s_destroy_horizon(hrc);
                    return false;
                }
            }
        }
        if (options & HORIZON_HIERARCHY) {
            int w = hrc->canvas_w, h = hrc->canvas_h;
            for (int i = 0; i < 3; i++) {
                w = s_ceil_div(w, 2);
                h = s_ceil_div(h, 2);
                hrc->relief_levels[i] = s_make_targets(w, h, 1, CF_FILTER_NEAREST);
                if (!hrc->relief_levels[i].id) {
                    s_destroy_horizon(hrc);
                    return false;
                }
            }
        }
        hrc->horizon_w = hrc->canvas_w;
        hrc->horizon_h = hrc->canvas_h;
        hrc->horizon_ping = 0;
    }
    hrc->horizon_ping = options & HORIZON_CACHE ? 1 - hrc->horizon_ping : 0;
    hrc->horizon_work.directions = directions;
    hrc->horizon_work.canvas_bytes = s_horizon_bytes(hrc);
    return true;
}

static void s_relief_inputs(HRC *hrc)
{
    cf_material_set_texture_fs(
        hrc->pass_material,
        "u_albedo",
        hrc->horizon_inputs.albedo
    );
    cf_material_set_texture_fs(
        hrc->pass_material,
        "u_cover",
        hrc->horizon_inputs.cover
    );
    cf_material_set_texture_fs(
        hrc->pass_material,
        "u_normal",
        hrc->horizon_inputs.normal
    );
}

static bool s_horizon_offset(HRC *hrc, render_view_2d view, int offset[2])
{
    render_view_2d prev = hrc->horizon_view;
    if (!hrc->horizon_history || view.width != prev.width ||
        view.height != prev.height || !s_same_projection(view, prev)) {
        return false;
    }
    if ((view.yaw != 0 || view.pitch != 0 || view.roll != 0 || view.focal_distance > 0) &&
        (view.center_x != prev.center_x || view.center_y != prev.center_y)) {
        return false;
    }
    float x = (view.min_x - prev.min_x) * hrc->canvas_w / view.width;
    float y = (prev.max_y - view.max_y) * hrc->canvas_h / view.height;
    if (fabsf(x) >= hrc->canvas_w || fabsf(y) >= hrc->canvas_h) {
        return false;
    }
    offset[0] = (int)roundf(x);
    offset[1] = (int)roundf(y);
    if (fabsf(x - offset[0]) > 0.001f || fabsf(y - offset[1]) > 0.001f) {
        return false;
    }
    if ((offset[0] || offset[1]) &&
        (view.yaw != 0 || view.pitch != 0 || view.roll != 0 ||
         view.focal_distance > 0 || !hrc->lab.horizon_reproject)) {
        return false;
    }
    // Coarse summaries remain aligned only for whole level-3 cells.
    return !(hrc->horizon_options & HORIZON_HIERARCHY) ||
           (offset[0] % 8 == 0 && offset[1] % 8 == 0);
}

static void s_horizon_dirty(HRC *hrc, int valid, int offset[2], const float spacing[2])
{
    CF_Material mat = hrc->pass_material;
    int w = s_ceil_div(hrc->canvas_w, HORIZON_TILE),
        h = s_ceil_div(hrc->canvas_h, HORIZON_TILE);
    cf_material_set_texture_fs(
        mat,
        "u_previous",
        cf_canvas_get_target(hrc->relief_canvas[1 - hrc->horizon_ping])
    );
    cf_material_set_uniform_fs(mat, "u_offset", offset, CF_UNIFORM_TYPE_INT2, 1);
    cf_material_set_uniform_fs(mat, "u_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    s_draw_region(hrc, hrc->relief_dirty_shd, hrc->dirty_canvas[0], w, h);
    for (int i = 0; i < 2; i++) {
        int axis[2] = { i == 0, i == 1 };
        int extra = hrc->horizon_options & HORIZON_HIERARCHY ? 8 : 1;
        int radius = (int)
            fminf((float)(i ? h : w), ceilf((6.0f / spacing[i] + extra) / HORIZON_TILE));
        cf_material_set_texture_fs(mat, "u_dirty", cf_canvas_get_target(hrc->dirty_canvas[i]));
        cf_material_set_uniform_fs(mat, "u_axis", axis, CF_UNIFORM_TYPE_INT2, 1);
        cf_material_set_uniform_fs(mat, "u_radius", &radius, CF_UNIFORM_TYPE_INT, 1);
        s_draw_region(hrc, hrc->dirty_expand_shd, hrc->dirty_canvas[1 - i], w, h);
    }
    hrc->horizon_work.dirty_passes = 3;
    hrc->horizon_work.tiles_x = w;
    hrc->horizon_work.tiles_y = h;
}

static void s_dispatch_horizon(HRC *hrc, render_view_2d view)
{
    CF_Material mat = hrc->pass_material;
    int ping = hrc->horizon_ping;
    if (!hrc->horizon_packed) {
        s_relief_inputs(hrc);
        s_draw_region(hrc, hrc->relief_pack_shd, hrc->relief_canvas[ping], hrc->canvas_w, hrc->canvas_h);
        hrc->horizon_work.pack_passes = 1;
    }
    CF_Texture relief = cf_canvas_get_target(hrc->relief_canvas[ping]);
    cf_material_set_texture_fs(mat, "u_relief", relief);
    float spacing[] = { view.width / hrc->canvas_w, view.height / hrc->canvas_h };
    int offset[2] = { 0 }, valid = 0, tile = HORIZON_TILE;
    if (hrc->horizon_options & HORIZON_CACHE) {
        valid = s_horizon_offset(hrc, view, offset);
        s_horizon_dirty(hrc, valid, offset, spacing);
        hrc->horizon_work.reuse_passes = valid;
        hrc->horizon_work.reproject_passes = valid && (offset[0] || offset[1]);
    }
    if (hrc->horizon_options & HORIZON_HIERARCHY) {
        int w = hrc->canvas_w, h = hrc->canvas_h;
        for (int i = 0; i < 3; i++) {
            int first = i == 0;
            cf_material_set_texture_fs(
                mat,
                "u_source",
                i ? cf_canvas_get_target(hrc->relief_levels[i - 1]) : relief
            );
            cf_material_set_uniform_fs(mat, "u_first_level", &first, CF_UNIFORM_TYPE_INT, 1);
            cf_material_set_uniform_fs(mat, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
            w = s_ceil_div(w, 2);
            h = s_ceil_div(h, 2);
            s_draw_region(hrc, hrc->relief_reduce_shd, hrc->relief_levels[i], w, h);
        }
        static const char *names[] = { "u_relief_half", "u_relief_quarter", "u_relief_eighth" };
        for (int i = 0; i < 3; i++) {
            cf_material_set_texture_fs(mat, names[i], cf_canvas_get_target(hrc->relief_levels[i]));
        }
        hrc->horizon_work.hierarchy_passes = 3;
    }
    int levels = 3;
    cf_material_set_uniform_fs(mat, "u_levels", &levels, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(mat, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_directions", &hrc->horizon_directions, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(mat, "u_cache_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(mat, "u_cache_offset", offset, CF_UNIFORM_TYPE_INT2, 1);
    cf_material_set_uniform_fs(mat, "u_cache_tile", &tile, CF_UNIFORM_TYPE_INT, 1);
    bool cache = hrc->horizon_options & HORIZON_CACHE;
    cf_material_set_texture_fs(
        mat,
        "u_previous_cardinal",
        cache ? cf_canvas_get_target2(hrc->horizon_canvas[1 - ping], 0) : hrc->black
    );
    cf_material_set_texture_fs(
        mat,
        "u_previous_diagonal",
        cache && hrc->horizon_directions == 8
            ? cf_canvas_get_target2(hrc->horizon_canvas[1 - ping], 1)
            : hrc->black
    );
    cf_material_set_texture_fs(
        mat,
        "u_dirty",
        cache ? cf_canvas_get_target(hrc->dirty_canvas[0]) : hrc->black
    );
    CF_Shader shader = hrc->horizon_options & HORIZON_HIERARCHY
                         ? (hrc->horizon_directions == 4 ? hrc->horizon_hierarchy4_shd
                                                         : hrc->horizon_hierarchy_shd)
                         : (hrc->horizon_directions == 4 ? hrc->horizon4_shd
                                                         : hrc->horizon_shd);
    s_draw_region(hrc, shader, hrc->horizon_canvas[ping], hrc->canvas_w, hrc->canvas_h);
    hrc->horizon_history = true;
    hrc->horizon_view = view;
    hrc->horizon_work.builds = 1;
    hrc->horizon_work.pixels = hrc->canvas_w * hrc->canvas_h;
}

static void s_destroy_depth_history(HRC *hrc)
{
    s_clear_texture_bindings(hrc);
    s_destroy_canvas(&hrc->depth_history[0]);
    s_destroy_canvas(&hrc->depth_history[1]);
    hrc->depth_has_history = false;
    hrc->depth_ping = 0;
}

static void s_destroy_surface(HRC *hrc)
{
    s_destroy_depth_history(hrc);
    s_clear_texture_bindings(hrc);
    s_destroy_canvas(&hrc->surface_canvas[0]);
    s_destroy_canvas(&hrc->surface_canvas[1]);
    s_destroy_canvas(&hrc->surface_half);
    s_destroy_canvas(&hrc->surface_history[0]);
    s_destroy_canvas(&hrc->surface_history[1]);
    for (int i = 0; i < SURFACE_EMISSION_LEVELS; i++) {
        s_destroy_canvas(&hrc->surface_emission[i]);
    }
    hrc->surface_ping = 0;
    hrc->surface_has_history = false;
    hrc->surface_w = hrc->surface_h = hrc->surface_block = 0;
}

static bool s_surface_resources(HRC *hrc, int width, int height, int block, HRCSurfaceMode mode)
{
    if (hrc->surface_w == width && hrc->surface_h == height &&
        hrc->surface_block == block && hrc->surface_resource_mode == mode) {
        return true;
    }
    s_destroy_surface(hrc);
    for (int i = 0; i < 2; i++) {
        if (i == 0 || mode != HRC_SURFACE_GATHER) {
            hrc->surface_canvas[i] = s_make_targets(width, height, 4, CF_FILTER_LINEAR);
        }
        if (mode != HRC_SURFACE_DEPTH) {
            hrc->surface_history[i] = s_make_targets(width, height, 4, CF_FILTER_LINEAR);
        }
        if (((i == 0 || mode != HRC_SURFACE_GATHER) && !hrc->surface_canvas[i].id) ||
            (mode != HRC_SURFACE_DEPTH && !hrc->surface_history[i].id)) {
            s_destroy_surface(hrc);
            return false;
        }
    }
    if (mode == HRC_SURFACE_BLUR && block >= 4) {
        int half = block / 2;
        hrc->surface_half = s_make_targets(width * half, height * half, 4, CF_FILTER_LINEAR);
        if (!hrc->surface_half.id) {
            s_destroy_surface(hrc);
            return false;
        }
    }
    if (mode == HRC_SURFACE_DEPTH) {
        /* The coarsest level fits the lowest four-tile source budget. */
        int base = 4;
        while (base * 128 < cf_max(hrc->canvas_w, hrc->canvas_h)) {
            base *= 2;
        }
        hrc->surface_emission_block = base;
        int w = s_ceil_div(hrc->canvas_w, base), h = s_ceil_div(hrc->canvas_h, base);
        for (int i = 0; i < SURFACE_EMISSION_LEVELS; i++) {
            hrc->surface_emission[i] = s_make_targets(w, h, 2, CF_FILTER_NEAREST);
            if (!hrc->surface_emission[i].id) {
                s_destroy_surface(hrc);
                return false;
            }
            w = s_ceil_div(w, 2);
            h = s_ceil_div(h, 2);
        }
    }
    hrc->surface_w = width;
    hrc->surface_h = height;
    hrc->surface_block = block;
    hrc->surface_resource_mode = mode;
    return true;
}

static void s_surface_inputs(HRC *hrc, CF_Canvas canvas, int first)
{
    static const char *const names[] = { "u_q0", "u_q1", "u_q2", "u_q3" };
    for (int i = 0; i < 4; i++) {
        cf_material_set_texture_fs(
            hrc->pass_material,
            names[i],
            cf_canvas_get_target2(canvas, first + i)
        );
    }
}

/* Blurs surface_canvas[from] along one axis into the other canvas. */
static void s_dispatch_surface_blur(HRC *hrc, int from, bool vertical, float sigma)
{
    CF_Material mat = hrc->pass_material;
    s_surface_inputs(hrc, hrc->surface_canvas[from], 0);
    int params[3] = { hrc->surface_w, hrc->surface_h, vertical };
    const char *names[3] = { "u_width", "u_height", "u_vertical" };
    s_set_ints(mat, names, params, 3);
    cf_material_set_uniform_fs(mat, "u_sigma", &sigma, CF_UNIFORM_TYPE_FLOAT, 1);
    s_draw_region(hrc, hrc->surface_blur_shd, hrc->surface_canvas[1 - from], hrc->surface_w, hrc->surface_h);
}

/* Stabilize the four directions on the small surface grid, retaining current
 * openness so history never replaces current geometry coverage. */
static void s_dispatch_surface_filter(HRC *hrc, int block)
{
    CF_Material mat = hrc->pass_material;
    int prev = hrc->surface_ping, curr = 1 - prev;
    s_surface_inputs(hrc, hrc->surface_canvas[0], 0);
    static const char *const names[] = { "u_prev0", "u_prev1", "u_prev2", "u_prev3" };
    for (int i = 0; i < 4; i++) {
        cf_material_set_texture_fs(mat, names[i], cf_canvas_get_target2(hrc->surface_history[prev], i));
    }
    float texel[2] = { 1.0f / hrc->surface_w, 1.0f / hrc->surface_h };
    CF_V2 offset = { hrc->history_offset.x * hrc->canvas_w / (hrc->surface_w * block),
                     hrc->history_offset.y * hrc->canvas_h / (hrc->surface_h * block) };
    float hysteresis = fminf(fmaxf(hrc->radiance_hysteresis, 0.01f), 1.0f);
    int valid = hrc->history_valid && hrc->surface_has_history;
    cf_material_set_uniform_fs(mat, "u_texel_size", texel, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_history_offset", &offset, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    s_draw_region(hrc, hrc->surface_filter_shd, hrc->surface_history[curr], hrc->surface_w, hrc->surface_h);
    hrc->surface_ping = curr;
    hrc->surface_has_history = true;
}

/* Receivers share a low-resolution grid. Blur uses two separable axes;
 * Gather samples bounded open-space probes with local height visibility.
 * Returns the reference blur width for the composite's direction gradient,
 * or zero when surface lighting is off. */
static CF_Shader s_make_pass_shader(const char *fragment);

/* Display emission must be normalized before averaging. Transport density
 * and display scale cannot be filtered independently and multiplied later. */
static void s_dispatch_surface_emission(HRC *hrc)
{
    CF_Material mat = hrc->pass_material;
    int base = hrc->surface_emission_block;
    int w = s_ceil_div(hrc->canvas_w, base), h = s_ceil_div(hrc->canvas_h, base);
    cf_material_set_uniform_fs(mat, "u_base_block", &base, CF_UNIFORM_TYPE_INT, 1);
    for (int i = 0; i < SURFACE_EMISSION_LEVELS; i++) {
        s_clear_texture_bindings(hrc);
        CF_Texture light = i ? cf_canvas_get_target2(hrc->surface_emission[i - 1], 0)
                             : hrc->source;
        CF_Texture geometry = i ? cf_canvas_get_target2(hrc->surface_emission[i - 1], 1)
                                : hrc->horizon_inputs.albedo;
        cf_material_set_texture_fs(mat, "u_source", light);
        cf_material_set_texture_fs(mat, "u_geometry", geometry);
        cf_material_set_texture_fs(mat, "u_cover", hrc->horizon_inputs.cover);
        int first = i == 0;
        cf_material_set_uniform_fs(mat, "u_first_level", &first, CF_UNIFORM_TYPE_INT, 1);
        s_draw_region(hrc, hrc->surface_emission_shd, hrc->surface_emission[i], w, h);
        w = s_ceil_div(w, 2);
        h = s_ceil_div(h, 2);
    }
    render_gpu_timing_detail("Surface emission pyramid");
    cf_material_set_uniform_fs(mat, "u_base_block", &base, CF_UNIFORM_TYPE_INT, 1);
    s_clear_texture_bindings(hrc);
    for (int i = 0; i < SURFACE_EMISSION_LEVELS; i++) {
        char name[32];
        snprintf(name, sizeof name, "u_emission%d", i);
        cf_material_set_texture_fs(mat, name, cf_canvas_get_target2(hrc->surface_emission[i], 0));
        snprintf(name, sizeof name, "u_emitter%d", i);
        cf_material_set_texture_fs(mat, name, cf_canvas_get_target2(hrc->surface_emission[i], 1));
    }
}

/* Depth history is independent of HRC transport history. Reprojection uses the
 * same camera basis as mesh/SDF projection; matching geometry rejects changes,
 * but tangential object motion needs motion vectors to identify perfectly. */
static bool s_dispatch_depth_history(HRC *hrc, render_view_2d view, float sigma)
{
    if (!hrc->lighting.surface_depth_temporal) {
        s_destroy_depth_history(hrc);
        return false;
    }
    if (!hrc->surface_depth_temporal_shd.id) {
        hrc->surface_depth_temporal_shd = s_make_pass_shader("hrc/surface_depth_temporal.frag");
    }
    if (!hrc->surface_depth_temporal_shd.id) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        if (!hrc->depth_history[i].id) {
            hrc->depth_history[i] = s_make_targets(hrc->surface_w, hrc->surface_h, 4, CF_FILTER_NEAREST);
        }
        if (!hrc->depth_history[i].id) {
            s_destroy_depth_history(hrc);
            return false;
        }
    }
    CF_Material mat = hrc->pass_material;
    s_clear_texture_bindings(hrc);
    s_surface_inputs(hrc, hrc->surface_canvas[1], 0);
    static const char *const names[] = { "u_prev0", "u_prev1", "u_prev2", "u_prev3" };
    int previous = hrc->depth_ping, current = 1 - previous;
    for (int i = 0; i < 4; i++) {
        cf_material_set_texture_fs(
            mat,
            names[i],
            cf_canvas_get_target2(hrc->depth_history[previous], i)
        );
    }
    render_view_2d prev = hrc->depth_has_history ? hrc->depth_view : view;
    CF_V3 basis[3], old[3];
    render_mesh_camera_basis(view, basis);
    render_mesh_camera_basis(prev, old);
    CF_V3 delta = cf_sub_v3(render_mesh_camera_target(view), render_mesh_camera_target(prev));
    CF_V3 columns[] = { basis[0], cf_mul_v3_f(basis[2], -1), basis[1] };
    CF_V3 rows[] = { old[0], cf_mul_v3_f(old[2], -1), old[1] };
    const char *const row_names[] = { "u_transform0", "u_transform1", "u_transform2" };
    for (int i = 0; i < 3; i++) {
        float row[] = { cf_dot_v3(rows[i], columns[0]),
                        cf_dot_v3(rows[i], columns[1]),
                        cf_dot_v3(rows[i], columns[2]),
                        cf_dot_v3(rows[i], delta) };
        cf_material_set_uniform_fs(mat, row_names[i], row, CF_UNIFORM_TYPE_FLOAT4, 1);
    }
    float projection[] = { view.min_x - view.center_x,
                           view.max_y - view.center_y,
                           view.width,
                           view.height };
    float old_projection[] = { prev.min_x - prev.center_x,
                               prev.max_y - prev.center_y,
                               prev.width,
                               prev.height };
    float focal[] = { view.focal_distance, prev.focal_distance };
    float raster[] = { (float)hrc->canvas_w, (float)hrc->canvas_h };
    int valid = hrc->depth_has_history;
    cf_material_set_uniform_fs(mat, "u_projection", projection, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(mat, "u_previous_projection", old_projection, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(mat, "u_focal", focal, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_raster", raster, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_block", &hrc->surface_block, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_fs(mat, "u_sigma", &sigma, CF_UNIFORM_TYPE_FLOAT, 1);
    float history = hrc->lighting.surface_depth_history > 0
                      ? hrc->lighting.surface_depth_history
                      : .65f;
    cf_material_set_uniform_fs(mat, "u_history", &history, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
    s_draw_region(
        hrc,
        hrc->surface_depth_temporal_shd,
        hrc->depth_history[current],
        hrc->surface_w,
        hrc->surface_h
    );
    hrc->depth_ping = current;
    hrc->depth_has_history = true;
    hrc->depth_view = view;
    hrc->surface_work.temporal_cells = hrc->surface_w * hrc->surface_h;
    hrc->surface_work.temporal_bytes = (size_t)hrc->surface_w * hrc->surface_h *
                                       4 * 8 * 2;
    render_gpu_timing_detail("Surface depth temporal filter");
    return true;
}

static float s_dispatch_surface(HRC *hrc, render_view_2d view, int *block)
{
    hrc->surface_work = (render_surface_work){ 0 };
    float radius = hrc->authored_lighting ? hrc->lighting.surface_radius : 0;
    if (radius <= 0 || !hrc->surface_seed_shd.id || view.width <= 0) {
        hrc->surface_has_history = false;
        return 0;
    }
    float sigma = .5f * radius * (float)hrc->canvas_w / view.width;
    *block = 1;
    while (*block < 8 && sigma / (float)(*block * 2) >= 2) {
        *block *= 2;
    }
    if (hrc->lighting.surface_mode == HRC_SURFACE_DEPTH) {
        *block = 4;
        if (hrc->lighting.surface_depth_radius <= 0 ||
            hrc->lighting.surface_depth_gain <= 0) {
            return 0;
        }
        if (!hrc->surface_depth_shd.id) {
            hrc->surface_depth_shd = s_make_pass_shader("hrc/surface_depth.frag");
        }
        if (!hrc->surface_emission_shd.id) {
            hrc->surface_emission_shd = s_make_pass_shader("hrc/surface_emission.frag");
        }
        if (!hrc->surface_depth_filter_shd.id) {
            hrc->surface_depth_filter_shd = s_make_pass_shader("hrc/surface_depth_filter.frag");
        }
        if (!hrc->surface_depth_shd.id || !hrc->surface_emission_shd.id ||
            !hrc->surface_depth_filter_shd.id) {
            return 0;
        }
    }
    int width = s_ceil_div(hrc->canvas_w, *block);
    int height = s_ceil_div(hrc->canvas_h, *block);
    if (!s_surface_resources(hrc, width, height, *block, hrc->lighting.surface_mode)) {
        return 0;
    }
    hrc->surface_work.cells = width * height;
    hrc->surface_work.block = *block;
    hrc->surface_work.visibility = hrc->lighting.surface_depth_visibility;
    hrc->surface_work.mode = hrc->lighting.surface_mode;
    if (hrc->lighting.surface_mode == HRC_SURFACE_DEPTH) {
        CF_Material mat = hrc->pass_material;
        s_dispatch_surface_emission(hrc);
        int axis = 4 + 2 * hrc->lighting.surface_depth_quality;
        int near_detail = hrc->lab.surface_depth_near_detail;
        int visibility = hrc->lighting.surface_depth_visibility;
        cf_material_set_uniform_fs(mat, "u_visibility_mode", &visibility, CF_UNIFORM_TYPE_INT, 1);
        cf_material_set_uniform_fs(mat, "u_source_axis", &axis, CF_UNIFORM_TYPE_INT, 1);
        cf_material_set_uniform_fs(mat, "u_near_detail", &near_detail, CF_UNIFORM_TYPE_INT, 1);
        cf_material_set_uniform_fs(
            mat,
            "u_shadow_softness",
            &hrc->lighting.surface_depth_shadow_softness,
            CF_UNIFORM_TYPE_FLOAT,
            1
        );
        cf_material_set_texture_fs(mat, "u_source", hrc->source);
        cf_material_set_texture_fs(mat, "u_albedo", hrc->horizon_inputs.albedo);
        cf_material_set_texture_fs(mat, "u_cover", hrc->horizon_inputs.cover);
        cf_material_set_texture_fs(mat, "u_normal", hrc->horizon_inputs.normal);
        float spacing[] = { view.width / hrc->canvas_w, view.height / hrc->canvas_h };
        float projection[] = { view.min_x - view.center_x,
                               view.max_y - view.center_y,
                               view.focal_distance,
                               0 };
        cf_material_set_uniform_fs(mat, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_fs(mat, "u_projection", projection, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(mat, "u_radius", &hrc->lighting.surface_depth_radius, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(mat, "u_thickness", &hrc->lighting.surface_thickness, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(mat, "u_specular", &hrc->lighting.specular, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(mat, "u_relief", &hrc->lighting.surface_relief, CF_UNIFORM_TYPE_FLOAT, 1);
        /* The merge's falloff per canvas pixel. It dims light over the
         * cascade intervals before the one that reaches it, which start at
         * powers of two: on average over 1 / (2 ln 2) of its distance. */
        float diagonal = hypotf((float)hrc->canvas_w, (float)hrc->canvas_h);
        float falloff = hrc->falloff * 2.0f / diagonal / (2.0f * logf(2.0f));
        cf_material_set_uniform_fs(mat, "u_falloff", &falloff, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(mat, "u_block", block, CF_UNIFORM_TYPE_INT, 1);
        s_draw_region(hrc, hrc->surface_depth_shd, hrc->surface_canvas[0], width, height);
        hrc->surface_work.candidates = (size_t)width * height * (axis + 1) *
                                       (axis + 1);
        /* Every receiver walks 96 radial taps for ambient occlusion, and in
         * radial mode for visibility too; segments walk each candidate. */
        size_t segment_taps = (size_t)(32 + 16 * hrc->lighting.surface_depth_quality);
        hrc->surface_work.visibility_taps = (size_t)width * height * 96;
        if (visibility == HRC_DEPTH_VISIBILITY_SEGMENTS) {
            hrc->surface_work.visibility_taps += hrc->surface_work.candidates *
                                                 segment_taps;
        }
        render_gpu_timing_detail("Surface depth thickness");
        s_clear_texture_bindings(hrc);
        s_surface_inputs(hrc, hrc->surface_canvas[0], 0);
        float depth_sigma = fmaxf(hrc->lighting.surface_thickness * .25f, spacing[0] * 8);
        cf_material_set_uniform_fs(mat, "u_sigma", &depth_sigma, CF_UNIFORM_TYPE_FLOAT, 1);
        projection[3] = (float)*block;
        float canvas_size[] = { (float)hrc->canvas_w, (float)hrc->canvas_h };
        cf_material_set_uniform_fs(mat, "u_projection", projection, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_material_set_uniform_fs(mat, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_fs(mat, "u_canvas_size", canvas_size, CF_UNIFORM_TYPE_FLOAT2, 1);
        s_draw_region(hrc, hrc->surface_depth_filter_shd, hrc->surface_canvas[1], width, height);
        render_gpu_timing_detail("Surface depth spatial filter");
        s_dispatch_depth_history(hrc, view, depth_sigma);
        return sigma / (float)*block;
    }
    if (hrc->lighting.surface_mode == HRC_SURFACE_GATHER) {
        CF_Material mat = hrc->pass_material;
        s_surface_inputs(hrc, hrc->gather_canvas, 2);
        cf_material_set_texture_fs(mat, "u_source", hrc->source);
        cf_material_set_texture_fs(mat, "u_albedo", hrc->horizon_inputs.albedo);
        cf_material_set_texture_fs(mat, "u_cover", hrc->horizon_inputs.cover);
        cf_material_set_texture_fs(mat, "u_normal", hrc->horizon_inputs.normal);
        float spacing[] = { view.width / hrc->canvas_w, view.height / hrc->canvas_h };
        float relief = hrc->lighting.surface_relief;
        cf_material_set_uniform_fs(mat, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_fs(mat, "u_radius", &radius, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(mat, "u_relief", &relief, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(mat, "u_block", block, CF_UNIFORM_TYPE_INT, 1);
        s_draw_region(hrc, hrc->surface_gather_shd, hrc->surface_canvas[0], width, height);
        hrc->surface_work.candidates = (size_t)width * height * 12;
        hrc->surface_work.visibility_taps = (size_t)width * height * 48;
        render_gpu_timing_detail("Surface gather");
        s_dispatch_surface_filter(hrc, *block);
        return sigma / (float)*block;
    }
    /* A seed fragment fetches its block's taps in turn. Blocks of 4 pixels or
     * more give too few fragments to hide that latency, so they seed 2-pixel
     * blocks, which have one tap each, and average those. */
    int seed = *block >= 4 ? 2 : *block;
    CF_Canvas seeded = seed < *block ? hrc->surface_half : hrc->surface_canvas[0];
    CF_Material mat = hrc->pass_material;
    s_surface_inputs(hrc, hrc->gather_canvas, 2);
    cf_material_set_texture_fs(mat, "u_scene", hrc->scene);
    int params[3] = { hrc->canvas_w, hrc->canvas_h, seed };
    const char *names[3] = { "u_canvas_w", "u_canvas_h", "u_block" };
    s_set_ints(mat, names, params, 3);
    s_draw_region(hrc, hrc->surface_seed_shd, seeded, width * (*block / seed), height * (*block / seed));
    if (seed < *block) {
        s_surface_inputs(hrc, hrc->surface_half, 0);
        int reduce = *block / seed;
        cf_material_set_uniform_fs(mat, "u_block", &reduce, CF_UNIFORM_TYPE_INT, 1);
        s_draw_region(hrc, hrc->surface_reduce_shd, hrc->surface_canvas[0], width, height);
    }
    render_gpu_timing_detail("Surface seed");
    float level_sigma = sigma / (float)*block;
    hrc->surface_work.blur_quadrant_fetches = (size_t)width * height * 8 *
                                              (2 * (int)ceilf(level_sigma * 3) + 1);
    s_dispatch_surface_blur(hrc, 0, false, level_sigma);
    s_dispatch_surface_blur(hrc, 1, true, level_sigma);
    s_dispatch_surface_filter(hrc, *block);
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
    s_clear_texture_bindings(hrc);
    s_destroy_canvas(&hrc->bounce_canvas);
    s_destroy_canvas(&hrc->spot_canvas);
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
        hrc->relief_pack_shd = s_make_pass_shader("hrc/relief_pack.frag");
        hrc->horizon_shd = s_make_pass_shader("hrc/horizon.frag");
        hrc->near_relief_shd = s_make_pass_shader("hrc/near_relief.frag");
        hrc->relief_dirty_shd = s_make_pass_shader("hrc/relief_dirty.frag");
        hrc->dirty_expand_shd = s_make_pass_shader("hrc/dirty_expand.frag");
        hrc->relief_reduce_shd = s_make_pass_shader("hrc/relief_reduce.frag");
        hrc->horizon_hierarchy_shd = s_make_pass_shader("hrc/horizon_hierarchy.frag");
        hrc->horizon4_shd = s_make_pass_shader("hrc/horizon4.frag");
        hrc->horizon_hierarchy4_shd = s_make_pass_shader("hrc/horizon_hierarchy4.frag");
        hrc->surface_gather_shd = s_make_pass_shader("hrc/surface_gather.frag");
        hrc->surface_seed_shd = s_make_pass_shader("hrc/surface_seed.frag");
        hrc->surface_reduce_shd = s_make_pass_shader("hrc/surface_reduce.frag");
        hrc->surface_blur_shd = s_make_pass_shader("hrc/surface_blur.frag");
        hrc->surface_filter_shd = s_make_pass_shader("hrc/surface_filter.frag");
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
        ok = ok && hrc->horizon4_shd.id && hrc->horizon_hierarchy4_shd.id &&
             hrc->near_relief_shd.id && hrc->relief_dirty_shd.id &&
             hrc->dirty_expand_shd.id && hrc->relief_reduce_shd.id &&
             hrc->horizon_hierarchy_shd.id && hrc->relief_pack_shd.id &&
             hrc->horizon_shd.id && hrc->prefilter_shd.id && hrc->near_shd.id &&
             hrc->gather_shd.id && hrc->filter_shd.id &&
             hrc->surface_gather_shd.id && hrc->surface_seed_shd.id &&
             hrc->surface_reduce_shd.id && hrc->surface_blur_shd.id &&
             hrc->surface_filter_shd.id && hrc->bounce_shd.id &&
             hrc->bounce_material.id && hrc->composite_shd.id && hrc->black.id;
    }
    hrc->scale = .5f;
    hrc->reconstruction_scale = 1;
    hrc->gpu_ms = -1;
    hrc->trace_levels = 3;
    hrc->feedback = true;
    hrc->falloff = hrc->bounce_intensity = 1;
    hrc->bounce_hysteresis = hrc->radiance_hysteresis = 0.8f;
    hrc->lab = render_radiance_lab_defaults();
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
    s_destroy_horizon(hrc);
    CF_Shader passes[] = { hrc->trace_shd,
                           hrc->spot_shd,
                           hrc->extend_shd,
                           hrc->merge_shd,
                           hrc->resolve_shd,
                           hrc->prefilter_shd,
                           hrc->downsample_shd,
                           hrc->near_shd,
                           hrc->gather_shd,
                           hrc->filter_shd,
                           hrc->bounce_shd,
                           hrc->surface_seed_shd,
                           hrc->surface_reduce_shd,
                           hrc->surface_blur_shd,
                           hrc->surface_filter_shd,
                           hrc->surface_gather_shd,
                           hrc->surface_depth_shd,
                           hrc->surface_emission_shd,
                           hrc->surface_depth_filter_shd,
                           hrc->surface_depth_temporal_shd,
                           hrc->composite_shd,
                           hrc->relief_pack_shd,
                           hrc->horizon_shd,
                           hrc->near_relief_shd,
                           hrc->relief_dirty_shd,
                           hrc->dirty_expand_shd,
                           hrc->relief_reduce_shd,
                           hrc->horizon_hierarchy_shd,
                           hrc->horizon4_shd,
                           hrc->horizon_hierarchy4_shd };
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
    hrc->surface_has_history = false;
    hrc->depth_has_history = false;
    hrc->surface_work = (render_surface_work){ 0 };
    hrc->horizon_history = false;
    hrc->horizon_work = (render_horizon_work){ 0 };
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
    s_forget_textures(hrc);
    bool ok = true;
    if (images_changed) {
        destroy_images(hrc);
        hrc->bounce_canvas = s_make_image(radiance_w, radiance_h);
        hrc->radiance_canvas[0] = s_make_image(radiance_w, radiance_h);
        hrc->radiance_canvas[1] = s_make_image(radiance_w, radiance_h);
        /* Surface lighting filters gather's per-quadrant luminance. */
        hrc->gather_canvas = s_make_targets(radiance_w, radiance_h, 6, CF_FILTER_LINEAR);
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
    render_radiance_color color;
    bool gather_half;
} hrc_rung;

/* Adaptive halves gather first when it is Auto. */
static int s_gather_steps(HRC2D lighting, render_radiance_lab lab)
{
    return lighting.adaptive && lab.gather_resolution == RENDER_RADIANCE_GATHER_AUTO;
}

/* Then it coarsens color up to Quarter at the authored resolution, which
 * costs less than halving the resolution and changes the image less. Below
 * that resolution the transport is a small part of the frame, so halvings
 * keep the authored color. */
static int s_color_steps(HRC2D lighting, render_radiance_lab lab)
{
    return lighting.adaptive
             ? cf_max(RENDER_RADIANCE_COLOR_QUARTER - (int)lab.color_resolution, 0)
             : 0;
}

/* Rung 0 is the authored lighting; each later one is cheaper. */
static int s_rung_count(HRC2D lighting, render_radiance_lab lab)
{
    if (!lighting.adaptive) {
        return 1;
    }
    int steps = s_gather_steps(lighting, lab) + s_color_steps(lighting, lab);
    int halvings = 0;
    while (steps + halvings + 2 < HRC_MAX_RUNGS &&
           lighting.resolution / (float)(2 << halvings) >=
               HRC_MIN_ADAPTIVE_RESOLUTION - 1e-4f) {
        halvings++;
    }
    return 1 + steps + halvings;
}

static hrc_rung s_rung(HRC2D lighting, render_radiance_lab lab, int rung)
{
    int gather = s_gather_steps(lighting, lab);
    int colors = s_color_steps(lighting, lab);
    int coarser = cf_min(cf_max(rung - gather, 0), colors);
    int halvings = cf_max(rung - gather - colors, 0);
    return (hrc_rung){
        .resolution = lighting.resolution / (float)(1 << halvings),
        .color = lab.color_resolution + (halvings ? 0 : coarser),
        .gather_half = lab.gather_resolution == RENDER_RADIANCE_GATHER_HALF ||
                       (gather && rung >= gather),
    };
}

/* Color's coarsest grid over the rungs, which the view reserves room for. */
static float s_min_scale(HRC2D lighting, render_radiance_lab lab)
{
    float scale = HRC_MAX_SCALE;
    for (int i = 0; i < s_rung_count(lighting, lab); i++) {
        hrc_rung rung = s_rung(lighting, lab, i);
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
        .min_scale = radiance->authored_lighting
                       ? s_min_scale(radiance->lighting, radiance->lab)
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

/* The ranges each independent HRC2D field supports. */
static const s_float_range s_float_ranges[] = {
    { offsetof(HRC2D, resolution), HRC_MIN_RESOLUTION, HRC_MAX_SCALE, "resolution" },
    { offsetof(HRC2D, target_fps), 1, 1000, "target_fps" },
    { offsetof(HRC2D, ambient), 0, 100, "ambient" },
    { offsetof(HRC2D, intensity), 0, 100, "intensity" },
    { offsetof(HRC2D, fog), 0, 100, "fog" },
    { offsetof(HRC2D, volume_density), 0, 1, "volume_density" },
    { offsetof(HRC2D, shadow_floor), 0, 100, "shadow_floor" },
    { offsetof(HRC2D, falloff), 0, 100, "falloff" },
    { offsetof(HRC2D, lighting_margin), 0, 256, "lighting_margin" },
    { offsetof(HRC2D, bounce_intensity), 0, 100, "bounce_intensity" },
    { offsetof(HRC2D, bounce_hysteresis), 0, 1, "bounce_hysteresis" },
    { offsetof(HRC2D, radiance_hysteresis), 0, 1, "radiance_hysteresis" },
    { offsetof(HRC2D, surface_radius), 0, 1024, "surface_radius" },
    { offsetof(HRC2D, surface_depth_radius), 0, 1024, "surface_depth_radius" },
    { offsetof(HRC2D, surface_depth_gain), 0, 100, "surface_depth_gain" },
    { offsetof(HRC2D, surface_depth_shadow_softness), 0, 1, "surface_depth_shadow_softness" },
    { offsetof(HRC2D, surface_depth_history), 0, .95f, "surface_depth_history" },
    { offsetof(HRC2D, surface_thickness), 0, 1024, "surface_thickness" },
    { offsetof(HRC2D, surface_relief), 0, 100, "surface_relief" },
    { offsetof(HRC2D, surface_bevel), 0, 100, "surface_bevel" },
    { offsetof(HRC2D, specular), 0, 100, "specular" },
    { offsetof(HRC2D, exposure), -16, 16, "exposure" },
    { offsetof(HRC2D, bloom), 0, 1, "bloom" },
};

static const s_int_range s_int_ranges[] = {
    { offsetof(HRC2D, tone_mapping), 0, 1, "tone_mapping" },
    { offsetof(HRC2D, surface_shadow_mode), 0, 2, "surface_shadow_mode" },
    { offsetof(HRC2D, surface_mode), 0, HRC_SURFACE_DEPTH, "surface_mode" },
    { offsetof(HRC2D, surface_depth_quality), HRC_DEPTH_LOW, HRC_DEPTH_HIGH, "surface_depth_quality" },
    { offsetof(HRC2D, surface_depth_visibility), 0, HRC_DEPTH_VISIBILITY_SEGMENTS, "surface_depth_visibility" },
};

static void s_note(char *notes, size_t size, const char *name, double from, double to)
{
    size_t used = strlen(notes);
    if (used + 1 < size) {
        snprintf(notes + used, size - used, "%s%s %g -> %g", used ? ", " : "", name, from, to);
    }
}

HRC2D render_radiance_normalize(HRC2D lighting, char *notes, size_t size)
{
    assert(size > 0);
    notes[0] = '\0';
    HRC2D defaults = hrc2d_defaults();
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
    hrc_rung rung = s_rung(hrc->lighting, hrc->lab, hrc->rung);
    float reconstruction = rung.gather_half ? .5f : 1;
    int shift = rung.color;
    bool layout_changed = hrc->reconstruction_scale != reconstruction ||
                          hrc->luminance_shift != shift;
    hrc->reconstruction_scale = reconstruction;
    hrc->luminance_shift = shift;
    render_radiance_settings settings = {
        .scale = rung.resolution / (float)(1 << shift),
        .trace_levels = hrc->lab.trace_levels,
        .feedback = hrc->lighting.feedback,
    };
    if (!render_radiance_set_settings(hrc, settings)) {
        return false;
    }
    return !layout_changed || !hrc->canvas_w ||
           render_radiance_resize(hrc, hrc->canvas_w, hrc->canvas_h);
}

static bool s_ladder_changed(const HRC *hrc, HRC2D lighting, render_radiance_lab lab)
{
    return hrc->lighting.resolution != lighting.resolution ||
           hrc->lighting.adaptive != lighting.adaptive ||
           hrc->lab.color_resolution != lab.color_resolution ||
           hrc->lab.gather_resolution != lab.gather_resolution;
}

render_radiance_lab render_radiance_lab_defaults()
{
    return (render_radiance_lab){ .trace_levels = 3 };
}

bool render_radiance_set_lighting(render_radiance *radiance, HRC2D lighting, render_radiance_lab lab)
{
    assert(radiance);
    assert(
        lab.color_resolution >= RENDER_RADIANCE_COLOR_FULL &&
        lab.color_resolution <= RENDER_RADIANCE_COLOR_EIGHTH
    );
    assert(
        lab.gather_resolution >= RENDER_RADIANCE_GATHER_FULL &&
        lab.gather_resolution <= RENDER_RADIANCE_GATHER_AUTO
    );
    assert(lab.trace_levels >= HRC_MIN_TRACE_LEVELS && lab.trace_levels <= HRC_MAX_TRACE_LEVELS);
    assert(
        lab.debug_view >= RENDER_RADIANCE_DEBUG_NORMAL &&
        lab.debug_view <= RENDER_RADIANCE_DEBUG_SHADING_NORMALS
    );
    char notes[256];
    render_radiance_normalize(lighting, notes, sizeof notes);
    if (notes[0]) {
        return false;
    }
    if (!radiance->authored_lighting || s_ladder_changed(radiance, lighting, lab)) {
        radiance->rung = radiance->settle = radiance->window_count = 0;
        radiance->over = radiance->rise = 0;
        radiance->window_sum = radiance->cost_above = 0;
        memset(radiance->rung_ratio, 0, sizeof radiance->rung_ratio);
    }
    if (radiance->lighting.surface_mode != lighting.surface_mode ||
        radiance->lighting.surface_radius != lighting.surface_radius ||
        radiance->lighting.surface_relief != lighting.surface_relief) {
        radiance->surface_has_history = false;
    }
    HRC2D prior = radiance->lighting;
    if (prior.falloff != lighting.falloff || prior.surface_mode != lighting.surface_mode ||
        prior.surface_radius != lighting.surface_radius ||
        prior.surface_depth_radius != lighting.surface_depth_radius ||
        prior.surface_thickness != lighting.surface_thickness ||
        prior.surface_depth_gain != lighting.surface_depth_gain ||
        prior.surface_depth_quality != lighting.surface_depth_quality ||
        prior.surface_depth_visibility != lighting.surface_depth_visibility ||
        radiance->lab.surface_depth_near_detail != lab.surface_depth_near_detail ||
        prior.surface_depth_shadow_softness != lighting.surface_depth_shadow_softness ||
        prior.surface_depth_temporal != lighting.surface_depth_temporal ||
        prior.surface_depth_history != lighting.surface_depth_history ||
        prior.specular != lighting.specular) {
        radiance->depth_has_history = false;
    }
    if (!lighting.surface_depth_temporal || lighting.surface_mode != HRC_SURFACE_DEPTH) {
        s_destroy_depth_history(radiance);
    }
    radiance->lighting = lighting;
    radiance->lab = lab;
    radiance->authored_lighting = true;
    radiance->falloff = lighting.falloff;
    radiance->bounce_intensity = lighting.feedback ? lighting.bounce_intensity : 0;
    radiance->bounce_hysteresis = lighting.bounce_hysteresis;
    radiance->radiance_hysteresis = lighting.radiance_hysteresis;
    return s_apply_rung(radiance);
}

void render_radiance_frame_time(render_radiance *hrc, double gpu_ms)
{
    assert(hrc);
    hrc->gpu_ms = gpu_ms;
    if (!hrc->authored_lighting || s_rung_count(hrc->lighting, hrc->lab) < 2 ||
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
    bool over = average > budget &&
                hrc->rung + 1 < s_rung_count(hrc->lighting, hrc->lab);
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
    (void)applied;
}

void render_radiance_frame_interval(render_radiance *hrc, double ms)
{
    render_radiance_frame_time(hrc, ms / HRC_ADAPT_INTERVAL_SLACK);
}

render_radiance_status render_radiance_get_status(const render_radiance *radiance)
{
    assert(radiance);
    HRC2D lighting = radiance->authored_lighting ? radiance->lighting
                                                 : hrc2d_defaults();
    hrc_rung rung = s_rung(lighting, radiance->lab, radiance->rung);
    return (render_radiance_status){
        .resolution = rung.resolution,
        .color = rung.color,
        .gather_half = rung.gather_half,
        .gpu_ms = render_gpu_span_available() ? radiance->gpu_ms : -1,
        .budget_ms = 1000 / lighting.target_fps,
        .adaptive = lighting.adaptive,
        .rung = radiance->rung,
        .rungs = s_rung_count(lighting, radiance->lab),
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

static bool s_dispatch_transport(HRC *hrc, render_radiance_inputs inputs, render_view_2d view)
{
    if (!hrc->canvas_w) {
        return false;
    }
    /* Materials retain sampler handles across draws. The next frame may have
     * replaced work targets or borrowed raster inputs; discard those bindings
     * before WebGL's draw fences inspect every retained texture handle. Each
     * transport phase binds its current inputs below. */
    cf_material_clear_textures(hrc->pass_material);
    if (hrc->bounce_material.id) {
        cf_material_clear_textures(hrc->bounce_material);
    }
    if (hrc->luminance) {
        cf_material_clear_textures(hrc->luminance->pass_material);
    }
    s_begin_view(hrc, view);
    hrc->screen = (CF_Aabb){ .min = { view.min_x + view.width * view.crop_min_u,
                                      view.max_y - view.height * view.crop_max_v },
                             .max = { view.min_x + view.width * view.crop_max_u,
                                      view.max_y - view.height * view.crop_min_v } };
    hrc->scene = inputs.material.scene;
    hrc->source = inputs.material.source;
    hrc->albedo = inputs.material.albedo;
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

render_surface_work render_radiance_surface_work(const render_radiance *hrc)
{
    assert(hrc);
    return hrc->surface_work;
}

render_horizon_work render_radiance_horizon_work(const render_radiance *hrc)
{
    assert(hrc);
    render_horizon_work work = hrc->horizon_work;
    work.canvas_bytes = s_horizon_bytes(hrc);
    return work;
}

CF_Texture render_radiance_horizon_dirty_texture(const render_radiance *hrc)
{
    return hrc->dirty_canvas[0].id ? cf_canvas_get_target(hrc->dirty_canvas[0])
                                   : (CF_Texture){ 0 };
}

static bool s_dispatch_spots(HRC *hrc, render_radiance_inputs inputs, render_view_2d view)
{
    assert(inputs.spot_count >= 0 && inputs.spot_count <= RENDER_SPOT_LIGHT_LIMIT);
    if (!inputs.spot_count) {
        return true;
    }
    assert(inputs.spots);
    if (!hrc->spot_shd.id) {
        hrc->spot_shd = s_make_pass_shader("hrc/spot.frag");
    }
    if (!hrc->spot_canvas.id) {
        hrc->spot_canvas = s_make_level(hrc->canvas_w, hrc->canvas_h, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
    }
    if (!hrc->spot_shd.id || !hrc->spot_canvas.id) {
        return false;
    }
    CF_V4 positions[RENDER_SPOT_LIGHT_LIMIT] = { 0 },
          colors[RENDER_SPOT_LIGHT_LIMIT] = { 0 },
          cones[RENDER_SPOT_LIGHT_LIMIT] = { 0 },
          options[RENDER_SPOT_LIGHT_LIMIT] = { 0 };
    for (int i = 0; i < inputs.spot_count; i++) {
        positions[i] = inputs.spots[i].position_radius;
        colors[i] = inputs.spots[i].color_intensity;
        cones[i] = inputs.spots[i].direction_outer;
        options[i] = (CF_V4){ inputs.spots[i].inner_cos, inputs.spots[i].shadows, 0, 0 };
    }
    CF_Material mat = hrc->pass_material;
    cf_material_set_texture_fs(mat, "u_albedo", inputs.material.albedo);
    cf_material_set_texture_fs(mat, "u_cover", inputs.material.cover);
    cf_material_set_texture_fs(mat, "u_normal", inputs.material.normal);
    cf_material_set_texture_fs(mat, "u_source", inputs.material.source);
    cf_material_set_uniform_fs(mat, "u_positions", positions, CF_UNIFORM_TYPE_FLOAT4, RENDER_SPOT_LIGHT_LIMIT);
    cf_material_set_uniform_fs(mat, "u_colors", colors, CF_UNIFORM_TYPE_FLOAT4, RENDER_SPOT_LIGHT_LIMIT);
    cf_material_set_uniform_fs(mat, "u_cones", cones, CF_UNIFORM_TYPE_FLOAT4, RENDER_SPOT_LIGHT_LIMIT);
    cf_material_set_uniform_fs(mat, "u_options", options, CF_UNIFORM_TYPE_FLOAT4, RENDER_SPOT_LIGHT_LIMIT);
    float projection[] = { view.min_x - view.center_x,
                           view.max_y - view.center_y,
                           view.focal_distance,
                           1 };
    float spacing[] = { view.width / hrc->canvas_w, view.height / hrc->canvas_h };
    cf_material_set_uniform_fs(mat, "u_projection", projection, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_uniform_fs(mat, "u_spacing", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(mat, "u_count", &inputs.spot_count, CF_UNIFORM_TYPE_INT, 1);
    HRC2D lighting = hrc->authored_lighting ? hrc->lighting : hrc2d_defaults();
    cf_material_set_uniform_fs(mat, "u_thickness", &lighting.surface_thickness, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_specular", &lighting.specular, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(mat, "u_density", &lighting.volume_density, CF_UNIFORM_TYPE_FLOAT, 1);
    s_draw_region(hrc, hrc->spot_shd, hrc->spot_canvas, hrc->canvas_w, hrc->canvas_h);
    return true;
}

bool render_radiance_draw(HRC *hrc, render_radiance_inputs inputs, render_view_2d view, CF_Canvas output)
{
    assert(hrc);
    hrc->horizon_work = (render_horizon_work){ 0 };
    hrc->surface_work = (render_surface_work){ 0 };
    if (!hrc->canvas_w || !output.id || !inputs.material.scene.id ||
        !inputs.material.source.id || !inputs.material.albedo.id) {
        return false;
    }
    s_clear_texture_bindings(hrc);
    if (hrc->luminance) {
        s_clear_texture_bindings(hrc->luminance);
    }
    CHIBI_TRACE_ZONE_BEGIN(zone, "HRC submission");
    /* Prefilter averages full-resolution inputs directly; only gather at a
     * reduced reconstruction resolution samples emission mipmaps. */
    if (hrc->radiance_w != hrc->canvas_w || hrc->radiance_h != hrc->canvas_h) {
        cf_generate_mipmaps(inputs.material.source);
        render_gpu_timing_mark("Emission mipmaps");
    }
    HRC2D lighting = hrc->authored_lighting ? hrc->lighting : hrc2d_defaults();
    int horizon_directions = lighting.surface_mode != HRC_SURFACE_DEPTH &&
                                     lighting.surface_radius > 0 &&
                                     lighting.surface_relief > 0
                               ? (lighting.surface_shadow_mode == HRC_SHADOW_HORIZON4 ? 4
                                  : lighting.surface_shadow_mode == HRC_SHADOW_HORIZON8
                                      ? 8
                                      : 0)
                               : 0;
    if (!s_prepare_horizon(hrc, inputs.material, horizon_directions) ||
        !s_dispatch_transport(hrc, inputs, view)) {
        CHIBI_TRACE_ZONE_END(zone);
        return false;
    }
    int block = 1;
    s_push_hrc_label(hrc, "surface");
    float surface_sigma = s_dispatch_surface(hrc, view, &block);
    cf_pop_gpu_label();
    render_gpu_timing_mark("Surface light");
    s_push_hrc_label(hrc, "horizon");
    if (horizon_directions) {
        s_dispatch_horizon(hrc, view);
    }
    cf_pop_gpu_label();
    render_gpu_timing_mark("Surface horizon");
    if (!s_dispatch_spots(hrc, inputs, view)) {
        CHIBI_TRACE_ZONE_END(zone);
        return false;
    }
    float ambient = lighting.ambient;
    float intensity = lighting.intensity;
    float shadow_floor = lighting.shadow_floor;
    float fog = lighting.fog;
    float debug = (float)hrc->lab.debug_view;
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
    cf_draw_set_texture("visibility_tex", inputs.material.visibility);
    cf_draw_set_texture("cover_tex", inputs.material.cover);
    cf_draw_set_texture(
        "spot_tex",
        inputs.spot_count ? cf_canvas_get_target(hrc->spot_canvas) : hrc->black
    );
    cf_draw_set_texture(
        "horizon_cardinal",
        horizon_directions
            ? cf_canvas_get_target2(hrc->horizon_canvas[hrc->horizon_ping], 0)
            : hrc->black
    );
    cf_draw_set_texture(
        "horizon_diagonal",
        horizon_directions == 8
            ? cf_canvas_get_target2(hrc->horizon_canvas[hrc->horizon_ping], 1)
            : hrc->black
    );
    cf_draw_set_uniform("horizon_directions", &horizon_directions, CF_UNIFORM_TYPE_INT, 1);
    static const char *const surface_names[] = { "surface_q0",
                                                 "surface_q1",
                                                 "surface_q2",
                                                 "surface_q3" };
    for (int i = 0; i < 4; i++) {
        cf_draw_set_texture(
            surface_names[i],
            surface_sigma > 0
                ? cf_canvas_get_target2(
                      lighting.surface_mode == HRC_SURFACE_DEPTH
                          ? (hrc->depth_has_history && lighting.surface_depth_temporal
                                 ? hrc->depth_history[hrc->depth_ping]
                                 : hrc->surface_canvas[1])
                          : hrc->surface_history[hrc->surface_ping],
                      i
                  )
                : hrc->black
        );
    }
    float world_per_px = view.width / (float)hrc->canvas_w;
    cf_draw_set_uniform("world_per_px", &world_per_px, CF_UNIFORM_TYPE_FLOAT, 1);
    float surface_projection[] = { view.min_x - view.center_x,
                                   view.max_y - view.center_y,
                                   view.focal_distance,
                                   (float)block };
    float surface_spacing[] = { view.width / hrc->canvas_w, view.height / hrc->canvas_h };
    cf_draw_set_uniform("surface_projection", surface_projection, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("surface_world_per_px", surface_spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
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
    int surface_mode = lighting.surface_mode;
    cf_draw_set_uniform("surface_mode", &surface_mode, CF_UNIFORM_TYPE_INT, 1);
    cf_draw_set_uniform("surface_thickness", &lighting.surface_thickness, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("surface_depth_gain", &lighting.surface_depth_gain, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform(
        "surface_depth_shadow_softness",
        &lighting.surface_depth_shadow_softness,
        CF_UNIFORM_TYPE_FLOAT,
        1
    );
    cf_draw_set_uniform("surface_sigma", &surface_sigma, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("surface_params", surface_params, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("surface_map", surface_map, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("ambient", &ambient, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("intensity", &intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("shadow_floor", &shadow_floor, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("fog", &fog, CF_UNIFORM_TYPE_FLOAT, 1);
    CF_V3 sky = s_sky(hrc);
    cf_draw_set_uniform("sky", &sky, CF_UNIFORM_TYPE_FLOAT3, 1);
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
        "visibility_tex",
        "cover_tex",
        "surface_q0",
        "surface_q1",
        "surface_q2",
        "surface_q3"
    );
    cf_pop_gpu_label();
    render_gpu_timing_mark("Composite");
    hrc->frame_count = (hrc->frame_count + 1) & 63;
    CHIBI_TRACE_ZONE_END(zone);
    return true;
}
