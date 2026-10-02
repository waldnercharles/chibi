#include "automation/prototype_edit.h"

#include "support/array.h"
#include "support/asset_json.h"
#include "third_party/yyjson/yyjson.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PROTOTYPE_EDIT_MAX_OPERATIONS = 128,
    PROTOTYPE_EDIT_MAX_FIELDS = 128,
};

typedef struct
{
    game_prototype *prototype;
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
    if (context->result) {
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
            context->error ? context->error : "prototype edit failed"
        );
    }
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

static const ecs_component_desc *find_component(const game_prototype_registry *registry, const char *name)
{
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *component = registry->components[i];
        if (component && ((component->name && !strcmp(component->name, name)) ||
                          (component->key && !strcmp(component->key, name)))) {
            return component;
        }
    }
    return NULL;
}

static bool valid_behavior_name(const char *name)
{
    return name && name[0] && !strchr(name, '/') && !strchr(name, '\\') &&
           !strstr(name, "..") && strlen(name) <= 512;
}

static int request_write_field(
    game_prototype *prototype,
    const char *component,
    const char *field,
    yyjson_val *value,
    char *error,
    size_t size
)
{
    av_doc *doc = av_doc_new();
    int result = game_prototype_write_field_value(
        prototype,
        component,
        field,
        av_from_json(doc, value),
        error,
        size
    );
    av_doc_free(doc);
    return result;
}

static int add_component(edit_context *context, yyjson_val *operation)
{
    const char *name = required_string(context, operation, "component");
    if (!name) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    const ecs_component_desc *component = find_component(context->prototype->registry, name);
    if (!component) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "component %s is unavailable", name);
    }
    if (game_prototype_add_component(context->prototype, component)) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "component %s is already attached", name);
    }
    yyjson_val *fields = yyjson_obj_get(operation, "fields");
    if (!fields) {
        return CHIBI_AUTOMATION_OK;
    }
    if (!yyjson_is_obj(fields) || yyjson_obj_size(fields) > PROTOTYPE_EDIT_MAX_FIELDS) {
        return fail(
            context,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            "fields must be an object with at most %d entries",
            PROTOTYPE_EDIT_MAX_FIELDS
        );
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(fields);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const char *field = yyjson_get_str(key);
        char field_error[256] = { 0 };
        if (request_write_field(
                context->prototype,
                name,
                field,
                yyjson_obj_iter_get_val(key),
                field_error,
                sizeof field_error
            )) {
            return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "%s", field_error);
        }
    }
    return CHIBI_AUTOMATION_OK;
}

static int remove_component(edit_context *context, yyjson_val *operation)
{
    const char *name = required_string(context, operation, "component");
    if (!name) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    const ecs_component_desc *component = find_component(context->prototype->registry, name);
    if (!component ||
        game_prototype_remove_component(context->prototype, component->name)) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "component %s is not attached", name);
    }
    return CHIBI_AUTOMATION_OK;
}

static int set_field(edit_context *context, yyjson_val *operation)
{
    const char *component = required_string(context, operation, "component");
    const char *field = required_string(context, operation, "field");
    yyjson_val *value = yyjson_obj_get(operation, "value");
    if (!component || !field) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (!value) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "value is required");
    }
    char field_error[256] = { 0 };
    if (request_write_field(context->prototype, component, field, value, field_error, sizeof field_error)) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "%s", field_error);
    }
    return CHIBI_AUTOMATION_OK;
}

static int add_behavior(edit_context *context, yyjson_val *operation)
{
    const char *name = required_string(context, operation, "behavior");
    if (!name) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (!valid_behavior_name(name)) {
        return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "behavior must be a safe asset name");
    }
    if (game_prototype_add_graph(context->prototype, name)) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "behavior %s is already attached", name);
    }
    return CHIBI_AUTOMATION_OK;
}

static int remove_behavior(edit_context *context, yyjson_val *operation)
{
    const char *name = required_string(context, operation, "behavior");
    if (!name) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (game_prototype_remove_graph(context->prototype, name)) {
        return fail(context, CHIBI_AUTOMATION_VALIDATION_ERROR, "behavior %s is not attached", name);
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
    if (!strcmp(op, "add_component")) {
        return add_component(context, operation);
    }
    if (!strcmp(op, "remove_component")) {
        return remove_component(context, operation);
    }
    if (!strcmp(op, "set_field")) {
        return set_field(context, operation);
    }
    if (!strcmp(op, "add_behavior")) {
        return add_behavior(context, operation);
    }
    if (!strcmp(op, "remove_behavior")) {
        return remove_behavior(context, operation);
    }
    return fail(context, CHIBI_AUTOMATION_INVALID_REQUEST, "unsupported prototype operation %s", op);
}

int automation_prototype_edit(
    const char *source_text,
    const game_prototype_registry *registry,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_text,
    char *error,
    size_t error_size
)
{
    if (edited_text) {
        *edited_text = NULL;
    }
    if (!source_text || !registry || !request || !doc || !result || !edited_text) {
        if (error && error_size) {
            snprintf(error, error_size, "prototype edit arguments are required");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_val *operations = yyjson_obj_get(request, "operations");
    if (!yyjson_is_arr(operations) ||
        yyjson_arr_size(operations) > PROTOTYPE_EDIT_MAX_OPERATIONS) {
        if (error && error_size) {
            snprintf(error, error_size, "operations must be an array with at most %d entries", PROTOTYPE_EDIT_MAX_OPERATIONS);
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    game_prototype *prototype = game_prototype_load(source_text, registry, error, error_size);
    if (!prototype) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    edit_context context = {
        .prototype = prototype,
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
            game_prototype_free(prototype);
            return status;
        }
        context.operation++;
    }
    char *saved = game_prototype_save(prototype);
    if (!saved) {
        game_prototype_free(prototype);
        if (error && error_size) {
            snprintf(error, error_size, "prototype could not be serialized");
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    char roundtrip_error[256] = { 0 };
    game_prototype *roundtrip = game_prototype_load(saved, registry, roundtrip_error, sizeof roundtrip_error);
    if (!roundtrip) {
        free(saved);
        game_prototype_free(prototype);
        if (error && error_size) {
            snprintf(error, error_size, "edited prototype failed round-trip validation: %s", roundtrip_error);
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    yyjson_mut_obj_add_uint(doc, result, "operation_count", (uint64_t)context.operation);
    yyjson_mut_obj_add_uint(
        doc,
        result,
        "component_count",
        (uint64_t)acount(prototype->components)
    );
    yyjson_mut_obj_add_uint(
        doc,
        result,
        "behavior_count",
        (uint64_t)acount(prototype->graphs)
    );
    game_prototype_free(roundtrip);
    game_prototype_free(prototype);
    *edited_text = saved;
    return CHIBI_AUTOMATION_OK;
}
