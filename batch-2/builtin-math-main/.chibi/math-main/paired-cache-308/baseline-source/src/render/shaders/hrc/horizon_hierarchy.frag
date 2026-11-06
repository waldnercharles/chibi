// Conservative sample skipping over the reference ten 0.6-unit positions.
// Coarse maxima are bounds only: they never become a shading horizon. A fully
// covered group can skip only if its padded height cannot raise the current
// slope; mixed cells and potentially higher blockers keep the fine samples.
layout(set = 2, binding = 0) uniform sampler2D u_relief;
layout(set = 2, binding = 1) uniform sampler2D u_relief_half;
layout(set = 2, binding = 2) uniform sampler2D u_relief_quarter;
layout(set = 2, binding = 3) uniform sampler2D u_relief_eighth;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec2 u_world_per_px;
    int u_directions;
    int u_levels;
    int u_cache_valid;
    ivec2 u_cache_offset;
    int u_cache_tile;
};
layout(location = 0) out vec4 out_cardinal;
#ifdef HRC_HORIZON_FOUR
vec4 out_diagonal;
#else
layout(location = 1) out vec4 out_diagonal;
#endif
#define HRC_CACHE_HIERARCHY
#include "hrc/horizon_cache.shd"
const float PI = 3.141592653589793;
vec4 bounds_at(int level, ivec2 p)
{
    if (level == 3) return texelFetch(u_relief_eighth, p, 0);
    if (level == 2) return texelFetch(u_relief_quarter, p, 0);
    return texelFetch(u_relief_half, p, 0);
}
float horizon(vec2 uv, vec2 toward, float height)
{
    ivec2 size = textureSize(u_relief, 0);
    // Keep the fine sample arithmetic identical to horizon.frag, including
    // UV rounding at texel boundaries.
    vec2 step_uv = toward * (0.6 / u_world_per_px) / vec2(size);
    float slope = 0.0;
    int i = 1;
    // One hierarchy lookup at most per iteration. A mixed cell falls straight
    // back to the reference sample rather than recursively fetching all levels.
    for (int walk = 0; walk < 10; walk++) {
        if (i > 10) break;
        vec2 sample_pos = (uv + step_uv * float(i)) * vec2(size);
        if (any(lessThan(sample_pos, vec2(0))) || any(greaterThanEqual(sample_pos, vec2(size)))) break;
        int selected = 0, chunk = 1;
        ivec2 cell = ivec2(0);
        for (int level = 3; level >= 1; level--) {
            if (level > u_levels) continue;
            int span = 1 << level;
            int count = min(span, 11 - i);
            if (count <= 1) continue;
            vec2 end = (uv + step_uv * float(i + count - 1)) * vec2(size);
            if (any(lessThan(end, vec2(0))) || any(greaterThanEqual(end, vec2(size)))) continue;
            ivec2 first_cell = ivec2(floor(sample_pos / float(span)));
            ivec2 last_cell = ivec2(floor(end / float(span)));
            if (any(notEqual(first_cell, last_cell))) continue;
            // The segment lies within one convex cell, so every scheduled
            // sample between its endpoints shares this coverage/height bound.
            selected = level;
            chunk = count;
            cell = first_cell;
            break;
        }
        if (selected != 0) {
            vec4 bounds = bounds_at(selected, cell);
            if (bounds.z < 0.5) break;
            if (bounds.y >= 0.5 && bounds.x < 65504.0) {
                // Only the first reduction rounds a newly calculated maximum;
                // later max operations preserve that half-float value. 0.05%
                // covers downward half rounding. The small float term also
                // bounds cancellation when a negative height gains a face-plane
                // extent (nz > .05 caps that extent at 10 * pixel-size sum).
                float pad = max(0.00001, abs(bounds.x) * 0.0005)
                    + (abs(bounds.x) + 20.0 * (u_world_per_px.x + u_world_per_px.y)) * 0.000001;
                // Actual bias may be larger for a high blocker; using only the
                // center term keeps this an upper bound on its candidate slope.
                float low_bias = max(0.002, abs(height) * 0.001);
                float upper_slope = (bounds.x + pad - height - low_bias) / (0.6 * float(i));
                // Positive numerator is largest at the closest sample. A
                // negative numerator cannot raise the nonnegative current slope.
                // Saturated maxima are excluded: their unclamped bound is lost.
                if (upper_slope <= slope) {
                    i += chunk;
                    continue;
                }
            }
        }
        ivec2 p = ivec2(sample_pos);
        vec4 relief = texelFetch(u_relief, p, 0);
        if (relief.y < 0.5) break;
        float nz = sqrt(max(1.0 - dot(relief.zw, relief.zw), 0.0));
        vec2 offset = (sample_pos - (vec2(p) + 0.5)) * u_world_per_px;
        float sample_height = relief.x;
        if (nz > 0.05) sample_height -= dot(relief.zw, vec2(offset.x, -offset.y)) / nz;
        float bias = max(0.002, max(abs(height), abs(sample_height)) * 0.001);
        slope = max(slope, (sample_height - height - bias) / (0.6 * float(i)));
        i++;
    }
    return atan(slope);
}
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (reuse_horizon(p)) return;
    vec4 center = texelFetch(u_relief, p, 0);
    out_cardinal = vec4(0);
    out_diagonal = vec4(0);
    if (center.y <= 0.5) return;
    vec2 origin = (vec2(p) + 0.5) / vec2(textureSize(u_relief, 0));
    vec4 cardinal = vec4(0), diagonal = vec4(0);
    for (int i = 0; i < 4; i++) {
        float angle = float(i) * PI * 0.5;
        cardinal[i] = horizon(origin, vec2(cos(angle), sin(angle)), center.x);
        if (u_directions == 8) {
            angle += PI * 0.25;
            diagonal[i] = horizon(origin, vec2(cos(angle), sin(angle)), center.x);
        }
    }
    // WebGL requires output-vector indices to be constant, so assign whole vectors.
    out_cardinal = cardinal;
    out_diagonal = diagonal;
}
