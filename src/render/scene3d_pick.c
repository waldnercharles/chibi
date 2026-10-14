#include "render/mesh_surface.h"
#include "render/scene3d.h"
#include "render/scene3d_geometry.h"
#include "render/scene3d_visual.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_pick.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"

#include <float.h>
#include <math.h>

static int entity_model(const ecs_world *world, ecs_entity entity)
{
    const SdfSurface3D *surface = SdfSurface3D_get_const(world, entity);
    const Shader *binding = Shader_get_const(world, entity);
    int id = surface && surface->enabled && binding
               ? render_shader_find(binding->shape)
               : 0;
    return render_shader_is_model(id) ? id : 0;
}

/* An entity's visuals at its exact world placement, which picks and bounds
 * read before any propagation: its Transform3D's when it has one. */
static int exact_visuals(
    const ecs_world *world,
    ecs_entity entity,
    scene3d_visual visuals[SCENE3D_PART_LIMIT],
    const render_mesh_asset **mesh,
    char *error,
    size_t size
)
{
    if (!Transform3D_has(world, entity)) {
        return render_scene3d_visuals(world, entity, NULL, NULL, visuals, mesh, error, size);
    }
    GlobalTransform3D transform = transform_world_3d(world, entity);
    return render_scene3d_visuals(world, entity, NULL, &transform, visuals, mesh, error, size);
}

/* A model's surface at the same exact placement. */
static bool exact_surface(
    const ecs_world *world,
    ecs_entity entity,
    render_mesh_surface *surface,
    char *error,
    size_t size
)
{
    if (!Transform3D_has(world, entity)) {
        return render_mesh_surface_build(world, entity, NULL, surface, error, size);
    }
    GlobalTransform3D transform = transform_world_3d(world, entity);
    return render_mesh_surface_build_with_bounds(world, entity, NULL, &transform, NULL, surface, error, size);
}

/* The box around a visual: its imported mesh's bounds, or its shape's. */
static CF_M4x4 visual_box(const scene3d_visual *visual, const render_mesh_asset *mesh)
{
    if (mesh) {
        return render_scene3d_mesh_box(mesh, render_scene3d_visual_model(visual));
    }
    CF_M4x4 model = cf_mul_m4(visual->frame, cf_m4_scale(visual->part.dimensions));
    int primitive = visual->part.primitive;
    float center_x = primitive == PRIMITIVE_TRIANGLE || primitive == PRIMITIVE_TRIANGLE_OUTLINE
                       ? 1.0f / 6
                       : 0;
    return cf_mul_m4(model, cf_m4_translate((CF_V3){ center_x, 0, 0 }));
}

bool render_scene3d_entity_bounds(const ecs_world *world, ecs_entity entity, CF_V3 corners[8])
{
    scene3d_visual visuals[SCENE3D_PART_LIMIT];
    const render_mesh_asset *mesh = NULL;
    char error[128];
    int count;
    if (entity_model(world, entity)) {
        render_mesh_surface surface = { 0 };
        bool ok = exact_surface(world, entity, &surface, error, sizeof error);
        count = ok ? acount(surface.parts) : 0;
        if (count) {
            visuals[0] = surface.parts[0].visual;
        }
        render_mesh_surface_free(&surface);
    } else {
        count = exact_visuals(world, entity, visuals, &mesh, error, sizeof error);
    }
    if (count <= 0) {
        return false;
    }
    CF_V3 minimum = { FLT_MAX, FLT_MAX, FLT_MAX },
          maximum = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int part = 0; part < count; part++) {
        CF_M4x4 box = visual_box(&visuals[part], mesh);
        for (int i = 0; i < 8; i++) {
            CF_V3 point = cf_m4_transform_point(
                box,
                (CF_V3){ i & 1 ? .5f : -.5f, i & 4 ? .5f : -.5f, i & 2 ? .5f : -.5f }
            );
            corners[i] = point;
            minimum = cf_min_v3(minimum, point);
            maximum = cf_max_v3(maximum, point);
        }
    }
    if (count > 1) {
        for (int i = 0; i < 8; i++) {
            corners[i] = (CF_V3){ i & 1 ? maximum.x : minimum.x,
                                  i & 4 ? maximum.y : minimum.y,
                                  i & 2 ? maximum.z : minimum.z };
        }
    }
    return true;
}

