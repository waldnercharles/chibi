// Traces this frame's rays from every probe through the scene's solids and
// lights where they land: the solid's emission, plus its albedo times the
// sun and lights that reach it unblocked and the probes' light from last
// frame, which carries light one bounce further each frame. A ray that
// reaches no solid sees the sky, or the ambient colors.
#include "scene3d_gi_pass.shd"

// Each solid's row, in its BVH leaf's order: its world-to-shape rows, its
// albedo and primitive, its emission and whether it is a mesh's box, the
// world sphere around it, and the edges of the box around the drawn solid,
// each with its dot product with the box's center.
layout(set = 2, binding = 1) uniform sampler2D gi_solids;
// The BVH over the solids, two texels a node: its box's low corner, then
// for an inner node the node of its second child, the first following it,
// or for a leaf its first solid; and its box's high corner, then for a leaf
// its count of solids, or 0.
layout(set = 2, binding = 2) uniform sampler2D gi_nodes;

const float PI = 3.141592653589793;
// A solid's texels in its row of gi_solids.
const int SHAPE_ROWS = 0;
const int ALBEDO = 3;
const int EMISSION = 4;
const int SPHERE = 5;
const int BOX_EDGES = 6;
// The BVH's nodes in a row of gi_nodes, and the deepest it goes, as
// scene3d_gi.c builds it.
const int NODES_PER_ROW = 64;
const int STACK = 32;
// What a ray starting inside a solid reports: no light, at almost no
// distance, negative so that the update counts it.
const float INSIDE = -0.0001;
// Geometry3D primitives (runtime/components.h).
const int BOX = 0;
const int BALL = 1;
const int CAPSULE = 2;
const int CONE = 3;
const int PRISM = 4;
const int OCTAHEDRON = 5;
const int TETRAHEDRON = 6;
const int CHEVRON = 7;
const int TRIANGLE = 10;
// Each convex shape's faces as planes n.p <= w in its unit space
// (render/scene3d_geometry.c); the chevron's is its hull.
const vec4 PRISM_PLANES[5] = vec4[5](vec4(0, 1, 0, 0.5), vec4(0, -1, 0, 0.5), vec4(-1, 0, 0, 0.5),
                                     vec4(0.5, 0, -1, 0.25), vec4(0.5, 0, 1, 0.25));
const vec4 OCTAHEDRON_PLANES[8] = vec4[8](vec4(1, 1, 1, 0.5), vec4(1, 1, -1, 0.5),
                                          vec4(1, -1, 1, 0.5), vec4(1, -1, -1, 0.5),
                                          vec4(-1, 1, 1, 0.5), vec4(-1, 1, -1, 0.5),
                                          vec4(-1, -1, 1, 0.5), vec4(-1, -1, -1, 0.5));
const vec4 TETRAHEDRON_PLANES[4] = vec4[4](vec4(-1, 0, 0, 0.5), vec4(0, -1, 0, 0.5),
                                           vec4(0, 0, -1, 0.5), vec4(1, 1, 1, -0.5));
const vec4 CHEVRON_PLANES[5] = vec4[5](vec4(-1, 0, 0, 0.5), vec4(1, 2, 0, 0.5), vec4(1, -2, 0, 0.5),
                                       vec4(1, 0, 2, 0.5), vec4(1, 0, -2, 0.5));
// The capsule's radius and the half length of its axis, in the space where
// its caps are round.
const float CAPSULE_RADIUS = 0.25;
const float CAPSULE_HALF = 0.25;

vec4 solid_texel(int solid, int texel)
{
    return texelFetch(gi_solids, ivec2(texel, solid), 0);
}

int convex_planes(int primitive)
{
    if (primitive == PRISM) return 5;
    if (primitive == OCTAHEDRON) return 8;
    if (primitive == TETRAHEDRON) return 4;
    return 5;
}

vec4 convex_plane(int primitive, int i)
{
    if (primitive == PRISM) return PRISM_PLANES[i];
    if (primitive == OCTAHEDRON) return OCTAHEDRON_PLANES[i];
    if (primitive == TETRAHEDRON) return TETRAHEDRON_PLANES[i];
    return CHEVRON_PLANES[i];
}

