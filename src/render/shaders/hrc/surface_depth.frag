// Finite depth slabs with area-filtered emission. Aligned source tiles
// partition energy; real source positions stay separate from coverage.
layout(set = 2, binding = 0) uniform sampler2D u_albedo;
layout(set = 2, binding = 1) uniform sampler2D u_cover;
layout(set = 2, binding = 2) uniform sampler2D u_normal;
layout(set = 2, binding = 3) uniform sampler2D u_emission0;
layout(set = 2, binding = 4) uniform sampler2D u_emitter0;
layout(set = 2, binding = 5) uniform sampler2D u_emission1;
layout(set = 2, binding = 6) uniform sampler2D u_emitter1;
layout(set = 2, binding = 7) uniform sampler2D u_emission2;
layout(set = 2, binding = 8) uniform sampler2D u_emitter2;
layout(set = 2, binding = 9) uniform sampler2D u_emission3;
layout(set = 2, binding = 10) uniform sampler2D u_emitter3;
layout(set = 2, binding = 11) uniform sampler2D u_emission4;
layout(set = 2, binding = 12) uniform sampler2D u_emitter4;
layout(set = 2, binding = 13) uniform sampler2D u_emission5;
layout(set = 2, binding = 14) uniform sampler2D u_emitter5;
layout(set = 2, binding = 15) uniform sampler2D u_source;
layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec2 u_world_per_px;
    vec4 u_projection;
    float u_radius;
    float u_thickness;
    int u_block;
    int u_base_block;
};
const float PI = 3.141592653589793;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

bool inside(vec2 uv)
{
    return all(greaterThanEqual(uv, vec2(0))) && all(lessThan(uv, vec2(1)));
}
ivec2 pixel_at(vec2 uv)
{
    ivec2 size = textureSize(u_cover, 0);
    return clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
}
float projection_scale(float height)
{
    return u_projection.z > 0.0 ? max(1.0 - height / u_projection.z, 0.001) : 1.0;
}
vec3 position_at(vec2 uv, float height)
{
    vec2 pixel = uv * vec2(textureSize(u_cover, 0));
    vec2 plane = vec2(u_projection.x + pixel.x * u_world_per_px.x,
                      u_projection.y - pixel.y * u_world_per_px.y);
    // Invert x' = x*focal/(focal-height), with the camera at (0,0,focal).
    return vec3(plane * projection_scale(height), height);
}
vec2 geometry_at(vec2 uv)
{
    ivec2 p = pixel_at(uv);
    float cover = texelFetch(u_cover, p, 0).g;
    float height = cover > 1e-6 ? texelFetch(u_albedo, p, 0).a / cover : 0.0;
    return vec2(height, cover);
}
int sector(float angle)
{
    return clamp(int(floor((angle + 0.5 * PI) * (32.0 / PI))), 0, 31);
}
uint slab_mask(float lower, float upper)
{
    // Mark a sector only when its centre is inside the slab. Expanding every
    // touched bin falsely blocks rays grazing above or below a thin surface.
    float scale = 32.0 / PI;
    int first = max(int(ceil((min(lower, upper) + 0.5 * PI) * scale - 0.5)), 0);
    int last = min(int(floor((max(lower, upper) + 0.5 * PI) * scale - 0.5)), 31);
    if (first > last) return 0u;
    return (0xffffffffu << uint(first)) & (0xffffffffu >> uint(31 - last));
}
float visible(uint mask, vec3 delta)
{
    float angle = atan(delta.z, max(length(delta.xy), 1e-6));
    return (mask & (1u << uint(sector(angle)))) == 0u ? 1.0 : 0.0;
}


vec4 emission_at(int level, ivec2 p)
{
    if (level == 0) return texelFetch(u_emission0, p, 0);
    if (level == 1) return texelFetch(u_emission1, p, 0);
    if (level == 2) return texelFetch(u_emission2, p, 0);
    if (level == 3) return texelFetch(u_emission3, p, 0);
    if (level == 4) return texelFetch(u_emission4, p, 0);
    return texelFetch(u_emission5, p, 0);
}
vec4 emitter_at(int level, ivec2 p)
{
    if (level == 0) return texelFetch(u_emitter0, p, 0);
    if (level == 1) return texelFetch(u_emitter1, p, 0);
    if (level == 2) return texelFetch(u_emitter2, p, 0);
    if (level == 3) return texelFetch(u_emitter3, p, 0);
    if (level == 4) return texelFetch(u_emitter4, p, 0);
    return texelFetch(u_emitter5, p, 0);
}

