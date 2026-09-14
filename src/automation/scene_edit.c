#include "automation/scene_edit.h"

#include "assets/scene_document.h"
#include "support/array.h"
#include "third_party/yyjson/yyjson.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SCENE_EDIT_MAX_OPERATIONS = 128,
    SCENE_EDIT_MAX_ENTITIES = 256,
    SCENE_EDIT_MAX_BEHAVIORS = 64,
};

typedef struct
{
    game_scene_document *scene;
    yyjson_mut_doc *doc;
    yyjson_mut_val *result;
    char *error;
    size_t error_size;
    int operation;
} edit_context;

static int fail(edit_context *context, int status, const char *format, ...)
{
    if (context->error && context->error_size) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(context->error, context->error_size, format, arguments);
        va_end(arguments);
    }
    yyjson_mut_obj_add_int(
        context->doc,
        context->result,
        "failed_operation",
        context->operation
    );
    yyjson_mut_obj_add_strcpy(
        context->doc,
        context->result,
        "error",
        context->error ? context->error : "scene edit failed"
    );
    return status;
}

static const char *required_string(edit_context *context, yyjson_val *operation, const char *key)
{
    yyjson_val *value = yyjson_obj_get(operation, key);
    if (!yyjson_is_str(value) || !yyjson_get_len(value) || yyjson_get_len(value) > 512) {
        fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "%s must be a non-empty bounded string", key);
        return NULL;
    }
    return yyjson_get_str(value);
}

static bool safe_asset_name(const char *name)
{
    return name && name[0] && strlen(name) <= 512 && !strchr(name, '/') &&
           !strchr(name, '\\') && !strstr(name, "..");
}

static int entity_index(edit_context *context, yyjson_val *operation)
{
    const char *name = required_string(context, operation, "entity");
    if (!name) {
        return -1;
    }
    int index = game_scene_document_entity_index(context->scene, name);
    if (index < 0) {
        fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "scene entity %s does not exist", name);
    }
    return index;
}

static int object_json(edit_context *context, yyjson_val *value, char **json, const char *key)
{
    if (!yyjson_is_obj(value)) {
        fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "%s must be an object", key);
        return -1;
    }
    *json = yyjson_val_write(value, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    if (!*json) {
        fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "%s could not be serialized", key);
        return -1;
    }
    return 0;
}

static void free_behavior_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

static int behavior_names(edit_context *context, yyjson_val *value, char ***names, const char *key)
{
    *names = NULL;
    if (!value) {
        return 0;
    }
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > SCENE_EDIT_MAX_BEHAVIORS) {
        fail(
            context,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            "%s must be an array with at most %d entries",
            key,
            SCENE_EDIT_MAX_BEHAVIORS
        );
        return -1;
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(value);
    yyjson_val *entry;
    while ((entry = yyjson_arr_iter_next(&iterator))) {
        if (!yyjson_is_str(entry) || !safe_asset_name(yyjson_get_str(entry))) {
            fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "%s must contain safe asset names", key);
            free_behavior_names(*names);
            *names = NULL;
            return -1;
        }
        for (int i = 0; i < acount(*names); i++) {
            if (!strcmp((*names)[i], yyjson_get_str(entry))) {
                fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "%s contains duplicate names", key);
                free_behavior_names(*names);
                *names = NULL;
                return -1;
            }
        }
        apush(*names, xstrdup(yyjson_get_str(entry)));
    }
    return 0;
}

static int set_entity_components(edit_context *context, yyjson_val *operation, bool retained)
{
    int index = entity_index(context, operation);
    if (index < 0) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    const char *key = retained ? "retained_components" : "components";
    yyjson_val *value = yyjson_obj_get(operation, key);
    char *json = NULL;
    if (object_json(context, value, &json, key) != 0) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    int result = retained
                   ? game_scene_document_set_entity_retained(context->scene, index, json)
                   : game_scene_document_set_entity_components(context->scene, index, json);
    free(json);
    if (result != 0) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "%s could not be applied", key);
    }
    return CHIBI_AUTOMATION_OK;
}

