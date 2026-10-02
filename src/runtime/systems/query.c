#include "runtime/systems/query.h"

#include "support/asset_text.h"

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

av_value *game_query_save_value(
    av_doc *doc,
    const ecs_query_expr *expression,
    game_query_component_resolver resolve,
    game_query_component_name_resolver name_resolver,
    void *user
)
{
    if (!expression || !resolve || !name_resolver) {
        return NULL;
    }
    const char *kind = kind_name(ecs_query_expr_get_kind(expression));
    if (!kind) {
        return NULL;
    }
    av_value *value = av_obj(doc);
    av_obj_add_str(doc, value, "op", kind);
    if (ecs_query_expr_get_kind(expression) == ECS_QUERY_HAS) {
        const char *component = name_resolver(ecs_query_expr_get_component(expression), user);
        const ecs_component_desc *resolved = component ? resolve(component, user)
                                                       : NULL;
        if (!component || !resolved ||
            resolved->id != ecs_query_expr_get_component(expression)) {
            return NULL;
        }
        av_obj_add_str(doc, value, "component", component);
        return value;
    }
    av_value *terms = av_arr(doc);
    for (int i = 0; i < ecs_query_expr_child_count(expression); i++) {
        av_value *child = game_query_save_value(
            doc,
            ecs_query_expr_child(expression, i),
            resolve,
            name_resolver,
            user
        );
        if (!child) {
            return NULL;
        }
        av_arr_add_val(terms, child);
    }
    av_obj_add_val(doc, value, "terms", terms);
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
    av_doc *doc = av_doc_new();
    av_value *root = game_query_save_value(doc, expression, resolve, name, user);
    if (!root) {
        av_doc_free(doc);
        return NULL;
    }
    av_doc_set_root(doc, root);
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

typedef struct
{
    game_query_component_resolver resolve;
    void *user;
    char *error;
    size_t error_size;
} load_context;

static ecs_query_expr *load_value(load_context *context, av_value *value)
{
    if (!av_is_obj(value)) {
        set_error(context->error, context->error_size, "query term must be an object");
        return NULL;
    }
    av_value *op_value = av_obj_get(value, "op");
    if (!av_is_str(op_value)) {
        set_error(context->error, context->error_size, "query term is missing op");
        return NULL;
    }
    const char *op = av_get_str(op_value);
    if (!strcmp(op, "has")) {
        av_obj_iter keys = av_obj_iter_with(value);
        av_value *key;
        while ((key = av_obj_iter_next(&keys))) {
            const char *name = av_get_str(key);
            if (strcmp(name, "op") != 0 && strcmp(name, "component") != 0) {
                set_error(context->error, context->error_size, "unknown has query key %s", name);
                return NULL;
            }
        }
        av_value *component_value = av_obj_get(value, "component");
        if (!av_is_str(component_value)) {
            set_error(context->error, context->error_size, "has term is missing component");
            return NULL;
        }
        const ecs_component_desc
            *component = context->resolve(av_get_str(component_value), context->user);
        if (!component) {
            set_error(
                context->error,
                context->error_size,
                "unknown query component %s",
                av_get_str(component_value)
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
    av_obj_iter keys = av_obj_iter_with(value);
    av_value *key;
    while ((key = av_obj_iter_next(&keys))) {
        const char *name = av_get_str(key);
        if (strcmp(name, "op") != 0 && strcmp(name, "terms") != 0) {
            set_error(context->error, context->error_size, "unknown %s query key %s", op, name);
            return NULL;
        }
    }
    av_value *terms = av_obj_get(value, "terms");
    if (!av_is_arr(terms)) {
        set_error(context->error, context->error_size, "%s query is missing terms", op);
        return NULL;
    }
    ecs_query_expr *expression = ecs_query_expr_new(kind);
    av_value *term;
    av_arr_iter iter;
    av_arr_iter_init(terms, &iter);
    while ((term = av_arr_iter_next(&iter))) {
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

ecs_query_expr *game_query_load_value(
    av_value *value,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    if (!value || !resolve) {
        set_error(error, error_size, "query value or resolver is missing");
        return NULL;
    }
    load_context context = { resolve, user, error, error_size };
    return load_value(&context, value);
}

ecs_query_expr *game_query_load(
    const char *text,
    game_query_component_resolver resolve,
    void *user,
    char *error,
    size_t error_size
)
{
    av_doc *doc = av_doc_read(text, error, error_size);
    if (!doc) {
        return NULL;
    }
    ecs_query_expr *expression = game_query_load_value(av_doc_root(doc), resolve, user, error, error_size);
    av_doc_free(doc);
    return expression;
}
