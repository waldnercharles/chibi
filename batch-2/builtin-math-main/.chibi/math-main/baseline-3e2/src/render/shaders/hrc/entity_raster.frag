// Adapted from the user-supplied Neon-Swarm shaders. Original shader equations retained.
// Entity tile rasterization pass.
//
// A full-screen fragment pass. Each pixel iterates its 16x16 tile's entity
// list, transforms to entity local space, evaluates the shape SDF, and blends
// the results into render targets, which the GPU compresses:
//   scene:  premultiplied color, A coverage
//   source: RGB emission, A optical depth (mean absorption) for the transport
//   albedo: RGB albedo, A material height times coverage; the coverage is
//           scene's alpha, which the same over-recurrence accumulates
//   normal: see render_material_textures
//   visibility: zero; fluid adds it
//   cover: R, the scale that returns source's emission to display's
// Later batches start from the previous batch's targets.

#include "entity.shd"
#ifdef SG_MESH_LAYER
layout(set = 2, binding = 5) uniform sampler2D u_mesh_scene;
layout(set = 2, binding = 6) uniform sampler2D u_mesh_emission;
layout(set = 2, binding = 7) uniform sampler2D u_mesh_absorption;
layout(set = 2, binding = 8) uniform sampler2D u_mesh_albedo;
layout(set = 2, binding = 9) uniform sampler2D u_mesh_normal;
#endif
#ifndef SG_ENTITY_BINDING
#define SG_PREVIOUS_SCENE_BINDING 0
#define SG_PREVIOUS_SOURCE_BINDING 1
#define SG_PREVIOUS_ALBEDO_BINDING 2
#define SG_PREVIOUS_NORMAL_BINDING 3
#define SG_PREVIOUS_COVER_BINDING 4
#define SG_ENTITY_BINDING 0
#define SG_COUNTS_BINDING 1
#define SG_INDICES_BINDING 2
#define SG_PARAMETERS_BINDING 3
#endif
#ifndef SG_GRAPHS
#define SG_OUTPUTS 9
void sg_dispatch_2d(int id,vec2 p,vec2 wp,vec2 half_size,vec4 object_frame,float time,vec4 values[32],out vec4 result[SG_OUTPUTS]) {
 for(int i=0;i<SG_OUTPUTS;i++) result[i]=vec4(0);
}
vec4 sg_image_sample(int id,vec2 uv) { return vec4(0); }
layout(set = 3, binding = 0) uniform uniform_block {
    float u_world_min_x;
    float u_world_max_y;
    vec2 u_unit_per_px;
    vec4 u_plane_right;
    vec4 u_plane_down;
    vec4 u_plane_target;
    int u_tiles_x;
    int u_has_previous;
    int u_first_entity;
    int u_last_entity;
    int u_metaball_group;
};
#endif
// GLSL ES emulates each storage buffer as texel fetches of a single vec4 or
// uvec4 array, so entities and tile lists are read through uvec4 arrays.
layout(set = 2, binding = SG_ENTITY_BINDING) readonly buffer EntityBuf { uvec4 entity_data[]; };
layout(set = 2, binding = SG_COUNTS_BINDING) readonly buffer TileOffsets { uvec4 tile_offsets[]; };
layout(set = 2, binding = SG_INDICES_BINDING) readonly buffer TileEntities { uvec4 tile_entities[]; };
layout(set = 2, binding = SG_PARAMETERS_BINDING) readonly buffer ShaderParameters { vec4 shader_parameters[]; };

#define ENTITY_VEC4S 19u

uint tile_offset(uint i) { return tile_offsets[i >> 2u][i & 3u]; }
uint tile_entity(uint i) { return tile_entities[i >> 2u][i & 3u]; }
vec4 entity_vec4(uint i) { return uintBitsToFloat(entity_data[i]); }

Entity entity_at(uint index)
{
    uint b = index * ENTITY_VEC4S;
    Entity e;
    e.inv_rot = entity_vec4(b);
    vec4 v = entity_vec4(b + 1u);
    e.pos = v.xy;
    e.extent = v.zw;
    uvec4 u = entity_data[b + 2u];
    e.shape_type = u.x;
    e.radius = uintBitsToFloat(u.y);
    e.thickness = uintBitsToFloat(u.z);
    e.relief = uintBitsToFloat(u.w);
    u = entity_data[b + 3u];
    e.aa = uintBitsToFloat(u.x);
    e.point_count = u.y;
    e.metaball_group = u.z;
    e.metaball_blend = uintBitsToFloat(u.w);
    v = entity_vec4(b + 4u);
    e.border_thickness = v.x;
    e.reserved0 = v.y;
    e.reserved1 = v.z;
    e.reserved2 = v.w;
    e.points_01 = entity_vec4(b + 5u);
    e.points_23 = entity_vec4(b + 6u);
    e.points_45 = entity_vec4(b + 7u);
    e.points_67 = entity_vec4(b + 8u);
    e.scene = entity_vec4(b + 9u);
    e.emission = entity_vec4(b + 10u);
    e.absorption = entity_vec4(b + 11u);
    e.albedo = entity_vec4(b + 12u);
    e.border_scene = entity_vec4(b + 13u);
    e.border_emission = entity_vec4(b + 14u);
    e.border_absorption = entity_vec4(b + 15u);
    e.border_albedo = entity_vec4(b + 16u);
    v = entity_vec4(b + 17u);
    e.simulation_time = v.xyz;
    e.tile_group = v.w;
    e.material_frame = entity_vec4(b + 18u);
    return e;
}

