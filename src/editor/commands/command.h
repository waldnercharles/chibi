#ifndef CHIBI_EDITOR_COMMANDS_H
#define CHIBI_EDITOR_COMMANDS_H

#include "editor/workspace/scene_authoring.h"

/* Editor UI actions outside the authoring model, registered by the host UI.
 * Without hooks the panel, frame, undo and redo operations are unavailable. */
typedef struct
{
    void *user;
    const char *const *panels;
    int panel_count;
    bool (*panel_visible)(void *user, int panel);
    void (*show_panel)(void *user, int panel, bool visible);
    /* The active document's history, as the UI's Undo and Redo. */
    void (*history)(void *user, bool redo);
    /* Frame the selected scene instance. */
    void (*frame)(void *user);
} editor_command_hooks;

/* JSON responses are owned by the caller. All editor access is on its frame thread. */
char *editor_commands_catalog(app_scene_authoring *scene);
char *editor_commands_api(app_scene_authoring *scene, const char *request);
void editor_commands_frame(app_scene_authoring *scene);
void editor_commands_free(app_scene_authoring *scene);
/* NULL hooks unregister them. */
void editor_commands_set_hooks(app_scene_authoring *scene, const editor_command_hooks *hooks);
const char *editor_commands_result(app_scene_authoring *scene);
int editor_commands_apply(
    app_scene_authoring *scene,
    const char *catalog,
    const char *proposal,
    char *error,
    int size
);

#endif