static float convex_hit(CF_Ray3 ray, const CF_V4 planes[], int count)
{
    /* Inside each face: dot(normal, point) <= 0.5. */
    float enter = 0, leave = ray.t;
    for (int i = 0; i < count; i++) {
        CF_V3 normal = { planes[i].x, planes[i].y, planes[i].z };
        float remaining = planes[i].w - cf_dot_v3(normal, ray.p);
        float speed = cf_dot_v3(normal, ray.d);
        if (fabsf(speed) < 1e-7f) {
            if (remaining < 0) {
                return FLT_MAX;
            }
            continue;
        }
        float t = remaining / speed;
        if (speed < 0) {
            enter = fmaxf(enter, t);
        } else {
            leave = fminf(leave, t);
        }
        if (enter > leave) {
            return FLT_MAX;
        }
    }
    return enter;
}

static float prism_hit(CF_Ray3 ray)
{
    static const CF_V4 normals[] = {
        { -1, 0, 0, .5f }, { 0, 1, 0, .5f },  { 0, -1, 0, .5f },
        { 1, 0, 2, .5f },  { 1, 0, -2, .5f },
    };
    return convex_hit(ray, normals, 5);
}

static float octahedron_hit(CF_Ray3 ray)
{
    static const CF_V4 normals[] = {
        { 1, 1, 1, .5f },   { 1, 1, -1, .5f },   { -1, 1, 1, .5f },
        { -1, 1, -1, .5f }, { 1, -1, 1, .5f },   { 1, -1, -1, .5f },
        { -1, -1, 1, .5f }, { -1, -1, -1, .5f },
    };
    return convex_hit(ray, normals, 8);
}

static float tetrahedron_hit(CF_Ray3 ray)
{
    static const CF_V4 planes[] = {
        { -1, 0, 0, .5f },
        { 0, -1, 0, .5f },
        { 0, 0, -1, .5f },
        { 1, 1, 1, -.5f },
    };
    return convex_hit(ray, planes, 4);
}

static float cone_hit(CF_Ray3 ray)
{
    float a = ray.d.x * ray.d.x + ray.d.z * ray.d.z - 0.25f * ray.d.y * ray.d.y;
    float b = 2 * (ray.p.x * ray.d.x + ray.p.z * ray.d.z) +
              0.5f * (0.5f - ray.p.y) * ray.d.y;
    float c = ray.p.x * ray.p.x + ray.p.z * ray.p.z -
              0.25f * (0.5f - ray.p.y) * (0.5f - ray.p.y);
    float roots[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    if (fabsf(a) < 1e-7f) {
        if (fabsf(b) > 1e-7f) {
            roots[0] = -c / b;
        }
    } else if (b * b - 4 * a * c >= 0) {
        float d = sqrtf(b * b - 4 * a * c);
        roots[0] = (-b - d) / (2 * a);
        roots[1] = (-b + d) / (2 * a);
    }
    if (fabsf(ray.d.y) > 1e-7f) {
        float t = (-0.5f - ray.p.y) / ray.d.y;
        float x = ray.p.x + ray.d.x * t, z = ray.p.z + ray.d.z * t;
        if (x * x + z * z <= 0.25f) {
            roots[2] = t;
        }
    }
    float best = FLT_MAX;
    for (int i = 0; i < 3; i++) {
        float t = roots[i];
        float y = ray.p.y + ray.d.y * t;
        if (t >= 0 && t <= ray.t && y >= -0.50001f && y <= 0.50001f) {
            best = fminf(best, t);
        }
    }
    return best;
}

static float primitive_hit(CF_Ray3 ray, int primitive)
{
    CF_Raycast3 hit = { 0 };
    switch (primitive) {
        case PRIMITIVE_BOX:
            hit = cf_ray3_to_aabb3(ray, (CF_Aabb3){ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } });
            break;
        case PRIMITIVE_SPHERE:
            hit = cf_ray3_to_sphere(ray, (CF_Sphere){ .p = { 0 }, .r = 0.5f });
            break;
        case PRIMITIVE_CAPSULE:
            hit = cf_ray3_to_capsule3(
                ray,
                (CF_Capsule3){ .a = { 0, -0.25f, 0 }, .b = { 0, 0.25f, 0 }, .r = 0.25f }
            );
            break;
        case PRIMITIVE_CONE: return cone_hit(ray);
        case PRIMITIVE_TRIANGLE_PRISM: return prism_hit(ray);
        case PRIMITIVE_OCTAHEDRON: return octahedron_hit(ray);
        case PRIMITIVE_TETRAHEDRON: return tetrahedron_hit(ray);
        case PRIMITIVE_CHEVRON_PYRAMID:
        case PRIMITIVE_BOX_FRAME:
        case PRIMITIVE_TRIANGLE_OUTLINE:
        case PRIMITIVE_TRIANGLE: {
            CF_V3 triangles[SCENE3D_MESH_TRIANGLE_LIMIT][3];
            int count = render_scene3d_mesh_triangles(primitive, triangles);
            float nearest = FLT_MAX;
            for (int i = 0; i < count; i++) {
                nearest = fminf(nearest, render_triangle_hit(ray, triangles[i]));
            }
            return nearest;
        }
    }
    return hit.hit ? hit.t : FLT_MAX;
}

