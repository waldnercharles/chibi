// Optional short Depth history. Geometry stays current; only light is blended.
layout(set = 2, binding = 0) uniform sampler2D u_q0;
layout(set = 2, binding = 1) uniform sampler2D u_q1;
layout(set = 2, binding = 2) uniform sampler2D u_q2;
layout(set = 2, binding = 3) uniform sampler2D u_q3;
layout(set = 2, binding = 4) uniform sampler2D u_prev0;
layout(set = 2, binding = 5) uniform sampler2D u_prev1;
layout(set = 2, binding = 6) uniform sampler2D u_prev2;
layout(set = 2, binding = 7) uniform sampler2D u_prev3;
layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec4 u_transform0;
    vec4 u_transform1;
    vec4 u_transform2;
    vec4 u_projection;
    vec4 u_previous_projection;
    vec2 u_focal;
    vec2 u_raster;
    float u_sigma;
    float u_history;
    int u_block;
    int u_valid;
};

#include "hrc/depth_shading.shd"

vec3 transform_direction(vec3 n)
{
    return vec3(dot(u_transform0.xyz, n), dot(u_transform1.xyz, n), dot(u_transform2.xyz, n));
}
float history_weight(vec3 current, vec3 previous)
{
    float scale = max(max(length(current), length(previous)), 0.001);
    float difference = length(previous - current) / scale;
    // Strong emitter changes and newly dark pixels replace history immediately.
    return u_history * (1.0 - smoothstep(0.2, 0.7, difference));
}
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 current = texelFetch(u_q0, p, 0);
    vec4 material = texelFetch(u_q1, p, 0);
    vec4 geometry = texelFetch(u_q2, p, 0);
    out_q0 = current;
    out_q1 = material;
    out_q2 = geometry;
    out_q3 = texelFetch(u_q3, p, 0);
    if (u_valid == 0 || geometry.a < 0.5 || u_history <= 0.0) return;

    // Match gather's actual receiver texel rather than the padded grid extent.
    vec2 pixel = floor(gl_FragCoord.xy * float(u_block)) + 0.5;
    vec2 uv = pixel / u_raster;
    vec2 plane = vec2(u_projection.x + uv.x * u_projection.z,
                      u_projection.y - uv.y * u_projection.w);
    float scale = u_focal.x > 0.0 ? max(1.0 - current.a / u_focal.x, 0.001) : 1.0;
    vec4 point = vec4(plane * scale, current.a, 1);
    vec3 prior_point = vec3(dot(u_transform0, point), dot(u_transform1, point), dot(u_transform2, point));
    float old_scale = u_focal.y > 0.0 ? 1.0 - prior_point.z / u_focal.y : 1.0;
    if (old_scale <= 0.001) return;
    vec2 old_plane = prior_point.xy / old_scale;
    vec2 old_uv = vec2((old_plane.x - u_previous_projection.x) / u_previous_projection.z,
                       (u_previous_projection.y - old_plane.y) / u_previous_projection.w);
    if (any(lessThan(old_uv, vec2(0))) || any(greaterThanEqual(old_uv, vec2(1)))) return;
    ivec2 old_p = ivec2(old_uv * u_raster / float(u_block));
    ivec2 size = textureSize(u_q0, 0);
    if (any(greaterThanEqual(old_p, size))) return;
    vec4 old_geometry = texelFetch(u_prev2, old_p, 0);
    if (old_geometry.a < 0.5 || dot(normalize(transform_direction(geometry.xyz)), old_geometry.xyz) < 0.95) return;
    vec4 previous = texelFetch(u_prev0, old_p, 0);
    // Small depth tolerance rejects disocclusion and changing geometry.
    if (abs(previous.a - prior_point.z) > max(u_sigma * 0.5, 0.001)) return;
    vec4 old_material = texelFetch(u_prev1, old_p, 0);
    if (abs(material.a - old_material.a) > 0.08) return;

    vec3 lo = current.rgb, hi = current.rgb;
    vec3 spec_lo = material.rgb, spec_hi = material.rgb;
    vec4 current_shading = depth_unpack_shading(out_q3);
    vec2 shading = current_shading.xw, shading_lo = shading, shading_hi = shading;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            ivec2 q = p + ivec2(x, y);
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size))) continue;
            vec4 n = texelFetch(u_q2, q, 0);
            vec4 d = texelFetch(u_q0, q, 0);
            if (n.a < 0.5 || dot(n.xyz, geometry.xyz) < 0.95 || abs(d.a - current.a) > u_sigma * 0.5) continue;
            lo = min(lo, d.rgb);
            hi = max(hi, d.rgb);
            vec2 shade = depth_unpack_shading(texelFetch(u_q3, q, 0)).xw;
            shading_lo = min(shading_lo, shade);
            shading_hi = max(shading_hi, shade);
            vec4 s = texelFetch(u_q1, q, 0);
            if (abs(material.a - s.a) > 0.08) continue;
            spec_lo = min(spec_lo, s.rgb);
            spec_hi = max(spec_hi, s.rgb);
        }
    }
    float diffuse_weight = history_weight(current.rgb, previous.rgb);
    float specular_weight = history_weight(material.rgb, old_material.rgb);
    out_q0.rgb = mix(current.rgb, clamp(previous.rgb, lo, hi), diffuse_weight);
    out_q1.rgb = mix(material.rgb, clamp(old_material.rgb, spec_lo, spec_hi), specular_weight);
    // The light HRC brings a solid that absorbs nothing from the lights Depth
    // sees, and how much Depth changes it.
    vec2 old_shading = depth_unpack_shading(texelFetch(u_prev3, old_p, 0)).xw;
    float shading_weight = history_weight(vec3(shading, 0), vec3(old_shading, 0));
    current_shading.xw = mix(shading, clamp(old_shading, shading_lo, shading_hi), shading_weight);
    out_q3 = depth_pack_shading(current_shading);
}
