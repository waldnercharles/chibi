#include "ui/rml_shadow.h"

#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static CF_Rect shadow_bounds(rml_shadow_target target)
{
    int left = target.scissor_enabled ? math_max(0, target.scissor.x) : 0;
    int top = target.scissor_enabled ? math_max(0, target.scissor.y) : 0;
    int right = target.scissor_enabled
                  ? math_min(target.width, target.scissor.x + target.scissor.w)
                  : target.width;
    int bottom = target.scissor_enabled
                   ? math_min(target.height, target.scissor.y + target.scissor.h)
                   : target.height;
    return (CF_Rect){ left, top, math_max(0, right - left), math_max(0, bottom - top) };
}

static float edge(rml_vec2 a, rml_vec2 b, rml_vec2 p)
{
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

static bool edge_is_top_left(rml_vec2 a, rml_vec2 b)
{
    return b.y < a.y || (b.y == a.y && b.x > a.x);
}

static rml_vec2 shadow_position(rml_shadow_target target, rml_vec2 position, rml_vec2 translation)
{
    CF_V4 p = cf_mul_m4_v4(
        target.transform,
        cf_v4(position.x + translation.x, position.y + translation.y, 0, 1)
    );
    float inverse_w = fabsf(p.w) > 1e-6f ? 1.0f / p.w : 1.0f;
    return (rml_vec2){ p.x * inverse_w, p.y * inverse_w };
}

static void raster_triangle(
    rml_shadow_target target,
    rml_shadow_layer *layer,
    const rml_vertex *va,
    const rml_vertex *vb,
    const rml_vertex *vc,
    rml_vec2 translation,
    rml_clip_mask_operation mask_operation
)
{
    enum
    {
        SAMPLE_COUNT = 4
    };
    static const rml_vec2 samples[SAMPLE_COUNT] = {
        { 0.25f, 0.25f },
        { 0.75f, 0.25f },
        { 0.25f, 0.75f },
        { 0.75f, 0.75f },
    };
    rml_vertex a = *va, b = *vb, c = *vc;
    rml_vec2 pa = shadow_position(target, a.position, translation);
    rml_vec2 pb = shadow_position(target, b.position, translation);
    rml_vec2 pc = shadow_position(target, c.position, translation);
    float area = edge(pa, pb, pc);
    if (fabsf(area) < 1e-5f) {
        return;
    }
    if (area < 0) {
        rml_vertex vertex = b;
        b = c;
        c = vertex;
        rml_vec2 point = pb;
        pb = pc;
        pc = point;
        area = -area;
    }

    int left = (int)floorf(fminf(pa.x, fminf(pb.x, pc.x)));
    int top = (int)floorf(fminf(pa.y, fminf(pb.y, pc.y)));
    int right = (int)ceilf(fmaxf(pa.x, fmaxf(pb.x, pc.x)));
    int bottom = (int)ceilf(fmaxf(pa.y, fmaxf(pb.y, pc.y)));
    left = math_max(left, 0);
    top = math_max(top, 0);
    right = math_min(right, target.width);
    bottom = math_min(bottom, target.height);
    if (target.scissor_enabled) {
        left = math_max(left, target.scissor.x);
        top = math_max(top, target.scissor.y);
        right = math_min(right, target.scissor.x + target.scissor.w);
        bottom = math_min(bottom, target.scissor.y + target.scissor.h);
    }

    const rml_vec2 points[3] = { pa, pb, pc };
    const rml_vertex vertices[3] = { a, b, c };
    const size_t stride = (size_t)target.width * 4;
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            float rgba[4] = { 0 };
            int covered = 0;
            for (int sample = 0; sample < SAMPLE_COUNT; sample++) {
                rml_vec2 p = { x + samples[sample].x, y + samples[sample].y };
                float e0 = edge(points[1], points[2], p);
                float e1 = edge(points[2], points[0], p);
                float e2 = edge(points[0], points[1], p);
                if (e0 < 0 ||
                    (fabsf(e0) < 1e-5f && !edge_is_top_left(points[1], points[2])) ||
                    e1 < 0 ||
                    (fabsf(e1) < 1e-5f && !edge_is_top_left(points[2], points[0])) ||
                    e2 < 0 ||
                    (fabsf(e2) < 1e-5f && !edge_is_top_left(points[0], points[1]))) {
                    continue;
                }
                const float weights[3] = { e0 / area, e1 / area, e2 / area };
                for (int channel = 0; channel < 4; channel++) {
                    const uint8_t colors[3] = {
                        channel == 0   ? vertices[0].color.r
                        : channel == 1 ? vertices[0].color.g
                        : channel == 2 ? vertices[0].color.b
                                       : vertices[0].color.a,
                        channel == 0   ? vertices[1].color.r
                        : channel == 1 ? vertices[1].color.g
                        : channel == 2 ? vertices[1].color.b
                                       : vertices[1].color.a,
                        channel == 0   ? vertices[2].color.r
                        : channel == 1 ? vertices[2].color.g
                        : channel == 2 ? vertices[2].color.b
                                       : vertices[2].color.a,
                    };
                    rgba[channel] += weights[0] * colors[0] +
                                     weights[1] * colors[1] + weights[2] * colors[2];
                }
                covered++;
            }
            if (!covered) {
                continue;
            }
            size_t pixel = (size_t)y * target.width + x;
            float coverage = (float)covered / SAMPLE_COUNT;
            if (mask_operation == RML_CLIP_MASK_SET) {
                int covered_mask = layer->mask[pixel] + (int)lroundf(coverage * 255);
                layer->mask[pixel] = (uint8_t)math_min(covered_mask, 255);
                continue;
            }
            float clip = target.clip_mask_enabled && layer->mask_set
                           ? (float)layer->mask[pixel] / 255.0f
                           : 1.0f;

            float source[4];
            for (int channel = 0; channel < 4; channel++) {
                source[channel] = rgba[channel] / (255.0f * SAMPLE_COUNT) * clip;
            }
            uint8_t *destination = layer->pixels + (size_t)y * stride + x * 4;
            for (int channel = 0; channel < 4; channel++) {
                destination[channel] = (uint8_t)lroundf(
                    fminf(255, source[channel] * 255 + destination[channel])
                );
            }
        }
    }
}

