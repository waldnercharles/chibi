/* HRC resource/dispatch core adapted from Neon-Swarm src/lighting.c.
 * Original algorithm and shader comments retained. */
#include "render/radiance.h"

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
struct render_radiance
{
    int canvas_w, canvas_h, work_w, work_h, world_w, world_h, N, trace_cutoff;
    float falloff, bounce_intensity, bounce_hysteresis, radiance_hysteresis;
    int upscale_mode, radiance_ping, frame_count;
    CF_Aabb screen;
    int capacity_w, capacity_h;
    float upscale_threshold;
    GameHRC2D lighting;
    bool authored_lighting;
    float scale;
    int trace_levels;
    bool feedback;
    double frame_average;
    int fast_windows;
    int frame_samples;
    CF_Texture scene, emission, transmittance, albedo, black;
    CF_Canvas bounce_canvas;
    CF_Texture radiance_tex[2], radiance_raw_tex, radiance_bounce_tex;
    CF_Texture emission_filtered, transmittance_filtered, bounce_filtered;
    CF_StorageBuffer t_buf[4][HRC_MAX_CASCADES], r_ping[4][2], r_zero;
    CF_StorageBuffer frustum[4], merge_weights[HRC_MAX_CASCADES];
    CF_ComputeShader trace_shd, extend_shd, merge_shd, gather_shd, blur_shd,
        prefilter_shd;
    CF_Shader bounce_shd, composite_shd;
    CF_Material compute_material;
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

static CF_Canvas s_make_canvas_rgba16f(int w, int h, bool mipmaps)
{
    CF_CanvasParams params = cf_canvas_defaults(w, h);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
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
    hrc->capacity_w = hrc->capacity_h = 0;
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

    for (int q = 0; q < 4; ++q) {
        for (int i = 0; i < HRC_MAX_CASCADES; ++i) {
            if (hrc->t_buf[q][i].id) {
                cf_destroy_storage_buffer(hrc->t_buf[q][i]);
            }
            hrc->t_buf[q][i] = (CF_StorageBuffer){ 0 };
        }
        for (int i = 0; i < 2; ++i) {
            if (hrc->r_ping[q][i].id) {
                cf_destroy_storage_buffer(hrc->r_ping[q][i]);
            }
            hrc->r_ping[q][i] = (CF_StorageBuffer){ 0 };
        }
    }
    if (hrc->r_zero.id) {
        cf_destroy_storage_buffer(hrc->r_zero);
    }
    hrc->r_zero = (CF_StorageBuffer){ 0 };
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
    bool reuse = work_w <= hrc->capacity_w && work_h <= hrc->capacity_h;
    if (!reuse) {
        s_destroy_work_resources(hrc);
        hrc->emission_filtered = s_make_compute_texture(work_w, work_h, CF_FILTER_LINEAR, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
        hrc->transmittance_filtered = s_make_compute_texture(work_w, work_h, CF_FILTER_LINEAR, CF_PIXEL_FORMAT_R16_FLOAT);
        hrc->bounce_filtered = s_make_compute_texture(work_w, work_h, CF_FILTER_LINEAR, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
        if (!hrc->emission_filtered.id || !hrc->transmittance_filtered.id ||
            !hrc->bounce_filtered.id) {
            goto fail;
        }
    }

    hrc->world_w = work_w;
    hrc->world_h = work_h;
    int span = work_w > work_h ? work_w : work_h;
    int ideal_N = ceil(log2(span));
    hrc->N = cf_min(ideal_N, HRC_MAX_CASCADES - 1);
    hrc->trace_cutoff = s_trace_cutoff(hrc);
    if (reuse) {
        hrc->work_w = work_w;
        hrc->work_h = work_h;
        return true;
    }

    int max_r_count = 0;
    int rot_w_options[2] = { hrc->world_w, hrc->world_h };
    int rot_h_options[2] = { hrc->world_h, hrc->world_w };

    for (int i = 0; i <= hrc->N; ++i) {
        int max_t_count = 0;
        for (int o = 0; o < 2; ++o) {
            int rot_w = rot_w_options[o];
            int rot_h = rot_h_options[o];
            int t_w = s_t_width(rot_w, i);
            int t_count = t_w * rot_h;
            if (t_count > max_t_count) {
                max_t_count = t_count;
            }

            int r_count = s_r_width(rot_w, i) * rot_h;
            if (r_count > max_r_count) {
                max_r_count = r_count;
            }
        }

        for (int q = 0; q < 4; ++q) {
            hrc->t_buf[q][i] = s_make_ssbo(max_t_count * 8);
            if (hrc->t_buf[q][i].id == 0) {
                goto fail;
            }
        }
    }

    hrc->r_zero = s_make_ssbo(max_r_count * 8);
    if (!hrc->r_zero.id) {
        goto fail;
    }

    int r_sz = max_r_count * 8;
    void *zeros = cf_calloc(r_sz, 1);
    cf_update_storage_buffer(hrc->r_zero, zeros, r_sz);

    for (int q = 0; q < 4; ++q) {
        for (int i = 0; i < 2; ++i) {
            hrc->r_ping[q][i] = s_make_ssbo(max_r_count * 8);
            if (!hrc->r_ping[q][i].id) {
                cf_free(zeros);
                goto fail;
            }
        }
    }
    int frustum_count = hrc->world_w * hrc->world_h;
    for (int i = 0; i < 4; ++i) {
        hrc->frustum[i] = s_make_ssbo(frustum_count * 8);
        if (!hrc->frustum[i].id) {
            cf_free(zeros);
            goto fail;
        }
    }

    cf_free(zeros);

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
    float step_scale = (float)hrc->canvas_w / (float)hrc->work_w;
    cf_material_set_uniform_cs(mat, "u_step_scale", &step_scale, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(s_t_width(rot_w, level), HRC_WG),
        s_ceil_div(rot_h, HRC_WG),
        1
    );
    CF_StorageBuffer rw[1] = { hrc->t_buf[quadrant][level] };
    d.rw_buffers = rw;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(hrc->trace_shd, mat, d);
}

static void s_dispatch_extend(HRC *hrc, int quadrant, int rot_w, int rot_h, int level)
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
    CF_StorageBuffer ro[1] = { hrc->t_buf[quadrant][level - 1] };
    d.ro_buffers = ro;
    d.ro_buffer_count = 1;
    CF_StorageBuffer rw[1] = { hrc->t_buf[quadrant][level] };
    d.rw_buffers = rw;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(hrc->extend_shd, mat, d);
}

static void s_dispatch_merge(
    HRC *hrc,
    int quadrant,
    int rot_w,
    int rot_h,
    int level,
    CF_StorageBuffer r_prev,
    CF_StorageBuffer r_curr
)
{
    CF_Material mat = hrc->compute_material;
    int t_curr_w = s_t_width(rot_w, level);
    int t_next_w = s_t_width(rot_w, level + 1);
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
    float ww = (float)hrc->work_w, wh = (float)hrc->work_h;
    float diag = sqrtf(ww * ww + wh * wh);
    float falloff = diag > 0 ? hrc->falloff * 2.0f / diag : 0.0f;
    cf_material_set_uniform_cs(mat, "u_falloff", &falloff, CF_UNIFORM_TYPE_FLOAT, 1);

    int dispatch_h = s_ceil_div(rot_h, HRC_WG);
    CF_ComputeDispatch d = cf_compute_dispatch_defaults(s_ceil_div(r_curr_w, HRC_WG), dispatch_h, 1);
    CF_StorageBuffer ro[4] = { hrc->t_buf[quadrant][level],
                               hrc->t_buf[quadrant][level + 1],
                               r_prev,
                               hrc->merge_weights[level] };
    d.ro_buffers = ro;
    d.ro_buffer_count = 4;
    CF_StorageBuffer rw[1] = { r_curr };
    d.rw_buffers = rw;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(hrc->merge_shd, mat, d);
}

static void s_dispatch_prefilter(HRC *hrc)
{
    CF_Material mat = hrc->compute_material;

    cf_material_set_texture_cs(mat, "u_emission", hrc->emission);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_bounce", cf_canvas_get_target(hrc->bounce_canvas));

    int params[4] = { hrc->canvas_w, hrc->canvas_h, hrc->work_w, hrc->work_h };
    cf_material_set_uniform_cs(mat, "u_canvas_w", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_canvas_h", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_w", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_h", params + 3, CF_UNIFORM_TYPE_INT, 1);

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

    cf_material_set_texture_cs(mat, "u_emission", hrc->emission);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_transmittance_filtered", hrc->transmittance_filtered);
    cf_material_set_texture_cs(mat, "u_bounce", cf_canvas_get_target(hrc->bounce_canvas));

    int params[5] = { hrc->canvas_w, hrc->canvas_h, hrc->work_w, hrc->work_h, hrc->upscale_mode };
    cf_material_set_uniform_cs(mat, "u_canvas_w", params + 0, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_canvas_h", params + 1, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_w", params + 2, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_work_h", params + 3, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_upscale_mode", params + 4, CF_UNIFORM_TYPE_INT, 1);
    cf_material_set_uniform_cs(mat, "u_upscale_threshold", &hrc->upscale_threshold, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_cs(mat, "u_bounce_intensity", &hrc->bounce_intensity, CF_UNIFORM_TYPE_FLOAT, 1);

    CF_ComputeDispatch d = cf_compute_dispatch_defaults(
        s_ceil_div(hrc->canvas_w, HRC_WG),
        s_ceil_div(hrc->canvas_h, HRC_WG),
        1
    );
    CF_StorageBuffer ro[4] = { hrc->frustum[0],
                               hrc->frustum[1],
                               hrc->frustum[2],
                               hrc->frustum[3] };
    d.ro_buffers = ro;
    d.ro_buffer_count = 4;
    CF_Texture rw_tex[2] = { hrc->radiance_raw_tex, hrc->radiance_bounce_tex };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 2;
    cf_dispatch_compute(hrc->gather_shd, mat, d);
}

static void s_dispatch_blur(HRC *hrc)
{
    CF_Material mat = hrc->compute_material;

    int prev = hrc->radiance_ping;
    int curr = 1 - prev;

    cf_material_set_texture_cs(mat, "u_in_radiance", hrc->radiance_raw_tex);
    cf_material_set_texture_cs(mat, "u_transmittance", hrc->transmittance);
    cf_material_set_texture_cs(mat, "u_prev_radiance", hrc->radiance_tex[prev]);

    float texel_size[2] = { 1.0f / hrc->canvas_w, 1.0f / hrc->canvas_h };
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
        s_ceil_div(hrc->canvas_w, HRC_WG),
        s_ceil_div(hrc->canvas_h, HRC_WG),
        1
    );
    CF_Texture rw_tex[1] = { hrc->radiance_tex[curr] };
    d.rw_textures = rw_tex;
    d.rw_texture_count = 1;
    cf_dispatch_compute(hrc->blur_shd, mat, d);

    hrc->radiance_ping = curr;
}

static void s_solve_trace(HRC *hrc, int j, int N)
{
    int cutoff = hrc->trace_cutoff < N ? hrc->trace_cutoff : N;
    for (int i = 0; i < cutoff; ++i) {
        s_dispatch_trace(hrc, j, i);
    }
}

static void s_solve_extend(HRC *hrc, int j, int N)
{
    int rot_w = s_rot_world_w(hrc, j);
    int rot_h = s_rot_world_h(hrc, j);
    int cutoff = hrc->trace_cutoff < N ? hrc->trace_cutoff : N;
    for (int i = cutoff; i < N; ++i) {
        s_dispatch_extend(hrc, j, rot_w, rot_h, i);
    }
}

static void s_solve_merge(HRC *hrc, int j, int N)
{
    int rot_w = s_rot_world_w(hrc, j);
    int rot_h = s_rot_world_h(hrc, j);
    for (int i = N - 1; i >= 0; --i) {
        int ping = (N - 1 - i) & 1;
        CF_StorageBuffer r_prev = (i == N - 1) ? hrc->r_zero
                                               : hrc->r_ping[j][1 - ping];
        CF_StorageBuffer r_out = (i == 0) ? hrc->frustum[j] : hrc->r_ping[j][ping];
        s_dispatch_merge(hrc, j, rot_w, rot_h, i, r_prev, r_out);
    }
}

static void s_render_bounce(HRC *hrc)
{
    float hysteresis = hrc->bounce_hysteresis;
    if (hysteresis < 0.01f) {
        hysteresis = 0.01f;
    }
    if (hysteresis > 1.0f) {
        hysteresis = 1.0f;
    }

    CF_RenderState rs = cf_render_state_defaults();
    rs.blend.enabled = true;
    rs.blend.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    rs.blend.rgb_src_blend_factor = CF_BLENDFACTOR_SRC_ALPHA;
    rs.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    rs.blend.rgb_op = CF_BLEND_OP_ADD;
    rs.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    rs.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ZERO;
    rs.blend.alpha_op = CF_BLEND_OP_ADD;

    cf_draw_push_render_state(rs);
    cf_draw_push_alpha_discard(false);
    cf_draw_push_shader(hrc->bounce_shd);
    cf_draw_set_texture("u_radiance", hrc->radiance_bounce_tex);
    cf_draw_set_texture("u_albedo", hrc->albedo);
    cf_draw_set_uniform("u_hysteresis", &hysteresis, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_quad_fill(hrc->screen, 0);
    cf_draw_pop_shader();
    cf_draw_pop_alpha_discard();
    cf_draw_pop_render_state();
    cf_render_to(hrc->bounce_canvas, false);
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
                               &hrc->radiance_bounce_tex };
    for (int i = 0; i < 4; i++) {
        if (textures[i]->id) {
            cf_destroy_texture(*textures[i]);
        }
        *textures[i] = (CF_Texture){ 0 };
    }
    hrc->canvas_w = hrc->canvas_h = 0;
}

render_radiance *render_radiance_new()
{
    if (cf_query_backend() == CF_BACKEND_TYPE_GLES3) {
        return NULL;
    }
    HRC *hrc = xmalloc(sizeof *hrc);
    memset(hrc, 0, sizeof *hrc);
    hrc->compute_material = cf_make_material();
    hrc->trace_shd = cf_make_compute_shader("hrc/trace.shd");
    hrc->extend_shd = cf_make_compute_shader("hrc/extend.shd");
    hrc->merge_shd = cf_make_compute_shader("hrc/merge.shd");
    hrc->gather_shd = cf_make_compute_shader("hrc/gather.shd");
    hrc->blur_shd = cf_make_compute_shader("hrc/blur.shd");
    hrc->prefilter_shd = cf_make_compute_shader("hrc/prefilter.shd");
    hrc->bounce_shd = cf_make_draw_shader("hrc/bounce.shd");
    hrc->composite_shd = cf_make_draw_shader("hrc/composite.shd");
    hrc->black = cf_make_texture(cf_texture_defaults(1, 1));
    unsigned black = 0;
    if (hrc->black.id) {
        cf_texture_update(hrc->black, &black, sizeof black);
    }
    hrc->scale = .5f;
    hrc->trace_levels = 3;
    hrc->feedback = true;
    hrc->falloff = hrc->bounce_intensity = 1;
    hrc->bounce_hysteresis = hrc->radiance_hysteresis = 0.8f;
    hrc->upscale_mode = 1;
    hrc->upscale_threshold = 0.1f;
    if (!hrc->compute_material.id || !hrc->trace_shd.id ||
        !hrc->extend_shd.id || !hrc->merge_shd.id || !hrc->gather_shd.id ||
        !hrc->blur_shd.id || !hrc->prefilter_shd.id || !hrc->bounce_shd.id ||
        !hrc->composite_shd.id || !hrc->black.id) {
        render_radiance_free(hrc);
        return NULL;
    }
    return hrc;
}

void render_radiance_free(render_radiance *hrc)
{
    if (!hrc) {
        return;
    }
    s_destroy_work_resources(hrc);
    destroy_images(hrc);
    CF_ComputeShader shaders[] = { hrc->trace_shd, hrc->extend_shd,
                                   hrc->merge_shd, hrc->gather_shd,
                                   hrc->blur_shd,  hrc->prefilter_shd };
    for (int i = 0; i < 6; i++) {
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
    hrc->frame_count = 0;
    hrc->radiance_ping = 0;
    if (!hrc->canvas_w) {
        return;
    }
    int bytes = hrc->canvas_w * hrc->canvas_h * 8;
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
    if (hrc->canvas_w == width && hrc->canvas_h == height &&
        hrc->work_w == (int)ceilf(width * hrc->scale) &&
        hrc->work_h == (int)ceilf(height * hrc->scale)) {
        return true;
    }
    bool images_changed = hrc->canvas_w != width || hrc->canvas_h != height;
    bool ok = true;
    if (images_changed) {
        destroy_images(hrc);
        hrc->bounce_canvas = s_make_canvas_rgba16f(width, height, true);
        CF_Texture *textures[] = { &hrc->radiance_tex[0],
                                   &hrc->radiance_tex[1],
                                   &hrc->radiance_raw_tex,
                                   &hrc->radiance_bounce_tex };
        for (int i = 0; i < 4; i++) {
            *textures[i] = s_make_compute_texture(width, height, CF_FILTER_NEAREST, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
        }
        ok = hrc->bounce_canvas.id != 0;
        for (int i = 0; i < 4; i++) {
            ok = ok && textures[i]->id != 0;
        }
    }
    float scale = hrc->scale;
    int work_w = (int)fmaxf(2, ceilf(width * scale));
    int work_h = (int)fmaxf(2, ceilf(height * scale));
    if (!ok || !s_create_work_resources(hrc, work_w, work_h)) {
        s_destroy_work_resources(hrc);
        destroy_images(hrc);
        return false;
    }
    hrc->canvas_w = width;
    hrc->canvas_h = height;
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
    bool resized = true;
    if (radiance->scale != settings.scale) {
        radiance->scale = settings.scale;
        if (radiance->canvas_w && radiance->canvas_h) {
            resized = render_radiance_resize(
                radiance,
                radiance->canvas_w,
                radiance->canvas_h
            );
        }
    }
    radiance->trace_levels = settings.trace_levels;
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
                             lighting.particle_focal_length };
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        if (!isfinite(values[i]) || values[i] < 0) {
            return false;
        }
    }
    if (lighting.min_scale < .1f || lighting.max_scale > 1 ||
        lighting.scale < lighting.min_scale ||
        lighting.scale > lighting.max_scale || lighting.target_fps <= 0 ||
        lighting.fps_tolerance > 1 || lighting.bounce_hysteresis > 1 ||
        lighting.radiance_hysteresis > 1 || lighting.particle_focal_length <= 0 ||
        lighting.upscale_mode < 0 || lighting.upscale_mode > 1 ||
        lighting.debug_view < 0 || lighting.debug_view > 6) {
        return false;
    }
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
    radiance->falloff = lighting.falloff;
    radiance->bounce_intensity = lighting.feedback ? lighting.bounce_intensity : 0;
    radiance->bounce_hysteresis = lighting.bounce_hysteresis;
    radiance->radiance_hysteresis = lighting.radiance_hysteresis;
    radiance->upscale_mode = lighting.upscale_mode;
    radiance->upscale_threshold = lighting.upscale_threshold;
    radiance->trace_cutoff = lighting.trace_levels;
    radiance->lighting = lighting;
    radiance->authored_lighting = true;
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

bool render_radiance_draw(render_radiance *hrc, render_radiance_inputs inputs, render_view_2d view, CF_Canvas output)
{
    assert(hrc);
    if (!hrc->canvas_w || !output.id || !inputs.scene.id ||
        !inputs.emission.id || !inputs.absorption.id || !inputs.albedo.id) {
        return false;
    }
    hrc->screen = (CF_Aabb){ .min = { view.min_x + view.width * view.crop_min_u,
                                      view.max_y - view.height * view.crop_max_v },
                             .max = { view.min_x + view.width * view.crop_max_u,
                                      view.max_y - view.height * view.crop_min_v } };
    hrc->scene = inputs.scene;
    hrc->emission = inputs.emission;
    hrc->transmittance = inputs.absorption;
    hrc->albedo = inputs.albedo;
    cf_generate_mipmaps(inputs.emission);
    cf_generate_mipmaps(inputs.absorption);
    s_dispatch_prefilter(hrc);
    for (int q = 0; q < 4; q++) {
        s_solve_trace(hrc, q, hrc->N);
        s_solve_extend(hrc, q, hrc->N);
        s_solve_merge(hrc, q, hrc->N);
    }
    s_dispatch_gather(hrc);
    s_dispatch_blur(hrc);
    s_render_bounce(hrc);
    GameHRC2D lighting = hrc->authored_lighting ? hrc->lighting
                                                : game_hrc2d_defaults();
    float ambient = lighting.ambient;
    float intensity = lighting.intensity;
    float shadow_floor = lighting.shadow_floor;
    float fog = lighting.fog;
    float debug = (float)lighting.debug_view;
    float phase = (float)(hrc->frame_count & 63);
    float crop[] = { view.crop_min_u, view.crop_min_v, view.crop_max_u, view.crop_max_v };
    cf_draw_push_shader(hrc->composite_shd);
    cf_draw_set_texture("scene_tex", hrc->scene);
    cf_draw_set_texture("emission_tex", hrc->emission);
    cf_draw_set_texture("absorption_tex", hrc->transmittance);
    cf_draw_set_texture("albedo_tex", hrc->albedo);
    cf_draw_set_texture("fluence_tex", hrc->radiance_tex[hrc->radiance_ping]);
    cf_draw_set_texture("normal_tex", inputs.normal);
    cf_draw_set_texture("background_tex", inputs.background.id ? inputs.background : hrc->black);
    cf_draw_set_uniform("ambient", &ambient, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("intensity", &intensity, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("shadow_floor", &shadow_floor, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("fog", &fog, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("debug_view", &debug, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("dither_phase", &phase, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_draw_set_uniform("crop_uv", crop, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_draw_push_filter(CF_DRAW_FILTER_LINEAR);
    cf_draw_quad_fill(hrc->screen, 0);
    cf_draw_pop_filter();
    cf_draw_pop_shader();
    cf_canvas_set_clear_color(output, cf_make_color_rgba_f(0, 0, 0, 1));
    cf_render_to(output, true);
    hrc->frame_count = (hrc->frame_count + 1) & 63;
    return true;
}
