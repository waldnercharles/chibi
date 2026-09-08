#include "runtime/systems/query.h"

#include "../third_party/yyjson/yyjson.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t size, const char *format, ...)
{
    if (!error || !size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

static const char *kind_name(ecs_query_expr_kind kind)
{
    switch (kind) {
        case ECS_QUERY_ALL: return "all";
        case ECS_QUERY_ANY: return "any";
        case ECS_QUERY_NOT: return "not";
        case ECS_QUERY_HAS: return "has";
    }
    return NULL;
}

static ecs_query_expr_kind parse_kind(const char *name)
{
    if (!strcmp(name, "all")) {
        return ECS_QUERY_ALL;
    }
    if (!strcmp(name, "any")) {
        return ECS_QUERY_ANY;
    }
    if (!strcmp(name, "not")) {
        return ECS_QUERY_NOT;
    }
    return ECS_QUERY_HAS;
}

static yyjson_mut_val *save_value(
    yyjson_mut_doc *doc,
    const ecs_query_expr *expression,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name_resolver,
    void *user
)
{
    const char *kind = kind_name(ecs_query_expr_get_kind(expression));
    if (!kind) {
        return NULL;
    }
    yyjson_mut_val *value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, value, "op", kind);
    if (ecs_query_expr_get_kind(expression) == ECS_QUERY_HAS) {
        const char *component = name_resolver(ecs_query_expr_get_component(expression), user);
        const ecs_component_desc *resolved = component ? resolve(component, user)
                                                       : NULL;
        if (!component || !resolved ||
            resolved->id != ecs_query_expr_get_component(expression)) {
            return NULL;
        }
        yyjson_mut_obj_add_str(doc, value, "component", component);
        return value;
    }
    yyjson_mut_val *terms = yyjson_mut_arr(doc);
    for (int i = 0; i < ecs_query_expr_child_count(expression); i++) {
        yyjson_mut_val *child = save_value(doc, ecs_query_expr_child(expression, i), resolve, name_resolver, user);
        if (!child) {
            return NULL;
        }
        yyjson_mut_arr_add_val(terms, child);
    }
    yyjson_mut_obj_add_val(doc, value, "terms", terms);
    return value;
}

char *game_query_save(
    const ecs_query_expr *expression,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name,
    void *user
)
{
    if (!expression || !resolve || !name) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = save_value(doc, expression, resolve, name, user);
    if (!root) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_doc_set_root(doc, root);
    size_t size = 0;
    char *json = yyjson_mut_write(doc, 0, &size);
    yyjson_mut_doc_free(doc);
    return json;
}

typedef struct
{
    game_query_component_resolver resolve;
    void *user;
    char *error;
    size_t error_size;
} load_context;

static ecs_query_expr *load_value(load_context *context, yyjson_val *value)
{
    if (!yyjson_is_obj(value)) {
        set_error(context->error, context->error_size, "query term must be an object");
        return NULL;
    }
    yyjson_val *op_value = yyjson_obj_get(value, "op");
    if (!yyjson_is_str(op_value)) {
        set_error(context->error, context->error_size, "query term is missing op");
        return NULL;
    }
    const char *op = yyjson_get_str(op_value);
    if (!strcmp(op, "has")) {
        yyjson_obj_iter keys = yyjson_obj_iter_with(value);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&keys))) {
            const char *name = yyjson_get_str(key);
            if (strcmp(name, "op") != 0 && strcmp(name, "component") != 0) {
                set_error(context->error, context->error_size, "unknown has query key %s", name);
                return NULL;
            }
        }
        yyjson_val *component_value = yyjson_obj_get(value, "component");
        if (!yyjson_is_str(component_value)) {
            set_error(context->error, context->error_size, "has term is missing component");
            return NULL;
        }
        const ecs_component_desc *component = context->resolve(
            yyjson_get_str(component_value),
            context->user
        );
        if (!component) {
            set_error(
                context->error,
                context->error_size,
                "unknown query component %s",
                yyjson_get_str(component_value)
            );
            return NULL;
        }
        return ecs_query_expr_has(component->id);
    }
    ecs_query_expr_kind kind = parse_kind(op);
    if (kind == ECS_QUERY_HAS) {
        set_error(context->error, context->error_size, "unknown query operator %s", op);
        return NULL;
    }
    yyjson_obj_iter keys = yyjson_obj_iter_with(value);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&keys))) {
        const char *name = yyjson_get_str(key);
        if (strcmp(name, "op") != 0 && strcmp(name, "terms") != 0) {
            set_error(context->error, context->error_size, "unknown %s query key %s", op, name);
            return NULL;
        }
    }
    yyjson_val *terms = yyjson_obj_get(value, "terms");
    if (!yyjson_is_arr(terms)) {
        set_error(context->error, context->error_size, "%s query is missing terms", op);
        return NULL;
    }
    ecs_query_expr *expression = ecs_query_expr_new(kind);
    yyjson_val *term;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(terms, &iter);
    while ((term = yyjson_arr_iter_next(&iter))) {
        ecs_query_expr *child = load_value(context, term);
        if (!child || ecs_query_expr_add(expression, child) != ECS_OK) {
            ecs_query_expr_free(child);
            ecs_query_expr_free(expression);
            return NULL;
        }
    }
    if (kind == ECS_QUERY_NOT && ecs_query_expr_child_count(expression) != 1) {
        set_error(context->error, context->error_size, "not query requires one term");
        ecs_query_expr_free(expression);
        return NULL;
    }
    return expression;
}

ecs_query_expr *game_query_load(
    const char *json,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    if (!json || !resolve) {
        set_error(error, error_size, "query JSON or resolver is missing");
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        set_error(error, error_size, "malformed query JSON");
        return NULL;
    }
    load_context context = { resolve, user, error, error_size };
    ecs_query_expr *expression = load_value(&context, yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return expression;
}
