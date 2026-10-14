#include "render/geometry_2d.h"

#include "render/appearance_geometry.h"
#include "render/mesh_surface.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
#include "runtime/tilemap.h"
#include "runtime/transform_propagation.h"

#include <assert.h>
#include <limits.h>
#include <math.h>

static bool finite_geometry(const Geometry3D *geometry)
{
    return geometry && isfinite(geometry->width) && isfinite(geometry->depth) &&
           geometry->width > 0 && geometry->depth > 0;
}

bool render_geometry_2d_mesh_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    CF_V2 *min,
    CF_V2 *max,
    char *error,
    size_t size
)
{
    const MeshSurface2D *mesh = MeshSurface2D_get_const(world, entity);
    const SdfSurface3D *sdf = SdfSurface3D_get_const(world, entity);
    return ((mesh && mesh->enabled) || (sdf && sdf->enabled)) &&
           render_mesh_surface_bounds(world, entity, position, min, max, error, size);
}

bool render_geometry_2d_project(
    const Geometry3D *geometry,
    const Appearance *fallback,
    const VisualTransform *visual,
    float heading,
    Appearance *appearance,
    float *angle
)
{
    assert(appearance && angle);
    if (!fallback || !isfinite(heading) ||
        (visual && (!isfinite(visual->yaw) || !isfinite(visual->scale) ||
                    visual->scale <= 0))) {
        return false;
    }
    Appearance result = *fallback;
    float scale = visual ? visual->scale : 1;
    if (geometry) {
        if (!finite_geometry(geometry)) {
            return false;
        }
        result.width = geometry->width;
        result.height = geometry->depth;
        switch (geometry->primitive) {
            case PRIMITIVE_BOX: result.shape = SHAPE_BOX; break;
            case PRIMITIVE_SPHERE: result.shape = SHAPE_CIRCLE; break;
            case PRIMITIVE_CAPSULE: result.shape = SHAPE_CAPSULE; break;
            case PRIMITIVE_OCTAHEDRON: result.shape = SHAPE_DIAMOND; break;
            case PRIMITIVE_CHEVRON_PYRAMID: result.shape = SHAPE_CHEVRON; break;
            case PRIMITIVE_BOX_FRAME:
                result.shape = SHAPE_BOX;
                if (result.border_width <= 0) {
                    result.border_width = fminf(result.width, result.height) * 0.12f;
                }
                break;
            case PRIMITIVE_TRIANGLE_OUTLINE:
                result.shape = SHAPE_STROKED_TRIANGLE;
                if (result.stroke_width <= 0) {
                    result.stroke_width = fminf(result.width, result.height) * 0.1f;
                }
                break;
            case PRIMITIVE_CONE:
            case PRIMITIVE_TRIANGLE_PRISM:
            case PRIMITIVE_TETRAHEDRON:
            case PRIMITIVE_TRIANGLE: result.shape = SHAPE_TRIANGLE; break;
            default: return false;
        }
    }
    result.width *= scale;
    result.height *= scale;
    result.border_width *= scale;
    result.glow_radius *= scale;
    result.stroke_width *= scale;
    result.relief *= scale;
    if (!isfinite(result.width) || !isfinite(result.height) ||
        !isfinite(result.relief) || result.width <= 0 || result.height <= 0) {
        return false;
    }
    *appearance = result;
    *angle = -heading - (visual ? visual->yaw : 0);
    return isfinite(*angle);
}