// Each test takes a ray in a shape's unit space with a unit direction and
// returns where it enters the shape, with the normal there, or -1. A ray
// starting inside returns 0 and sets inside.

bool box_inside(vec3 o)
{
    return all(lessThan(abs(o), vec3(0.5)));
}

float box_hit(vec3 o, vec3 d, out vec3 normal)
{
    // Signs that are never zero keep rays along a face's plane finite.
    vec3 s = vec3(gi_sign(d.x), gi_sign(d.y), gi_sign(d.z));
    vec3 inverse = s / max(abs(d), vec3(1e-12));
    vec3 near = (-0.5 * s - o) * inverse;
    vec3 far = (0.5 * s - o) * inverse;
    float enter = max(max(near.x, near.y), near.z);
    float leave = min(min(far.x, far.y), far.z);
    if (enter > leave || enter < 0.0) return -1.0;
    normal = -s * step(near.yzx, near.xyz) * step(near.zxy, near.xyz);
    return enter;
}

float ball_hit(vec3 o, vec3 d, vec3 center, float radius, out vec3 normal)
{
    vec3 offset = o - center;
    float b = dot(offset, d);
    float h = b * b - dot(offset, offset) + radius * radius;
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    if (t < 0.0) return -1.0;
    normal = offset + d * t;
    return t;
}

// The nearer of a hit and a candidate, keeping its normal.
float nearer(float t, float candidate, vec3 candidate_normal, inout vec3 normal)
{
    if (candidate < 0.0 || (t >= 0.0 && t <= candidate)) return t;
    normal = candidate_normal;
    return candidate;
}

bool capsule_inside(vec3 o)
{
    vec3 axis = vec3(0.0, clamp(o.y, -CAPSULE_HALF, CAPSULE_HALF), 0.0);
    return length(o - axis) < CAPSULE_RADIUS;
}

// The capsule is the union of its two balls and the cylinder between them,
// so it begins where the first of them does.
float capsule_hit(vec3 o, vec3 d, out vec3 normal)
{
    vec3 n;
    float t = -1.0;
    float side = dot(d.xz, d.xz);
    if (side > 1e-12) {
        float b = dot(o.xz, d.xz);
        float h = b * b - side * (dot(o.xz, o.xz) - CAPSULE_RADIUS * CAPSULE_RADIUS);
        if (h >= 0.0) {
            float candidate = (-b - sqrt(h)) / side;
            vec3 p = o + d * candidate;
            if (candidate >= 0.0 && abs(p.y) <= CAPSULE_HALF) {
                t = candidate;
                normal = vec3(p.x, 0.0, p.z);
            }
        }
    }
    float top = ball_hit(o, d, vec3(0.0, CAPSULE_HALF, 0.0), CAPSULE_RADIUS, n);
    t = nearer(t, top, n, normal);
    float bottom = ball_hit(o, d, vec3(0.0, -CAPSULE_HALF, 0.0), CAPSULE_RADIUS, n);
    return nearer(t, bottom, n, normal);
}

// The cone narrows from a unit-diameter base at y = -0.5 to its tip at 0.5.
bool cone_inside(vec3 o)
{
    return abs(o.y) < 0.5 && length(o.xz) < 0.5 * (0.5 - o.y);
}

float cone_hit(vec3 o, vec3 d, out vec3 normal)
{
    float t = -1.0;
    // x^2 + z^2 = k^2 (0.5 - y)^2, with the base's radius over the height.
    float k2 = 0.25;
    float a = dot(d.xz, d.xz) - k2 * d.y * d.y;
    float b = dot(o.xz, d.xz) + k2 * (0.5 - o.y) * d.y;
    float c = dot(o.xz, o.xz) - k2 * (0.5 - o.y) * (0.5 - o.y);
    if (abs(a) > 1e-12) {
        float h = b * b - a * c;
        if (h >= 0.0) {
            float root = sqrt(h);
            for (int i = 0; i < 2; i++) {
                float candidate = (-b + (i == 0 ? -root : root)) / a;
                vec3 p = o + d * candidate;
                if (candidate >= 0.0 && abs(p.y) <= 0.5 && (t < 0.0 || candidate < t)) {
                    t = candidate;
                    normal = vec3(p.x, k2 * (0.5 - p.y), p.z);
                }
            }
        }
    }
    if (abs(d.y) > 1e-12) {
        float candidate = (-0.5 - o.y) / d.y;
        vec3 p = o + d * candidate;
        t = nearer(t, dot(p.xz, p.xz) <= 0.25 ? candidate : -1.0, vec3(0.0, -1.0, 0.0), normal);
    }
    return t;
}

