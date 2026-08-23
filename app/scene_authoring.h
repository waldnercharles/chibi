#ifndef CBLUEPRINT_APP_SCENE_AUTHORING_H
#define CBLUEPRINT_APP_SCENE_AUTHORING_H

#include "../engine/engine.h"
#include "../game/live.h"

#include <stdint.h>

typedef struct app_scene_authoring app_scene_authoring;

app_scene_authoring *app_scene_authoring_new(
    ecs_world *world,
    game_live_world *live,
    game_live_definition *definition,
    const blueprint *editor_working,
    engine *runtime
);
void app_scene_authoring_set_editor_working(app_scene_authoring *scene, const blueprint *editor_working);
void app_scene_authoring_free(app_scene_authoring *scene);
void app_scene_authoring_draw(app_scene_authoring *scene);
int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time);
int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed);

#endif
