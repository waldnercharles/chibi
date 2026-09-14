#ifndef CHIBI_RENDER_ARENA3D_H
#define CHIBI_RENDER_ARENA3D_H

#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "render/particles.h"
#include "runtime/components.h"

#include <cute_math3d.h>

typedef struct render_arena_camera
{
    CF_M4x4 view, projection;
} render_arena_camera;

/* Framing uses the existing canvas coordinates: center_y points up. */
render_arena_camera render_arena_camera_make(float center_x, float center_y, float span_width, float span_height);
/* Temporary arena presentation tilt, parameterized by elapsed presentation seconds. */
render_arena_camera render_arena_camera_make_showcase(
    float center_x,
    float center_y,
    float span_width,
    float span_height,
    float time
);
CF_V3 render_arena_project(const render_arena_camera *camera, CF_V3 world);
CF_Ray3 render_arena_ray(const render_arena_camera *camera, CF_V2 ndc);
bool render_arena_ground(const render_arena_camera *camera, CF_V2 ndc, CF_V2 *game);
/* Rotated visual bounds; corner bits select positive X, Z and Y respectively. */
bool render_arena_entity_bounds(const ecs_world *world, ecs_entity entity, CF_V3 corners[8]);
/* Projected bounds in NDC, clipped to the camera's near/far planes. */
bool render_arena_screen_bounds(
    const render_arena_camera *camera,
    const CF_V3 corners[8],
    CF_Aabb *bounds
);
/* Canvas framing rectangle with a 20 percent fit margin. */
CF_Aabb render_arena_fit(const CF_V3 *points, int count, float aspect);
ecs_entity render_arena_pick(ecs_world *world, const render_arena_camera *camera, CF_V2 ndc);

struct engine;
typedef struct render_arena render_arena;
render_arena *render_arena_new();
void render_arena_free(render_arena *arena);
void render_arena_reset(render_arena *arena);
const char *render_arena_error(const render_arena *arena);
/* Retained simulation particles plus renderer emitter tails and attached heads. */
int render_arena_particle_count(const render_arena *arena);
/* Submits scene-linear opaque solids followed by sorted transparent surfaces
 * into the caller's HDR depth-enabled canvas. Time is simulation seconds. */
bool render_arena_draw(
    render_arena *arena,
    ecs_world *world,
    const game_particles *particles,
    const render_arena_camera *camera,
    float time,
    const struct engine *runtime
);
/* A sphere of diameter 100 at the origin, using the same surface path. */
bool render_arena_preview(
    render_arena *arena,
    const behavior_asset *asset,
    const render_arena_camera *camera,
    const char *parameters_json,
    float time
);

#endif
