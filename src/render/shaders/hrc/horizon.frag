// Bounded, lighting-independent horizons. Each direction uses ten normalized
// 0.6-world-unit steps, including diagonals. Empty coverage ends the local surface.
layout(set = 2, binding = 0) uniform sampler2D u_relief;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec2 u_world_per_px;
    int u_directions;
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
#include "hrc/horizon_cache.shd"
const float PI = 3.141592653589793;
float horizon(vec2 uv, vec2 toward, float height)
{
    ivec2 size = textureSize(u_relief, 0);
    vec2 step_uv = toward * (0.6 / u_world_per_px) / vec2(size);
    float slope = 0.0;
    for (int i = 1; i <= 10; i++) {
        vec2 sample_uv = uv + step_uv * float(i);
        if (any(lessThan(sample_uv, vec2(0))) || any(greaterThanEqual(sample_uv, vec2(1)))) break;
        ivec2 p = ivec2(sample_uv * vec2(size));
        vec4 relief = texelFetch(u_relief, p, 0);
        if (relief.y < 0.5) break;
        float nz = sqrt(max(1.0 - dot(relief.zw, relief.zw), 0.0));
        vec2 offset = (sample_uv * vec2(size) - (vec2(p) + 0.5)) * u_world_per_px;
        float sample_height = relief.x;
        if (nz > 0.05) sample_height -= dot(relief.zw, vec2(offset.x, -offset.y)) / nz;
        float bias = max(0.002, max(abs(height), abs(sample_height)) * 0.001);
        slope = max(slope, (sample_height - height - bias) / (0.6 * float(i)));
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
    vec2 uv = (vec2(p) + 0.5) / vec2(textureSize(u_relief, 0));
    // GLSL ES requires constant indices when writing fragment outputs.
    vec4 cardinal = vec4(0), diagonal = vec4(0);
    // Stored in UV azimuth order: +X, +Y, -X, -Y, then their diagonals.
    for (int i = 0; i < 4; i++) {
        float angle = float(i) * PI * 0.5;
        cardinal[i] = horizon(uv, vec2(cos(angle), sin(angle)), center.x);
        if (u_directions == 8) {
            angle += PI * 0.25;
            diagonal[i] = horizon(uv, vec2(cos(angle), sin(angle)), center.x);
        }
    }
    out_cardinal = cardinal;
    out_diagonal = diagonal;
}