void rml_shadow_geometry(rml_shadow_target target, rml_shadow_layer *layer, rml_mesh mesh, rml_vec2 translation)
{
    size_t pixels = (size_t)target.width * target.height;
    rml_shadow_layer shape = { .pixels = xcalloc(pixels * 4, 1) };
    assert(mesh.vertex_count && mesh.index_count && mesh.index_count % 3 == 0);
    for (size_t i = 0; i < mesh.index_count; i += 3) {
        int a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        assert(a >= 0 && b >= 0 && c >= 0);
        assert(
            (size_t)a < mesh.vertex_count && (size_t)b < mesh.vertex_count &&
            (size_t)c < mesh.vertex_count
        );
        raster_triangle(
            target,
            &shape,
            &mesh.vertices[a],
            &mesh.vertices[b],
            &mesh.vertices[c],
            translation,
            (rml_clip_mask_operation)-1
        );
    }
    CF_Rect bounds = shadow_bounds(target);
    for (int y = bounds.y; y < bounds.y + bounds.h; y++) {
        for (int x = bounds.x; x < bounds.x + bounds.w; x++) {
            size_t i = (size_t)y * target.width + x;
            float clip = target.clip_mask_enabled && layer->mask_set
                           ? layer->mask[i] / 255.0f
                           : 1;
            float inverse_alpha = 1 - shape.pixels[i * 4 + 3] / 255.0f * clip;
            for (int channel = 0; channel < 4; channel++) {
                layer->pixels[i * 4 + channel] = (uint8_t)lroundf(fminf(
                    255,
                    shape.pixels[i * 4 + channel] * clip +
                        layer->pixels[i * 4 + channel] * inverse_alpha
                ));
            }
        }
    }
    free(shape.pixels);
}

/* The Rml filter uses Gaussian sigma, rather than CSS blur radius. Keep
   intermediate samples as floats so the second pass retains soft tails. */