vec3 receiver_origin;
vec3 receiver_normal;
float pixel_world;
float soft_distance;
float receiver_scale;
vec3 diffuse = vec3(0);
vec2 dominant = vec2(0);
float energy = 0.0;
vec2 receiver_uv;
float radial_step;
uint slab_prefix[96];
float sampling_rotation;

void receive(vec3 emission, vec4 emitter, float area)
{
    vec3 delta = position_at(emitter.xy, emitter.z) - receiver_origin;
    float distance2 = dot(delta, delta);
    if (distance2 <= pixel_world * pixel_world * 0.0625) return;
    vec3 direction = delta * inversesqrt(max(distance2, 1e-12));
    float scale = projection_scale(emitter.z) / receiver_scale;
    float weight = min(0.25, area * scale * scale /
        (PI * max(distance2 + soft_distance * soft_distance, 1e-6)));
    float projected_distance = length((emitter.xy - receiver_uv) *
        vec2(textureSize(u_cover, 0)) * u_world_per_px) * receiver_scale;
    int previous = int(ceil(max(projected_distance - pixel_world, 0.0) / radial_step - 0.5)) - 1;
    vec2 toward = (emitter.xy - receiver_uv) * vec2(textureSize(u_cover, 0)) * u_world_per_px;
    float azimuth = atan(-toward.y, toward.x) * (8.0 / (2.0 * PI)) - sampling_rotation;
    int ray = int(mod(floor(azimuth + 0.5), 8.0));
    uint mask = previous >= 0 ? slab_prefix[ray * 12 + min(previous, 11)] : 0u;
    // Keep the cutoff wider than source-position quantization at small radii.
    float taper = min(u_radius, max(u_radius * 0.15, pixel_world * 2.0));
    float falloff = 1.0 - smoothstep(u_radius - taper, u_radius, projected_distance);
    vec3 received = emission * weight * visible(mask, delta) * falloff;
    diffuse += received * max(dot(receiver_normal, direction), 0.0);
    float power = dot(received, LUMA);
    dominant += direction.xy * power;
    energy += power;
}

// At the finest level, a source crossing the radius taper must not move
// all its energy by one texel when its brightest pixel changes. Integrate the
// actual covered pixels in that small cell; fractional coverage stays smooth.
void source_pixels(ivec2 cell, float area)
{
    ivec2 size = textureSize(u_cover, 0);
    for (int y = 0; y < u_base_block; y++) {
        for (int x = 0; x < u_base_block; x++) {
            ivec2 p = cell * u_base_block + ivec2(x, y);
            if (any(greaterThanEqual(p, size))) continue;
            vec2 cover = texelFetch(u_cover, p, 0).rg;
            vec3 light = max(texelFetch(u_source, p, 0).rgb, vec3(0)) * max(cover.r, 0.0);
            if (dot(light, LUMA) <= 0.0) continue;
            float height = cover.g > 1e-6 ? texelFetch(u_albedo, p, 0).a / cover.g : 0.0;
            receive(light, vec4((vec2(p) + 0.5) / vec2(size), height, 0), area);
        }
    }
}

void source_cell(int level, ivec2 p, float area)
{
    int block = u_base_block << level;
    ivec2 size = (textureSize(u_cover, 0) + block - 1) / block;
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) return;
    vec4 light = emission_at(level, p);
    if (light.a <= 0.0) return;
    vec4 emitter = emitter_at(level, p);
    float distance = length((emitter.xy - receiver_uv) * vec2(textureSize(u_cover, 0)) * u_world_per_px) * receiver_scale;
    float taper = min(u_radius, max(u_radius * 0.15, pixel_world * 2.0));
    float cell_extent = float(block) * pixel_world * 1.4142135624;
    if (level == 0 && distance + cell_extent > u_radius - taper) {
        source_pixels(p, area / float(block * block));
        return;
    }
    // One bounded refinement retains separate depths in mixed cells. Even the
    // coarse path uses a real source position, never an averaged phantom depth.
    if (level > 0 && emitter.w > max(u_thickness, pixel_world * 4.0)) {
        ivec2 child_size = (textureSize(u_cover, 0) + block / 2 - 1) / (block / 2);
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 2; x++) {
                ivec2 child = p * 2 + ivec2(x, y);
                if (any(greaterThanEqual(child, child_size))) continue;
                vec4 part = emission_at(level - 1, child);
                if (part.a > 0.0) receive(part.rgb / part.a, emitter_at(level - 1, child), area * 0.25 * part.a);
            }
        }
        return;
    }
    // Coverage already includes zero padding in partial viewport edge tiles.
    receive(light.rgb / light.a, emitter, area * light.a);
}

