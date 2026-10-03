#include "render/arena3d.h"
#include "render/arena_geometry.h"
#include "render/arena_visual.h"
#include "runtime/generated.h"

#include <float.h>
#include <math.h>

bool render_arena_entity_bounds(const ecs_world *world, ecs_entity entity, CF_V3 corners[8])
{
    arena_visual visuals[ARENA_PART_LIMIT];
    char error[128];
    int count = render_arena_visuals(world, entity, NULL, visuals, error, sizeof error);
    if (count <= 0) {
        return false;
    }
    CF_V3 minimum = { FLT_MAX, FLT_MAX, FLT_MAX },
          maximum = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int part = 0; part < count; part++) {
        CF_M4x4 model = cf_mul_m4(
            visuals[part].frame,
            cf_m4_scale(visuals[part].part.dimensions)
        );
        int primitive = visuals[part].part.primitive;
        float center_x = primitive == GAME_PRIMITIVE_TRIANGLE ||
                                 primitive == GAME_PRIMITIVE_TRIANGLE_OUTLINE
                           ? 1.0f / 6
                           : 0;
        for (int i = 0; i < 8; i++) {
            CF_V3 point = cf_m4_transform_point(
                model,
                (CF_V3){ center_x + (i & 1 ? .5f : -.5f),
                         i & 4 ? .5f : -.5f,
                         i & 2 ? .5f : -.5f }
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
        case GAME_PRIMITIVE_BOX:
            hit = cf_ray3_to_aabb3(ray, (CF_Aabb3){ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } });
            break;
        case GAME_PRIMITIVE_SPHERE:
            hit = cf_ray3_to_sphere(ray, (CF_Sphere){ .p = { 0 }, .r = 0.5f });
            break;
        case GAME_PRIMITIVE_CAPSULE:
            hit = cf_ray3_to_capsule3(
                ray,
                (CF_Capsule3){ .a = { 0, -0.25f, 0 }, .b = { 0, 0.25f, 0 }, .r = 0.25f }
            );
            break;
        case GAME_PRIMITIVE_CONE: return cone_hit(ray);
        case GAME_PRIMITIVE_TRIANGLE_PRISM: return prism_hit(ray);
        case GAME_PRIMITIVE_OCTAHEDRON: return octahedron_hit(ray);
        case GAME_PRIMITIVE_TETRAHEDRON: return tetrahedron_hit(ray);
        case GAME_PRIMITIVE_CHEVRON_PYRAMID:
        case GAME_PRIMITIVE_BOX_FRAME:
        case GAME_PRIMITIVE_TRIANGLE_OUTLINE:
        case GAME_PRIMITIVE_TRIANGLE: {
            CF_V3 triangles[ARENA_MESH_TRIANGLE_LIMIT][3];
            int count = render_arena_mesh_triangles(primitive, triangles);
            float nearest = FLT_MAX;
            for (int i = 0; i < count; i++) {
                CF_V3 edge1 = cf_sub_v3(triangles[i][1], triangles[i][0]);
                CF_V3 edge2 = cf_sub_v3(triangles[i][2], triangles[i][0]);
                CF_V3 p = cf_cross_v3(ray.d, edge2);
                float determinant = cf_dot_v3(edge1, p);
                if (fabsf(determinant) < 1e-7f) {
                    continue;
                }
                CF_V3 delta = cf_sub_v3(ray.p, triangles[i][0]);
                float u = cf_dot_v3(delta, p) / determinant;
                CF_V3 q = cf_cross_v3(delta, edge1);
                float v = cf_dot_v3(ray.d, q) / determinant;
                float t = cf_dot_v3(edge2, q) / determinant;
                if (u >= 0 && v >= 0 && u + v <= 1 && t >= 0 && t <= ray.t) {
                    nearest = fminf(nearest, t);
                }
            }
            return nearest;
        }
    }
    return hit.hit ? hit.t : FLT_MAX;
}

ecs_entity render_arena_pick(ecs_world *world, const render_arena_camera *camera, CF_V2 ndc, render_skip skip)
{
    CF_Ray3 ray = render_arena_ray(camera, ndc);
    CF_V3 end = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, ray.t));
    ecs_entity nearest = { 0 };
    float distance = FLT_MAX;
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameGeometry3D.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return nearest;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (render_skip_has(skip, entity)) {
            continue;
        }
        arena_visual visuals[ARENA_PART_LIMIT];
        char error[128];
        int count = render_arena_visuals(world, entity, NULL, visuals, error, sizeof error);
        for (int i = 0; i < count; i++) {
            CF_M4x4 inverse = cf_m4_invert(render_arena_visual_model(&visuals[i]));
            CF_Ray3 local = cf_make_ray3(
                cf_m4_transform_point(inverse, ray.p),
                cf_m4_transform_point(inverse, end)
            );
            float t = primitive_hit(local, visuals[i].part.primitive);
            if (t == FLT_MAX) {
                continue;
            }
            float world_t = t * ray.t / local.t;
            if (world_t < distance) {
                nearest = entity;
                distance = world_t;
            }
        }
    }
    ecs_query_end(&query);
    return nearest;
}
