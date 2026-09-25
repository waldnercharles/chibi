#ifndef CHIBI_EDITOR_COMMAND_INTERNAL_H
#define CHIBI_EDITOR_COMMAND_INTERNAL_H

#include "editor/commands/command.h"
#include "editor/workspace/scene_internal.h"

#include <yyjson.h>

const char *command_string(yyjson_val *object, const char *key);
yyjson_val *command_find(yyjson_val *array, const char *id);
bool command_value(yyjson_val *json, pin_value old, pin_value *out);
void command_hash(const char *json, char out[17]);
/* The registered UI hooks, or NULL. */
const editor_command_hooks *command_hooks(app_scene_authoring *scene);
uint64_t command_incarnation(app_scene_authoring *scene);

/* The document a command changed and where it left that document's history
 * (history.c). */
typedef enum
{
    COMMAND_EDITED_NONE,
    COMMAND_EDITED_SCENE,
    COMMAND_EDITED_PROTOTYPE,
    COMMAND_EDITED_GRAPH, /* the graph open in the graph editor */
} command_edited;

typedef struct
{
    command_edited edited;
    char *name;        /* the scene tab key or prototype name */
    uint64_t position; /* the scene history group, or the undo depth */
    const void *top;   /* the newest prototype or graph undo snapshot */
    char hash[17];     /* the prototype or graph right after the change */
} command_mark;

command_mark command_mark_take(app_scene_authoring *scene, command_edited edited, const char *name);
/* Whether the change is still its document's latest edit. Cheap enough for
 * every frame; command_mark_undo also checks the document itself. */
bool command_mark_latest(app_scene_authoring *scene, const command_mark *mark);
/* Undoes the change while it is the latest edit; -1 otherwise. */
int command_mark_undo(app_scene_authoring *scene, const command_mark *mark);
void command_mark_free(command_mark *mark);

/* Authoring operations run alone in a transaction (creation.c). */
bool command_authoring_operation(const char *op);
int command_authoring_apply(
    app_scene_authoring *scene,
    yyjson_val *catalog,
    yyjson_val *operation,
    bool dry,
    char *error,
    int size
);
int command_input(app_scene_authoring *scene, yyjson_val *operation, bool dry, char *error, int size);
char *command_inspect(app_scene_authoring *scene, const char *query, yyjson_val *kinds);
char *command_resolve_objects(app_scene_authoring *scene, const char *query, yyjson_val *kinds);

#endif
