// Averages the 4x4 occlusion texels around each frame pixel, which hold every
// turn of the search's slices, weighting each by how near its point lies to
// the pixel's surface plane, so that occlusion keeps to its own surface across
// edges. At half resolution it also brings the occlusion up to the frame's.
#include "scene3d_occlusion.shd"

layout(set = 2, binding = 1) uniform sampler2D occlusion_map;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float depth = depth_at(p);
    if (depth >= 1.0) {
        result = vec4(1.0);
        return;
    }
    vec2 pixel = vec2(p) + 0.5;
    vec3 center = view_at(pixel, depth);
    vec3 normal = view_normal(p, center, toward_eye(pixel));
    int scale = int(occlusion_frame.z);
    // Two pixels' width at the surface, scaled with the search's texels.
    float w = occlusion_lens.y * center.z + occlusion_lens.z;
    float tolerance = 2.0 * float(scale) * w / occlusion_lens.x;
    ivec2 last = textureSize(occlusion_map, 0) - 1;
    ivec2 first = p / scale - 1;
    float sum = 0.0, weights = 0.0;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            ivec2 texel = clamp(first + ivec2(x, y), ivec2(0), last);
            float off = dot(normal, view_point(texel * scale) - center) / tolerance;
            // A floor keeps a pixel among other surfaces from dividing by zero.
            float weight = exp2(-off * off) + 1e-3;
            sum += texelFetch(occlusion_map, texel, 0).r * weight;
            weights += weight;
        }
    }
    // The lit pass reads occlusion in alpha; screen light fills rgb.
    result = vec4(0.0, 0.0, 0.0, sum / weights);
}
