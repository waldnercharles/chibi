#include "render/geometry_2d.h"

#include "render/mesh_surface.h"
#include "runtime/component_catalog.h"

#include <assert.h>
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
