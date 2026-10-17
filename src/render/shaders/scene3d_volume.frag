layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D volume_depth;
layout(set = 2, binding = 1) uniform sampler2DShadow volume_sun_map;
layout(set = 2, binding = 2) uniform sampler2DShadow volume_local_map;
layout(set = 3, binding = 0) uniform uniform_block {
    mat4 volume_rays;
    mat4 volume_sun_matrices[4];
    vec4 volume_eye;
    vec4 volume_frame;
    // Extinction in 1/world-unit, start, maximum distance, HG anisotropy.
    vec4 volume_medium;
    vec4 volume_albedo;
    vec4 volume_counts;
    vec4 volume_sun_direction;
    vec4 volume_sun_color;
    vec4 volume_sun_shadow;
    vec4 volume_local_shadow;
    vec4 volume_light_positions[32];
    vec4 volume_light_colors[32];
    vec4 volume_light_cones[32];
};

const float PI = 3.141592653589793;

vec3 eye_offset(vec2 uv, float depth)
{
    vec4 point = volume_rays * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1);
    return point.xyz / point.w;
}

float phase(float cosine)
{
    float g = volume_medium.w;
    float denominator = max(1.0 + g * g - 2.0 * g * cosine, 0.0001);
    return (1.0 - g * g) / (4.0 * PI * denominator * sqrt(denominator));
}

// Volume samples have no surface normal or receiver slope to lift. A small
// depth comparison bias avoids roundoff at a mesh without moving the sample
// through a nearby blocker. The atlas is the opaque/masked 3D shadow map.
float sun_visibility(vec3 position)
{
    float edge = volume_sun_shadow.y;
    for (int i = 0; i < 4; i++) {
        if (i >= int(volume_sun_shadow.x)) break;
        vec4 clip = volume_sun_matrices[i] * vec4(position, 1);
        clip.xyz /= clip.w;
        vec2 uv = vec2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
        if (any(lessThan(uv, vec2(edge))) || any(greaterThan(uv, vec2(1.0 - edge))) ||
            clip.z < 0.0 || clip.z >= 1.0) continue;
        vec2 tile = vec2(i & 1, i >> 1);
        return textureLod(volume_sun_map, vec3((tile + uv) * volume_sun_shadow.zw,
                                             clip.z - 0.00005), 0.0);
    }
    return 1.0;
}

float local_visibility(int light, vec3 position)
{
    if (light >= int(volume_local_shadow.x)) return 1.0;
    vec3 delta = position - volume_light_positions[light].xyz;
    vec4 cone = volume_light_cones[light];
    float axis_length = length(cone.xyz);
    vec3 forward, up;
    int face = 0;
    float spread = volume_local_shadow.y;
    if (axis_length > 0.0) {
        forward = cone.xyz / axis_length;
        up = abs(forward.y) > 0.99 ? vec3(0, 0, 1) : vec3(0, 1, 0);
        float cosine = clamp(-cone.w / axis_length, 0.0001, 1.0);
        spread *= sqrt(max(1.0 - cosine * cosine, 0.0)) / cosine;
    } else {
        vec3 a = abs(delta);
        int along = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
        face = 2 * along + (delta[along] >= 0.0 ? 0 : 1);
        forward = vec3(0);
        forward[along] = delta[along] >= 0.0 ? 1.0 : -1.0;
        up = along == 1 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    }
    float ahead = dot(delta, forward);
    float near_distance = volume_local_shadow.z * volume_light_positions[light].w;
    if (ahead <= near_distance) return 1.0;
    vec3 side = normalize(cross(forward, up));
    vec3 above = cross(side, forward);
    vec2 uv = vec2(dot(delta, side), -dot(delta, above)) / (ahead * spread) * 0.5 + 0.5;
    float face_texels = volume_local_shadow.w;
    uv = clamp(uv, 0.5 / face_texels, 1.0 - 0.5 / face_texels);
    float depth = (1.0 - near_distance / ahead) / (1.0 - volume_local_shadow.z);
    vec2 atlas = (vec2(face, light) + uv) * face_texels / vec2(textureSize(volume_local_map, 0));
    return textureLod(volume_local_map, vec3(atlas, depth - 0.00005), 0.0);
}