bool render_geometry_2d_bounds(const ecs_world *world, ecs_entity entity, CF_V2 *min, CF_V2 *max)
{
    if (!Transform_has(world, entity)) {
        return false;
    }
    GlobalTransform2D pose = transform_world_2d(world, entity);
    CF_V2 lo, hi;
    const Tilemap *map = Tilemap_get_const(world, entity);
    if (map) {
        if (!tilemap_bounds(
                map->columns,
                map->rows,
                map->frame_width,
                map->frame_height,
                (CF_V2){ pose.position.x, pose.position.y },
                pose.radians,
                &lo,
                &hi
            )) {
            return false;
        }
    } else {
        char error[128];
        if (!render_geometry_2d_mesh_bounds(
                world,
                entity,
                &(Transform){ pose.position.x, pose.position.y },
                &lo,
                &hi,
                error,
                sizeof error
            )) {
            const Appearance *appearance = Appearance_get_const(world, entity);
            if (!appearance) {
                return false;
            }
            Appearance body = *appearance;
            const Shader *shader = Shader_get_const(world, entity);
            float extent[2];
            if (shader &&
                render_shader_bounds(render_shader_find(shader->shape), extent)) {
                body.shape = SHAPE_BOX;
                body.width = 2 * (shader->bounds_x > 0 ? shader->bounds_x : extent[0]);
                body.height = 2 * (shader->bounds_y > 0 ? shader->bounds_y
                                                        : extent[1]);
            }
            if (!appearance_rotated_bounds(&body, -pose.radians, &lo, &hi)) {
                return false;
            }
            CF_V2 local_lo = lo;
            lo = (CF_V2){ pose.position.x + lo.x, pose.position.y - hi.y };
            hi = (CF_V2){ pose.position.x + hi.x, pose.position.y - local_lo.y };
        }
    }
    if (!isfinite(lo.x) || !isfinite(lo.y) || !isfinite(hi.x) || !isfinite(hi.y)) {
        return false;
    }
    *min = lo;
    *max = hi;
    return true;
}

static bool tilemap_contains(const Tilemap *map, GlobalTransform2D pose, CF_V2 point)
{
    CF_V2 min, max;
    if (!tilemap_bounds(
            map->columns,
            map->rows,
            map->frame_width,
            map->frame_height,
            (CF_V2){ pose.position.x, pose.position.y },
            pose.radians,
            &min,
            &max
        )) {
        return false;
    }
    CF_V2 local;
    return appearance_rotate_point(
               (CF_V2){ point.x - pose.position.x, point.y - pose.position.y },
               -pose.radians,
               &local
           ) &&
           local.x >= 0 && local.y >= 0 &&
           local.x < (float)map->columns * map->frame_width &&
           local.y < (float)map->rows * map->frame_height;
}

ecs_entity render_geometry_2d_pick(ecs_world *world, CF_V2 point, float time, render_skip skip)
{
    ecs_entity picked = { 0 };
    if (!world || !isfinite(point.x) || !isfinite(point.y) || !isfinite(time)) {
        return picked;
    }
    ecs_component_id required[] = { COMPONENT_Transform.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 1) != ECS_OK) {
        return picked;
    }
    int picked_layer = INT_MIN;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const RenderState *state = RenderState_get_const(world, entity);
        if ((state && state->hidden) || render_skip_has(skip, entity)) {
            continue;
        }
        const Tilemap *map = Tilemap_get_const(world, entity);
        const MeshSurface2D *mesh = MeshSurface2D_get_const(world, entity);
        const SdfSurface3D *sdf = SdfSurface3D_get_const(world, entity);
        const Appearance *appearance = Appearance_get_const(world, entity);
        GlobalTransform2D pose = transform_world_2d(world, entity);
        bool hit = false;
        if (map) {
            hit = tilemap_contains(map, pose, point);
        } else if ((mesh && mesh->enabled) || (sdf && sdf->enabled)) {
            render_mesh_surface surface = { 0 };
            char error[128];
            float height;
            hit = render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error) &&
                  render_mesh_surface_pick(&surface, point, &height);
            render_mesh_surface_free(&surface);
        } else if (appearance) {
            CF_V2 local;
            if (appearance_rotate_point(
                    (CF_V2){ point.x - pose.position.x,
                             -(point.y - pose.position.y) },
                    pose.radians,
                    &local
                )) {
                const Shader *shader = Shader_get_const(world, entity);
                if (shader &&
                    render_shader_is_shape(render_shader_find(shader->shape))) {
                    hit = render_shader_contains(
                        shader,
                        world,
                        entity,
                        (float[2]){ local.x, local.y },
                        (float[2]){ point.x, -point.y },
                        time
                    );
                } else {
                    Appearance body = *appearance;
                    if (body.shape == SHAPE_STROKED_CHEVRON) {
                        body.shape = SHAPE_CHEVRON;
                    }
                    hit = appearance_contains(&body, local);
                }
            }
        }
        int layer = map ? map->layer : appearance ? appearance->layer : 0;
        if (hit && (!picked.slot || layer > picked_layer ||
                    (layer == picked_layer && entity.slot > picked.slot))) {
            picked = entity;
            picked_layer = layer;
        }
    }
    ecs_query_end(&query);
    return picked;
}
