// Updates every texel of the probes' atlas from this frame's rays, keeping a
// share of last frame's: an irradiance tile texel averages its probe's rays
// by their cosine to its direction, and a depth tile texel averages their
// distance and squared distance, weighted toward rays close to it. A border
// texel repeats the texel across the octahedron's fold from it, so that
// filtering a tile's edge blends the directions beyond it. Rays that start
// inside a solid count against the probe: where a quarter of them do, the
// probe lies inside it and its alpha turns to zero.
#include "scene3d_gi_pass.shd"

// A texel per ray, at gi_ray_texel: the light that came back along the ray,
// and how far it went in the probes' depth units, negative where it started
// inside a solid.
layout(set = 2, binding = 1) uniform sampler2D gi_rays;

// The interior texel a tile texel holds: itself, or for a border texel, the
// one across the fold, mirrored along the edge.
ivec2 folded(ivec2 t, int texels)
{
    int last = texels - 1;
    ivec2 mirrored = ivec2(last) - t;
    if (t.x == 0 || t.x == last) {
        if (t.y == 0 || t.y == last) return ivec2(t.x == 0 ? last - 1 : 1, t.y == 0 ? last - 1 : 1);
        return ivec2(t.x == 0 ? 1 : last - 1, mirrored.y);
    }
    if (t.y == 0 || t.y == last) return ivec2(mirrored.x, t.y == 0 ? 1 : last - 1);
    return t;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int top = int(scene_gi_grid[2].y);
    bool irradiance = p.y < top;
    int texels = irradiance ? GI_IRRADIANCE_TEXELS : GI_DISTANCE_TEXELS;
    ivec2 q = ivec2(p.x, irradiance ? p.y : p.y - top);
    ivec2 tile = q / texels;
    int row = int(scene_gi_grid[2].x);
    int probe = tile.y * row + tile.x;
    ivec3 counts = gi_probe_counts();
    if (tile.x >= row || probe >= counts.x * counts.y * counts.z) {
        result = vec4(0.0);
        return;
    }
    ivec2 t = folded(q - tile * texels, texels);
    vec3 d = gi_direction((vec2(t - 1) + 0.5) / float(texels - 2) * 2.0 - 1.0);
    int rays = int(gi_trace.x);
    vec4 sum = vec4(0.0);
    int backfaces = 0;
    for (int i = 0; i < rays; i++) {
        vec4 ray = texelFetch(gi_rays, gi_ray_texel(probe, i), 0);
        float cosine = max(dot(d, gi_ray(i)), 0.0);
        if (irradiance) {
            if (ray.a < 0.0) {
                backfaces++;
                continue;
            }
            sum += vec4(ray.rgb * cosine, cosine);
            continue;
        }
        // Sharpened toward the texel's direction, as DDGI's depths are.
        float weight = pow(cosine, 50.0);
        float depth = min(abs(ray.a), 1.0);
        sum += vec4(depth * weight, depth * depth * weight, 0.0, weight);
    }
    vec4 fresh;
    if (irradiance) {
        fresh = vec4(sum.w > 0.0 ? sum.rgb / sum.w : vec3(0.0), backfaces * 4 < rays ? 1.0 : 0.0);
    } else {
        fresh = vec4(sum.w > 0.0 ? sum.xy / sum.w : vec2(1.0), 0.0, 0.0);
    }
    // Without last frame's light, its texels hold nothing to keep.
    float keep = irradiance ? gi_keep.x : gi_keep.y;
    if (keep <= 0.0) {
        result = fresh;
        return;
    }
    vec4 kept = texelFetch(scene_gi, p, 0);
    // A probe that moved keeps the light last frame's probes saw where it now
    // stands, and its own view of whether it lies inside a solid.
    if (irradiance && gi_keep.z > 0.0) {
        vec4 seen = gi_irradiance(gi_probe_at(probe), d, d);
        kept = seen.a > 0.0 ? vec4(seen.rgb, fresh.a) : fresh;
    }
    result = mix(fresh, kept, keep);
    // A probe holds no light from the frame it comes to lie inside a solid,
    // and one that leaves it gains its share as its light gathers.
    if (irradiance && fresh.a == 0.0) result.a = 0.0;
}