/* March authored model space inside the same compiled bounds used for rendering.
 * Inverting frame (rather than the scaled proxy box) preserves SDF units. */
static float model_ray_hit(ecs_world *world, ecs_entity entity, int id, CF_Ray3 ray)
{
    render_mesh_surface surface = { 0 };
    char message[128];
    if (!exact_surface(world, entity, &surface, message, sizeof message) ||
        !acount(surface.parts)) {
        render_mesh_surface_free(&surface);
        return FLT_MAX;
    }
    CF_M4x4 inverse = cf_m4_invert(surface.parts[0].visual.frame);
    CF_V3 center = surface.parts[0].sdf_center;
    render_mesh_surface_free(&surface);
    CF_V3 end = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, ray.t));
    CF_Ray3 local = cf_make_ray3(
        cf_add_v3(cf_m4_transform_point(inverse, ray.p), center),
        cf_add_v3(cf_m4_transform_point(inverse, end), center)
    );
    const Shader *binding = Shader_get_const(world, entity);
    float parameters[32][4], minimum[3], maximum[3];
    bool empty;
    shader_error error = { 0 };
    if (!render_shader_model_snapshot(id, world, entity, binding->parameters, parameters, minimum, maximum, &empty, &error) ||
        empty) {
        return FLT_MAX;
    }
    float origin[] = { local.p.x, local.p.y, local.p.z };
    float direction[] = { local.d.x, local.d.y, local.d.z };
    float enter = 0, leave = local.t, extent = 0;
    for (int axis = 0; axis < 3; axis++) {
        extent = fmaxf(extent, maximum[axis] - minimum[axis]);
        if (fabsf(direction[axis]) < 1e-7f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
                return FLT_MAX;
            }
            continue;
        }
        float a = (minimum[axis] - origin[axis]) / direction[axis];
        float b = (maximum[axis] - origin[axis]) / direction[axis];
        enter = fmaxf(enter, fminf(a, b));
        leave = fminf(leave, fmaxf(a, b));
    }
    float epsilon = fmaxf(.00001f, extent * .0002f);
    float lipschitz = render_shader_distance_lipschitz(id);
    for (int step = 0; step < (lipschitz > 1 ? 512 : 128) && enter <= leave; step++) {
        CF_V3 point = cf_add_v3(local.p, cf_mul_v3_f(local.d, enter));
        shader_sample_context sample = {
            .object_position = { point.x, point.y, point.z }
        };
        float result[SHADER_OUTPUT_MAX][4];
        if (!render_shader_sample(id, &sample, parameters, result, &error) ||
            !isfinite(result[0][0])) {
            return FLT_MAX;
        }
        float distance = fabsf(result[0][0]);
        if (distance <= epsilon) {
            return enter * ray.t / local.t;
        }
        enter += lipschitz > 1 ? distance * .8f / lipschitz
                               : fmaxf(distance * .8f, epsilon * .25f);
    }
    return FLT_MAX;
}

