// Finite depth slabs with area-filtered emission. Aligned source tiles
// partition energy; bounded child estimates keep distinct sources separate.
// q0 holds the light a closed receiver takes at HRC's scale; see receive. q3
// holds the light HRC brings an open receiver from the lights it sees, its
// open sky share and how much Depth changes that light.
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
    float u_specular;
    int u_block;
    int u_base_block;
    int u_source_axis;
    int u_near_detail;
    float u_shadow_softness;
    int u_visibility_mode;
    float u_relief;
    // HRC's distance falloff per canvas pixel.
    float u_falloff;
};
#include "hrc/brdf.shd"
#include "hrc/depth_shading.shd"

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
// A covered pixel blocks light unless it is a light that absorbs nothing, so
// props that let HRC's light pass still shadow by their height.
bool occludes(vec2 uv)
{
    vec4 source = texelFetch(u_source, pixel_at(uv), 0);
    return source.a > 0.01 || dot(source.rgb, LUMA) <= 0.0;
}
// HRC's optical depth per canvas pixel, which dims the light it brings past.
float absorption_at(vec2 uv)
{
    return max(texelFetch(u_source, pixel_at(uv), 0).a, 0.0);
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
float receiver_gloss;
float pixel_world;
float soft_distance;
float receiver_scale;
// The light HRC would bring a closed receiver from the lights it sees.
vec3 closed = vec3(0);
vec3 specular = vec3(0);
// The light HRC brings an open receiver from the lights Depth sees, and how
// much Depth changes it, at HRC's scale.
float brought = 0.0;
float shading = 0.0;
float flat_elevation;
vec2 receiver_uv;
float radial_step;
uint slab_prefix[96];
float sampling_rotation;
float tile_visibility;
float tile_transmittance;
// HRC's optical depth along each ray up to each radial step.
float depth_prefix[96];
// Past this optical depth HRC brings nothing worth a segment walk.
const float OPAQUE_DEPTH = 10.0;

// Softness fades a shadow's edge within the bin that blocks, from where the
// hard shadow ends to the blocked bin's centre, so it never shadows what the
// hard shadow leaves lit. A flat floor's own slab ends at the horizon, which
// a softened edge would otherwise let half-shadow a light on the floor.
float mask_visibility(uint mask, float elevation)
{
    int bin = clamp(int(floor(elevation + 0.5)), 0, 31);
    float hard = (mask & (1u << uint(bin))) == 0u ? 1.0 : 0.0;
    if (u_shadow_softness <= 0.0) return hard;
    int lower = clamp(int(floor(elevation)), 0, 31);
    int upper = clamp(int(floor(elevation)) + 1, 0, 31);
    float a = (mask & (1u << uint(lower))) == 0u ? 1.0 : 0.0;
    float b = (mask & (1u << uint(upper))) == 0u ? 1.0 : 0.0;
    float f = fract(elevation);
    float soft = a == b ? a : min(2.0 * (a < b ? f : 1.0 - f), 1.0);
    return mix(hard, soft, u_shadow_softness);
}

// The last radial step of the masks before a light `radial` steps away.
int step_before(float radial)
{
    return min(int(ceil(radial)) - 1, 11);
}

float ray_visibility(int ray, float radial, float elevation)
{
    ray = int(mod(float(ray), 8.0));
    int previous = step_before(radial);
    uint mask = previous >= 0 ? slab_prefix[ray * 12 + previous] : 0u;
    float hard = mask_visibility(mask, elevation);
    if (u_shadow_softness <= 0.0) return hard;
    int a = int(floor(radial - 0.5));
    int b = a + 1;
    uint lower = a >= 0 ? slab_prefix[ray * 12 + min(a, 11)] : 0u;
    uint upper = b >= 0 ? slab_prefix[ray * 12 + min(b, 11)] : 0u;
    float filtered = mix(mask_visibility(lower, elevation), mask_visibility(upper, elevation), fract(radial - 0.5));
    return mix(hard, filtered, u_shadow_softness);
}

float source_visibility(vec3 delta, float radial, float azimuth)
{
    float elevation = (atan(delta.z, max(length(delta.xy), 1e-6)) + 0.5 * PI) * (32.0 / PI) - 0.5;
    float hard = ray_visibility(int(floor(azimuth + 0.5)), radial, elevation);
    if (u_shadow_softness <= 0.0) return hard;
    int ray = int(floor(azimuth));
    float filtered = mix(ray_visibility(ray, radial, elevation),
                         ray_visibility(ray + 1, radial, elevation), fract(azimuth));
    return mix(hard, filtered, u_shadow_softness);
}

// The share of a light HRC brings along the ray its masks use: the
// transmittance of the steps before it.
float hrc_transmittance(float radial, float azimuth)
{
    int ray = int(mod(floor(azimuth + 0.5), 8.0));
    int previous = step_before(radial);
    return previous >= 0 ? exp(-depth_prefix[ray * 12 + previous]) : 1.0;
}

// A single source-tile query is shared by all its refined energy estimates,
// and so is the share of the tile's light HRC brings: the transmittance of
// the way there. The walk covers the complete segment with a fixed quality
// budget. When the footprint exceeds that budget this is explicit
// undersampling, never an unfinished walk declared clear. Screen-space slabs
// cannot see hidden layers.
float segment_visibility(vec4 emitter)
{
    vec2 size = vec2(textureSize(u_cover, 0));
    vec2 pixels = (emitter.xy - receiver_uv) * size;
    float extent = max(abs(pixels.x), abs(pixels.y));
    int budget = u_source_axis <= 4 ? 32 : (u_source_axis <= 6 ? 48 : 64);
    int steps = min(max(int(ceil(extent)), 1), budget);
    float visible = 1.0, depth = 0.0;
    float step_pixels = length(pixels) / float(steps);
    for (int i = 0; i < 64; i++) {
        if (i >= steps) break;
        float t = (float(i) + 0.5) / float(steps);
        // Avoid testing the receiver and emitter's own rasterized footprint.
        if (t * extent < 1.0 || (1.0 - t) * extent < 1.0) continue;
        vec2 uv = mix(receiver_uv, emitter.xy, t);
        if (!inside(uv)) continue;
        depth += absorption_at(uv) * step_pixels;
        vec2 obstacle = geometry_at(uv);
        if (obstacle.y <= 0.5 || !occludes(uv)) continue;
        float height = mix(receiver_origin.z, emitter.z, t);
        if (u_projection.z > 0.0) {
            float q0 = 1.0 / max(u_projection.z - receiver_origin.z, 0.001);
            float q1 = 1.0 / max(u_projection.z - emitter.z, 0.001);
            height = u_projection.z - 1.0 / mix(q0, q1, t);
        }
        float bias = max(pixel_world * 0.05, 0.001);
        float interior = min(obstacle.x - height, height - (obstacle.x - u_thickness));
        float hard = interior > bias ? 0.0 : 1.0;
        float soft = 1.0 - smoothstep(bias, bias + pixel_world, interior);
        visible = min(visible, mix(hard, soft, u_shadow_softness));
        if (visible <= 0.0 && depth >= OPAQUE_DEPTH) break;
    }
    tile_transmittance = exp(-depth);
    return visible;
}

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
    vec2 toward = (emitter.xy - receiver_uv) * vec2(textureSize(u_cover, 0)) * u_world_per_px;
    float azimuth = atan(-toward.y, toward.x) * (8.0 / (2.0 * PI)) - sampling_rotation;
    // Keep the cutoff wider than source-position quantization at small radii.
    float taper = min(u_radius, max(u_radius * 0.15, pixel_world * 2.0));
    float falloff = 1.0 - smoothstep(u_radius - taper, u_radius, projected_distance);
    float radial = max(projected_distance - pixel_world, 0.0) / radial_step - 0.5;
    float visibility = (u_visibility_mode == 1 ? tile_visibility
        : source_visibility(delta, radial, azimuth)) * falloff;
    // HRC spreads light in the plane: emission E over A canvas pixels lights
    // open space s pixels away by E A / (2 pi s), dimmed by its falloff.
    float pixels = area / (u_world_per_px.x * u_world_per_px.y * receiver_scale * receiver_scale);
    float planar = max(length(toward / u_world_per_px), 0.5 * sqrt(pixels));
    float spread = pixels / (2.0 * PI * planar) * exp(-u_falloff * planar);
    // A flat receiver keeps HRC's light from any light it sees. Facing a light
    // takes more of it, up to the steepest relief flat_elevation allows, and
    // facing away or a blocked view takes less. Lights fading out at the
    // radius leave their light as it was. Each light changes as much of
    // HRC's light as HRC brings from it, so one behind a solid that absorbs,
    // which HRC stops, changes almost nothing.
    float facing = max(1.0 + (dot(receiver_normal, direction) - direction.z) /
                                 max(direction.z, flat_elevation), 0.0);
    float transmittance = u_visibility_mode == 1 ? tile_transmittance
                                                 : hrc_transmittance(radial, azimuth);
    float light = dot(emission, LUMA) * spread * transmittance;
    brought += light;
    shading += light * (visibility * facing - falloff);
    if (visibility <= 0.0) return;
    // A closed receiver takes HRC's light by Lambert's law relative to a flat
    // one, so a light at flat_elevation or above lights a flat receiver as
    // HRC lights open space, and one below its plane lights nothing.
    closed += emission * spread * visibility *
              max(dot(receiver_normal, direction), 0.0) / max(direction.z, flat_elevation);
    vec3 received = emission * weight * visibility;
    if (receiver_gloss > 0.0) {
        specular += received * PI * highlight(receiver_normal, direction, 1.0 - receiver_gloss);
    }
}

