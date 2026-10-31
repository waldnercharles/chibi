// One cascade of the screen light: each texel is a probe's ray, cast from the
// visible surface at the probe's pixel over the cascade's interval, merged
// with the cascade above where the ray reaches its end unblocked.
// Reference: Alexander Sannikov, Radiance Cascades,
// https://github.com/Raikiri/RadianceCascadesPaper
#include "scene3d_cascades.shd"

// How far behind the depth a sample may lie and still meet that surface, and
// how far rays leave their surface so as not to meet it at once, in the
// first cascade's probe spacing.
const float THICKNESS = 4.0;
const float LIFT = 0.25;
float thickness;

// Radiance and transmittance along the ray from origin in direction over
// [t0, t1], marching the depth: a sample just behind the visible surface
// meets it and takes its color from the last frame. Off the screen nothing is
// known, and the ray passes on.
float offset;

vec4 trace_screen(vec3 origin, vec3 direction, float t0, float t1, int steps)
{
    for (int s = 0; s < steps; s++) {
        float t = mix(t0, t1, (float(s) + offset) / float(steps));
        vec3 p = origin + direction * t;
        vec4 clip = cascade_to_clip * vec4(p, 1.0);
        if (clip.w <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
        vec2 ndc = clip.xy / clip.w;
        if (any(greaterThan(abs(ndc), vec2(1.0)))) return vec4(0.0, 0.0, 0.0, 1.0);
        ivec2 pixel = ivec2((ndc * vec2(0.5, -0.5) + 0.5) * cascade_frame.xy);
        float depth = depth_at(pixel);
        if (depth >= 1.0) continue;
        float surface = view_at(vec2(pixel) + 0.5, depth).z;
        float ray = (cascade_to_view * vec4(p, 1.0)).z;
        float behind = surface - ray;
        if (behind > 0.0 && behind < thickness + (t1 - t0) / float(steps)) {
            if (cascade_frame.z <= 0.0) return vec4(0.0);
            return vec4(history_at(p), 0.0);
        }
    }
    return vec4(0.0, 0.0, 0.0, 1.0);
}

// The last frame's light from the surface at world point p, where the camera
// saw it then and sees it now; nothing where it is hidden or off the screen.
vec3 seen_light(vec3 p)
{
    if (cascade_frame.z <= 0.0) return vec3(0.0);
    vec4 clip = cascade_to_clip * vec4(p, 1.0);
    if (clip.w <= 0.0) return vec3(0.0);
    vec2 ndc = clip.xy / clip.w;
    if (any(greaterThan(abs(ndc), vec2(1.0)))) return vec3(0.0);
    ivec2 pixel = ivec2((ndc * vec2(0.5, -0.5) + 0.5) * cascade_frame.xy);
    float depth = depth_at(pixel);
    if (depth >= 1.0) return vec3(0.0);
    float surface = view_at(vec2(pixel) + 0.5, depth).z;
    if (abs(surface - (cascade_to_view * vec4(p, 1.0)).z) > thickness) return vec3(0.0);
    return history_at(p);
}

// Radiance and transmittance along the ray through the world: the light the
// emitters send along it, until it meets rock or another solid, which gives
// back the light the camera saw on it. Solids fill whole grid cells, so the
// ray passes those within a cell of where it left.
vec4 trace_world(vec3 origin, vec3 direction, float t0, float t1, int steps)
{
    vec3 light_sum = vec3(0.0);
    float step_length = (t1 - t0) / float(steps);
    for (int s = 0; s < steps; s++) {
        float t = t0 + (float(s) + offset) * step_length;
        vec3 p = origin + direction * t;
        vec4 cell = volume_at(light, light_corner, light_cells, p, vec4(0.0));
        light_sum += cell.rgb * step_length;
        bool solid = cell.a > 0.5 && t > light_corner.w;
        if (solid || volume_at(rock, rock_corner, rock_cells, p, vec4(1.0)).r < 0.0) {
            return vec4(light_sum + seen_light(p), 0.0);
        }
    }
    return vec4(light_sum, 1.0);
}

// The cascade above's light toward the four directions that refine
// direction texel d, blended between the four of its probes around pixel,
// each weighted by how near its surface, which its texels keep in alpha,
// lies to this probe's.
vec3 upper_light(vec2 pixel, ivec2 d, float depth)
{
    float spacing = cascade_upper.x;
    int side = int(cascade_upper.y);
    ivec2 last = ivec2(cascade_upper.zw) - 1;
    vec2 g = pixel / spacing - 0.5;
    ivec2 base = ivec2(floor(g));
    vec2 f = g - vec2(base);
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int c = 0; c < 4; c++) {
        ivec2 corner = ivec2(c & 1, c >> 1);
        ivec2 q = clamp(base + corner, ivec2(0), last);
        float w = (corner.x == 1 ? f.x : 1.0 - f.x) * (corner.y == 1 ? f.y : 1.0 - f.y);
        ivec2 t = q * side + d * 2;
        vec4 first = texelFetch(upper, t, 0);
        float apart = first.a == 0.0 ? 1e4 : (first.a - depth) / thickness;
        w *= 1.0 / (1.0 + apart * apart) + 1e-4;
        vec3 light = first.rgb + texelFetch(upper, t + ivec2(1, 0), 0).rgb +
                     texelFetch(upper, t + ivec2(0, 1), 0).rgb + texelFetch(upper, t + ivec2(1, 1), 0).rgb;
        sum += light * 0.25 * w;
        weights += w;
    }
    return sum / weights;
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    int side = int(cascade_probe.y);
    ivec2 probe = texel / side, d = texel - probe * side;
    vec2 pixel = probe_pixel(probe, cascade_probe.x);
    ivec2 at = clamp(ivec2(pixel), ivec2(0), frame_size() - 1);
    float depth = depth_at(at);
    if (depth >= 1.0) {
        result = vec4(0.0);
        return;
    }
    vec3 view = view_at(vec2(at) + 0.5, depth);
    float unit = probe_unit(vec2(at) + 0.5, view);
    thickness = THICKNESS * unit;
    vec3 normal = (cascade_to_world * vec4(view_normal(at, view), 0.0)).xyz;
    vec3 origin = (cascade_to_world * vec4(view, 1.0)).xyz + normal * LIFT * unit;
    // Each frame turns the ray to another point of its direction's cell and
    // shifts its samples, so the accumulated light covers the whole cell.
    offset = hash(uvec3(uvec2(texel), uint(cascade_jitter.x) + 104729u));
    vec3 direction = octahedral_direction(d, float(side), ray_jitter(texel));
    float t0 = cascade_ray.x * unit, t1 = cascade_ray.y * unit;
    vec4 light = cascade_ray.w > 1.5
                     ? trace_world(origin, direction, t0, t1, int(cascade_ray.z))
                     : trace_screen(origin, direction, t0, t1, int(cascade_ray.z));
    if (light.a > 0.0 && cascade_upper.x > 0.0) {
        light.rgb += light.a * upper_light(pixel, d, view.z);
    }
    result = vec4(light.rgb, view.z);
}
