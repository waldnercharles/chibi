#ifndef CHIBI_TEST_COMMAND_FIXTURE_H
#define CHIBI_TEST_COMMAND_FIXTURE_H

#include "editor/workspace/scene_internal.h"

/* A windowless editor whose scene has Player and Enemy prototypes, used by
 * Ship and Foe, and no selection. */
typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
} command_fixture;

command_fixture command_fixture_new();
void command_fixture_free(command_fixture *f);
/* Submit a natural language command; the reply is owned by the caller. */
char *command_fixture_submit(app_scene_authoring *scene, const char *goal);
/* Polls a pending reply until the model answers; frees `reply`. */
char *command_fixture_settled(app_scene_authoring *scene, char *reply);

#endif