// At the finest level, a source crossing the radius taper must not move
// all its energy by one texel when its brightest pixel changes. Integrate the
// actual covered pixels in that small cell; fractional coverage stays smooth.
void source_pixels(ivec2 cell, float area, float fraction)
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
            receive(light * fraction, vec4((vec2(p) + 0.5) / vec2(size), height, 0), area);
        }
    }
}

// Evaluate leaf pixels explicitly, so tiny sources retain their position and
// integrated power even when several fit into the first pyramid cell.
void source_value(int level, ivec2 p, float area, float fraction)
{
    if (fraction <= 0.0) return;
    int block = u_base_block << level;
    ivec2 size = (textureSize(u_cover, 0) + block - 1) / block;
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) return;
    vec4 light = emission_at(level, p);
    if (light.a <= 0.0) return;
    if (level == 0) {
        // The fractional estimate weight must be applied to emission, outside
        // receive's area clamp, so blending estimates never changes energy.
        source_pixels(p, area / float(block * block), fraction);
        return;
    }
    receive(light.rgb * fraction / light.a, emitter_at(level, p), area * light.a);
}

void source_children(int level, ivec2 p, float area, float fraction)
{
    if (fraction <= 0.0) return;
    if (level == 0) {
        source_value(level, p, area, fraction);
        return;
    }
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            source_value(level - 1, p * 2 + ivec2(x, y), area * 0.25, fraction);
        }
    }
}

