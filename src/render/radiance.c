/* HRC resource/dispatch core adapted from Neon-Swarm src/lighting.c.
 * Original algorithm and shader comments retained. */
#include "render/radiance.h"

#include "diagnostics/tracy.h"
#include "render/draw_textures.h"
#include "render/gpu_timing.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HRC_WG 16
#define HRC_MIN_SCALE .1f
#define HRC_MAX_SCALE 1.f
#define HRC_MIN_TRACE_LEVELS 1
#define HRC_MAX_TRACE_LEVELS 5
#define HRC_MAX_CASCADES 16
#define HRC_INTERVAL_BYTES 8
#define HRC_LUMINANCE_INTERVAL_BYTES 4
#define HRC_FLUENCE_BYTES 4
struct render_radiance
{
    int canvas_w, canvas_h, radiance_w, radiance_h, work_w, work_h, world_w,
        world_h, N, trace_cutoff;
    float falloff, bounce_intensity, bounce_hysteresis, radiance_hysteresis;
    int upscale_mode, radiance_ping, frame_count;
    CF_Aabb screen;
    int capacity_w, capacity_h;
    float upscale_threshold;
    GameHRC2D lighting;
    bool authored_lighting;
    bool luminance_only;
    bool luminance_reuse;
    bool child_luminance_reuse;
    bool work_resources_ready;
    bool compact_intervals;
    bool compact_t0, compact_t1;
    bool allocated_compact_t0, allocated_compact_t1;
    struct render_radiance *luminance;
    float scale, reconstruction_scale;
    int trace_levels;
    bool feedback;
    double frame_average;
    int fast_windows;
    int frame_samples;
    CF_Texture scene, emission, transmittance, albedo, black;
    CF_Canvas bounce_canvas;
    CF_Texture radiance_tex[2], radiance_raw_tex, radiance_bounce_tex;
    /* RGB transport only: per-quadrant arriving luminance, see gather.shd. */
    CF_Texture radiance_dirs_tex;
    CF_Texture emission_filtered, transmittance_filtered, bounce_filtered;
    /* Quadrants run sequentially; only their final frusta survive for gather. */
    CF_StorageBuffer t_buf[HRC_MAX_CASCADES], r_ping[2];
    /* Split transport: frames per full solve, the frame's share, whether every
     * frustum holds a solve, and the view corner each was solved at. */
    int split_frames, split_phase;
    bool frusta_solved;
    CF_V2 frustum_corner[4];
    /* This frame's view: top-left corner and width in world units. */
    CF_V2 view_corner;
    float view_width;
    render_view_2d history_view;
    CF_V2 history_offset;
    bool has_history, history_valid;
    CF_StorageBuffer frustum[4], merge_weights[HRC_MAX_CASCADES];
    CF_ComputeShader trace_shd, extend_shd, merge_shd, gather_shd, blur_shd,
        blur_luminance_rgb_shd, prefilter_shd, surface_seed_shd, surface_blur_shd;
    CF_Shader bounce_shd, composite_shd;
    CF_Material compute_material;
    /* Received light around solids at block resolution: [0] holds the result,
     * [1] the intermediate blur. RGB is light times openness, A openness; the
     * direction textures hold per-quadrant luminance times openness. */
    CF_Texture surface_tex[2], surface_dir_tex[2];
    int surface_w, surface_h;
};
typedef render_radiance HRC;
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

static int s_t_storage_width(const HRC *hrc, int width, int level)
{
    int probes = s_probe_count(width, level);
    if (level == 0 && hrc->compact_t0) {
        return (probes / 2) * 2;
    }
    if (level == 1 && hrc->compact_t1) {
        return (probes / 2) * 5 + (probes % 2) * 2;
    }
    return probes * ((1 << level) + 1);
}

static int s_cascade_count(int work_w, int work_h)
{
    int span = work_w > work_h ? work_w : work_h;
    return cf_min((int)ceil(log2((double)span)), HRC_MAX_CASCADES - 1);
}

