// Uniform source proxies survive camera occlusion. Additive blending preserves
// overlapping sources' energy, but their weighted position is an approximation.
#include "hrc/emitter_encoding.shd"
layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 center;
layout(location = 2) in vec3 emission;
layout(location = 3) in float density;
layout(location = 4) in float source_radius;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec2 u_capture_size;
};
layout(location = 0) out vec4 out_source;
layout(location = 1) out vec4 out_moments;
void main()
{
    float distance = length(uv);
    float width = max(fwidth(distance), 1e-5);
    float coverage = 1.0 - smoothstep(1.0 - width * 0.5, 1.0 + width * 0.5, distance);
    if (density < 0.0) {
        vec2 weight = max(vec2(1.0) - abs(gl_FragCoord.xy - center.xy * u_capture_size), vec2(0));
        coverage = weight.x * weight.y;
    }
    vec3 light = max(emission, vec3(0)) * abs(density) * coverage;
    float power = dot(light, vec3(0.2126, 0.7152, 0.0722));
    float depth = center.z / 1024.0;
    out_source = vec4(light / EMITTER_STORAGE_SCALE, coverage * abs(density));
    out_moments = vec4(center.xy, depth, source_radius) * power / EMITTER_STORAGE_SCALE;
}
