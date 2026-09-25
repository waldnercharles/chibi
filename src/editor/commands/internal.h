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