uint8_t *rml_shadow_blur(rml_shadow_target target, const uint8_t *source, float sigma)
{
    int width = target.width, height = target.height;
    int radius = (int)ceilf(3 * sigma);
    int count = radius * 2 + 1;
    float *weights = xmalloc((size_t)count * sizeof *weights);
    float total = 0;
    for (int i = -radius; i <= radius; i++) {
        float weight = expf(-(float)i * i / (2 * sigma * sigma));
        weights[i + radius] = weight;
        total += weight;
    }
    for (int i = 0; i < count; i++) {
        weights[i] /= total;
    }
    int left = target.scissor_enabled ? math_max(0, target.scissor.x) : 0;
    int top = target.scissor_enabled ? math_max(0, target.scissor.y) : 0;
    int right = target.scissor_enabled
                  ? math_min(width, target.scissor.x + target.scissor.w)
                  : width;
    int bottom = target.scissor_enabled
                   ? math_min(height, target.scissor.y + target.scissor.h)
                   : height;
    int region_width = math_max(0, right - left),
        region_height = math_max(0, bottom - top);
    float *horizontal = xcalloc((size_t)region_width * region_height * 4, sizeof *horizontal);
    uint8_t *output = xcalloc((size_t)width * height * 4, 1);
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            for (int i = -radius; i <= radius; i++) {
                int sx = x + i;
                if (sx < left || sx >= right) {
                    continue;
                }
                for (int c = 0; c < 4; c++) {
                    horizontal
                        [((size_t)(y - top) * region_width + x - left) * 4 +
                         c] += source[((size_t)y * width + sx) * 4 + c] *
                               weights[i + radius];
                }
            }
        }
    }
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            float sum[4] = { 0 };
            for (int i = -radius; i <= radius; i++) {
                int sy = y + i;
                if (sy < top || sy >= bottom) {
                    continue;
                }
                for (int c = 0; c < 4; c++) {
                    sum[c] += horizontal[((size_t)(sy - top) * region_width + x - left) * 4 + c] *
                              weights[i + radius];
                }
            }
            for (int c = 0; c < 4; c++) {
                output[((size_t)y * width + x) * 4 + c] = (uint8_t)lroundf(sum[c]);
            }
        }
    }
    free(horizontal);
    free(weights);
    return output;
}

void rml_shadow_mask(
    rml_shadow_target target,
    rml_shadow_layer *layer,
    rml_mesh mesh,
    rml_vec2 translation,
    rml_clip_mask_operation operation
)
{
    size_t pixels = (size_t)target.width * target.height;
    /* Accumulate one complete shape before applying its Boolean operation.
       Applying intersection triangle-by-triangle leaves pixels outside the
       shape unchanged and creates seams on shared edges. */
    rml_shadow_layer shape = { .mask = xcalloc(pixels, 1) };
    for (size_t i = 0; i < mesh.index_count; i += 3) {
        int a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        assert(a >= 0 && b >= 0 && c >= 0);
        assert(
            (size_t)a < mesh.vertex_count && (size_t)b < mesh.vertex_count &&
            (size_t)c < mesh.vertex_count
        );
        raster_triangle(
            target,
            &shape,
            &mesh.vertices[a],
            &mesh.vertices[b],
            &mesh.vertices[c],
            translation,
            RML_CLIP_MASK_SET
        );
    }
    CF_Rect bounds = shadow_bounds(target);
    for (int y = bounds.y; y < bounds.y + bounds.h; y++) {
        for (int x = bounds.x; x < bounds.x + bounds.w; x++) {
            size_t i = (size_t)y * target.width + x;
            switch (operation) {
                case RML_CLIP_MASK_SET: layer->mask[i] = shape.mask[i]; break;
                case RML_CLIP_MASK_SET_INVERSE:
                    layer->mask[i] = 255 - shape.mask[i];
                    break;
                case RML_CLIP_MASK_INTERSECT:
                    layer->mask[i] = layer->mask_set
                                       ? (layer->mask[i] * shape.mask[i] + 127) / 255
                                       : shape.mask[i];
                    break;
            }
        }
    }
    free(shape.mask);
    layer->mask_set = true;
}