static void s_compact_interval_levels(const HRC *hrc, int N, bool *compact_t0, bool *compact_t1)
{
    int cutoff = hrc->trace_levels < N ? hrc->trace_levels : N;
    *compact_t0 = hrc->compact_intervals && N >= 2 && cutoff >= 2;
    *compact_t1 = hrc->compact_intervals && N >= 3 && cutoff >= 3;
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

static int s_rot_world_w(const HRC *hrc, int rot)
{
    return (rot & 1) ? hrc->world_h : hrc->world_w;
}

static int s_rot_world_h(const HRC *hrc, int rot)
{
    return (rot & 1) ? hrc->world_w : hrc->world_h;
}

static int s_trace_cutoff(const HRC *hrc)
{
    return hrc->trace_levels;
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
    snprintf(label, sizeof label, "%s%s", hrc->luminance_only ? "HRC luminance " : "HRC ", phase);
    cf_push_gpu_label(label);
}

static CF_StorageBuffer s_make_ssbo(int bytes)
{
    CF_StorageBufferParams p = cf_storage_buffer_defaults(bytes);
    p.compute_readable = true;
    p.compute_writable = true;
    return cf_make_storage_buffer(p);
}

static CF_Texture s_make_compute_texture(int w, int h, CF_Filter filter, CF_PixelFormat format)
{
    CF_TextureParams params = cf_texture_defaults(w, h);
    params.pixel_format = format;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT |
                   CF_TEXTURE_USAGE_COMPUTE_STORAGE_READ_BIT |
                   CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    params.filter = filter;
    params.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    return cf_make_texture(params);
}

static CF_Canvas s_make_canvas(int w, int h, bool mipmaps, CF_PixelFormat format)
{
    CF_CanvasParams params = cf_canvas_defaults(w, h);
    params.target.pixel_format = format;
    params.target.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_READ_BIT |
                           CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    if (mipmaps) {
        params.target.allocate_mipmaps = true;
        params.target.filter = CF_FILTER_LINEAR;
        params.target.mip_filter = CF_MIP_FILTER_LINEAR;
    } else {
        params.target.filter = CF_FILTER_LINEAR;
    }
    return cf_make_canvas(params);
}

static void s_destroy_work_resources(HRC *hrc)
{
    hrc->work_resources_ready = false;
    hrc->capacity_w = hrc->capacity_h = 0;
    hrc->allocated_compact_t0 = hrc->allocated_compact_t1 = false;
    if (hrc->emission_filtered.id) {
        cf_destroy_texture(hrc->emission_filtered);
    }
    if (hrc->transmittance_filtered.id) {
        cf_destroy_texture(hrc->transmittance_filtered);
    }
    if (hrc->bounce_filtered.id) {
        cf_destroy_texture(hrc->bounce_filtered);
    }
    hrc->emission_filtered = (CF_Texture){ 0 };
    hrc->transmittance_filtered = (CF_Texture){ 0 };
    hrc->bounce_filtered = (CF_Texture){ 0 };

    for (int i = 0; i < HRC_MAX_CASCADES; ++i) {
        if (hrc->t_buf[i].id) {
            cf_destroy_storage_buffer(hrc->t_buf[i]);
        }
        hrc->t_buf[i] = (CF_StorageBuffer){ 0 };
    }
    for (int i = 0; i < 2; ++i) {
        if (hrc->r_ping[i].id) {
            cf_destroy_storage_buffer(hrc->r_ping[i]);
        }
        hrc->r_ping[i] = (CF_StorageBuffer){ 0 };
    }
    for (int i = 0; i < 4; ++i) {
        if (hrc->frustum[i].id) {
            cf_destroy_storage_buffer(hrc->frustum[i]);
        }
        hrc->frustum[i] = (CF_StorageBuffer){ 0 };
    }

    for (int i = 0; i < HRC_MAX_CASCADES; ++i) {
        if (hrc->merge_weights[i].id != 0) {
            cf_destroy_storage_buffer(hrc->merge_weights[i]);
        }
        hrc->merge_weights[i] = (CF_StorageBuffer){ 0 };
    }
}

static bool s_create_work_resources(HRC *hrc, int work_w, int work_h)
{
    int N = s_cascade_count(work_w, work_h);
    bool compact_t0, compact_t1;
    s_compact_interval_levels(hrc, N, &compact_t0, &compact_t1);
    bool reuse = work_w <= hrc->capacity_w && work_h <= hrc->capacity_h &&
                 compact_t0 == hrc->allocated_compact_t0 &&
                 compact_t1 == hrc->allocated_compact_t1;
    if (!reuse) {
        s_destroy_work_resources(hrc);
        CF_PixelFormat radiance_format = hrc->luminance_only
                                           ? CF_PIXEL_FORMAT_R16_FLOAT
                                           : CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        hrc->emission_filtered = s_make_compute_texture(work_w, work_h, CF_FILTER_LINEAR, radiance_format);
        hrc->transmittance_filtered = s_make_compute_texture(work_w, work_h, CF_FILTER_LINEAR, CF_PIXEL_FORMAT_R16_FLOAT);
        hrc->bounce_filtered = s_make_compute_texture(work_w, work_h, CF_FILTER_LINEAR, radiance_format);
        if (!hrc->emission_filtered.id || !hrc->transmittance_filtered.id ||
            !hrc->bounce_filtered.id) {
            goto fail;
        }
    }

    hrc->world_w = work_w;
    hrc->world_h = work_h;
    hrc->frusta_solved = false;
    hrc->N = N;
    hrc->trace_cutoff = s_trace_cutoff(hrc);
    hrc->compact_t0 = compact_t0;
    hrc->compact_t1 = compact_t1;
    if (reuse) {
        hrc->work_w = work_w;
        hrc->work_h = work_h;
        hrc->work_resources_ready = true;
        return true;
    }

    int max_r_count = 0;
    int rot_w_options[2] = { hrc->world_w, hrc->world_h };
    int rot_h_options[2] = { hrc->world_h, hrc->world_w };

    for (int i = 0; i < hrc->N; ++i) {
        int max_t_count = 0;
        for (int o = 0; o < 2; ++o) {
            int rot_w = rot_w_options[o];
            int rot_h = rot_h_options[o];
            int t_w = s_t_storage_width(hrc, rot_w, i);
            int t_count = t_w * rot_h;
            if (t_count > max_t_count) {
                max_t_count = t_count;
            }

            int r_count = s_r_width(rot_w, i) * rot_h;
            if (r_count > max_r_count) {
                max_r_count = r_count;
            }
        }

        int interval_bytes = hrc->luminance_only ? HRC_LUMINANCE_INTERVAL_BYTES
                                                 : HRC_INTERVAL_BYTES;
        hrc->t_buf[i] = s_make_ssbo(max_t_count * interval_bytes);
        if (hrc->t_buf[i].id == 0) {
            goto fail;
        }
    }

    for (int i = 0; i < 2; ++i) {
        hrc->r_ping[i] = s_make_ssbo(max_r_count * HRC_FLUENCE_BYTES);
        if (!hrc->r_ping[i].id) {
            goto fail;
        }
    }
    int frustum_count = hrc->world_w * hrc->world_h;
    for (int i = 0; i < 4; ++i) {
        hrc->frustum[i] = s_make_ssbo(frustum_count * HRC_FLUENCE_BYTES);
        if (!hrc->frustum[i].id) {
            goto fail;
        }
    }

    for (int level = 0; level < hrc->N; ++level) {
        int directions = 1 << level;
        hrc->merge_weights[level] = s_make_ssbo(directions * 8);
        if (!hrc->merge_weights[level].id) {
            goto fail;
        }

        int float_count = directions * 2;
        float *weights = (float *)cf_calloc(float_count * (int)sizeof(float), 1);
        if (!weights) {
            goto fail;
        }

        for (int i = 0; i < directions; ++i) {
            float j_plus = 2.0f * (float)i + 1.0f;
            float j_minus = 2.0f * (float)i;
            float parent = s_angular_span(level, (float)i + 0.5f);
            weights[i * 2 + 0] = s_angular_span(level + 1, j_plus + 0.5f) / parent;
            weights[i * 2 + 1] = s_angular_span(level + 1, j_minus + 0.5f) / parent;
        }

        cf_update_storage_buffer(hrc->merge_weights[level], weights, float_count * (int)sizeof(float));
        cf_free(weights);
    }

    hrc->work_w = hrc->capacity_w = work_w;
    hrc->work_h = hrc->capacity_h = work_h;
    hrc->allocated_compact_t0 = hrc->compact_t0;
    hrc->allocated_compact_t1 = hrc->compact_t1;
    hrc->work_resources_ready = true;
    return true;

fail:
    fprintf(stderr, "[lighting] work resource creation failed for work=%dx%d.\n", work_w, work_h);
    s_destroy_work_resources(hrc);
    return false;
}

static void s_dispatch_trace(HRC *hrc, int quadrant, int level)
{
    CF_Material mat = hrc->compute_material;
    int rot_w = s_rot_world_w(hrc, quadrant);
    int rot_h = s_rot_world_h(hrc, quadrant);
    int storage_w = s_t_storage_width(hrc, rot_w, level);
    int compact_mode = level == 0 && hrc->compact_t0 ? 1
                     : level == 1 && hrc->compact_t1 ? 2
                                                     : 0;

    cf_material_set_texture_cs(mat, "u_emission", hrc->emission_filtered);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance_filtered);
    cf_material_set_texture_cs(mat, "u_bounce", hrc->bounce_filtered);

    int params[6] = { level, quadrant, rot_w, rot_h, hrc->work_w, hrc->work_h };
    cf_material_set_uniform_cs(mat, "u_cascade", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_rotate", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_world_w", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_world_h", params + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_w", params + 4, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_h", params + 5, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_storage_w", &storage_w, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_compact_mode", &compact_mode, CF_UNIFORM_TYPE_INT, 1);
    float step_scale = (float)hrc->canvas_w / (float)hrc->work_w;
    cf_material_set_uniform_cs(mat, "u_step_scale", &step_scale, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    int luminance_only = hrc->luminance_only;
    cf_material_set_uniform_cs(mat, "u_luminance_only", &luminance_only, CF_UNIFORM_TYPE_INT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(storage_w, HRC_WG),
        s_ceil_div(rot_h, HRC_WG),
        1
    );
    CF_StorageBuffer rw[1] = { hrc->t_buf[level] };
    d.rw_buffers = rw;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(hrc->trace_shd, mat, d);
}

static void s_dispatch_extend(HRC *hrc, int rot_w, int rot_h, int level)
{
    CF_Material mat = hrc->compute_material;
    int prev_w = s_t_width(rot_w, level - 1);
    int curr_w = s_t_width(rot_w, level);

    int params[5] = { level, rot_w, rot_h, prev_w, curr_w };
    cf_material_set_uniform_cs(mat, "u_cascade", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_world_w", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_world_h", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_prev_w", params + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_curr_w", params + 4, CF_UNIFORM_TYPE_INT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(curr_w, HRC_WG),
        s_ceil_div(rot_h, HRC_WG),
        1
    );
    CF_StorageBuffer ro[1] = { hrc->t_buf[level - 1] };
    d.ro_buffers = ro;
    d.ro_buffer_count = 1;
    CF_StorageBuffer rw[1] = { hrc->t_buf[level] };
    d.rw_buffers = rw;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(hrc->extend_shd, mat, d);
}

static void s_dispatch_merge(HRC *hrc, int rot_w, int rot_h, int level, CF_StorageBuffer r_prev, CF_StorageBuffer r_curr)
{
    CF_Material mat = hrc->compute_material;
    int t_curr_w = s_t_storage_width(hrc, rot_w, level);
    int t_next_w = s_t_storage_width(hrc, rot_w, level + 1);
    int r_prev_w = s_r_width(rot_w, level + 1);
    int r_curr_w = s_r_width(rot_w, level);

    int params[7] = { level,    rot_w,    rot_h,   t_curr_w,
                      t_next_w, r_prev_w, r_curr_w };
    cf_material_set_uniform_cs(mat, "u_cascade", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_world_w", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_world_h", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_t_curr_w", params + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_t_next_w", params + 4, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_r_prev_w", params + 5, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_r_curr_w", params + 6, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_cascade_count", &hrc->N, CF_UNIFORM_TYPE_INT, 1);
    int compact_t0 = hrc->compact_t0;
    int compact_t1 = hrc->compact_t1;
    cf_material_set_uniform_cs(mat, "u_compact_t0", &compact_t0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_compact_t1", &compact_t1, CF_UNIFORM_TYPE_INT, 1);
    float ww = (float)hrc->work_w, wh = (float)hrc->work_h;
    float diag = sqrtf(ww * ww + wh * wh);
    float falloff = diag > 0 ? hrc->falloff * 2.0f / diag : 0.0f;
    cf_material_set_uniform_cs(mat, "u_falloff", &falloff, CF_UNIFORM_TYPE_FLOAT, 1);

    int dispatch_h = s_ceil_div(rot_h, HRC_WG);
    CF_ComputeDispatch d = cf_compute_dispatch_defaults(s_ceil_div(r_curr_w, HRC_WG), dispatch_h, 1);
    /* T_N and R_N are implicit zero boundaries. The shader skips those loads,
     * so the terminal merge can bind existing buffers without allocating them. */
    CF_StorageBuffer ro[4] = { hrc->t_buf[level],
                               hrc->t_buf[level + 1 < hrc->N ? level + 1 : level],
                               r_prev,
                               hrc->merge_weights[level] };
    d.ro_buffers = ro;
    d.ro_buffer_count = 4;
    CF_StorageBuffer rw[1] = { r_curr };
    d.rw_buffers = rw;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(hrc->merge_shd, mat, d);
}

static void s_begin_view(HRC *hrc, render_view_2d view)
{
    bool same_domain = hrc->has_history && hrc->history_view.width == view.width &&
                       hrc->history_view.height == view.height;
    if (hrc->has_history && !same_domain) {
        hrc->frusta_solved = false;
    }
    hrc->history_valid = same_domain;
    hrc->history_offset = hrc->history_valid
                            ? (CF_V2){ (view.min_x - hrc->history_view.min_x) / view.width,
                                       (hrc->history_view.max_y - view.max_y) / view.height }
                            : (CF_V2){ 0 };
    hrc->history_view = view;
    hrc->has_history = true;
}

static void s_history_uniforms(HRC *hrc)
{
    int valid = hrc->history_valid;
    cf_material_set_uniform_cs(
        hrc->compute_material,
        "u_history_offset",
        &hrc->history_offset,
        CF_UNIFORM_TYPE_FLOAT2,
        1
    );
    cf_material_set_uniform_cs(hrc->compute_material, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
}

static void s_domain_uniforms(HRC *hrc)
{
    render_view_2d view = hrc->history_view;
    float crop[] = { view.crop_min_u, view.crop_min_v, view.crop_max_u, view.crop_max_v };
    cf_material_set_uniform_cs(hrc->compute_material, "u_crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
}

static void s_dispatch_prefilter(HRC *hrc)
{
    CF_Material mat = hrc->compute_material;

    cf_material_set_texture_cs(mat, "u_emission", hrc->emission);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_bounce", cf_canvas_get_target(hrc->bounce_canvas));
    s_history_uniforms(hrc);
    s_domain_uniforms(hrc);

    int params[4] = { hrc->canvas_w, hrc->canvas_h, hrc->work_w, hrc->work_h };
    cf_material_set_uniform_cs(mat, "u_canvas_w", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_canvas_h", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_w", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_h", params + 3, CF_UNIFORM_TYPE_INT, 1);
    int luminance_only = hrc->luminance_only;
    cf_material_set_uniform_cs(mat, "u_luminance_only", &luminance_only, CF_UNIFORM_TYPE_INT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(hrc->work_w, HRC_WG),
        s_ceil_div(hrc->work_h, HRC_WG),
        1
    );
    CF_Texture rw_tex[3] = { hrc->emission_filtered,
                             hrc->transmittance_filtered,
                             hrc->bounce_filtered };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 3;
    cf_dispatch_compute(hrc->prefilter_shd, mat, d);
}

static void s_dispatch_gather(HRC *hrc)
{
    CF_Material mat = hrc->compute_material;
    s_history_uniforms(hrc);
    s_domain_uniforms(hrc);

    cf_material_set_texture_cs(mat, "u_emission", hrc->emission);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_transmittance_filtered", hrc->transmittance_filtered);
    cf_material_set_texture_cs(mat, "u_bounce", cf_canvas_get_target(hrc->bounce_canvas));

    /* Frusta solved on earlier frames are read where the view has since
     * carried their content, to the nearest work pixel. */
    int shift_x[4] = { 0 }, shift_y[4] = { 0 };
    if (hrc->view_width > 0) {
        float per_px = hrc->view_width / (float)hrc->work_w;
        for (int q = 0; q < 4; q++) {
            shift_x[q] = (int)lroundf(
                (hrc->view_corner.x - hrc->frustum_corner[q].x) / per_px
            );
            shift_y[q] = (int)lroundf(
                (hrc->frustum_corner[q].y - hrc->view_corner.y) / per_px
            );
        }
    }
    cf_material_set_uniform_cs(mat, "u_shift_x", shift_x, CF_UNIFORM_TYPE_INT4, 1);
    cf_material_set_uniform_cs(mat, "u_shift_y", shift_y, CF_UNIFORM_TYPE_INT4, 1);
    int params[5] = { hrc->canvas_w, hrc->canvas_h, hrc->work_w, hrc->work_h, hrc->upscale_mode };
    cf_material_set_uniform_cs(mat, "u_canvas_w", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_canvas_h", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_w", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_h", params + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_upscale_mode", params + 4, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_upscale_threshold", &hrc->upscale_threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    int luminance_only = hrc->luminance_only;
    cf_material_set_uniform_cs(mat, "u_luminance_only", &luminance_only, CF_UNIFORM_TYPE_INT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(hrc->radiance_w, HRC_WG),
        s_ceil_div(hrc->radiance_h, HRC_WG),
        1
    );
    CF_StorageBuffer ro[4] = { hrc->frustum[0],
                               hrc->frustum[1],
                               hrc->frustum[2],
                               hrc->frustum[3] };
    d.ro_buffers = ro;
    d.ro_buffer_count = 4;
    CF_Texture rw_tex[3] = { hrc->radiance_raw_tex,
                             hrc->radiance_bounce_tex,
                             hrc->radiance_dirs_tex };
    d.rw_textures = rw_tex;
    d.rw_texture_count = hrc->luminance_only ? 2 : 3;
    cf_dispatch_compute(hrc->gather_shd, mat, d);
}

static void s_dispatch_blur(HRC *hrc)
{
    CF_Material mat = hrc->compute_material;
    s_history_uniforms(hrc);
    int prev = hrc->radiance_ping;
    int curr = 1 - prev;

    cf_material_set_texture_cs(mat, "u_in_radiance", hrc->radiance_raw_tex);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_prev_radiance", hrc->radiance_tex[prev]);

    float texel_size[2] = { 1.0f / hrc->radiance_w, 1.0f / hrc->radiance_h };
    float threshold = 0.1f;
    float hysteresis = hrc->radiance_hysteresis;
    if (hysteresis < 0.01f) {
        hysteresis = 0.01f;
    }
    if (hysteresis > 1.0f) {
        hysteresis = 1.0f;
    }
    cf_material_set_uniform_cs(mat, "u_texel_size", texel_size, CF_UNIFORM_TYPE_FLOAT, 2);
    cf_material_set_uniform_cs(mat, "u_threshold", &threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(mat, "u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(hrc->radiance_w, HRC_WG),
        s_ceil_div(hrc->radiance_h, HRC_WG),
        1
    );
    CF_Texture rw_tex[1] = { hrc->radiance_tex[curr] };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 1;
    cf_dispatch_compute(hrc->blur_shd, mat, d);

    hrc->radiance_ping = curr;
}

static void s_dispatch_luminance_blur_from_rgb(HRC *hrc, CF_Texture radiance_source)
{
    CF_Material mat = hrc->compute_material;
    s_history_uniforms(hrc);

    int prev = hrc->radiance_ping;
    int curr = 1 - prev;

    cf_material_set_texture_cs(mat, "u_in_radiance", radiance_source);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_prev_radiance", hrc->radiance_tex[prev]);

    float texel_size[2] = { 1.0f / hrc->radiance_w, 1.0f / hrc->radiance_h };
    float threshold = 0.1f;
    float hysteresis = hrc->radiance_hysteresis;
    if (hysteresis < 0.01f) {
        hysteresis = 0.01f;
    }
    if (hysteresis > 1.0f) {
        hysteresis = 1.0f;
    }
    cf_material_set_uniform_cs(mat, "u_texel_size", texel_size, CF_UNIFORM_TYPE_FLOAT, 2);
    cf_material_set_uniform_cs(mat, "u_threshold", &threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(mat, "u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(hrc->radiance_w, HRC_WG),
        s_ceil_div(hrc->radiance_h, HRC_WG),
        1
    );
    CF_Texture rw_tex[1] = { hrc->radiance_tex[curr] };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 1;
    cf_dispatch_compute(hrc->blur_luminance_rgb_shd, mat, d);

    hrc->radiance_ping = curr;
}

static void s_destroy_surface(HRC *hrc)
{
    for (int i = 0; i < 2; i++) {
        CF_Texture *textures[] = { &hrc->surface_tex[i], &hrc->surface_dir_tex[i] };
        for (int j = 0; j < 2; j++) {
            if (textures[j]->id) {
                cf_destroy_texture(*textures[j]);
            }
            *textures[j] = (CF_Texture){ 0 };
        }
    }
    hrc->surface_w = hrc->surface_h = 0;
}

static bool s_surface_resources(HRC *hrc, int width, int height)
{
    if (hrc->surface_w == width && hrc->surface_h == height) {
        return true;
    }
    s_destroy_surface(hrc);
    for (int i = 0; i < 2; i++) {
        CF_Texture *textures[] = { &hrc->surface_tex[i], &hrc->surface_dir_tex[i] };
        for (int j = 0; j < 2; j++) {
            *textures[j] = s_make_compute_texture(width, height, CF_FILTER_LINEAR, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
            if (!textures[j]->id) {
                s_destroy_surface(hrc);
                return false;
            }
        }
    }
    hrc->surface_w = width;
    hrc->surface_h = height;
    return true;
}

static void s_dispatch_surface_blur(HRC *hrc, CF_Texture in, CF_Texture out, bool vertical, float sigma)
{
    CF_Material mat = hrc->compute_material;
    cf_material_set_texture_cs(mat, "u_in", in);
    int params[3] = { hrc->surface_w, hrc->surface_h, vertical };
    cf_material_set_uniform_cs(mat, "u_width", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_height", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_vertical", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_sigma", &sigma, CF_UNIFORM_TYPE_FLOAT, 1);
    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(hrc->surface_w, HRC_WG),
        s_ceil_div(hrc->surface_h, HRC_WG),
        1
    );
    CF_Texture rw_tex[1] = { out };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 1;
    cf_dispatch_compute(hrc->surface_blur_shd, mat, d);
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
    if (!s_surface_resources(hrc, width, height)) {
        return 0;
    }
    CF_Material mat = hrc->compute_material;
    cf_material_set_texture_cs(mat, "u_fluence", hrc->radiance_tex[hrc->radiance_ping]);
    cf_material_set_texture_cs(mat, "u_scene", hrc->scene);
    cf_material_set_texture_cs(mat, "u_dirs", hrc->radiance_dirs_tex);
    int params[5] = { hrc->canvas_w, hrc->canvas_h, width, height, *block };
    cf_material_set_uniform_cs(mat, "u_canvas_w", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_canvas_h", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_out_w", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_out_h", params + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_block", params + 4, CF_UNIFORM_TYPE_INT, 1);
    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(width, HRC_WG),
        s_ceil_div(height, HRC_WG),
        1
    );
    CF_Texture rw_tex[2] = { hrc->surface_tex[0], hrc->surface_dir_tex[0] };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 2;
    cf_dispatch_compute(hrc->surface_seed_shd, mat, d);
    float level_sigma = sigma / (float)*block;
    CF_Texture *layers[] = { hrc->surface_tex, hrc->surface_dir_tex };
    for (int i = 0; i < 2; i++) {
        s_dispatch_surface_blur(hrc, layers[i][0], layers[i][1], false, level_sigma);
        s_dispatch_surface_blur(hrc, layers[i][1], layers[i][0], true, level_sigma);
    }
    return level_sigma;
}

static void s_solve_trace(HRC *hrc, int j, int N)
{
    int cutoff = hrc->trace_cutoff < N ? hrc->trace_cutoff : N;
    for (int i = 0; i < cutoff; ++i) {
        s_push_gpu_level_label("Trace", i);
        s_dispatch_trace(hrc, j, i);
        cf_pop_gpu_label();
    }
}

static void s_solve_extend(HRC *hrc, int j, int N)
{
    int rot_w = s_rot_world_w(hrc, j);
    int rot_h = s_rot_world_h(hrc, j);
    int cutoff = hrc->trace_cutoff < N ? hrc->trace_cutoff : N;
    for (int i = cutoff; i < N; ++i) {
        s_push_gpu_level_label("Extend", i);
        s_dispatch_extend(hrc, rot_w, rot_h, i);
        cf_pop_gpu_label();
    }
}

static void s_solve_merge(HRC *hrc, int j, int N)
{
    int rot_w = s_rot_world_w(hrc, j);
    int rot_h = s_rot_world_h(hrc, j);
    for (int i = N - 1; i >= 0; --i) {
        int ping = (N - 1 - i) & 1;
        CF_StorageBuffer r_prev = hrc->r_ping[1 - ping];
        CF_StorageBuffer r_out = (i == 0) ? hrc->frustum[j] : hrc->r_ping[ping];
        s_push_gpu_level_label("Merge", i);
        s_dispatch_merge(hrc, rot_w, rot_h, i, r_prev, r_out);
        cf_pop_gpu_label();
    }
}

static void s_render_bounce(HRC *hrc, CF_Texture radiance_source)
{
    if (!hrc->feedback) {
        return;
    }
    float hysteresis = hrc->bounce_hysteresis;
    /* Transport samples the old bounce in world coordinates. Fixed-function
     * blending cannot move its destination, so replace it when the view moves. */
    if (!hrc->history_valid || hrc->history_offset.x || hrc->history_offset.y) {
        hysteresis = 1;
    }
    if (hysteresis < 0.01f) {
        hysteresis = 0.01f;
    }
    if (hysteresis > 1.0f) {
        hysteresis = 1.0f;
    }

    CF_RenderState rs = cf_render_state_defaults();
    rs.blend.enabled = true;
    rs.blend.pixel_format = hrc->luminance_only ? CF_PIXEL_FORMAT_R16_FLOAT
                                                : CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    rs.blend.rgb_src_blend_factor = CF_BLENDFACTOR_SRC_ALPHA;
    rs.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    rs.blend.rgb_op = CF_BLEND_OP_ADD;
    rs.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    rs.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ZERO;
    rs.blend.alpha_op = CF_BLEND_OP_ADD;

    cf_draw_push_render_state(rs);
    cf_draw_push_alpha_discard(false);
    cf_draw_push_shader(hrc->bounce_shd);
    cf_draw_set_texture("u_radiance", radiance_source);
    cf_draw_set_texture("u_albedo", hrc->albedo);
    cf_draw_set_uniform("u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);
    int luminance_only = hrc->luminance_only;
    cf_draw_set_uniform("u_luminance_only", &luminance_only, CF_UNIFORM_TYPE_INT, 1);
    cf_draw_quad_fill(hrc->screen, 0);
    cf_draw_pop_shader();
    cf_draw_pop_alpha_discard();
    cf_draw_pop_render_state();
    cf_render_to(hrc->bounce_canvas, false);
    RENDER_DRAW_UNBIND_TEXTURES("u_radiance", "u_albedo");
    cf_generate_mipmaps(cf_canvas_get_target(hrc->bounce_canvas));
}

static void destroy_images(HRC *hrc)
{
    if (hrc->bounce_canvas.id) {
        cf_destroy_canvas(hrc->bounce_canvas);
    }
    hrc->bounce_canvas = (CF_Canvas){ 0 };
    CF_Texture *textures[] = { &hrc->radiance_tex[0],
                               &hrc->radiance_tex[1],
                               &hrc->radiance_raw_tex,
                               &hrc->radiance_bounce_tex,
                               &hrc->radiance_dirs_tex };
    for (int i = 0; i < 5; i++) {
        if (textures[i]->id) {
            cf_destroy_texture(*textures[i]);
        }
        *textures[i] = (CF_Texture){ 0 };
    }
    hrc->canvas_w = hrc->canvas_h = 0;
    hrc->radiance_w = hrc->radiance_h = 0;
}

static render_radiance *s_new(bool luminance_only)
{
    if (cf_query_backend() == CF_BACKEND_TYPE_GLES3) {
        return NULL;
    }
    HRC *hrc = xmalloc(sizeof *hrc);
    memset(hrc, 0, sizeof *hrc);
    hrc->luminance_only = luminance_only;
    const char *compact_intervals = getenv("CHIBI_HRC_COMPACT_INTERVALS");
    hrc->compact_intervals = compact_intervals && !strcmp(compact_intervals, "1");
    hrc->compute_material = cf_make_material();
    hrc->trace_shd = cf_make_compute_shader(
        luminance_only ? "hrc/trace_luminance.shd" : "hrc/trace.shd"
    );
    hrc->extend_shd = cf_make_compute_shader(
        luminance_only ? "hrc/extend_luminance.shd" : "hrc/extend.shd"
    );
    hrc->merge_shd = cf_make_compute_shader(
        luminance_only ? "hrc/merge_luminance.shd" : "hrc/merge.shd"
    );
    hrc->gather_shd = cf_make_compute_shader(
        luminance_only ? "hrc/gather_luminance.shd" : "hrc/gather.shd"
    );
    hrc->blur_shd = cf_make_compute_shader(
        luminance_only ? "hrc/blur_luminance.shd" : "hrc/blur.shd"
    );
    hrc->blur_luminance_rgb_shd = luminance_only
                                    ? cf_make_compute_shader("hrc/blur_luminance_from_rgb.shd")
                                    : (CF_ComputeShader){ 0 };
    hrc->prefilter_shd = cf_make_compute_shader(
        luminance_only ? "hrc/prefilter_luminance.shd" : "hrc/prefilter.shd"
    );
    if (!luminance_only) {
        hrc->surface_seed_shd = cf_make_compute_shader("hrc/surface_seed.shd");
        hrc->surface_blur_shd = cf_make_compute_shader("hrc/surface_blur.shd");
    }
    hrc->bounce_shd = cf_make_draw_shader("hrc/bounce.shd");
    hrc->composite_shd = cf_make_draw_shader("hrc/composite.shd");
    hrc->black = cf_make_texture(cf_texture_defaults(1, 1));
    unsigned black = 0;
    if (hrc->black.id) {
        cf_texture_update(hrc->black, &black, sizeof black);
    }
    hrc->scale = .5f;
    hrc->reconstruction_scale = 1;
    hrc->trace_levels = 3;
    hrc->feedback = true;
    hrc->falloff = hrc->bounce_intensity = 1;
    hrc->bounce_hysteresis = hrc->radiance_hysteresis = 0.8f;
    hrc->upscale_mode = 1;
    hrc->upscale_threshold = 0.1f;
    if (!hrc->compute_material.id || !hrc->trace_shd.id || !hrc->extend_shd.id ||
        !hrc->merge_shd.id || !hrc->gather_shd.id || !hrc->blur_shd.id ||
        (luminance_only && !hrc->blur_luminance_rgb_shd.id) ||
        !hrc->prefilter_shd.id || !hrc->bounce_shd.id ||
        (!luminance_only && (!hrc->surface_seed_shd.id || !hrc->surface_blur_shd.id)) ||
        !hrc->composite_shd.id || !hrc->black.id) {
        render_radiance_free(hrc);
        return NULL;
    }
    return hrc;
}

render_radiance *render_radiance_new()
{
    return s_new(false);
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
    CF_ComputeShader shaders[] = {
        hrc->trace_shd,     hrc->extend_shd,       hrc->merge_shd,
        hrc->gather_shd,    hrc->blur_shd,         hrc->blur_luminance_rgb_shd,
        hrc->prefilter_shd, hrc->surface_seed_shd, hrc->surface_blur_shd
    };
    for (int i = 0; i < (int)(sizeof shaders / sizeof *shaders); i++) {
        if (shaders[i].id) {
            cf_destroy_compute_shader(shaders[i]);
        }
    }
    if (hrc->bounce_shd.id) {
        cf_destroy_shader(hrc->bounce_shd);
    }
    if (hrc->composite_shd.id) {
        cf_destroy_shader(hrc->composite_shd);
    }
    if (hrc->black.id) {
        cf_destroy_texture(hrc->black);
    }
    if (hrc->compute_material.id) {
        cf_destroy_material(hrc->compute_material);
    }
    free(hrc);
}

void render_radiance_reset(render_radiance *hrc)
{
    assert(hrc);
    if (hrc->luminance) {
        render_radiance_reset(hrc->luminance);
    }
    hrc->frame_count = 0;
    hrc->radiance_ping = 0;
    hrc->has_history = hrc->history_valid = false;
    hrc->frusta_solved = false;
    if (!hrc->canvas_w) {
        return;
    }
    int bytes = hrc->radiance_w * hrc->radiance_h * (hrc->luminance_only ? 2 : 8);
    void *zero = xmalloc((size_t)bytes);
    memset(zero, 0, (size_t)bytes);
    for (int i = 0; i < 2; i++) {
        cf_texture_update(hrc->radiance_tex[i], zero, bytes);
    }
    free(zero);
    cf_canvas_set_clear_color(hrc->bounce_canvas, cf_make_color_rgba_f(0, 0, 0, 0));
    cf_render_to(hrc->bounce_canvas, true);
    cf_generate_mipmaps(cf_canvas_get_target(hrc->bounce_canvas));
}

bool render_radiance_resize(render_radiance *hrc, int width, int height)
{
    assert(hrc);
    /* Bound shifts and SSBO byte counts, including a highly elongated viewport. */
    if (width < 2 || height < 2 || width > 4096 || height > 4096) {
        return false;
    }
    int radiance_w = (int)fmaxf(2, ceilf(width * hrc->reconstruction_scale));
    int radiance_h = (int)fmaxf(2, ceilf(height * hrc->reconstruction_scale));
    int work_w = (int)fmaxf(2, ceilf(width * hrc->scale));
    int work_h = (int)fmaxf(2, ceilf(height * hrc->scale));
    bool compact_t0, compact_t1;
    s_compact_interval_levels(hrc, s_cascade_count(work_w, work_h), &compact_t0, &compact_t1);
    bool interval_layout_ready = hrc->luminance_reuse ||
                                 (compact_t0 == hrc->allocated_compact_t0 &&
                                  compact_t1 == hrc->allocated_compact_t1);
    bool luminance_ready = !hrc->luminance ||
                           (hrc->luminance->canvas_w == width &&
                            hrc->luminance->canvas_h == height &&
                            hrc->luminance->radiance_w == radiance_w &&
                            hrc->luminance->radiance_h == radiance_h &&
                            hrc->luminance->work_w ==
                                (int)ceilf(width * hrc->luminance->scale) &&
                            hrc->luminance->work_h ==
                                (int)ceilf(height * hrc->luminance->scale));
    if (hrc->canvas_w == width && hrc->canvas_h == height &&
        hrc->radiance_w == radiance_w && hrc->radiance_h == radiance_h &&
        hrc->work_w == (int)ceilf(width * hrc->scale) &&
        hrc->work_h == (int)ceilf(height * hrc->scale) && luminance_ready &&
        hrc->work_resources_ready == !hrc->luminance_reuse && interval_layout_ready) {
        return true;
    }
    bool images_changed = hrc->canvas_w != width || hrc->canvas_h != height ||
                          hrc->radiance_w != radiance_w ||
                          hrc->radiance_h != radiance_h;
    bool ok = true;
    if (images_changed) {
        destroy_images(hrc);
        CF_PixelFormat radiance_format = hrc->luminance_only
                                           ? CF_PIXEL_FORMAT_R16_FLOAT
                                           : CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
        hrc->bounce_canvas = s_make_canvas(radiance_w, radiance_h, true, radiance_format);
        CF_Texture *textures[] = { &hrc->radiance_tex[0],
                                   &hrc->radiance_tex[1],
                                   &hrc->radiance_raw_tex,
                                   &hrc->radiance_bounce_tex };
        for (int i = 0; i < 4; i++) {
            *textures[i] = s_make_compute_texture(
                radiance_w,
                radiance_h,
                i < 2 ? CF_FILTER_LINEAR : CF_FILTER_NEAREST,
                radiance_format
            );
        }
        ok = hrc->bounce_canvas.id != 0;
        for (int i = 0; i < 4; i++) {
            ok = ok && textures[i]->id != 0;
        }
        if (!hrc->luminance_only) {
            hrc->radiance_dirs_tex = s_make_compute_texture(
                radiance_w,
                radiance_h,
                CF_FILTER_LINEAR,
                CF_PIXEL_FORMAT_R16G16B16A16_FLOAT
            );
            ok = ok && hrc->radiance_dirs_tex.id != 0;
        }
    }
    if (hrc->luminance_reuse) {
        s_destroy_work_resources(hrc);
        hrc->work_w = hrc->world_w = work_w;
        hrc->work_h = hrc->world_h = work_h;
        hrc->N = 0;
        hrc->trace_cutoff = 0;
    } else if (!ok || !s_create_work_resources(hrc, work_w, work_h)) {
        s_destroy_work_resources(hrc);
        destroy_images(hrc);
        return false;
    }
    hrc->canvas_w = width;
    hrc->canvas_h = height;
    hrc->radiance_w = radiance_w;
    hrc->radiance_h = radiance_h;
    if (hrc->luminance && !render_radiance_resize(hrc->luminance, width, height)) {
        s_destroy_work_resources(hrc);
        destroy_images(hrc);
        return false;
    }
    if (images_changed) {
        render_radiance_reset(hrc);
    }
    return true;
}

render_radiance_settings render_radiance_get_settings(const render_radiance *radiance)
{
    if (!radiance) {
        return (render_radiance_settings){ 0 };
    }
    return (render_radiance_settings){
        .scale = radiance->scale,
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
    bool scale_changed = radiance->scale != settings.scale;
    bool trace_changed = radiance->trace_levels != settings.trace_levels;
    radiance->scale = settings.scale;
    radiance->trace_levels = settings.trace_levels;
    radiance->trace_cutoff = settings.trace_levels;
    bool resized = true;
    if ((scale_changed || (radiance->compact_intervals && trace_changed)) &&
        radiance->canvas_w && radiance->canvas_h) {
        resized = render_radiance_resize(radiance, radiance->canvas_w, radiance->canvas_h);
    }
    if (radiance->feedback != settings.feedback) {
        radiance->feedback = settings.feedback;
        render_radiance_reset(radiance);
    }
    return resized;
}

bool render_radiance_set_lighting(render_radiance *radiance, GameHRC2D lighting)
{
    assert(radiance);
    const float values[] = { lighting.scale,
                             lighting.reconstruction_scale,
                             lighting.lighting_margin,
                             lighting.luminance_scale,
                             lighting.min_scale,
                             lighting.max_scale,
                             lighting.target_fps,
                             lighting.fps_tolerance,
                             lighting.ambient,
                             lighting.intensity,
                             lighting.fog,
                             lighting.shadow_floor,
                             lighting.falloff,
                             lighting.bounce_intensity,
                             lighting.bounce_hysteresis,
                             lighting.radiance_hysteresis,
                             lighting.upscale_threshold,
                             lighting.particle_focal_length,
                             lighting.surface_radius,
                             lighting.surface_relief,
                             lighting.surface_bevel,
                             lighting.specular,
                             lighting.bloom };
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        if (!isfinite(values[i]) || values[i] < 0) {
            return false;
        }
    }
    if (lighting.reconstruction_scale < .5f ||
        lighting.reconstruction_scale > 1 || lighting.lighting_margin > 256 ||
        lighting.luminance_mode < 0 || lighting.luminance_mode > 1) {
        return false;
    }
    if (lighting.min_scale < .1f || lighting.max_scale > 1 ||
        lighting.scale < lighting.min_scale ||
        lighting.scale > lighting.max_scale || lighting.target_fps <= 0 ||
        (lighting.luminance_scale != 0 &&
         (lighting.luminance_scale < lighting.min_scale ||
          lighting.luminance_scale > lighting.max_scale)) ||
        lighting.fps_tolerance > 1 || lighting.bounce_hysteresis > 1 ||
        lighting.radiance_hysteresis > 1 ||
        lighting.particle_focal_length <= 0 || lighting.upscale_mode < 0 ||
        lighting.upscale_mode > 3 || lighting.debug_view < 0 ||
        lighting.debug_view > 9 || lighting.tone_mapping < 0 ||
        lighting.tone_mapping > 1 || !isfinite(lighting.exposure) ||
        fabsf(lighting.exposure) > 16 || lighting.bloom > 1 ||
        (lighting.split_frames != 1 && lighting.split_frames != 2 &&
         lighting.split_frames != 4)) {
        return false;
    }
    bool luminance_reuse = radiance->luminance_only && lighting.luminance_mode == 1;
    bool reuse_changed = radiance->luminance_reuse != luminance_reuse;
    bool reconstruction_changed = radiance->reconstruction_scale !=
                                  lighting.reconstruction_scale;
    radiance->reconstruction_scale = lighting.reconstruction_scale;
    render_radiance_settings settings = {
        .scale = radiance->authored_lighting && lighting.adaptive &&
                         lighting.scale == radiance->lighting.scale
                   ? fminf(
                         lighting.max_scale,
                         fmaxf(lighting.min_scale, radiance->scale)
                     )
                   : lighting.scale,
        .trace_levels = lighting.trace_levels,
        .feedback = lighting.feedback
    };
    if (!render_radiance_set_settings(radiance, settings)) {
        return false;
    }
    if (reconstruction_changed && radiance->canvas_w &&
        !render_radiance_resize(radiance, radiance->canvas_w, radiance->canvas_h)) {
        return false;
    }
    radiance->falloff = lighting.falloff;
    radiance->luminance_reuse = luminance_reuse;
    radiance->child_luminance_reuse = !radiance->luminance_only &&
                                      lighting.luminance_mode == 1;
    radiance->bounce_intensity = lighting.feedback ? lighting.bounce_intensity : 0;
    radiance->bounce_hysteresis = lighting.bounce_hysteresis;
    radiance->radiance_hysteresis = lighting.radiance_hysteresis;
    radiance->upscale_mode = lighting.upscale_mode;
    radiance->upscale_threshold = lighting.upscale_threshold;
    radiance->trace_cutoff = lighting.trace_levels;
    radiance->split_frames = lighting.split_frames;
    radiance->lighting = lighting;
    radiance->authored_lighting = true;
    if (lighting.luminance_scale > 0) {
        if (!radiance->luminance) {
            radiance->luminance = s_new(true);
            if (!radiance->luminance) {
                return false;
            }
        }
        GameHRC2D luminance = lighting;
        luminance.scale = lighting.luminance_scale;
        luminance.luminance_scale = 0;
        luminance.adaptive = false;
        if (!render_radiance_set_lighting(radiance->luminance, luminance)) {
            return false;
        }
        if (radiance->canvas_w && !render_radiance_resize(
                                      radiance->luminance,
                                      radiance->canvas_w,
                                      radiance->canvas_h
                                  )) {
            return false;
        }
    }
    if (reuse_changed && radiance->canvas_w &&
        !render_radiance_resize(radiance, radiance->canvas_w, radiance->canvas_h)) {
        return false;
    }
    return true;
}

static bool s_dispatch_luminance_from_parent(
    HRC *luminance,
    HRC *parent,
    render_radiance_inputs inputs,
    render_view_2d view
)
{
    if (!luminance->canvas_w || !parent->radiance_tex[parent->radiance_ping].id) {
        return false;
    }
    s_begin_view(luminance, view);
    luminance->screen = (CF_Aabb){
        .min = { view.min_x + view.width * view.crop_min_u,
                 view.max_y - view.height * view.crop_max_v },
        .max = { view.min_x + view.width * view.crop_max_u,
                 view.max_y - view.height * view.crop_min_v }
    };
    luminance->transmittance = inputs.absorption;
    s_push_hrc_label(luminance, "derived filter");
    s_dispatch_luminance_blur_from_rgb(luminance, parent->radiance_tex[parent->radiance_ping]);
    cf_pop_gpu_label();
    return true;
}

void render_radiance_frame_time(render_radiance *hrc, double seconds)
{
    if (!hrc || !hrc->authored_lighting || !hrc->lighting.adaptive ||
        !isfinite(seconds) || seconds <= 0 || seconds > .25) {
        return;
    }
    hrc->frame_average = hrc->frame_samples ? hrc->frame_average * .9 + seconds * .1
                                            : seconds;
    if (++hrc->frame_samples < 30) {
        return;
    }
    hrc->frame_samples = 0;
    double budget = 1.0 / hrc->lighting.target_fps;
    float scale = hrc->scale;
    if (hrc->frame_average > budget * (1 + hrc->lighting.fps_tolerance)) {
        scale *= fmaxf(.65f, sqrtf((float)(budget * .8 / hrc->frame_average)));
        hrc->fast_windows = 0;
    } else if (hrc->frame_average < budget * .65) {
        if (++hrc->fast_windows >= 10) {
            scale *= 1.05f;
            hrc->fast_windows = 0;
        }
    } else {
        hrc->fast_windows = 0;
    }
    scale = fminf(
        fminf(hrc->lighting.max_scale, hrc->lighting.scale),
        fmaxf(hrc->lighting.min_scale, scale)
    );
    if (fabsf(scale - hrc->scale) < .01f) {
        return;
    }
    render_radiance_settings settings = render_radiance_get_settings(hrc);
    settings.scale = scale;
    bool resized = render_radiance_set_settings(hrc, settings);
    assert(resized);
}

static bool s_dispatch_transport(HRC *hrc, render_radiance_inputs inputs, render_view_2d view, CF_Texture bounce_source)
{
    if (!hrc->canvas_w) {
        return false;
    }
    s_begin_view(hrc, view);
    hrc->screen = (CF_Aabb){ .min = { view.min_x + view.width * view.crop_min_u,
                                      view.max_y - view.height * view.crop_max_v },
                             .max = { view.min_x + view.width * view.crop_max_u,
                                      view.max_y - view.height * view.crop_min_v } };
    hrc->scene = inputs.scene;
    hrc->emission = inputs.emission;
    hrc->transmittance = inputs.absorption;
    hrc->albedo = inputs.albedo;
    s_push_hrc_label(hrc, "prefilter");
    s_dispatch_prefilter(hrc);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC prefilter");
    int split = hrc->frusta_solved ? hrc->split_frames : 1;
    CF_V2 corner = { view.min_x, view.max_y };
    hrc->view_corner = corner;
    hrc->view_width = view.width;
    for (int q = 0; q < 4; q++) {
        if (q % split != hrc->split_phase % split) {
            continue;
        }
        hrc->frustum_corner[q] = corner;
        const char *quadrants[] = { "+X", "+Y", "-X", "-Y" };
        s_push_hrc_label(hrc, quadrants[q]);
        cf_push_gpu_label("Trace");
        s_solve_trace(hrc, q, hrc->N);
        cf_pop_gpu_label();
        render_gpu_timing_mark("HRC trace");
        cf_push_gpu_label("Extend");
        s_solve_extend(hrc, q, hrc->N);
        cf_pop_gpu_label();
        render_gpu_timing_mark("HRC extend");
        cf_push_gpu_label("Merge");
        s_solve_merge(hrc, q, hrc->N);
        cf_pop_gpu_label();
        render_gpu_timing_mark("HRC merge");
        cf_pop_gpu_label();
    }
    hrc->frusta_solved = true;
    hrc->split_phase = (hrc->split_phase + 1) & 3;
    s_push_hrc_label(hrc, "gather");
    s_dispatch_gather(hrc);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC gather");
    s_push_hrc_label(hrc, "filter");
    s_dispatch_blur(hrc);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC filter");
    s_push_hrc_label(hrc, "bounce");
    s_render_bounce(hrc, bounce_source);
    cf_pop_gpu_label();
    render_gpu_timing_mark("HRC bounce");
    return true;
}

bool render_radiance_draw(HRC *hrc, render_radiance_inputs inputs, render_view_2d view, CF_Canvas output)
{
    assert(hrc);
    if (!hrc->canvas_w || !output.id || !inputs.scene.id ||
        !inputs.emission.id || !inputs.absorption.id || !inputs.albedo.id) {
        return false;
    }
    CHIBI_TRACE_ZONE_BEGIN(zone, "HRC submission");
    cf_generate_mipmaps(inputs.emission);
    cf_generate_mipmaps(inputs.absorption);
    render_gpu_timing_mark("Emission mipmaps");
    if (!s_dispatch_transport(hrc, inputs, view, hrc->radiance_bounce_tex)) {
        CHIBI_TRACE_ZONE_END(zone);
        return false;
    }
    bool split_lighting = hrc->luminance && hrc->authored_lighting &&
                          hrc->lighting.luminance_scale > 0;
    if (split_lighting && hrc->child_luminance_reuse &&
        !s_dispatch_luminance_from_parent(hrc->luminance, hrc, inputs, view)) {
        CHIBI_TRACE_ZONE_END(zone);
        return false;
    }
    if (split_lighting && !hrc->child_luminance_reuse &&
        !s_dispatch_transport(hrc->luminance, inputs, view, hrc->radiance_bounce_tex)) {
        CHIBI_TRACE_ZONE_END(zone);
        return false;
    }
    GameHRC2D lighting = hrc->authored_lighting ? hrc->lighting
                                                : game_hrc2d_defaults();
    int block = 1;
    s_push_hrc_label(hrc, "surface");
    float surface_sigma = s_dispatch_surface(hrc, view, &block);
    cf_pop_gpu_label();
    render_gpu_timing_mark("Surface light");
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
    cf_draw_set_texture("emission_tex", hrc->emission);
    cf_draw_set_texture("absorption_tex", hrc->transmittance);
    cf_draw_set_texture("albedo_tex", hrc->albedo);
    cf_draw_set_texture("fluence_tex", hrc->radiance_tex[hrc->radiance_ping]);
    cf_draw_set_texture(
        "luminance_tex",
        split_lighting ? hrc->luminance->radiance_tex[hrc->luminance->radiance_ping]
                       : hrc->black
    );
    cf_draw_set_texture("normal_tex", inputs.normal);
    cf_draw_set_texture("background_tex", inputs.background.id ? inputs.background : hrc->black);
    cf_draw_set_texture("visibility_tex", inputs.visibility.id ? inputs.visibility : hrc->black);
    cf_draw_set_texture("surface_tex", surface_sigma > 0 ? hrc->surface_tex[0] : hrc->black);
    cf_draw_set_texture("surface_dir_tex", surface_sigma > 0 ? hrc->surface_dir_tex[0] : hrc->black);
    cf_draw_set_texture("relief_tex", inputs.relief.id ? inputs.relief : hrc->black);
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
    cf_draw_set_uniform("surface_params", surface_params, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_draw_set_uniform("surface_map", surface_map, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_draw_set_uniform("ambient", &ambient, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("intensity", &intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("shadow_floor", &shadow_floor, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("fog", &fog, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("debug_view", &debug, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("dither_phase", &phase, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("crop_uv", crop, CF_UNIFORM_TYPE_FLOAT, 4);
    int split = split_lighting;
    cf_draw_set_uniform("split_lighting", &split, CF_UNIFORM_TYPE_INT, 1);
    cf_draw_push_filter(CF_DRAW_FILTER_LINEAR);
    cf_draw_quad_fill(hrc->screen, 0);
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    cf_canvas_set_clear_color(output, cf_make_color_rgba_f(0, 0, 0, 1));
    cf_render_to(output, true);
    RENDER_DRAW_UNBIND_TEXTURES(
        "scene_tex",
        "emission_tex",
        "absorption_tex",
        "albedo_tex",
        "fluence_tex",
        "luminance_tex",
        "normal_tex",
        "background_tex",
        "visibility_tex",
        "surface_tex",
        "surface_dir_tex",
        "relief_tex"
    );
    cf_pop_gpu_label();
    render_gpu_timing_mark("Composite");
    hrc->frame_count = (hrc->frame_count + 1) & 63;
    CHIBI_TRACE_ZONE_END(zone);
    return true;
}