#ifdef SG_ACCUMULATE
layout(set = 2, binding = SG_PREVIOUS_SCENE_BINDING) uniform sampler2D u_prev_scene;
layout(set = 2, binding = SG_PREVIOUS_SOURCE_BINDING) uniform sampler2D u_prev_source;
layout(set = 2, binding = SG_PREVIOUS_ALBEDO_BINDING) uniform sampler2D u_prev_albedo;
layout(set = 2, binding = SG_PREVIOUS_NORMAL_BINDING) uniform sampler2D u_prev_normal;
layout(set = 2, binding = SG_PREVIOUS_COVER_BINDING) uniform sampler2D u_prev_cover;
#endif

layout(location = 0) out vec4 out_scene;
layout(location = 1) out vec4 out_source;
layout(location = 2) out vec4 out_albedo;
layout(location = 3) out vec4 out_normal;
layout(location = 4) out vec4 out_visibility;
layout(location = 5) out vec4 out_cover;

// Tile lists are already ordered by stable CPU sort and deterministic binning.

#include "sdf.shd"
#include "noise.shd"

float sdf_polyline(vec2 p, Entity e)
{
    int count = int(e.point_count);
    if (count < 3) count = 3;
    if (count > 8) count = 8;

    // Extract points.
    vec2 pts[8];
    pts[0] = e.points_01.xy;
    pts[1] = e.points_01.zw;
    pts[2] = e.points_23.xy;
    pts[3] = e.points_23.zw;
    pts[4] = e.points_45.xy;
    pts[5] = e.points_45.zw;
    pts[6] = e.points_67.xy;
    pts[7] = e.points_67.zw;

    // Min distance to any closed polyline segment.
    float min_d = 1e30;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        // Manually index to avoid dynamic indexing issues.
        vec2 a, b;
        // Use ternary chain for compile-time indexable access.
        a = (i == 0) ? pts[0] : (i == 1) ? pts[1] : (i == 2) ? pts[2] : (i == 3) ? pts[3] :
            (i == 4) ? pts[4] : (i == 5) ? pts[5] : (i == 6) ? pts[6] : pts[7];
        b = (j == 0) ? pts[0] : (j == 1) ? pts[1] : (j == 2) ? pts[2] : (j == 3) ? pts[3] :
            (j == 4) ? pts[4] : (j == 5) ? pts[5] : (j == 6) ? pts[6] : pts[7];
        float d = sdf_segment(p, a, b);
        min_d = min(min_d, d);
    }
    return min_d - e.thickness * 0.5;
}

float sdf_polygon_filled(vec2 p, Entity e)
{
    int count = int(e.point_count);
    if (count < 3) count = 3;
    if (count > 8) count = 8;

    // Extract points.
    vec2 pts[8];
    pts[0] = e.points_01.xy;
    pts[1] = e.points_01.zw;
    pts[2] = e.points_23.xy;
    pts[3] = e.points_23.zw;
    pts[4] = e.points_45.xy;
    pts[5] = e.points_45.zw;
    pts[6] = e.points_67.xy;
    pts[7] = e.points_67.zw;

    // Winding-number SDF for filled polygon.
    float min_d = 1e30;
    float winding = 0.0;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        vec2 a = (i == 0) ? pts[0] : (i == 1) ? pts[1] : (i == 2) ? pts[2] : (i == 3) ? pts[3] :
                 (i == 4) ? pts[4] : (i == 5) ? pts[5] : (i == 6) ? pts[6] : pts[7];
        vec2 b = (j == 0) ? pts[0] : (j == 1) ? pts[1] : (j == 2) ? pts[2] : (j == 3) ? pts[3] :
                 (j == 4) ? pts[4] : (j == 5) ? pts[5] : (j == 6) ? pts[6] : pts[7];

        vec2 edge = b - a;
        vec2 pa = p - a;
        float t = clamp(dot(pa, edge) / max(dot(edge, edge), 1e-20), 0.0, 1.0);
        float d = length(pa - edge * t);
        min_d = min(min_d, d);

        // Winding number contribution.
        bvec3 cond = bvec3(a.y <= p.y, b.y > p.y, edge.x * pa.y - edge.y * pa.x > 0.0);
        if (all(cond)) winding += 1.0;
        cond = bvec3(a.y > p.y, b.y <= p.y, edge.x * pa.y - edge.y * pa.x < 0.0);
        if (all(cond)) winding -= 1.0;
    }
    return (winding != 0.0) ? -min_d : min_d;
}

// Where a material samples the world: the laid-out frame when the entity
// carries one, so patterns stay attached to moving fragments.
vec2 material_position(Entity e, vec2 local_p, vec2 world_pos)
{
    vec4 f = e.material_frame;
    if (f.z == 0.0 && f.w == 0.0) return world_pos;
    return f.xy + vec2(f.z * local_p.x - f.w * local_p.y, f.w * local_p.x + f.z * local_p.y);
}

vec2 shader_half_size(Entity e)
{
    if (e.shape_type == SHAPE_CIRCLE)
        return vec2(e.radius + e.thickness * 0.5);
    if (e.shape_type == SHAPE_DIAMOND)
        return vec2(e.radius + e.thickness * 0.5);
    if (e.shape_type == SHAPE_BOX || e.shape_type == SHAPE_SPRITE)
        return abs(e.points_01.xy) + e.thickness * 0.5;
    vec2 bounds = max(abs(e.points_01.xy), abs(e.points_01.zw));
    if (e.shape_type == SHAPE_CAPSULE) return bounds + e.radius + e.thickness * 0.5;
    if (e.shape_type == SHAPE_LINE) return bounds + e.thickness * 0.5;
    vec2 points[6] = vec2[6](e.points_23.xy,e.points_23.zw,e.points_45.xy,
                            e.points_45.zw,e.points_67.xy,e.points_67.zw);
    int count = e.shape_type == SHAPE_TRI ? 3 : clamp(int(e.point_count),3,8);
    for (int i = 2; i < count; i++) bounds = max(bounds, abs(points[i-2]));
    return bounds + e.thickness * 0.5;
}

