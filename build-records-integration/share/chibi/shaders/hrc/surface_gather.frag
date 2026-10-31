// Fixed-budget open-space probes for a projected heightfield receiver.
// Twelve candidates and at most 48 visibility taps per low-resolution cell.
// This reuses HRC quadrants; it does not trace SDFs or loop over emitters.
layout(set = 2, binding = 0) uniform sampler2D u_q0;
layout(set = 2, binding = 1) uniform sampler2D u_q1;
layout(set = 2, binding = 2) uniform sampler2D u_q2;
layout(set = 2, binding = 3) uniform sampler2D u_q3;
layout(set = 2, binding = 4) uniform sampler2D u_source;
layout(set = 2, binding = 5) uniform sampler2D u_albedo;
layout(set = 2, binding = 6) uniform sampler2D u_cover;
layout(set = 2, binding = 7) uniform sampler2D u_normal;
layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec2 u_world_per_px;
    float u_radius;
    float u_relief;
    int u_block;
};

vec3 quadrant(int i, vec2 uv)
{
    if (i == 0) return texture(u_q0, uv).rgb;
    if (i == 1) return texture(u_q1, uv).rgb;
    if (i == 2) return texture(u_q2, uv).rgb;
    return texture(u_q3, uv).rgb;
}

bool inside(vec2 uv)
{
    return all(greaterThanEqual(uv, vec2(0))) && all(lessThan(uv, vec2(1)));
}

// Occupied taps are checked against a rising ray, not against equal depth.
// Equal-height receiver pixels stay below it: a flat face can receive light
// through its own footprint. Empty taps do not stop the visibility search.
float visibility(vec2 uv, vec2 offset, float height, float distance)
{
    if (u_relief <= 0.0) return 1.0;
    ivec2 size = textureSize(u_cover, 0);
    for (int j = 1; j <= 4; j++) {
        float fraction = float(j) / 5.0;
        ivec2 p = clamp(ivec2((uv + offset * fraction) * vec2(size)), ivec2(0), size - 1);
        float cover = texelFetch(u_cover, p, 0).g;
        if (cover < 0.5) continue;
        float obstacle = texelFetch(u_albedo, p, 0).a / cover;
        float ray_height = height + distance * fraction / u_relief;
        // Half-float height precision and one projected pixel of slope.
        float bias = max(0.002, max(abs(height), abs(obstacle)) * 0.001);
        bias += max(u_world_per_px.x, u_world_per_px.y) / u_relief;
        if (obstacle > ray_height + bias) return 0.0;
    }
    return 1.0;
}

void main()
{
    ivec2 size = textureSize(u_cover, 0);
    vec2 uv = (gl_FragCoord.xy * float(u_block)) / vec2(size);
    ivec2 p = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    vec4 geometry = texelFetch(u_cover, p, 0);
    float height = geometry.g > 1e-6 ? texelFetch(u_albedo, p, 0).a / geometry.g : 0.0;
    if (geometry.b > geometry.g * 0.5 && geometry.g > 1e-6) {
        vec2 tilt = texelFetch(u_normal, p, 0).xy / geometry.g;
        float nz = sqrt(max(1.0 - dot(tilt, tilt), 0.0));
        vec2 offset = (uv * vec2(size) - (vec2(p) + 0.5)) * u_world_per_px;
        if (nz > 0.05) height -= dot(tilt, vec2(offset.x, -offset.y)) / nz;
    }
    vec2 axes[4] = vec2[4](vec2(1, 0), vec2(0, 1), vec2(-1, 0), vec2(0, -1));
    vec4 gathered[4];
    for (int i = 0; i < 4; i++) {
        vec3 light = vec3(0);
        float openness = 0.0;
        // Uniform radial quadrature preserves face reach without Gaussian
        // convolution across unrelated geometry. Missing/blocked probes stay
        // in the denominator rather than amplifying the surviving light.
        for (int k = 1; k <= 3; k++) {
            float distance = u_radius * float(k) * 0.5;
            vec2 offset = axes[i] * distance / (u_world_per_px * vec2(size));
            vec2 sample_uv = uv + offset;
            if (!inside(sample_uv)) continue;
            // Camera opacity is independent of transport: a nonabsorbing
            // floor remains a valid probe despite its opaque display color.
            float open = exp(-max(texture(u_source, sample_uv).a, 0.0));
            if (open <= 1e-6) continue;
            openness += open;
            float visible = geometry.g > 0.5 ? visibility(uv, offset, height, distance) : 1.0;
            light += max(quadrant(i, sample_uv), vec3(0)) * open * visible;
        }
        gathered[i] = vec4(light, openness) / 3.0;
    }
    // Composite uses quadrant zero's alpha as the shared receiver reach.
    float open = 0.25 * (gathered[0].a + gathered[1].a + gathered[2].a + gathered[3].a);
    out_q0 = vec4(gathered[0].rgb, open);
    out_q1 = vec4(gathered[1].rgb, open);
    out_q2 = vec4(gathered[2].rgb, open);
    out_q3 = vec4(gathered[3].rgb, open);
}