bool convex_inside(vec3 o, int primitive)
{
    for (int i = 0; i < convex_planes(primitive); i++) {
        vec4 plane = convex_plane(primitive, i);
        if (dot(plane.xyz, o) >= plane.w) return false;
    }
    return true;
}

// The ray enters the last of the faces it enters before leaving any.
float convex_hit(vec3 o, vec3 d, int primitive, out vec3 normal)
{
    float enter = -1e30, leave = 1e30;
    for (int i = 0; i < convex_planes(primitive); i++) {
        vec4 plane = convex_plane(primitive, i);
        float toward = dot(plane.xyz, d);
        float room = plane.w - dot(plane.xyz, o);
        if (abs(toward) < 1e-12) {
            if (room < 0.0) return -1.0;
            continue;
        }
        float t = room / toward;
        if (toward < 0.0 && t > enter) {
            enter = t;
            normal = plane.xyz;
        }
        if (toward > 0.0) leave = min(leave, t);
    }
    if (enter > leave || enter < 0.0) return -1.0;
    return enter;
}

// The flat triangle in its plane y = 0, seen from either side.
float triangle_hit(vec3 o, vec3 d, out vec3 normal)
{
    if (abs(d.y) < 1e-12) return -1.0;
    float t = -o.y / d.y;
    vec3 p = o + d * t;
    // Inside its edges from (2/3, 0) to (-1/3, 1/2) and (-1/3, -1/2).
    if (t < 0.0 || p.x < -1.0 / 3.0 || p.x + 2.0 * abs(p.z) > 2.0 / 3.0) return -1.0;
    normal = vec3(0.0, -gi_sign(d.y), 0.0);
    return t;
}

bool shape_inside(vec3 o, int primitive)
{
    if (primitive == BOX) return box_inside(o);
    if (primitive == BALL) return length(o) < 0.5;
    if (primitive == CAPSULE) return capsule_inside(o);
    if (primitive == CONE) return cone_inside(o);
    if (primitive == TRIANGLE) return false;
    return convex_inside(o, primitive);
}

float shape_hit(vec3 o, vec3 d, int primitive, out vec3 normal)
{
    if (primitive == BOX) return box_hit(o, d, normal);
    if (primitive == BALL) return ball_hit(o, d, vec3(0.0), 0.5, normal);
    if (primitive == CAPSULE) return capsule_hit(o, d, normal);
    if (primitive == CONE) return cone_hit(o, d, normal);
    if (primitive == TRIANGLE) return triangle_hit(o, d, normal);
    return convex_hit(o, d, primitive, normal);
}

// Whether a ray from origin along unit direction meets solid i nearer than
// best: where, and its world normal facing back along the ray; or that the
// ray starts inside it. A mesh's box stands in for the mesh only from
// outside, as the mesh within it may be open, so rays starting inside it
// pass through it.
bool solid_hit(int i, vec3 origin, vec3 direction, float best, out float t, out vec3 normal, out bool inside)
{
    vec4 sphere = solid_texel(i, SPHERE);
    vec3 offset = sphere.xyz - origin;
    float along = dot(offset, direction);
    float apart = dot(offset, offset) - along * along;
    if (apart > sphere.w * sphere.w) return false;
    float half_chord = sqrt(sphere.w * sphere.w - apart);
    if (along + half_chord < 0.0 || along - half_chord > best) return false;
    vec4 row0 = solid_texel(i, SHAPE_ROWS), row1 = solid_texel(i, SHAPE_ROWS + 1), row2 = solid_texel(i, SHAPE_ROWS + 2);
    vec3 o = vec3(dot(row0.xyz, origin) + row0.w, dot(row1.xyz, origin) + row1.w, dot(row2.xyz, origin) + row2.w);
    vec3 d = vec3(dot(row0.xyz, direction), dot(row1.xyz, direction), dot(row2.xyz, direction));
    // Distances along the unit shape's direction shrink by its length in the
    // world.
    float stretch = length(d);
    d /= stretch;
    int primitive = int(solid_texel(i, ALBEDO).w);
    inside = shape_inside(o, primitive);
    if (inside && solid_texel(i, EMISSION).w > 0.0) return false;
    vec3 n = vec3(0.0, 1.0, 0.0);
    float shape_t = inside ? 0.0 : shape_hit(o, d, primitive, n);
    if (shape_t < 0.0 || shape_t / stretch >= best) return false;
    t = shape_t / stretch;
    // Normals go back to the world through the shape rows' transpose.
    normal = normalize(n.x * row0.xyz + n.y * row1.xyz + n.z * row2.xyz);
    if (dot(normal, direction) > 0.0) normal = -normal;
    return true;
}

