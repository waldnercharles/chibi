#ifndef CBLUEPRINT_APP_SCENE_RENDERER_H
#define CBLUEPRINT_APP_SCENE_RENDERER_H
#include "../engine/engine.h"
#include "../game/particles.h"

#include <stdbool.h>
typedef struct app_scene_renderer app_scene_renderer;
/* Call once after creating the Cute app, before creating scene renderers. */
void app_scene_renderer_initialize();
app_scene_renderer *app_scene_renderer_new();
void app_scene_renderer_free(app_scene_renderer *renderer);
void app_scene_renderer_reset(app_scene_renderer *renderer);
/* Draws the material composite into the current viewport camera. */
bool app_scene_renderer_draw(
    app_scene_renderer *renderer,
    ecs_world *world,
    const game_particles *particles,
    engine_viewport *viewport
);
#endif
