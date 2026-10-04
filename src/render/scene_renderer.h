#ifndef CHIBI_APP_SCENE_RENDERER_H
#define CHIBI_APP_SCENE_RENDERER_H
#include "render/arena3d.h"
#include "render/material_raster.h"
#include "render/particles.h"
#include "render/radiance.h"
#include "runtime/engine.h"

#include <stdbool.h>
typedef struct app_scene_renderer app_scene_renderer;
/* Call once after creating the Cute app, before drawing scenes. */
void app_scene_renderer_initialize();
app_scene_renderer *app_scene_renderer_new();
void app_scene_renderer_free(app_scene_renderer *renderer);
void app_scene_renderer_set_parallel(app_scene_renderer *renderer, bool enabled);
render_arena_work app_scene_renderer_last_work(const app_scene_renderer *renderer);
void app_scene_renderer_set_sdf_batch_limit(app_scene_renderer *renderer, int limit);
/* Capture overrides. Without these, the scene's authored settings apply. */
void app_scene_renderer_set_hidden_rejection(app_scene_renderer *renderer, bool enabled);
void app_scene_renderer_set_spatial_scratch(app_scene_renderer *renderer, bool enabled);
void app_scene_renderer_set_surface_quality(app_scene_renderer *renderer, GameSurfaceQuality quality);
void app_scene_renderer_set_sdf_trace_debug(app_scene_renderer *renderer, bool enabled);
void app_scene_renderer_set_shadow_mode(app_scene_renderer *renderer, GameHRCShadowMode mode);
/* Bits 0..3 override relief fusion, cache, reprojection and hierarchy. */
void app_scene_renderer_set_horizon_reuse(app_scene_renderer *renderer, unsigned mask, GameHRC2D values);
render_surface_work app_scene_renderer_surface_work(const app_scene_renderer *renderer);
render_horizon_work app_scene_renderer_horizon_work(const app_scene_renderer *renderer);
render_mesh_material_work app_scene_renderer_mesh_work(const app_scene_renderer *renderer);
void app_scene_renderer_reset(app_scene_renderer *renderer);
const char *app_scene_renderer_error(const app_scene_renderer *renderer);
/* Settings the renderer adjusted to run, described for authors; empty if none. */
const char *app_scene_renderer_warning(const app_scene_renderer *renderer);
int app_scene_renderer_particle_count(const app_scene_renderer *renderer);
/* Lighting in use: its grids, adaptive's step and its GPU time. False when the
 * backend has no HRC. */
bool app_scene_renderer_lighting_status(const app_scene_renderer *renderer, render_radiance_status *status);
float app_scene_renderer_time(const app_scene_renderer *renderer);
void app_scene_renderer_set_time(app_scene_renderer *renderer, float time);
/* Draws the material composite into the current viewport camera. */
bool app_scene_renderer_draw(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    engine_viewport *viewport,
    const engine *runtime
);
bool app_scene_renderer_draw3d(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    const render_arena_camera *camera,
    engine_viewport *viewport,
    const engine *runtime
);
#endif