float evaluate_sdf(vec2 local_p, Entity e)
{
    uint shape = e.shape_type;
    float d;

    // LINE and POLY: inherently stroked, thickness built into SDF.
    if (shape == SHAPE_LINE) {
        return sdf_line(local_p, e.points_01.xy, e.points_01.zw, e.thickness);
    }
    if (shape == SHAPE_POLY) {
        return sdf_polyline(local_p, e);
    }

    // All other shapes: compute filled SDF, then optionally stroke.
    if (shape == SHAPE_CIRCLE) {
        d = sdf_circle(local_p, e.radius);
    } else if (shape == SHAPE_BOX || shape == SHAPE_SPRITE) {
        d = sdf_box(local_p, abs(e.points_01.xy));
        if (e.points_23.w > 0.0) d = max(d, dot(e.points_23.xy, local_p) - e.points_23.z);
    } else if (shape == SHAPE_CAPSULE) {
        d = sdf_capsule(local_p, e.points_01.xy, e.points_01.zw, e.radius);
    } else if (shape == SHAPE_TRI) {
        d = sdf_triangle(local_p, e.points_01.xy, e.points_01.zw, e.points_23.xy);
    } else if (shape == SHAPE_DIAMOND) {
        d = sdf_diamond(local_p, e.radius);
    } else if (shape == SHAPE_POLY_FILL) {
        d = sdf_polygon_filled(local_p, e);
    } else {
        d = sdf_circle(local_p, e.radius);
    }

    // Unified stroke: thickness > 0 converts filled shape to outline.
    if (e.thickness > 0.0)
        d = abs(d) - e.thickness * 0.5;

    return d;
}

// The sprite's image uv under a point inside its box. points_45 is the
// frame's uv rect. The top clamp keeps an edge pixel inside the frame rather
// than on its neighbour.
vec2 sprite_uv(vec2 local_p, Entity e)
{
    vec2 t = vec2(local_p.x, -local_p.y) / (2.0 * abs(e.points_01.xy)) + 0.5;
    t = clamp(t, vec2(0.0), vec2(0.9999));
    return mix(e.points_45.xy, e.points_45.zw, t);
}

// The sprite's texel; points_67.x is its image variant.
vec4 sprite_texel(vec2 local_p, Entity e)
{
    return sg_image_sample(int(e.points_67.x), sprite_uv(local_p, e));
}

#include "color.shd"

// The sprite's tilt, and its height map's scale of its relief. points_67.y is
// its normal map variant, whose red is image right and green image up, and
// points_67.z its height map variant. A mirrored frame mirrors its normals,
// which the sprite's rotation turns into world axes. A normal facing away
// keeps a maximal tilt, as a material's does.
void sprite_relief(vec2 local_p, Entity e, inout vec4 surface, inout float height)
{
    vec2 uv = sprite_uv(local_p, e);
    if (e.points_67.y > 0.0) {
        vec3 n = sg_image_sample(int(e.points_67.y), uv).rgb * 2.0 - 1.0;
        if (e.points_45.z < e.points_45.x) n.x = -n.x;
        vec2 tilt = vec2(e.inv_rot.x * n.x + e.inv_rot.z * n.y,
                         e.inv_rot.y * n.x + e.inv_rot.w * n.y);
        float n2 = dot(n, n);
        tilt = n2 > 1e-8 ? tilt * inversesqrt(n2) : vec2(0.0);
        float t2 = dot(tilt, tilt);
        if (n.z <= 0.0 && t2 > 1e-8) tilt *= 0.999 * inversesqrt(t2);
        surface.xy = tilt;
    }
    if (e.points_67.z > 0.0) height *= sg_image_sample(int(e.points_67.z), uv).r;
}

vec4 unpack_color(vec4 p) { return p; }

// Ordinary authored shapes live on world Y=0. Mesh resolve coordinates stay
// in the camera plane and never use this inverse mapping.
vec2 plane_pixel;
float plane_height;
bool plane_position(vec2 screen, out vec3 hit)
{
    vec3 right = u_plane_right.xyz, down = u_plane_down.xyz;
    vec3 up = cross(down, right), target = u_plane_target.xyz;
    vec3 offset = right * (screen.x - target.x) + down * (-screen.y - target.z);
    float focal = u_plane_right.w;
    if (focal > 0.0) {
        vec3 eye = target + up * focal;
        vec3 ray = offset - up * focal;
        if (abs(ray.y) < 1e-5) return false;
        float t = -eye.y / ray.y;
        if (t * focal < 1.0) return false;
        hit = eye + ray * t;
    } else {
        if (abs(up.y) < 1e-5) return false;
        vec3 origin = target + offset;
        hit = origin - up * (origin.y / up.y);
    }
    return !any(isnan(hit)) && !any(isinf(hit)) && all(lessThan(abs(hit),vec3(1e20)));
}

void plane_surface(inout vec4 surface, inout float height)
{
    if (u_plane_target.w == 0.0) return;
    vec3 right = u_plane_right.xyz, down = u_plane_down.xyz;
    vec3 up = cross(down, right);
    vec2 tilt = surface.xy;
    vec3 normal = normalize(vec3(tilt.x, sqrt(max(1.0-dot(tilt,tilt), 1e-6)), -tilt.y));
    surface.xy = vec2(dot(normal,right), -dot(normal,down));
    float t2 = dot(surface.xy,surface.xy);
    if (dot(normal,up) <= 0.0 && t2 > 1e-8) surface.xy *= 0.999 * inversesqrt(t2);
    height = clamp(plane_height + height * up.y, -65504.0, 65504.0);
}

float circle_emission_half_width(Entity e)
{
    float max_pixel = max(plane_pixel.x, plane_pixel.y);
    return max(1.5 * max_pixel, max(e.aa, 0.0) * 0.5);
}

