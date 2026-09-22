#ifndef CHIBI_APP_SCENE_RENDERER_H
#define CHIBI_APP_SCENE_RENDERER_H
#include "render/arena3d.h"
#include "render/particles.h"
#include "runtime/engine.h"

#include <stdbool.h>
typedef struct app_scene_renderer app_scene_renderer;
/* Call once after creating the Cute app, before creating scene renderers. */
void app_scene_renderer_initialize();
unsigned app_scene_renderer_shader_compile_count();
app_scene_renderer *app_scene_renderer_new();
void app_scene_renderer_free(app_scene_renderer *renderer);
void app_scene_renderer_set_parallel(app_scene_renderer *renderer, bool enabled);
render_arena_work app_scene_renderer_last_work(const app_scene_renderer *renderer);
void app_scene_renderer_reset(app_scene_renderer *renderer);
const char *app_scene_renderer_error(const app_scene_renderer *renderer);
int app_scene_renderer_particle_count(const app_scene_renderer *renderer);
float app_scene_renderer_hrc_scale(const app_scene_renderer *renderer);
float app_scene_renderer_time(const app_scene_renderer *renderer);
void app_scene_renderer_set_time(app_scene_renderer *renderer, float time);
bool app_scene_renderer_preview(
    app_scene_renderer *renderer,
    const behavior_asset *asset,
    float time,
    int width,
    int height
);
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