void gather_sources(vec2 uv)
{
    vec2 size = vec2(textureSize(u_cover, 0));
    vec2 radius = u_radius / (u_world_per_px * receiver_scale);
    vec2 lower = max(uv * size - radius, vec2(0));
    vec2 upper = min(uv * size + radius, size);
    float extent = max(upper.x - lower.x, upper.y - lower.y);
    int level = 0;
    int block = u_base_block;
    while (level < 5 && extent > float(block * 8)) {
        level++;
        block *= 2;
    }
    ivec2 first = ivec2(floor(lower / float(block)));
    ivec2 last = ivec2(ceil(upper / float(block))) - 1;
    float area = float(block * block) * u_world_per_px.x * u_world_per_px.y * receiver_scale * receiver_scale;
    // Every tile contributes once. The clipped box spans at most nine cells
    // on each axis, independent of the number of emitters in those cells.
    for (int y = 0; y < 9; y++) {
        for (int x = 0; x < 9; x++) {
            ivec2 p = first + ivec2(x, y);
            if (any(greaterThan(p, last))) continue;
            source_cell(level, p, area);
        }
    }
}

void main()
{
    ivec2 size = textureSize(u_cover, 0);
    vec2 requested = gl_FragCoord.xy * float(u_block) / vec2(size);
    ivec2 receiver_pixel = pixel_at(requested);
    // Use an actual surface texel for receiver geometry; no depth averaging.
    vec2 uv = (vec2(receiver_pixel) + 0.5) / vec2(size);
    vec2 receiver = geometry_at(uv);
    float valid = receiver.y > 0.5 ? 1.0 : 0.0;
    vec2 tilt = valid > 0.5 ? texelFetch(u_normal, receiver_pixel, 0).xy / receiver.y : vec2(0);
    vec3 normal = normalize(vec3(tilt, sqrt(max(1.0 - dot(tilt, tilt), 0.0))));
    if (valid < 0.5 || u_radius <= 0.0) {
        out_q0 = vec4(0, 0, 0, receiver.x);
        out_q1 = vec4(0, 0, 0, 1);
        out_q2 = vec4(normal, 0);
        out_q3 = vec4(0, 0, 1, 1);
        return;
    }
    receiver_origin = position_at(uv, receiver.x);
    receiver_normal = normal;
    receiver_scale = projection_scale(receiver.x);
    radial_step = u_radius / 12.0;
    receiver_uv = uv;
    pixel_world = max(u_world_per_px.x, u_world_per_px.y) * receiver_scale;
    soft_distance = max(pixel_world, max(u_thickness, 0.0) * 0.25);
    float blocked = 0.0;
    // Stable interleaved rotation distributes directions across neighboring
    // receivers; spatial reconstruction combines them without temporal trails.
    sampling_rotation = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715))));
    for (int azimuth = 0; azimuth < 8; azimuth++) {
        float angle = (float(azimuth) + sampling_rotation) * (2.0 * PI / 8.0);
        vec2 axis = vec2(cos(angle), sin(angle));
        vec2 screen_axis = vec2(axis.x, -axis.y);
        uint mask = 0u;
        // Build radial prefix visibility first. A filtered footprint may find a
        // source nearer or farther than its centre, so use its actual distance.
        for (int radial = 0; radial < 12; radial++) {
            float radius = (float(radial) + 0.5) * radial_step;
            vec2 center = uv + screen_axis * radius / (u_world_per_px * vec2(size) * receiver_scale);
            if (inside(center)) {
                vec2 obstacle = geometry_at(center);
                if (obstacle.y > 0.5 && u_thickness > 0.0 &&
                    texelFetch(u_source, pixel_at(center), 0).a > 0.01) {
                    vec3 top = position_at(center, obstacle.x) - receiver_origin;
                    vec3 bottom = position_at(center, obstacle.x - u_thickness) - receiver_origin;
                    float upper = atan(top.z, max(length(top.xy), 1e-6));
                    float lower = atan(bottom.z, max(length(bottom.xy), 1e-6));
                    mask |= slab_mask(lower, upper);
                }
            }
            slab_prefix[azimuth * 12 + radial] = mask;
        }
        for (int bit = 16; bit < 32; bit++) {
            if ((mask & (1u << uint(bit))) != 0u) blocked += 1.0;
        }
    }
    gather_sources(uv);
    // Area weights integrate emission; do not renormalize by accepted samples.
    out_q0 = vec4(min(diffuse, vec3(65504)), receiver.x);
    out_q1 = vec4(0, 0, 0, 1);
    out_q2 = vec4(normal, valid);
    out_q3 = vec4(dominant / max(energy, 1e-8), 1.0 - blocked / 128.0, 1);
}
