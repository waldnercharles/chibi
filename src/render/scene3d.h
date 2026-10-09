#ifndef CHIBI_RENDER_SCENE3D_H
#define CHIBI_RENDER_SCENE3D_H

#include "ecs/ecs.h"
#include "render/skip.h"
#include "runtime/components.h"
#include "runtime/particles.h"

#include <cute_math3d.h>

typedef struct render_scene3d_camera
{
    CF_M4x4 view, projection;
} render_scene3d_camera;

/* Framing uses the existing canvas coordinates: center_y points up. */
render_scene3d_camera render_scene3d_camera_make(
    float center_x,
    float center_y,
    float span_width,
    float span_height
);
CF_V3 render_scene3d_project(const render_scene3d_camera *camera, CF_V3 world);
CF_Ray3 render_scene3d_ray(const render_scene3d_camera *camera, CF_V2 ndc);
bool render_scene3d_ground(const render_scene3d_camera *camera, CF_V2 ndc, CF_V2 *game);
/* Rotated visual bounds; corner bits select positive X, Z and Y respectively. */
bool render_scene3d_entity_bounds(const ecs_world *world, ecs_entity entity, CF_V3 corners[8]);
/* Projected bounds in NDC, clipped to the camera's near/far planes. */
bool render_scene3d_screen_bounds(
    const render_scene3d_camera *camera,
    const CF_V3 corners[8],
    CF_Aabb *bounds
);
/* Canvas framing rectangle with a 20 percent fit margin. */
CF_Aabb render_scene3d_fit(const CF_V3 *points, int count, float aspect);

/* The nearest entity under `ndc` that `skip` does not hold. */
ecs_entity render_scene3d_pick(
    ecs_world *world,
    const render_scene3d_camera *camera,
    CF_V2 ndc,
    render_skip skip
);

struct engine;
typedef struct render_scene3d render_scene3d;
typedef struct
{
    int entities, tasks, workers;
} render_scene3d_work;
render_scene3d *render_scene3d_new();
void render_scene3d_set_parallel(render_scene3d *scene3d, bool enabled);
render_scene3d_work render_scene3d_last_work(const render_scene3d *scene3d);
void render_scene3d_free(render_scene3d *scene3d);
void render_scene3d_reset(render_scene3d *scene3d);
const char *render_scene3d_error(const render_scene3d *scene3d);
/* Retained simulation particles plus renderer emitter tails and attached heads. */
int render_scene3d_particle_count(const render_scene3d *scene3d);
/* Submits scene-linear opaque solids followed by sorted transparent surfaces
 * into the caller's HDR depth-enabled canvas. Time is simulation seconds. */
bool render_scene3d_draw(
    render_scene3d *scene3d,
    ecs_world *world,
    const game_particles *particles,
    const render_scene3d_camera *camera,
    float time,
    const struct engine *runtime,
    render_skip skip
);
#define RENDER_SCENE3D_PREVIEW_RADIUS 50.0f
/* Scene inputs a lone preview surface would otherwise read as zero. */
typedef struct
{
    float beat_phase, beat_pulse;
    float influence[3]; /* Linear color at the origin, fading out at the preview radius. */
} render_scene3d_activity;
/* Stores an owned preparation diagnostic supplied by the host. */
void render_scene3d_set_error(render_scene3d *, const char *message);
/* The same sphere for a material the catalog already holds, by its id. */
bool render_scene3d_preview_material(
    render_scene3d *scene3d,
    int material,
    const render_scene3d_camera *camera,
    const char *parameters_json,
    float time,
    const render_scene3d_activity *activity
);

#endif