void source_estimate(int level, ivec2 p, float area, float fraction)
{
    if (fraction <= 0.0) return;
    int block = u_base_block << level;
    ivec2 size = (textureSize(u_cover, 0) + block - 1) / block;
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) return;
    vec4 light = emission_at(level, p);
    if (light.a <= 0.0) return;
    if (level == 0) {
        source_value(level, p, area, fraction);
        return;
    }
    vec4 emitter = emitter_at(level, p);
    float distance = length(position_at(emitter.xy, emitter.z) - receiver_origin);
    float extent = float(block) * pixel_world * 1.4142135624;
    float depth_scale = max(u_thickness, pixel_world * 4.0);
    float depth_detail = smoothstep(depth_scale * 0.5, depth_scale * 1.5, emitter.w);
    float power = max(dot(light.rgb, LUMA), 1e-12);
    vec3 chroma = light.rgb / power;
    float variation = 0.0;
    ivec2 child_size = (textureSize(u_cover, 0) + block / 2 - 1) / (block / 2);
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            ivec2 child = p * 2 + ivec2(x, y);
            if (any(greaterThanEqual(child, child_size))) continue;
            vec3 rgb = emission_at(level - 1, child).rgb;
            float child_power = dot(rgb, LUMA);
            variation += length(rgb - chroma * child_power) / (4.0 * power);
        }
    }
    float angular_detail = smoothstep(0.15, 0.7, extent / max(distance, pixel_world));
    float color_detail = smoothstep(0.15, 0.8, variation) * angular_detail;
    float detail = max(depth_detail, color_detail);
    if (u_near_detail != 0) detail = max(detail, angular_detail);
    source_value(level, p, area, fraction * (1.0 - detail));
    source_children(level, p, area, fraction * detail);
}

void source_cell(int level, ivec2 p, float area, float lod_blend)
{
    source_estimate(level, p, area, 1.0 - lod_blend);
    if (level == 0 || lod_blend <= 0.0) return;
    // Blend complete estimates, including their refinement. At the boundary
    // both sides evaluate exactly the same child cells and preserve energy.
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            source_estimate(level - 1, p * 2 + ivec2(x, y), area * 0.25, lod_blend);
        }
    }
}

