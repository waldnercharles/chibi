#ifndef CHIBI_EDITOR_COMMANDS_H
#define CHIBI_EDITOR_COMMANDS_H

#include "editor/workspace/scene_authoring.h"

/* JSON responses are owned by the caller. All editor access is on its frame thread. */
char *editor_commands_catalog(app_scene_authoring *scene);
char *editor_commands_api(app_scene_authoring *scene, const char *request);
void editor_commands_frame(app_scene_authoring *scene);
void editor_commands_free(app_scene_authoring *scene);
const char *editor_commands_result(app_scene_authoring *scene);
int editor_commands_apply(
    app_scene_authoring *scene,
    const char *catalog,
    const char *proposal,
    char *error,
    int size
);

#endif