float circle_emission_coverage(float sdf, float half_width)
{
    return 1.0 - smoothstep(-half_width, half_width, sdf);
}

// Area of the rectangle [lo, hi] where dot(n, q) <= d, for a box's cut.
// Corners' clamped squared distances give the exact area by inclusion and
// exclusion.
float cut_area(vec2 lo, vec2 hi, vec2 n, float d)
{
    // Mirror so both normal components are positive.
    vec2 m = vec2(n.x < 0.0 ? -1.0 : 1.0, n.y < 0.0 ? -1.0 : 1.0);
    vec2 a = min(lo * m, hi * m), b = max(lo * m, hi * m);
    n = max(abs(n), vec2(1e-3));
    vec4 f = max(vec4(d) - vec4(dot(n, a), dot(n, vec2(b.x, a.y)), dot(n, vec2(a.x, b.y)), dot(n, b)), 0.0);
    f *= f;
    return max((f.x - f.y - f.z + f.w) / (2.0 * n.x * n.y), 0.0);
}

// The fraction of a square footprint of half size r around local point p
// that a tile group box, cut or not, covers. Boxes tiling a shape cover each
// footprint exactly once between them.
float footprint_coverage(Entity e, vec2 p, float r)
{
    vec2 h = abs(e.points_01.xy);
    vec2 lo = max(p - r, -h), hi = min(p + r, h);
    if (any(lessThanEqual(hi, lo))) return 0.0;
    float area = e.points_23.w > 0.0 ? cut_area(lo, hi, e.points_23.xy, e.points_23.z)
                                     : (hi.x - lo.x) * (hi.y - lo.y);
    return clamp(area / (4.0 * r * r), 0.0, 1.0);
}

// Material accumulated at this pixel. Emission and albedo alpha never reach
// their color. Absorption blends under earlier absorbers by its own alpha;
// only its mean, the optical depth, is kept.
vec4 scene_acc;
vec4 emission_acc;
// The part of emission_acc from absorbing layers. The transport reads it as
// radiance and the rest as density; see hrc/source_average.shd.
vec3 absorber_emission;
vec4 albedo_acc;
vec4 normal_acc;
float tau_acc;
float absorption_cover;
float relief_acc;
float relief_cover;
float mesh_cover;

void emission_over(vec4 em, bool absorbs)
{
    absorber_emission = (absorbs ? em.rgb : vec3(0.0)) + absorber_emission * (1.0 - em.a);
    emission_acc = alpha_over(em, emission_acc);
}

// Composites one layer of premultiplied channels over the accumulated material.
void layer_over(vec4 sc, vec4 em, vec4 tr, vec4 al, vec4 surface, float height)
{
    scene_acc = alpha_over(sc, scene_acc);
    normal_acc = mix(normal_acc, surface, sc.a);
    relief_acc = mix(relief_acc, height, sc.a);
    relief_cover = sc.a + relief_cover * (1.0 - sc.a);
    mesh_cover = (u_plane_target.w != 0.0 ? sc.a : 0.0) + mesh_cover * (1.0 - sc.a);
    emission_over(em, tr.a > 0.0);
    tau_acc += dot(tr.rgb, vec3(1.0 / 3.0)) * (1.0 - absorption_cover);
    absorption_cover += tr.a * (1.0 - absorption_cover);
    albedo_acc = alpha_over(al, albedo_acc);
}

// A layer of premultiplied channels and the footprint coverage behind them.
// Surface and height are weighted by scene alpha until it is composited.
struct Layer
{
    vec4 scene;
    vec4 emission;
    vec4 absorption;
    vec4 albedo;
    vec4 surface;
    float height;
    float cover;
};

Layer layer_empty()
{
    return Layer(vec4(0), vec4(0), vec4(0), vec4(0), vec4(0), 0.0, 0.0);
}

Layer layer_scaled(Layer l, float k)
{
    return Layer(l.scene * k, l.emission * k, l.absorption * k, l.albedo * k,
                 l.surface * k, l.height * k, l.cover * k);
}

Layer layer_sum(Layer a, Layer b)
{
    return Layer(a.scene + b.scene, a.emission + b.emission, a.absorption + b.absorption,
                 a.albedo + b.albedo, a.surface + b.surface, a.height + b.height,
                 a.cover + b.cover);
}

// Pieces that overlap by float error still cover a footprint once.
// Pieces that overlap by float error still cover a footprint once.
void layer_composite(Layer l)
{
    l = layer_scaled(l, 1.0 / max(l.cover, 1.0));
    float a = max(l.scene.a, 1e-6);
    layer_over(l.scene, l.emission, l.absorption, l.albedo, l.surface / a, l.height / a);
}

// The pieces of a tile group composite as one layer: their footprint
// coverages add, so edges they share stay covered wherever the shape sits
// against the pixels. Layering each piece would dim those edges by an amount
// that follows the shape's subpixel position, which flickers as it moves.
//
// A group whose id has a half, a mask's border, comes before the group with
// its whole part, the mask's fill, and lies over it. Both share the
// silhouette, so the border hides as much fill as it can: fill pieces add
// only where they cover more than the border. Layering them independently
// would count partly covered silhouette pixels twice.
float group_id;
Layer group_layer;
float border_cover;

void group_flush()
{
    if (group_id == 0.0) return;
    layer_composite(group_layer);
    group_id = 0.0;
    group_layer = layer_empty();
}

