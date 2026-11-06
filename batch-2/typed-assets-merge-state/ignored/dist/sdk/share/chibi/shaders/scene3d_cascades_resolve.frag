// The lit pass's screen light and occlusion at every frame pixel: the four
// nearest probes' irradiance, each weighted by how near its surface lies to
// the pixel's, in rgb, and the ambient occlusion in alpha.
#include "scene3d_cascades.shd"

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float occlusion = texelFetch(occlusion_map, p, 0).a;
    float depth = depth_at(p);
    if (depth >= 1.0) {
        result = vec4(0.0, 0.0, 0.0, occlusion);
        return;
    }
    vec3 view = view_at(vec2(p) + 0.5, depth);
    float near = 4.0 * probe_unit(vec2(p) + 0.5, view);
    float spacing = cascade_probe.x;
    ivec2 last = ivec2(cascade_probe.zw) - 1;
    vec2 g = (vec2(p) + 0.5) / spacing - 0.5;
    ivec2 base = ivec2(floor(g));
    vec2 f = g - vec2(base);
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int c = 0; c < 4; c++) {
        ivec2 corner = ivec2(c & 1, c >> 1);
        ivec2 q = clamp(base + corner, ivec2(0), last);
        float w = (corner.x == 1 ? f.x : 1.0 - f.x) * (corner.y == 1 ? f.y : 1.0 - f.y);
        float apart = (view_point(ivec2(probe_pixel(q, spacing))).z - view.z) / near;
        w *= 1.0 / (1.0 + apart * apart) + 1e-4;
        sum += texelFetch(upper, q, 0).rgb * w;
        weights += w;
    }
    result = vec4(sum / weights, occlusion);
}