// Whether the box around drawn solid i holds a world point, along each of
// its edges within half the edge of its center, as the lit pass tests for a
// light (render_scene3d_light_shadow_near).
bool around(int i, vec3 p)
{
    for (int axis = 0; axis < 3; axis++) {
        vec4 edge = solid_texel(i, BOX_EDGES + axis);
        if (abs(dot(p, edge.xyz) - edge.w) > 0.5 * dot(edge.xyz, edge.xyz)) return false;
    }
    return true;
}

vec4 node_texel(int node, int texel)
{
    return texelFetch(gi_nodes, ivec2(node % NODES_PER_ROW * 2 + texel, node / NODES_PER_ROW), 0);
}

// Whether a ray from origin, with its direction's inverse, passes through a
// box before far.
bool reaches(vec3 low, vec3 high, vec3 origin, vec3 inverse, float far)
{
    vec3 a = (low - origin) * inverse, b = (high - origin) * inverse;
    vec3 near = min(a, b), away = max(a, b);
    float enter = max(max(near.x, near.y), near.z);
    float leave = min(min(away.x, away.y), away.z);
    return enter <= leave && leave >= 0.0 && enter <= far;
}

// The solid a ray meets first within reach, or -1, through the BVH. A
// shadow ray toward a light at reach stops at any solid but those whose
// drawn box is around the light, which cast no shadow from it in the lit
// pass either, as a bulb or a lantern's frame.
int trace(vec3 origin, vec3 direction, float reach, bool shadow, out float t, out vec3 normal, out bool inside)
{
    int found = -1;
    t = reach;
    inside = false;
    normal = -direction;
    vec3 s = vec3(gi_sign(direction.x), gi_sign(direction.y), gi_sign(direction.z));
    vec3 inverse = s / max(abs(direction), vec3(1e-12));
    vec3 light = origin + direction * reach;
    int stack[STACK];
    int depth = 0, node = 0;
    for (int visit = 0; visit < int(gi_trace.y); visit++) {
        vec4 low = node_texel(node, 0), high = node_texel(node, 1);
        int count = int(high.w);
        if (reaches(low.xyz, high.xyz, origin, inverse, t)) {
            if (count == 0) {
                stack[depth++] = int(low.w);
                node++;
                continue;
            }
            for (int i = int(low.w); i < int(low.w) + count; i++) {
                float candidate;
                vec3 n;
                bool within;
                if (!solid_hit(i, origin, direction, t, candidate, n, within)) continue;
                if (shadow) {
                    if (!around(i, light)) return i;
                    continue;
                }
                found = i;
                t = candidate;
                normal = n;
                inside = within;
            }
        }
        if (depth == 0) break;
        node = stack[--depth];
    }
    return found;
}

bool blocked(vec3 origin, vec3 direction, float reach)
{
    float t;
    vec3 normal;
    bool inside;
    return trace(origin, direction, reach, true, t, normal, inside) >= 0;
}

