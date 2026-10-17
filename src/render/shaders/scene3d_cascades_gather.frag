// Each first-cascade probe's irradiance over pi: its merged light from every
// direction, weighted by the cosine to its surface's normal and the solid
// angle at the point of its texel this frame's ray took.
#include "scene3d_cascades.shd"

void main()
{
    ivec2 probe = ivec2(gl_FragCoord.xy);
    ivec2 at = clamp(ivec2(probe_pixel(probe, cascade_probe.x)), ivec2(0), frame_size() - 1);
    float depth = depth_at(at);
    if (depth >= 1.0) {
        result = vec4(0.0);
        return;
    }
    vec3 view = view_at(vec2(at) + 0.5, depth);
    vec3 normal = (cascade_to_world * vec4(view_normal(at, view), 0.0)).xyz;
    int side = int(cascade_probe.y);
    vec3 sum = vec3(0.0);
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            ivec2 d = ivec2(x, y), texel = probe * side + d;
            vec2 sub = ray_jitter(texel);
            float facing = max(dot(normal, octahedral_direction(d, float(side), sub)), 0.0);
            sum += texelFetch(upper, texel, 0).rgb * facing * texel_solid_angle(d, float(side), sub);
        }
    }
    result = vec4(sum / PI, view.z);
}
