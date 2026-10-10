// Ground-truth ambient occlusion: the share of cosine-weighted sky above each
// texel's surface that the depth around it leaves open. Each slice through
// the view direction searches the depth on both sides for the highest horizon
// within the radius, then integrates the visible arc between them. The
// slices' turn and the steps' offsets change over each 4x4 block of texels,
// which the blur averages.
// Reference: https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// and XeGTAO, https://github.com/GameTechDev/XeGTAO
#include "scene3d_occlusion.shd"

const float PI = 3.141592653589793;
const float HALF_PI = 1.5707963267948966;
// The radius's outer share over which a sample's occlusion fades out.
const float FALLOFF = 0.615;
// The 4x4 ordered dither, one turn per texel of a block.
const float BLOCK[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

// How far up from the slice's lowest possible horizon the point at pixel q
// rises seen from the center, as the cosine of its angle from the view
// direction, faded out toward the radius. Pixels past the frame's edge show
// nothing; clamping them to it would raise false horizons off the slice.
float horizon(ivec2 q, vec3 center, vec3 eye, float lowest, vec2 falloff)
{
    if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, frame_size()))) return lowest;
    vec3 delta = view_point(q) - center;
    float distance = length(delta);
    if (distance <= 0.0) return lowest;
    float weight = clamp(distance * falloff.x + falloff.y, 0.0, 1.0);
    return mix(lowest, dot(delta, eye) / distance, weight);
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 p = min(texel * int(occlusion_frame.z), frame_size() - 1);
    float depth = depth_at(p);
    // Nothing covers the sky.
    if (depth >= 1.0) {
        result = vec4(1.0);
        return;
    }
    vec2 pixel = vec2(p) + 0.5;
    vec3 center = view_at(pixel, depth);
    vec3 eye = toward_eye(pixel);
    vec3 normal = view_normal(p, center, eye);
    float w = occlusion_lens.y * center.z + occlusion_lens.z;
    float per_unit = occlusion_lens.x / w;
    float reach = min(occlusion_lens.w * per_unit, occlusion_frame.w);
    // A radius under a pixel finds nothing to search.
    if (reach < 1.0) {
        result = vec4(1.0);
        return;
    }
    float radius = reach / per_unit;
    vec2 falloff = vec2(-1.0 / (FALLOFF * radius), (1.0 - FALLOFF) / FALLOFF + 1.0);
    float turn = BLOCK[(texel.x & 3) + 4 * (texel.y & 3)];
    vec2 noise = fract(0.5 + turn * vec2(0.7548776662, 0.5698402910));
    int slices = int(occlusion_search.x), steps = int(occlusion_search.y);
    // Samples start past the center pixel, which tells nothing.
    float nearest = 1.3 / reach;
    float visibility = 0.0;
    for (int slice = 0; slice < slices; slice++) {
        float phi = (float(slice) + noise.x) / float(slices) * PI;
        // Screen rows run down the frame.
        vec2 omega = vec2(cos(phi), -sin(phi));
        // The slice's direction across the view, toward where omega leads.
        vec3 direction = view_at(pixel + omega, depth) - center;
        direction = normalize(direction - dot(direction, eye) * eye);
        vec3 axis = cross(direction, eye);
        vec3 projected = normal - axis * dot(normal, axis);
        float projected_length = length(projected);
        float cos_n = clamp(dot(projected, eye) / max(projected_length, 1e-6), 0.0, 1.0);
        float n = sign(dot(direction, projected)) * acos(cos_n);
        float lowest0 = cos(n + HALF_PI), lowest1 = cos(n - HALF_PI);
        float horizon0 = lowest0, horizon1 = lowest1;
        for (int step = 0; step < steps; step++) {
            float jitter = fract(noise.y + float(slice + step * steps) * 0.6180339887);
            float s = (float(step) + jitter) / float(steps);
            ivec2 offset = ivec2(round((s * s + nearest) * reach * omega));
            horizon0 = max(horizon0, horizon(p + offset, center, eye, lowest0, falloff));
            horizon1 = max(horizon1, horizon(p - offset, center, eye, lowest1, falloff));
        }
        // XeGTAO's correction for slight overdarkening on steep slopes.
        projected_length = mix(projected_length, 1.0, 0.05);
        float h0 = -acos(clamp(horizon1, -1.0, 1.0));
        float h1 = acos(clamp(horizon0, -1.0, 1.0));
        h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
        h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);
        float arc0 = cos_n + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n);
        float arc1 = cos_n + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n);
        visibility += projected_length * (arc0 + arc1) * 0.25;
    }
    // Fades out where the radius covers only a few pixels.
    visibility = visibility / float(slices) + max(0.0, 10.0 - reach) * 0.005;
    result = vec4(clamp(visibility, 0.03, 1.0), 0.0, 0.0, 1.0);
}
