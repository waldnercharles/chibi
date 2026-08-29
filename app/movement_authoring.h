#ifndef CBLUEPRINT_APP_MOVEMENT_AUTHORING_H
#define CBLUEPRINT_APP_MOVEMENT_AUTHORING_H

#include "../ecs/ecs.h"
#include "../editor/editor.h"
#include "../engine/engine.h"
#include "../game/system.h"

#include <stdbool.h>

/* The branch's only authored runtime: one Entity, one System, one graph. */
typedef struct movement_authoring movement_authoring;

movement_authoring *movement_authoring_new(
    ecs_world *world,
    ecs_entity player,
    game_system *system,
    editor *editor,
    engine *runtime
);
void movement_authoring_free(movement_authoring *authoring);
void movement_authoring_sync(movement_authoring *authoring);
void movement_authoring_input(movement_authoring *authoring, bool viewport_focused);
void movement_authoring_system(ecs_world *, double monotonic_time, void *user);
void movement_authoring_draw(movement_authoring *authoring);
void movement_authoring_render(void *user);
int movement_authoring_save(editor *editor, void *user);

#endif