void gather_sources(vec2 uv)
{
    vec2 size = vec2(textureSize(u_cover, 0));
    vec2 radius = u_radius / (u_world_per_px * receiver_scale);
    vec2 lower = max(uv * size - radius, vec2(0));
    vec2 upper = min(uv * size + radius, size);
    // Use the unclipped footprint for LOD. Passing a viewport edge must not
    // change the representation of every light still inside the viewport.
    float extent = min(max(radius.x, radius.y) * 2.0, max(size.x, size.y));
    // Segment visibility belongs to a tile, including all its child estimates.
    // Fix that tile grouping to the canvas so receiver height/radius cannot
    // switch the shared visibility at an integer LOD boundary. Several lights
    // in the same tile still share one representative visibility estimate.
    if (u_visibility_mode == 1) extent = max(size.x, size.y);
    float lod = max(log2(max(extent / float(u_base_block * u_source_axis), 1.0)), 0.0);
    int level = min(int(ceil(lod)), 5);
    int block = u_base_block << level;
    float blend = level > 0 ? 1.0 - smoothstep(float(level - 1), float(level), lod) : 0.0;
    ivec2 first = ivec2(floor(lower / float(block)));
    ivec2 last = ivec2(ceil(upper / float(block))) - 1;
    float area = float(block * block) * u_world_per_px.x * u_world_per_px.y * receiver_scale * receiver_scale;
    // At most (axis + 1)^2 tiles; each has a fixed refinement limit. More
    // emitters add source raster work but never lengthen this gather's loops.
    for (int y = 0; y < 9; y++) {
        for (int x = 0; x < 9; x++) {
            ivec2 p = first + ivec2(x, y);
            if (any(greaterThan(p, last))) continue;
            if (u_visibility_mode == 1) {
                vec4 light = emission_at(level, p);
                if (light.a <= 0.0) continue;
                vec4 emitter = emitter_at(level, p);
                tile_visibility = segment_visibility(emitter);
            }
            source_cell(level, p, area, blend);
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
        out_q3 = vec4(0, 0, 1, 0);
        return;
    }
    receiver_origin = position_at(uv, receiver.x);
    receiver_normal = normal;
    receiver_gloss = u_specular > 0.0
        ? clamp(texelFetch(u_normal, receiver_pixel, 0).w / receiver.y, 0.0, 1.0) : 0.0;
    receiver_scale = projection_scale(receiver.x);
    flat_elevation = inversesqrt(1.0 + u_relief * u_relief);
    radial_step = u_radius / 12.0;
    receiver_uv = uv;
    pixel_world = max(u_world_per_px.x, u_world_per_px.y) * receiver_scale;
    soft_distance = max(pixel_world, max(u_thickness, 0.0) * 0.25);
    // Ambient light from above the plane reaches the receiver past the slabs
    // around it: the cosine-weighted share of each azimuth's upper elevations
    // that no slab blocks.
    float sky = 0.0, sky_total = 0.0;
    // Stable interleaved rotation distributes directions across neighboring
    // receivers; spatial reconstruction combines them without temporal trails.
    sampling_rotation = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715))));
    for (int azimuth = 0; azimuth < 8; azimuth++) {
        float angle = (float(azimuth) + sampling_rotation) * (2.0 * PI / 8.0);
        vec2 axis = vec2(cos(angle), sin(angle));
        vec2 screen_axis = vec2(axis.x, -axis.y);
        uint mask = 0u;
        float depth = 0.0;
        float step_pixels = radial_step * length(screen_axis / u_world_per_px) / receiver_scale;
        // Build radial prefix visibility first. A filtered footprint may find a
        // source nearer or farther than its centre, so use its actual distance.
        for (int radial = 0; radial < 12; radial++) {
            float radius = (float(radial) + 0.5) * radial_step;
            vec2 center = uv + screen_axis * radius / (u_world_per_px * vec2(size) * receiver_scale);
            if (inside(center)) {
                depth += absorption_at(center) * step_pixels;
                vec2 obstacle = geometry_at(center);
                if (obstacle.y > 0.5 && u_thickness > 0.0 && occludes(center)) {
                    vec3 top = position_at(center, obstacle.x) - receiver_origin;
                    vec3 bottom = position_at(center, obstacle.x - u_thickness) - receiver_origin;
                    float upper = atan(top.z, max(length(top.xy), 1e-6));
                    float lower = atan(bottom.z, max(length(bottom.xy), 1e-6));
                    mask |= slab_mask(lower, upper);
                }
            }
            slab_prefix[azimuth * 12 + radial] = mask;
            depth_prefix[azimuth * 12 + radial] = depth;
        }
        for (int bit = 16; bit < 32; bit++) {
            // The sine of twice the bin's elevation follows its solid angle
            // times its cosine to the plane's normal.
            float weight = sin((float(bit) - 15.5) * (PI / 16.0));
            sky_total += weight;
            if ((mask & (1u << uint(bit))) == 0u) sky += weight;
        }
    }
    gather_sources(uv);
    // Area weights integrate emission; do not renormalize by accepted samples.
    out_q0 = vec4(min(closed, vec3(65504)), receiver.x);
    out_q1 = vec4(min(specular, vec3(65504)), receiver_gloss);
    out_q2 = vec4(normal, valid);
    out_q3 = depth_pack_shading(vec4(brought, 0, sky / sky_total, shading));
}
