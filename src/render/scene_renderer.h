#ifndef CHIBI_APP_SCENE_RENDERER_H
#define CHIBI_APP_SCENE_RENDERER_H
#include "render/material_raster.h"
#include "render/radiance.h"
#include "render/scene3d.h"
#include "render/viewport.h"
#include "runtime/engine.h"
#include "runtime/particles.h"

#include <stdbool.h>
typedef struct app_scene_renderer app_scene_renderer;
/* Call once after creating the Cute app, before drawing scenes. */
void app_scene_renderer_initialize();
app_scene_renderer *app_scene_renderer_new();
void app_scene_renderer_free(app_scene_renderer *renderer);
void app_scene_renderer_set_parallel(app_scene_renderer *renderer, bool enabled);
render_scene3d_work app_scene_renderer_last_work(const app_scene_renderer *renderer);
void app_scene_renderer_set_model_inspection(
    app_scene_renderer *renderer,
    ecs_entity entity,
    int part,
    bool section,
    const float normal[3],
    float offset
);
void app_scene_renderer_set_sdf_batch_limit(app_scene_renderer *renderer, int limit);
/* Capture overrides. Without these, the scene's authored settings apply. */
void app_scene_renderer_set_hidden_rejection(app_scene_renderer *renderer, bool enabled);
void app_scene_renderer_set_spatial_scratch(app_scene_renderer *renderer, bool enabled);
void app_scene_renderer_set_surface_quality(app_scene_renderer *renderer, SurfaceQuality quality);
void app_scene_renderer_set_sdf_trace_debug(app_scene_renderer *renderer, bool enabled);
void app_scene_renderer_set_shadow_mode(app_scene_renderer *renderer, HRCShadowMode mode);
/* HRC's lab settings, render_radiance_lab_defaults() until set. */
void app_scene_renderer_set_lab(app_scene_renderer *renderer, render_radiance_lab lab);
render_surface_work app_scene_renderer_surface_work(const app_scene_renderer *renderer);
render_horizon_work app_scene_renderer_horizon_work(const app_scene_renderer *renderer);
render_mesh_material_work app_scene_renderer_mesh_work(const app_scene_renderer *renderer);
void app_scene_renderer_reset(app_scene_renderer *renderer);
const char *app_scene_renderer_error(const app_scene_renderer *renderer);
/* Settings the renderer adjusted to run, described for authors; empty if none. */
const char *app_scene_renderer_warning(const app_scene_renderer *renderer);
int app_scene_renderer_particle_count(const app_scene_renderer *renderer);
/* The shapes and mask pieces the last 2D draw rasterized. */
int app_scene_renderer_material_pieces(const app_scene_renderer *renderer);
/* Lighting in use: its grids, adaptive's step and its GPU time. False when the
 * view is unlit, the scene has no HRC2D or the backend has no HRC. */
bool app_scene_renderer_lighting_status(const app_scene_renderer *renderer, render_radiance_status *status);
/* What an editor's view leaves out of the game's presentation. Unlit keeps
 * the material raster and skips HRC lighting in the 2D viewport. Without fog,
 * the fog of war neither draws nor hides objects. A new renderer shows both,
 * as games do. */
void app_scene_renderer_set_view(app_scene_renderer *renderer, bool unlit, bool fogless);
float app_scene_renderer_time(const app_scene_renderer *renderer);
void app_scene_renderer_set_time(app_scene_renderer *renderer, float time);
/* A positive `increment` draws an editor's grid (render/grid_2d.h) under the
 * scene from the next draw on, at that finest spacing; zero hides it. */
void app_scene_renderer_set_grid(app_scene_renderer *renderer, float increment);
/* Draws the material composite into the current viewport camera, without
 * the entities `skip` holds. */
bool app_scene_renderer_draw(
    app_scene_renderer *renderer,
    ecs_world *world,
    const particles *particles,
    engine_viewport *viewport,
    const engine *runtime,
    render_skip skip
);
bool app_scene_renderer_draw3d(
    app_scene_renderer *renderer,
    ecs_world *world,
    const particles *particles,
    const render_scene3d_camera *camera,
    engine_viewport *viewport,
    const engine *runtime,
    render_skip skip
);
#endif
