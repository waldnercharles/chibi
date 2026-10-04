// Denoise the interleaved gather on its receiver grid. Keep each receiver's
// height and normal intact so reconstruction can still reject object edges.
layout(set = 2, binding = 0) uniform sampler2D u_q0;
layout(set = 2, binding = 1) uniform sampler2D u_q1;
layout(set = 2, binding = 2) uniform sampler2D u_q2;
layout(set = 2, binding = 3) uniform sampler2D u_q3;
layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;
layout(set = 3, binding = 0) uniform uniform_block
{
    float u_sigma;
};

void main()
{
    ivec2 size = textureSize(u_q0, 0);
    ivec2 center = ivec2(gl_FragCoord.xy);
    vec4 receiver = texelFetch(u_q0, center, 0);
    vec4 geometry = texelFetch(u_q2, center, 0);
    out_q0 = receiver;
    out_q1 = vec4(0, 0, 0, 1);
    out_q2 = geometry;
    out_q3 = texelFetch(u_q3, center, 0);
    if (geometry.a < 0.5) return;

    vec3 diffuse_sum = vec3(0);
    float total = 0.0;
    float sigma = max(u_sigma * 0.5, 0.001);
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            ivec2 p = center + ivec2(x, y);
            if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) continue;
            float spatial = max(1.0 - float(x * x + y * y) / 6.25, 0.0);
            if (spatial <= 0.0) continue;
            vec4 neighbor = texelFetch(u_q2, p, 0);
            if (neighbor.a < 0.5) continue;
            float agreement = dot(geometry.xyz, neighbor.xyz);
            if (agreement < 0.9) continue;
            vec4 diffuse = texelFetch(u_q0, p, 0);
            float dz = (receiver.a - diffuse.a) / sigma;
            if (abs(dz) > 1.0) continue;
            float weight = spatial * spatial * pow(agreement, 8.0) * exp(-dz * dz);
            diffuse_sum += max(diffuse.rgb, vec3(0)) * weight;
            total += weight;
        }
    }
    // The valid center always contributes. Zero radiance remains a sample,
    // retaining its weight rather than renormalizing only the lit neighbors.
    out_q0.rgb = diffuse_sum / total;
}