// Where a ray reaches nothing: the light the lit pass's sky or ambient colors
// give off that way, so that open surfaces take the same light from the
// probes as without them. The ambient colors are a sky above the horizon and
// ground below it, whose irradiance is the lit pass's blend. The sky's
// radiance is its irradiance's harmonics, each band divided by the factor
// the cosine scaled it by (Ramamoorthi and Hanrahan 2001): pi, 2pi/3 and
// pi/4.
vec3 sky_radiance(vec3 d)
{
    if (gi_sky_light.x <= 0.0) return d.y >= 0.0 ? gi_ambient_sky.rgb : gi_ambient_ground.rgb;
    float c = gi_sky_light.z, s = gi_sky_light.w;
    vec3 n = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    vec3 zeroth_band = gi_sky_irradiance[0].rgb;
    vec3 linear = gi_sky_irradiance[1].rgb * n.x + gi_sky_irradiance[2].rgb * n.y +
        gi_sky_irradiance[3].rgb * n.z;
    vec3 quadratic = gi_sky_irradiance[4].rgb * (n.x * n.y) + gi_sky_irradiance[5].rgb * (n.y * n.z) +
        gi_sky_irradiance[6].rgb * (3.0 * n.y * n.y - 1.0) + gi_sky_irradiance[7].rgb * (n.x * n.z) +
        gi_sky_irradiance[8].rgb * (n.x * n.x - n.z * n.z);
    vec3 radiance = zeroth_band / PI + linear * (1.5 / PI) + quadratic * (4.0 / PI);
    return max(radiance, vec3(0.0)) * gi_sky_light.x;
}

// The irradiance from the sun and lights at p facing n, with the lit pass's
// falloffs, where no solid blocks those that cast shadows.
vec3 direct_irradiance(vec3 p, vec3 n)
{
    vec3 start = p + n * (0.01 * gi_probes[0].w);
    vec3 sum = vec3(0.0);
    float sun = dot(n, gi_sun_direction.xyz);
    if (dot(gi_sun_color.rgb, gi_sun_color.rgb) > 0.0 && sun > 0.0 &&
        (gi_light_count.z == 0.0 || !blocked(start, gi_sun_direction.xyz, gi_trace.z))) {
        sum += gi_sun_color.rgb * sun;
    }
    for (int i = 0; i < int(gi_light_count.x); i++) {
        vec3 delta = gi_light_positions[i].xyz - p;
        float radius = gi_light_positions[i].w;
        float distance_squared = dot(delta, delta);
        if (distance_squared >= radius * radius || dot(n, delta) <= 0.0) continue;
        float fade = 1.0 - distance_squared / (radius * radius);
        vec3 light = normalize(delta);
        float edge = clamp(dot(-light, gi_light_cones[i].xyz) + gi_light_cones[i].w, 0.0, 1.0);
        if (edge <= 0.0) continue;
        vec3 toward = gi_light_positions[i].xyz - start;
        if (i < int(gi_light_count.y) && blocked(start, normalize(toward), length(toward))) continue;
        sum += gi_light_colors[i].rgb * gi_light_colors[i].w * fade * fade * edge * edge * dot(n, light);
    }
    return sum;
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec3 counts = gi_probe_counts();
    int rays = int(gi_trace.x);
    int probe = texel.y * (GI_RAY_ROW / rays) + texel.x / rays;
    if (probe >= counts.x * counts.y * counts.z) {
        result = vec4(0.0);
        return;
    }
    vec3 origin = gi_probe_at(probe);
    vec3 direction = gi_ray(texel.x % rays);
    float t;
    vec3 normal;
    bool inside;
    int solid = trace(origin, direction, gi_trace.z, false, t, normal, inside);
    // A miss reaches past every depth that counts.
    if (solid < 0) {
        result = vec4(sky_radiance(direction), 1.0);
        return;
    }
    // A probe inside a solid sees only its inside, which the update counts.
    if (inside) {
        result = vec4(0.0, 0.0, 0.0, INSIDE);
        return;
    }
    vec3 p = origin + direction * t;
    vec3 albedo = solid_texel(solid, ALBEDO).rgb;
    vec3 emission = solid_texel(solid, EMISSION).rgb;
    // Last frame's probes light the hit, where they hold light around it.
    vec4 bounce = scene_gi_grid[1].w > 0.0 ? gi_irradiance(p, normal, -direction) : vec4(0.0);
    vec3 light = emission + albedo * (direct_irradiance(p, normal) / PI + bounce.rgb);
    result = vec4(light, t / gi_probes[1].w);
}