static int add_entity(edit_context *context, yyjson_val *operation)
{
    if (acount(context->scene->entities) >= SCENE_EDIT_MAX_ENTITIES) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "scene has too many entities");
    }
    const char *name = required_string(context, operation, "name");
    const char *prototype = required_string(context, operation, "prototype");
    if (!name || !prototype || !safe_asset_name(prototype)) {
        if (prototype) {
            fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "prototype must be a safe asset name");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    char **names = NULL;
    if (behavior_names(context, yyjson_obj_get(operation, "behaviors"), &names, "behaviors") !=
        0) {
        free_behavior_names(names);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    int result = game_scene_document_add_entity(
        context->scene,
        name,
        prototype,
        (const char *const *)names,
        acount(names)
    );
    free_behavior_names(names);
    if (result != 0) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "scene entity %s already exists", name);
    }
    int index = acount(context->scene->entities) - 1;
    yyjson_val *components = yyjson_obj_get(operation, "components");
    if (components) {
        char *json = NULL;
        if (object_json(context, components, &json, "components") != 0) {
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        result = game_scene_document_set_entity_components(context->scene, index, json);
        free(json);
        if (result != 0) {
            return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "components could not be applied");
        }
    }
    yyjson_val *retained = yyjson_obj_get(operation, "retained_components");
    if (retained) {
        char *json = NULL;
        if (object_json(context, retained, &json, "retained_components") != 0) {
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        result = game_scene_document_set_entity_retained(context->scene, index, json);
        free(json);
        if (result != 0) {
            return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "retained_components could not be applied");
        }
    }
    return CHIBI_AUTOMATION_OK;
}

static int set_entity_behaviors(edit_context *context, yyjson_val *operation)
{
    int index = entity_index(context, operation);
    if (index < 0) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    yyjson_val *behavior_value = yyjson_obj_get(operation, "behaviors");
    if (!behavior_value) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "behaviors is required");
    }
    char **names = NULL;
    if (behavior_names(context, behavior_value, &names, "behaviors") != 0) {
        free_behavior_names(names);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    int result = game_scene_document_set_entity_behaviors(
        context->scene,
        index,
        (const char *const *)names,
        acount(names)
    );
    free_behavior_names(names);
    if (result != 0) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "behaviors could not be applied");
    }
    return CHIBI_AUTOMATION_OK;
}

static int add_or_remove_behavior(edit_context *context, yyjson_val *operation, bool remove)
{
    int index = entity_index(context, operation);
    const char *name = required_string(context, operation, "behavior");
    if (index < 0 || !name) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (!safe_asset_name(name)) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "behavior must be a safe asset name");
    }
    game_scene_entity *entity = &context->scene->entities[index];
    const char **names = NULL;
    bool found = false;
    for (int i = 0; i < acount(entity->behaviors); i++) {
        if (!strcmp(entity->behaviors[i], name)) {
            found = true;
        }
        if (!remove || strcmp(entity->behaviors[i], name)) {
            apush(names, entity->behaviors[i]);
        }
    }
    if ((remove && !found) || (!remove && found)) {
        afree(names);
        return fail(
            context,
            CHIBI_AUTOMATION_VALIDATION_ERROR,
            "behavior %s is %s",
            name,
            remove ? "not attached" : "already attached"
        );
    }
    if (!remove) {
        if (acount(names) >= SCENE_EDIT_MAX_BEHAVIORS) {
            afree(names);
            return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "entity has too many behaviors");
        }
        apush(names, name);
    }
    int result = game_scene_document_set_entity_behaviors(context->scene, index, names, acount(names));
    afree(names);
    if (result != 0) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "behavior list could not be applied");
    }
    return CHIBI_AUTOMATION_OK;
}