vec3 incoming(vec3 position, vec3 ray)
{
    vec3 light = vec3(0);
    if (dot(volume_sun_color.rgb, volume_sun_color.rgb) > 0.0) {
        vec3 toward_sun = normalize(volume_sun_direction.xyz);
        light += volume_sun_color.rgb * phase(dot(ray, toward_sun)) * sun_visibility(position);
    }
    for (int i = 0; i < 32; i++) {
        if (i >= int(volume_counts.y)) break;
        vec4 source = volume_light_positions[i];
        vec3 delta = source.xyz - position;
        float distance_squared = dot(delta, delta);
        if (source.w <= 0.0 || distance_squared >= source.w * source.w) continue;
        float distance = sqrt(max(distance_squared, 0.000001));
        vec3 toward_light = delta / distance;
        float edge = clamp(dot(-toward_light, volume_light_cones[i].xyz) + volume_light_cones[i].w, 0.0, 1.0);
        if (edge <= 0.0) continue;
        float fade = 1.0 - distance_squared / (source.w * source.w);
        vec3 radiance = volume_light_colors[i].rgb * volume_light_colors[i].w * fade * fade * edge * edge;
        // Match the surface pass's finite radius/cone falloff, then attenuate
        // the direct light's path through the same homogeneous medium.
        radiance *= exp(-volume_medium.x * distance) * local_visibility(i, position);
        light += radiance * phase(dot(ray, toward_light));
    }
    return light;
}

void main()
{
    vec2 uv = gl_FragCoord.xy / volume_frame.zw;
    ivec2 pixel = clamp(ivec2(uv * volume_frame.xy), ivec2(0), ivec2(volume_frame.xy) - 1);
    uv = (vec2(pixel) + 0.5) / volume_frame.xy;
    // Every full-resolution pixel center whose nearest upsample selects this
    // field texel contributes its depth. The nearest opaque surface bounds
    // the whole block, so fog behind it cannot cover a foreground silhouette.
    // Resolution >= 0.25 bounds each block to at most four pixels per axis.
    ivec2 cell = ivec2(gl_FragCoord.xy);
    vec2 ratio = volume_frame.xy / volume_frame.zw;
    ivec2 low = max(ivec2(ceil(vec2(cell) * ratio - 0.5)), ivec2(0));
    ivec2 high = min(ivec2(ceil(vec2(cell + 1) * ratio - 0.5)), ivec2(volume_frame.xy));
    float depth = 1.0;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            ivec2 sample_pixel = low + ivec2(x, y);
            if (all(lessThan(sample_pixel, high)))
                depth = min(depth, texelFetch(volume_depth, sample_pixel, 0).r);
        }
    }
    vec3 near_point = eye_offset(uv, 0.0);
    vec3 far_point = eye_offset(uv, 1.0);
    // Near-plane origins vary per orthographic pixel while the rays remain
    // parallel. Perspective rays follow the same unprojection contract.
    vec3 ray = normalize(far_point - near_point);
    float end_distance = min(volume_medium.z, length(eye_offset(uv, depth) - near_point));
    float start = volume_medium.y;
    if (end_distance <= start) {
        result = vec4(0);
        return;
    }
    int steps = int(volume_counts.x);
    float dt = (end_distance - start) / float(steps);
    float segment_transmittance = exp(-volume_medium.x * dt);
    vec3 origin = volume_eye.xyz + near_point;
    vec3 scattering = vec3(0);
    float transmittance = 1.0;
    for (int i = 0; i < 64; i++) {
        if (i >= steps || transmittance < 0.0001) break;
        vec3 position = origin + ray * (start + (float(i) + 0.5) * dt);
        // Analytic integration of a constant source within each segment:
        // sigma_s/sigma_t is the authored scattering albedo. Extinction is
        // therefore energy-consistent and independent of the step count.
        scattering += transmittance * (1.0 - segment_transmittance) *
                      volume_albedo.rgb * incoming(position, ray);
        transmittance *= segment_transmittance;
    }
    // Compute final extinction analytically even if the source loop ended
    // early, so distant surfaces receive the full depth-dependent attenuation.
    transmittance = exp(-volume_medium.x * (end_distance - start));
    result = vec4(clamp(scattering, vec3(0), vec3(65504)), 1.0 - transmittance);
}
