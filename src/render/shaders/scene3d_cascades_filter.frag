// Each probe's light, blurred over its neighbors on the same surface and
// blended into what earlier frames gathered there. A probe whose surface has
// moved, or one in a frame that starts over, keeps only this frame's.
#include "scene3d_cascades.shd"

void main()
{
    ivec2 probe = ivec2(gl_FragCoord.xy);
    vec4 center = texelFetch(upper, probe, 0);
    if (center.a == 0.0) {
        result = vec4(0.0);
        return;
    }
    ivec2 at = clamp(ivec2(probe_pixel(probe, cascade_probe.x)), ivec2(0), frame_size() - 1);
    float near = probe_unit(vec2(at) + 0.5, view_at(vec2(at) + 0.5, depth_at(at)));
    ivec2 last = ivec2(cascade_probe.zw) - 1;
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec4 other = texelFetch(upper, clamp(probe + ivec2(x, y), ivec2(0), last), 0);
            if (other.a == 0.0) continue;
            float apart = (other.a - center.a) / near;
            float w = (x == 0 && y == 0 ? 2.0 : 1.0) / (1.0 + apart * apart);
            sum += other.rgb * w;
            weights += w;
        }
    }
    vec3 light = kept_light(sum / weights);
    vec4 before = texelFetch(previous, probe, 0);
    bool moved = cascade_jitter.z > 0.5 || before.a == 0.0 || abs(before.a - center.a) > near ||
                 any(notEqual(kept_light(before.rgb), before.rgb));
    result = vec4(moved ? light : mix(before.rgb, light, cascade_jitter.y), center.a);
}
