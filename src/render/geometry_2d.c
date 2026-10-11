#include "render/geometry_2d.h"

#include "render/mesh_surface.h"
#include "runtime/component_catalog.h"

#include <assert.h>
#include <math.h>

static bool finite_geometry(const GameGeometry3D *geometry)
{
    return geometry && isfinite(geometry->width) && isfinite(geometry->depth) &&
           geometry->width > 0 && geometry->depth > 0;
}

bool render_geometry_2d_mesh_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    CF_V2 *min,
    CF_V2 *max,
    char *error,
    size_t size
)
{
    const GameMeshSurface2D *mesh = GAME_GameMeshSurface2D_get_const(world, entity);
    const GameSdfSurface3D *sdf = GAME_GameSdfSurface3D_get_const(world, entity);
    return ((mesh && mesh->enabled) || (sdf && sdf->enabled)) &&
           render_mesh_surface_bounds(world, entity, position, min, max, error, size);
}

bool render_geometry_2d_project(
    const GameGeometry3D *geometry,
    const GameAppearance *fallback,
    const GameVisualTransform *visual,
    float heading,
    GameAppearance *appearance,
    float *angle
)
{
    assert(appearance && angle);
    if (!fallback || !isfinite(heading) ||
        (visual && (!isfinite(visual->yaw) || !isfinite(visual->scale) ||
                    visual->scale <= 0))) {
        return false;
    }
    GameAppearance result = *fallback;
    float scale = visual ? visual->scale : 1;
    if (geometry) {
        if (!finite_geometry(geometry)) {
            return false;
        }
        result.width = geometry->width;
        result.height = geometry->depth;
        switch (geometry->primitive) {
            case GAME_PRIMITIVE_BOX: result.shape = GAME_SHAPE_BOX; break;
            case GAME_PRIMITIVE_SPHERE: result.shape = GAME_SHAPE_CIRCLE; break;
            case GAME_PRIMITIVE_CAPSULE:
                result.shape = GAME_SHAPE_CAPSULE;
                break;
            case GAME_PRIMITIVE_OCTAHEDRON:
                result.shape = GAME_SHAPE_DIAMOND;
                break;
            case GAME_PRIMITIVE_CHEVRON_PYRAMID:
                result.shape = GAME_SHAPE_CHEVRON;
                break;
            case GAME_PRIMITIVE_BOX_FRAME:
                result.shape = GAME_SHAPE_BOX;
                if (result.border_width <= 0) {
                    result.border_width = fminf(result.width, result.height) * 0.12f;
                }
                break;
            case GAME_PRIMITIVE_TRIANGLE_OUTLINE:
                result.shape = GAME_SHAPE_STROKED_TRIANGLE;
                if (result.stroke_width <= 0) {
                    result.stroke_width = fminf(result.width, result.height) * 0.1f;
                }
                break;
            case GAME_PRIMITIVE_CONE:
            case GAME_PRIMITIVE_TRIANGLE_PRISM:
            case GAME_PRIMITIVE_TETRAHEDRON:
            case GAME_PRIMITIVE_TRIANGLE:
                result.shape = GAME_SHAPE_TRIANGLE;
                break;
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