static int apply_operation(edit_context *context, yyjson_val *operation)
{
    if (!yyjson_is_obj(operation)) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "operation must be an object");
    }
    yyjson_val *op_value = yyjson_obj_get(operation, "op");
    if (!yyjson_is_str(op_value)) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "op must be a string");
    }
    const char *op = yyjson_get_str(op_value);
    if (!strcmp(op, "set_presentation")) {
        const char *presentation = required_string(context, operation, "presentation");
        if (!presentation) {
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        if (strcmp(presentation, "2d") && strcmp(presentation, "3d")) {
            return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "presentation must be 2d or 3d");
        }
        game_scene_document_set_perspective(context->scene, !strcmp(presentation, "3d"));
        return CHIBI_AUTOMATION_OK;
    }
    if (!strcmp(op, "add_entity")) {
        return add_entity(context, operation);
    }
    if (!strcmp(op, "remove_entity")) {
        int index = entity_index(context, operation);
        return index < 0 ? CHIBI_AUTOMATION_VALIDATION_ERROR
             : game_scene_document_remove_entity(context->scene, index) == 0
                 ? CHIBI_AUTOMATION_OK
                 : fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "entity could not be removed");
    }
    if (!strcmp(op, "set_entity_prototype")) {
        int index = entity_index(context, operation);
        const char *prototype = required_string(context, operation, "prototype");
        if (index < 0 || !prototype) {
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        if (!safe_asset_name(prototype)) {
            return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "prototype must be a safe asset name");
        }
        return game_scene_document_set_entity_prototype(context->scene, index, prototype) == 0
                 ? CHIBI_AUTOMATION_OK
                 : fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "prototype could not be applied");
    }
    if (!strcmp(op, "set_entity_components")) {
        return set_entity_components(context, operation, false);
    }
    if (!strcmp(op, "set_entity_retained_components")) {
        return set_entity_components(context, operation, true);
    }
    if (!strcmp(op, "set_entity_behaviors")) {
        return set_entity_behaviors(context, operation);
    }
    if (!strcmp(op, "add_behavior")) {
        return add_or_remove_behavior(context, operation, false);
    }
    if (!strcmp(op, "remove_behavior")) {
        return add_or_remove_behavior(context, operation, true);
    }
    return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "unsupported scene operation %s", op);
}

int automation_scene_edit(
    const char *source_json,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_json,
    char *error,
    size_t error_size
)
{
    if (edited_json) {
        *edited_json = NULL;
    }
    if (!source_json || !request || !doc || !result || !edited_json) {
        if (error && error_size) {
            snprintf(error, error_size, "scene edit arguments are required");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_val *operations = yyjson_obj_get(request, "operations");
    if (!yyjson_is_arr(operations) ||
        yyjson_arr_size(operations) > SCENE_EDIT_MAX_OPERATIONS) {
        if (error && error_size) {
            snprintf(error, error_size, "operations must be an array with at most %d entries", SCENE_EDIT_MAX_OPERATIONS);
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    char load_error[256] = { 0 };
    game_scene_document *scene = game_scene_document_load(source_json, load_error, sizeof load_error);
    if (!scene) {
        if (error && error_size) {
            snprintf(error, error_size, "%s", load_error);
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    edit_context context = {
        .scene = scene,
        .doc = doc,
        .result = result,
        .error = error,
        .error_size = error_size,
    };
    yyjson_arr_iter iterator = yyjson_arr_iter_with(operations);
    yyjson_val *operation;
    while ((operation = yyjson_arr_iter_next(&iterator))) {
        int status = apply_operation(&context, operation);
        if (status != CHIBI_AUTOMATION_OK) {
            game_scene_document_free(scene);
            return status;
        }
        context.operation++;
    }
    char *saved = game_scene_document_save(scene);
    if (!saved) {
        game_scene_document_free(scene);
        if (error && error_size) {
            snprintf(error, error_size, "scene could not be serialized");
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    game_scene_document *roundtrip = game_scene_document_load(saved, load_error, sizeof load_error);
    if (!roundtrip) {
        free(saved);
        game_scene_document_free(scene);
        if (error && error_size) {
            snprintf(error, error_size, "edited scene failed round-trip validation: %s", load_error);
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    yyjson_mut_obj_add_uint(doc, result, "operation_count", (uint64_t)context.operation);
    yyjson_mut_obj_add_uint(doc, result, "entity_count", (uint64_t)acount(scene->entities));
    yyjson_mut_obj_add_strcpy(doc, result, "presentation", scene->perspective ? "3d" : "2d");
    game_scene_document_free(roundtrip);
    game_scene_document_free(scene);
    *edited_json = saved;
    return CHIBI_AUTOMATION_OK;
}