/* How far along `ray` it first meets the shape `primitive` drawn by
 * `model`, or FLT_MAX. */
static float shape_hit(int primitive, CF_M4x4 model, CF_Ray3 ray)
{
    CF_M4x4 inverse = cf_m4_invert(model);
    CF_V3 end = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, ray.t));
    CF_Ray3 local = cf_make_ray3(
        cf_m4_transform_point(inverse, ray.p),
        cf_m4_transform_point(inverse, end)
    );
    float t = primitive_hit(local, primitive);
    return t == FLT_MAX ? FLT_MAX : t * ray.t / local.t;
}

static float along(CF_V3 v, int axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

/* How far along `ray` it first meets `mesh` drawn by `model`, or FLT_MAX.
 * The ray is cut to the mesh's box in the box's own frame, which stays
 * near unit size however far the mesh's coordinates reach, and only that
 * span goes into them. Half the span is the mesh ray's direction, which a
 * float holds even where the whole span passes float's range, so the pick
 * runs from 0 to 2 along it. So a mesh near or past float's range that its
 * model scales down to view picks as it draws. */
static float mesh_hit(const render_mesh_asset *mesh, CF_M4x4 model, CF_Ray3 ray)
{
    CF_M4x4 extent = render_scene3d_mesh_extent(mesh);
    CF_M4x4 inverse = cf_m4_invert(cf_mul_m4(model, extent));
    CF_V3 from = cf_m4_transform_point(inverse, ray.p);
    CF_V3 end = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, ray.t));
    CF_V3 step = cf_sub_v3(cf_m4_transform_point(inverse, end), from);
    float enter = 0, leave = 1;
    for (int axis = 0; axis < 3; axis++) {
        float origin = along(from, axis), toward = along(step, axis);
        if (toward == 0) {
            if (fabsf(origin) > 1) {
                return FLT_MAX;
            }
            continue;
        }
        float a = (-1 - origin) / toward, b = (1 - origin) / toward;
        enter = fmaxf(enter, fminf(a, b));
        leave = fminf(leave, fmaxf(a, b));
    }
    if (enter >= leave) {
        return FLT_MAX;
    }
    CF_V3 first = cf_m4_transform_point(extent, cf_add_v3(from, cf_mul_v3_f(step, enter)));
    CF_V3 last = cf_m4_transform_point(extent, cf_add_v3(from, cf_mul_v3_f(step, leave)));
    CF_Ray3 local = { .p = first,
                      .d = { (float)(((double)last.x - first.x) / 2),
                             (float)(((double)last.y - first.y) / 2),
                             (float)(((double)last.z - first.z) / 2) },
                      .t = 2 };
    float t = render_mesh_pick(mesh, local).t;
    return t == FLT_MAX ? FLT_MAX : (enter + (leave - enter) * t / 2) * ray.t;
}

ecs_entity render_scene3d_pick(ecs_world *world, const render_scene3d_camera *camera, CF_V2 ndc, render_skip skip)
{
    CF_Ray3 ray = render_scene3d_ray(camera, ndc);
    ecs_entity nearest = { 0 };
    float distance = FLT_MAX;
    ecs_entity *entities = NULL;
    render_scene3d_entities(world, skip, &entities);
    for (int e = 0; e < acount(entities); e++) {
        ecs_entity entity = entities[e];
        int model = entity_model(world, entity);
        if (model) {
            float t = model_ray_hit(world, entity, model, ray);
            if (t < distance) {
                nearest = entity;
                distance = t;
            }
            continue;
        }
        scene3d_visual visuals[SCENE3D_PART_LIMIT];
        const render_mesh_asset *mesh;
        char error[128];
        int count = exact_visuals(world, entity, visuals, &mesh, error, sizeof error);
        for (int i = 0; i < count; i++) {
            CF_M4x4 model = render_scene3d_visual_model(&visuals[i]);
            float world_t = mesh ? mesh_hit(mesh, model, ray)
                                 : shape_hit(visuals[i].part.primitive, model, ray);
            if (world_t < distance) {
                nearest = entity;
                distance = world_t;
            }
        }
    }
    afree(entities);
    return nearest;
}