void group_add(float id, float cover, vec4 sc, vec4 em, vec4 tr, vec4 al, vec4 surface, float height)
{
    if (id != group_id) {
        bool fill = fract(group_id) > 0.0 && id == floor(group_id);
        border_cover = fill ? group_layer.cover : 0.0;
        if (!fill) group_flush();
        group_id = id;
    }
    float k = border_cover > 0.0 ? max(cover - border_cover, 0.0) / max(cover, 1e-6) : 1.0;
    Layer piece = Layer(sc, em, tr, al, surface * sc.a, height * sc.a, cover);
    group_layer = layer_sum(group_layer, layer_scaled(piece, k));
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 tile_id = pixel / 16;
    uint tile_idx = uint(tile_id.y * u_tiles_x + tile_id.x);
    uint tile_begin = tile_offset(tile_idx);
    uint tile_end = tile_offset(tile_idx + 1u);

    // Pixel center -> world position (Y flipped: pixel 0 = world max Y).
    vec2 world_pos = vec2(
        u_world_min_x + (float(pixel.x) + 0.5) * u_unit_per_px.x,
        u_world_max_y - (float(pixel.y) + 0.5) * u_unit_per_px.y
    );

    plane_pixel = u_unit_per_px;
    plane_height = 0.0;
    bool plane_valid = true;
    if (u_plane_target.w != 0.0) {
        vec3 hit, hx, hy;
        plane_valid = plane_position(world_pos, hit);
        if (plane_valid) {
            // Compute the authored-space pixel footprint before divergent work.
            bool vx = plane_position(world_pos + vec2(u_unit_per_px.x,0.0), hx);
            bool vy = plane_position(world_pos - vec2(0.0,u_unit_per_px.y), hy);
            if (vx) plane_pixel.x = length(hx.xz-hit.xz);
            if (vy) plane_pixel.y = length(hy.xz-hit.xz);
            plane_height = dot(hit-u_plane_target.xyz, cross(u_plane_down.xyz,u_plane_right.xyz));
            world_pos = vec2(hit.x,-hit.z);
        }
    }

    // Tile entity list.
    uint total = plane_valid ? tile_end - tile_begin : 0u;
    uint base = tile_begin;

    scene_acc = vec4(0);
    emission_acc = vec4(0, 0, 0, 1);
    absorber_emission = vec3(0.0);
    tau_acc = 0.0;
    absorption_cover = 0.0;
    albedo_acc = vec4(0, 0, 0, 1);
    normal_acc = vec4(0);
    relief_acc = 0.0;
    relief_cover = 0.0;
    mesh_cover = 0.0;
    group_id = 0.0;
    border_cover = 0.0;
    group_layer = layer_empty();
#ifdef SG_ACCUMULATE
    // Earlier batches continue as displayed emission, absorbing wherever they
    // absorbed, over the whole pixel.
    // A leading mesh batch has no previous material layer.
    if (u_has_previous != 0) {
        scene_acc = texelFetch(u_prev_scene, pixel, 0);
        vec4 prev_source = texelFetch(u_prev_source, pixel, 0);
        emission_acc.rgb = prev_source.rgb * texelFetch(u_prev_cover, pixel, 0).r;
        absorber_emission = prev_source.a > 0.0 ? emission_acc.rgb : vec3(0.0);
        absorption_cover = prev_source.a > 0.0 ? 1.0 : 0.0;
        tau_acc = prev_source.a;
        vec4 prev_albedo = texelFetch(u_prev_albedo, pixel, 0);
        albedo_acc.rgb = prev_albedo.rgb;
        relief_acc = prev_albedo.a;
        normal_acc = texelFetch(u_prev_normal, pixel, 0);
        vec4 previous_cover = texelFetch(u_prev_cover, pixel, 0);
        relief_cover = previous_cover.g;
        mesh_cover = previous_cover.b;
    }
#endif

#ifdef SG_MESH_LAYER
    // Color and coverage resolve the configured subpixels; geometry retains one
    // visible face. Averaging unrelated heights invents a sloping surface.
    vec2 mesh_uv = (gl_FragCoord.xy - u_mesh_rect.xy) / u_mesh_rect.zw;
    if (all(greaterThanEqual(mesh_uv, vec2(0))) && all(lessThan(mesh_uv, vec2(1)))) {
        ivec2 size = ivec2(u_mesh_extent);
        int samples = int(round(float(size.x) / u_mesh_rect.z));
        float sample_scale = 1.0 / float(samples);
        float weight = sample_scale * sample_scale;
        ivec2 first = ivec2(floor(mesh_uv * vec2(size) - (samples == 2 ? 0.5 : 0.0)));
        vec4 sc = vec4(0), em = vec4(0), tr = vec4(0), al = vec4(0);
        vec4 selected_normal = vec4(0);
        float selected_height = 0.0;
        bool selected = false;
        vec4 raw_normal=vec4(0);
        float raw_height=0.0, first_plane_height=0.0;
        vec3 first_normal=vec3(0);
        int covered_samples=0;
        bool coherent_plane=true;
        for (int y = 0; y < samples; y++) {
            for (int x = 0; x < samples; x++) {
                ivec2 p = clamp(first + ivec2(x, y), ivec2(0), size - 1);
                vec4 scene = texelFetch(u_mesh_scene, p, 0);
                vec4 sample_albedo = texelFetch(u_mesh_albedo, p, 0);
                sc += scene * weight;
                em += texelFetch(u_mesh_emission, p, 0) * weight;
                tr += texelFetch(u_mesh_absorption, p, 0) * weight;
                al += sample_albedo * weight;
                if (scene.a <= 0.0) continue;
                vec4 normal = texelFetch(u_mesh_normal, p, 0);
                float height = sample_albedo.a;
                float ny = sqrt(max(1.0 - dot(normal.xy, normal.xy), 0.0));
                if(covered_samples==0||height>raw_height) {
                    raw_height=height; raw_normal=normal;
                }
                // Evaluate this face at the output pixel's center, so a tilted
                // plane does not acquire a quarter-pixel height offset.
                vec2 offset = (vec2(p) + 0.5 - mesh_uv * vec2(size)) * sample_scale * u_unit_per_px;
                if (ny > 0.01) height += dot(normal.xy, vec2(offset.x, -offset.y)) / ny;
                else coherent_plane=false;
                vec3 face_normal=vec3(normal.x,ny,normal.y);
                if(covered_samples==0) {
                    first_normal=face_normal; first_plane_height=height;
                } else {
                    // A 0.01 chord (about 0.57 degrees) distinguishes a plane
                    // from curved/creased samples. Height agreement also rejects
                    // parallel adjacent layers; 0.2% covers RGBA16F quantization.
                    float height_tolerance=max(0.002*max(abs(height),abs(first_plane_height)),
                        0.02*max(u_unit_per_px.x,u_unit_per_px.y));
                    vec3 normal_delta=face_normal-first_normal;
                    if(dot(normal_delta,normal_delta)>0.0001||abs(height-first_plane_height)>height_tolerance)
                        coherent_plane=false;
                }
                covered_samples++;
                // Strict comparison keeps ties deterministic in scan order.
                if (!selected || height > selected_height) {
                    selected = true;
                    selected_height = height;
                    selected_normal = normal;
                }
            }
        }
        if (sc.a > 0.0) {
            // Tangent extrapolation is safe only when multiple samples confirm
            // the same plane. Curvature, silhouettes and sharp adjacent faces
            // retain the raw frontmost sample instead of inventing a height.
            if(!coherent_plane||covered_samples<2) {
                selected_height=raw_height; selected_normal=raw_normal;
            }
            layer_over(sc, em, tr, vec4(al.rgb, sc.a), selected_normal, selected_height);
            // An active plane camera already marks this layer geometric.
            if (u_plane_target.w == 0.0) mesh_cover += sc.a;
        }
    }
#endif

    // Metaball group accumulators.
    #define MAX_METABALL_GROUPS 1
    float mb_sdf[MAX_METABALL_GROUPS];
    vec4 mb_scene[MAX_METABALL_GROUPS];
    vec4 mb_emission[MAX_METABALL_GROUPS];
    vec4 mb_absorption[MAX_METABALL_GROUPS];
    vec4 mb_albedo[MAX_METABALL_GROUPS];
    vec4 mb_border_scene[MAX_METABALL_GROUPS];
    vec4 mb_border_emission[MAX_METABALL_GROUPS];
    vec4 mb_border_absorption[MAX_METABALL_GROUPS];
    vec4 mb_border_albedo[MAX_METABALL_GROUPS];
    float mb_aa[MAX_METABALL_GROUPS];
    float mb_border_thickness[MAX_METABALL_GROUPS];
    // Solid surface: xy bump tilt (world axes, y up), z edge light, w gloss.
    vec4 mb_surface[MAX_METABALL_GROUPS];
    float mb_height[MAX_METABALL_GROUPS];
    bool mb_active[MAX_METABALL_GROUPS];
    for (int g = 0; g < MAX_METABALL_GROUPS; g++) {
        mb_sdf[g] = 1e30;
        mb_scene[g] = vec4(0);
        mb_emission[g] = vec4(0);
        mb_absorption[g] = vec4(0);
        mb_albedo[g] = vec4(0);
        mb_border_scene[g] = vec4(0);
        mb_border_emission[g] = vec4(0);
        mb_border_absorption[g] = vec4(0);
        mb_border_albedo[g] = vec4(0);
        mb_aa[g] = 0.0;
        mb_border_thickness[g] = 0.0;
        mb_surface[g] = vec4(0);
        mb_height[g] = 0.0;
        mb_active[g] = false;
    }

    for (uint i = 0u; i < total; i++) {
        uint eidx = tile_entity(base + i);
        if (eidx < uint(u_first_entity) || eidx >= uint(u_last_entity)) continue;
        Entity e = entity_at(eidx);
        if (e.metaball_group != uint(u_metaball_group)) continue;

        // Transform pixel to entity local space.
        vec2 dp = world_pos - e.pos;
        vec2 local_p = vec2(
            e.inv_rot.x * dp.x + e.inv_rot.y * dp.y,
            e.inv_rot.z * dp.x + e.inv_rot.w * dp.y
        );

        float sdf = evaluate_sdf(local_p, e);
        vec2 half_size = shader_half_size(e);
        vec2 material_pos = material_position(e, local_p, world_pos);
        // Pieces of one shape share a block of material and shape parameters.
        vec4 graph_output[SG_OUTPUTS];
        vec4 parameters[32];
        if (e.reserved1 > 0.0) {
            for(int p=0;p<32;p++) parameters[p]=shader_parameters[floatBitsToUint(e.simulation_time.y)+32u+uint(p)];
            sg_dispatch_2d(int(e.reserved1), local_p, material_pos, half_size, e.inv_rot, e.simulation_time.x, parameters, graph_output);
            sdf = graph_output[0].x;
        }
        float aa = e.aa > 0.0 ? e.aa : max(plane_pixel.x, plane_pixel.y) * 0.5;
        bool filtered_circle_emission = e.shape_type == SHAPE_CIRCLE &&
                                        e.thickness <= 0.0 && e.border_thickness <= 0.0 &&
                                        e.reserved0 <= 0.0 && e.reserved1 <= 0.0 &&
                                        e.metaball_group == 0u;
        bool may_emit = any(notEqual(e.emission.rgb, vec3(0.0)));
        float emission_half_width = filtered_circle_emission
                                      ? circle_emission_half_width(e)
                                      : aa;
        // Tile group pieces cover the pixel's footprint, at least a pixel wide.
        bool tiled = e.tile_group != 0.0 && e.shape_type == SHAPE_BOX;
        float footprint = 0.5 * max(aa, max(plane_pixel.x, plane_pixel.y));
        float reach = tiled ? 1.4143 * footprint : aa;
        // Uncovered pixels need no material; metaballs still blend beyond their edge.
        if (e.metaball_group == 0u && sdf >= reach &&
            (!filtered_circle_emission || !may_emit || sdf >= emission_half_width)) continue;
        // A sprite's texel alpha is its edge, so its box covers fully.
        if (e.shape_type == SHAPE_SPRITE) {
            vec4 texel = sprite_texel(local_p, e);
            if (sdf > 0.0 || texel.a <= 0.0) continue;
            sdf = -reach;
            e.scene *= texel;
            e.albedo *= texel;
            e.emission.a *= texel.a;
            e.absorption *= texel.a;
        }
        vec4 surface = vec4(0.0, 0.0, e.reserved2, 0.0);
        float height = e.relief;
        if (e.shape_type == SHAPE_SPRITE) sprite_relief(local_p, e, surface, height);
        if (e.reserved0 > 0.0) {
            for(int p=0;p<32;p++) parameters[p]=shader_parameters[floatBitsToUint(e.simulation_time.y)+uint(p)];
            sg_dispatch_2d(int(e.reserved0), local_p, material_pos, half_size, e.inv_rot, e.simulation_time.x, parameters, graph_output);
            e.scene=graph_output[0]; e.emission=graph_output[1];
            e.albedo=graph_output[2]; e.absorption=graph_output[3];
            e.border_scene=graph_output[0]; e.border_emission=graph_output[1];
            e.border_albedo=graph_output[2]; e.border_absorption=graph_output[3];
            surface.xyw = graph_output[4].xyz;
            // A material's Height adds relief above the shape's surface.
            height += graph_output[4].w;
        }

        plane_surface(surface, height);

        if (e.metaball_group > 0u) {
            // Accumulate into metaball group.
            uint g = 0u; // Each dispatch processes one authored group.
            if (g < MAX_METABALL_GROUPS) {
                float k = e.metaball_blend;
                vec4 e_scene = unpack_color(e.scene);
                vec4 e_emission = unpack_color(e.emission);
                vec4 e_absorption = unpack_color(e.absorption);
                vec4 e_albedo = unpack_color(e.albedo);
                vec4 e_bscene = unpack_color(e.border_scene);
                vec4 e_bemission = unpack_color(e.border_emission);
                vec4 e_babsorption = unpack_color(e.border_absorption);
                vec4 e_balbedo = unpack_color(e.border_albedo);
                if (!mb_active[g] || k <= 0.0) {
                    // First entity in group or no blending: just take the closer one.
                    if (sdf < mb_sdf[g]) {
                        mb_sdf[g] = sdf;
                        mb_scene[g] = e_scene;
                        mb_emission[g] = e_emission;
                        mb_absorption[g] = e_absorption;
                        mb_albedo[g] = e_albedo;
                        mb_border_scene[g] = e_bscene;
                        mb_border_emission[g] = e_bemission;
                        mb_border_absorption[g] = e_babsorption;
                        mb_border_albedo[g] = e_balbedo;
                        mb_aa[g] = e.aa;
                        mb_border_thickness[g] = e.border_thickness;
                        mb_surface[g] = surface;
                        mb_height[g] = height;
                    }
                } else {
                    float h = clamp(0.5 + 0.5 * (sdf - mb_sdf[g]) / k, 0.0, 1.0);
                    mb_sdf[g] = mix(sdf, mb_sdf[g], h) - k * h * (1.0 - h);
                    mb_scene[g] = mix(e_scene, mb_scene[g], h);
                    mb_emission[g] = mix(e_emission, mb_emission[g], h);
                    mb_absorption[g] = mix(e_absorption, mb_absorption[g], h);
                    mb_albedo[g] = mix(e_albedo, mb_albedo[g], h);
                    mb_border_scene[g] = mix(e_bscene, mb_border_scene[g], h);
                    mb_border_emission[g] = mix(e_bemission, mb_border_emission[g], h);
                    mb_border_absorption[g] = mix(e_babsorption, mb_border_absorption[g], h);
                    mb_border_albedo[g] = mix(e_balbedo, mb_border_albedo[g], h);
                    mb_aa[g] = mix(e.aa, mb_aa[g], h);
                    mb_border_thickness[g] = mix(e.border_thickness, mb_border_thickness[g], h);
                    mb_surface[g] = mix(surface, mb_surface[g], h);
                    mb_height[g] = mix(height, mb_height[g], h);
                }
                mb_active[g] = true;
            }
            continue;
        }

        // Normal entity (metaball_group == 0).
        float bt = e.border_thickness;

        if (bt > 0.0) {
            group_flush();
            // Border rendering: fill is interior, border is edge band.
            float shape_coverage = 1.0 - smoothstep(0.0, aa, sdf);
            float fill_coverage = 1.0 - smoothstep(-bt, -bt + aa, sdf);
            float border_coverage = shape_coverage - fill_coverage;
            if (shape_coverage <= 0.0) continue;

            // Scene: border over fill, then onto canvas.
            vec4 sc = unpack_color(e.scene); sc.rgb *= sc.a; sc *= fill_coverage;
            vec4 bsc = unpack_color(e.border_scene); bsc.rgb *= bsc.a; bsc *= border_coverage;
            scene_acc = alpha_over(alpha_over(bsc, sc), scene_acc);
            float cover = bsc.a + sc.a * (1.0 - bsc.a);
            normal_acc = mix(normal_acc, surface, cover);
            relief_acc = mix(relief_acc, height, cover);
            if (u_plane_target.w != 0.0) {
                relief_cover = cover + relief_cover * (1.0-cover);
                mesh_cover = cover + mesh_cover * (1.0-cover);
            }

            // Emission and absorption.
            vec4 em = unpack_color(e.emission); em.rgb *= em.a;
            em *= fill_coverage;
            vec4 bem = unpack_color(e.border_emission); bem.rgb *= bem.a; bem *= border_coverage;
            vec4 tr = unpack_color(e.absorption); tr *= fill_coverage;
            vec4 btr = unpack_color(e.border_absorption); btr *= border_coverage;
            emission_over(em, tr.a > 0.0);
            emission_over(bem, btr.a > 0.0);
            vec4 combined_tr = tr + btr;
            tau_acc += dot(combined_tr.rgb, vec3(1.0 / 3.0)) * (1.0 - absorption_cover);
            absorption_cover += combined_tr.a * (1.0 - absorption_cover);

            // Albedo.
            vec4 al = unpack_color(e.albedo); al.rgb *= al.a; al *= fill_coverage;
            vec4 bal = unpack_color(e.border_albedo); bal.rgb *= bal.a; bal *= border_coverage;
            albedo_acc = alpha_over(alpha_over(bal, al), albedo_acc);
        } else {
            // No border: original path.
            float coverage = !tiled         ? 1.0 - smoothstep(0.0, aa, sdf)
                           : sdf <= -reach ? 1.0
                                           : footprint_coverage(e, local_p, footprint);
            float emission_coverage = filtered_circle_emission
                                        ? circle_emission_coverage(sdf, emission_half_width)
                                        : coverage;
            if (coverage <= 0.0 && emission_coverage <= 0.0) continue;

            vec4 sc = unpack_color(e.scene); sc.rgb *= sc.a; sc *= coverage;
            vec4 em = unpack_color(e.emission); em.rgb *= em.a;
            em.rgb *= e.simulation_time.z;
            em *= emission_coverage;
            vec4 tr = unpack_color(e.absorption); tr *= coverage;
            vec4 al = unpack_color(e.albedo); al.rgb *= al.a; al *= coverage;
            if (tiled) {
                group_add(e.tile_group, coverage, sc, em, tr, al, surface, height);
                continue;
            }
            group_flush();
            layer_over(sc, em, tr, al, surface, height);
        }
    }
    group_flush();


    // Composite metaball groups on top.
    for (int g = 0; g < MAX_METABALL_GROUPS; g++) {
        if (!mb_active[g]) continue;
        float aa = mb_aa[g] > 0.0 ? mb_aa[g] : max(plane_pixel.x, plane_pixel.y) * 0.5;
        float bt = mb_border_thickness[g];

        if (bt > 0.0) {
            float shape_coverage = 1.0 - smoothstep(0.0, aa, mb_sdf[g]);
            float fill_coverage = 1.0 - smoothstep(-bt, -bt + aa, mb_sdf[g]);
            float border_coverage = shape_coverage - fill_coverage;
            if (shape_coverage <= 0.0) continue;

            vec4 sc = mb_scene[g]; sc.rgb *= sc.a; sc *= fill_coverage;
            vec4 bsc = mb_border_scene[g]; bsc.rgb *= bsc.a; bsc *= border_coverage;
            scene_acc = alpha_over(alpha_over(bsc, sc), scene_acc);
            float cover = bsc.a + sc.a * (1.0 - bsc.a);
            normal_acc = mix(normal_acc, mb_surface[g], cover);
            relief_acc = mix(relief_acc, mb_height[g], cover);

            vec4 em = mb_emission[g]; em.rgb *= em.a; em *= fill_coverage;
            vec4 bem = mb_border_emission[g]; bem.rgb *= bem.a; bem *= border_coverage;
            vec4 tr = mb_absorption[g]; tr *= fill_coverage;
            vec4 btr = mb_border_absorption[g]; btr *= border_coverage;
            emission_over(em, tr.a > 0.0);
            emission_over(bem, btr.a > 0.0);
            vec4 combined_tr = tr + btr;
            tau_acc += dot(combined_tr.rgb, vec3(1.0 / 3.0)) * (1.0 - absorption_cover);
            absorption_cover += combined_tr.a * (1.0 - absorption_cover);

            vec4 al = mb_albedo[g]; al.rgb *= al.a; al *= fill_coverage;
            vec4 bal = mb_border_albedo[g]; bal.rgb *= bal.a; bal *= border_coverage;
            albedo_acc = alpha_over(alpha_over(bal, al), albedo_acc);
        } else {
            float coverage = 1.0 - smoothstep(0.0, aa, mb_sdf[g]);
            if (coverage <= 0.0) continue;

            vec4 sc = mb_scene[g]; sc.rgb *= sc.a; sc *= coverage;
            vec4 em = mb_emission[g]; em.rgb *= em.a; em *= coverage;
            vec4 al = mb_albedo[g]; al.rgb *= al.a; al *= coverage;
            layer_over(sc, em, mb_absorption[g] * coverage, al, mb_surface[g], mb_height[g]);
        }
    }

    // Transport sources are emission density; see hrc/source_average.shd.
    // Absorbers covering part of the pixel give the transmittance of a whole
    // pixel of them scaled by their coverage, so it follows coverage
    // linearly, and emit their radiance times the pixel's optical depth.
    // Clear emitters emit by density. Display scales the density back to
    // emission premultiplied by coverage.
    float tau = absorption_cover > 1e-4 ? tau_acc / absorption_cover : 0.0;
    float opacity = absorption_cover * (1.0 - exp(-tau));
    float depth = -log(max(1.0 - opacity, 1e-6));
    vec3 clear = max(emission_acc.rgb - absorber_emission, 0.0);
    vec3 density = clear + absorber_emission / max(absorption_cover, 1e-4) * depth;
    vec3 luma = vec3(.2126, .7152, .0722);
    float dense = dot(density, luma);
    float display = dense > 1e-8 ? min(dot(emission_acc.rgb, luma) / dense, 60000.0) : 1.0;
    out_scene = scene_acc;
    out_source = vec4(density, depth);
    out_cover = vec4(display, relief_cover, mesh_cover, 0.0);
    out_albedo = vec4(albedo_acc.rgb, relief_acc);
    out_normal = normal_acc;
    out_visibility = vec4(0);
}
