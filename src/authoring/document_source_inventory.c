#include "authoring/document_source_inventory.h"

#include "runtime/text_search.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static const char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    size_t bytes = strlen(text) + 1;
    char *out = arena_alloc(storage, bytes);
    memcpy(out, text, bytes);
    return out;
}
static const char *identity(arena *storage, uint64_t value)
{
    char text[32];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return copy(storage, text);
}
static const char *slice(arena *storage, const char *text, int start, int end)
{
    if (start < 0 || end < start || (size_t)end > strlen(text)) {
        return copy(storage, "");
    }
    char *out = arena_alloc(storage, (size_t)(end - start) + 1);
    memcpy(out, text + start, (size_t)(end - start));
    out[end - start] = 0;
    return out;
}
static bool page(int first, int count, const char *query, char *error, size_t size)
{
    return (first >= 0 && count >= 0 && count <= 256 && query &&
            strlen(query) <= 256 && text_search_contains(query, query, false)) ||
           fail(error, size, "Source inventory requires First>=0, Count in0..256 and a UTF-8 query within256bytes");
}
static const char *kind(script_symbol_kind kind)
{
    switch (kind) {
        case SCRIPT_STATE: return "state";
        case SCRIPT_SHARED_STATE: return "shared-state";
        case SCRIPT_FUNCTION: return "function";
        case SCRIPT_MACRO: return "macro";
        case SCRIPT_EVENT: return "event";
        case SCRIPT_PARAMETER: return "parameter";
        case SCRIPT_RESULT: return "result";
        case SCRIPT_COMPONENT: return "component";
        case SCRIPT_GLOBAL: return "global";
        case SCRIPT_CONSTANT: return "constant";
        case SCRIPT_LOCAL: return "local";
        case SCRIPT_QUERY_VARIABLE: return "query";
        case SCRIPT_TYPE: return "type";
        case SCRIPT_TYPE_PARAMETER: return "type-parameter";
        default: return NULL;
    }
}
static const char *signature(arena *storage, const script_syntax *syntax, const script_analysis *a, int function)
{
    if (function < 0 || function >= a->function_count) {
        return copy(storage, "");
    }
    script_range range = a->functions[function].range;
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)range.start,
        (uint32_t)range.start
    );
    while (!ts_node_is_null(node) &&
           strcmp(ts_node_type(node), "function_declaration") &&
           strcmp(ts_node_type(node), "macro_declaration")) {
        node = ts_node_parent(node);
    }
    TSNode parameters = ts_node_child_by_field_name(node, "parameters", 10),
           results = ts_node_child_by_field_name(node, "results", 7);
    const char *out = copy(storage, "");
    if (!ts_node_is_null(parameters)) {
        TSNode end = ts_node_is_null(results) ? parameters : results;
        out = slice(storage, a->text, (int)ts_node_start_byte(parameters), (int)ts_node_end_byte(end));
    }
    return out;
}
static bool match(const char *query, const char *name, const char *type)
{
    return text_search_contains(query, name, true) ||
           text_search_contains(query, type, true);
}
bool document_source_members(
    const document_semantics *snapshot,
    uint64_t graph_id,
    int first,
    int count,
    const char *query,
    arena *storage,
    document_source_member **rows,
    int *row_count,
    int *total,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *row_count = *total = 0;
    if (size) {
        *error = 0;
    }
    if (!page(first, count, query, error, size)) {
        return false;
    }
    const script_analysis *a = document_semantics_analysis(snapshot);
    const authoring_source_presentation *p = document_semantics_presentation(snapshot);
    if (!a || !p) {
        return fail(error, size, "Source declarations require a Chibi program snapshot");
    }
    const script_catalog *catalog = document_semantics_catalog(snapshot);
    const authoring_navigation *navigation = document_semantics_navigation(snapshot);
    const script_syntax *syntax = authoring_navigation_document_at(navigation, 0)
                                      ->syntax;
    int selected = -1;
    bool found = !graph_id;
    const behavior_asset *asset = document_semantics_graph(snapshot);
    for (int i = 0; i < acount(asset->graphs); i++) {
        found |= asset->graphs[i]->graph_id == graph_id;
    }
    if (!found) {
        return fail(error, size, "Function graph no longer belongs to this source");
    }
    const source_presentation_origin *
        *symbols = xcalloc((size_t)a->symbol_count, sizeof *symbols),
       **functions = xcalloc((size_t)a->function_count, sizeof *functions),
       **bodies = xcalloc((size_t)a->function_count, sizeof *bodies),
       **expressions = xcalloc((size_t)a->expression_count, sizeof *expressions);
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->graph == graph_id && o->function >= 0) {
            selected = o->function;
        }
        if (o->kind == SOURCE_PRESENTATION_EXPRESSION && o->record >= 0 &&
            o->record < a->expression_count && !expressions[o->record]) {
            expressions[o->record] = o;
        }
        if (o->pin) {
            continue;
        }
        if (o->kind == SOURCE_PRESENTATION_SYMBOL && o->record >= 0 &&
            o->record < a->symbol_count && !symbols[o->record]) {
            symbols[o->record] = o;
        }
        if (o->kind == SOURCE_PRESENTATION_FUNCTION && o->record >= 0 &&
            o->record < a->function_count) {
            functions[o->record] = o;
        }
        if (o->function >= 0 && o->function < a->function_count &&
            o->kind == SOURCE_PRESENTATION_STATEMENT &&
            o->record == a->functions[o->function].body) {
            bodies[o->function] = o;
        }
    }
    *rows = arena_alloc(storage, (size_t)count * sizeof **rows);
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *s = &a->symbols[i];
        const char *category = kind(s->kind);
        if (!category || !s->name || s->declaration.end <= s->declaration.start) {
            continue;
        }
        int owner = s->scope >= 0 && s->scope < a->scope_count
                      ? a->scopes[s->scope].function
                      : -1;
        if (owner >= 0 && owner != selected) {
            continue;
        }
        if (owner < 0 &&
            (s->kind == SCRIPT_LOCAL || s->kind == SCRIPT_PARAMETER ||
             s->kind == SCRIPT_RESULT || s->kind == SCRIPT_QUERY_VARIABLE)) {
            continue;
        }
        char *type = authoring_type_label(catalog, s->type);
        if (!match(query, s->name, type)) {
            free(type);
            continue;
        }
        int index = (*total)++;
        if (index < first || index - first >= count) {
            free(type);
            continue;
        }
        bool callable = s->kind == SCRIPT_FUNCTION || s->kind == SCRIPT_MACRO;
        int function = callable ? s->function : owner;
        const source_presentation_origin *o = callable && function >= 0 &&
                                                      function < a->function_count
                                                ? functions[function]
                                                : symbols[i];
        const source_presentation_origin *body = function >= 0 && function < a->function_count
                                                   ? bodies[function]
                                                   : NULL;
        script_range initial = s->initializer >= 0
                                 ? a->expressions[s->initializer].range
                                 : (script_range){ 0 };
        (*rows)[(*row_count)++] = (document_source_member){
            .kind = copy(storage, category),
            .name = copy(storage, s->name),
            .id = identity(storage, s->id),
            .origin = identity(storage, o ? o->identity : 0),
            .body = identity(storage, body ? body->identity : 0),
            .graph = identity(storage, o ? o->graph : 0),
            .type = copy(storage, type),
            .signature = callable ? signature(storage, syntax, a, function)
                                  : copy(storage, ""),
            .text = slice(storage, a->text, initial.start, initial.end),
            .initializer = identity(
                storage,
                s->initializer >= 0 && s->initializer < a->expression_count &&
                        expressions[s->initializer]
                    ? expressions[s->initializer]->identity
                    : 0
            ),
            .start = s->declaration.start,
            .end = s->declaration.end,
            .function = function,
            .disabled = s->disabled,
            .pure = callable && function >= 0 && function < a->function_count &&
                    a->functions[function].pure,
            .writable = s->writable
        };
        free(type);
    }
    free(symbols);
    free(functions);
    free(bodies);
    free(expressions);
    return true;
}
typedef struct
{
    const script_catalog *catalog;
    script_type expected;
    const char *query;
    arena *storage;
    document_source_binding *rows;
    int first, count, row_count, total;
} choices;
static void offer(
    choices *c,
    int binding,
    const script_symbol *symbol,
    const script_catalog_type *component,
    const script_catalog_field *field,
    script_type type,
    const char *label,
    const char *unit,
    bool writable
)
{
    char *text = authoring_type_label(c->catalog, type);
    if (!match(c->query, label, text)) {
        free(text);
        return;
    }
    int index = c->total++;
    if (index < c->first || index - c->first >= c->count) {
        free(text);
        return;
    }
    c->rows[c->row_count++] = (document_source_binding){
        .symbol = identity(c->storage, symbol->id),
        .component = identity(c->storage, component ? component->id : 0),
        .member = identity(c->storage, field ? field->id : 0),
        .label = copy(c->storage, label),
        .type = copy(c->storage, text),
        .unit = copy(c->storage, unit),
        .binding = binding,
        .container = type.container,
        .compatible = (c->expected.flags & SCRIPT_TYPE_ERROR) ||
                      script_catalog_type_assignable(c->catalog, c->expected, type),
        .writable = writable,
        .range = field && field->min != field->max,
        .min = field ? field->min : 0,
        .max = field ? field->max : 0
    };
    free(text);
}
bool document_source_bindings(
    const document_semantics *snapshot,
    uint64_t origin,
    int first,
    int count,
    const char *query,
    arena *storage,
    document_source_binding **rows,
    int *row_count,
    int *total,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *row_count = *total = 0;
    if (size) {
        *error = 0;
    }
    if (!page(first, count, query, error, size)) {
        return false;
    }
    const script_analysis *a = document_semantics_analysis(snapshot);
    const authoring_source_presentation *p = document_semantics_presentation(snapshot);
    if (!a || !p) {
        return fail(error, size, "Binding choices require a Chibi program snapshot");
    }
    const source_presentation_origin *target = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->identity == origin) {
            target = o;
            break;
        }
    }
    if (!target || target->kind != SOURCE_PRESENTATION_EXPRESSION || target->flow) {
        return fail(error, size, "Select one current source value input");
    }
    choices c = { .catalog = document_semantics_catalog(snapshot),
                  .expected = target->type,
                  .query = query,
                  .storage = storage,
                  .first = first,
                  .count = count };
    c.rows = arena_alloc(storage, (size_t)count * sizeof *c.rows);
    int visible_count;
    const script_symbol **visible = authoring_visible_symbols(a, target->range.start, &visible_count);
    for (int i = 0; i < visible_count; i++) {
        const script_symbol *s = visible[i];
        if (!s->name || s->kind == SCRIPT_FUNCTION || s->kind == SCRIPT_MACRO ||
            s->kind == SCRIPT_EVENT || s->kind == SCRIPT_TYPE ||
            s->kind == SCRIPT_TYPE_PARAMETER) {
            continue;
        }
        int binding = AUTHORING_BIND_SYMBOL;
        for (int j = 0; j < c.catalog->global_count; j++) {
            if (c.catalog->globals[j].id == s->id) {
                binding = AUTHORING_BIND_GLOBAL;
                break;
            }
        }
        offer(&c, binding, s, NULL, NULL, s->type, s->name, s->unit, s->writable);
        const script_catalog_type
            *aggregate = script_catalog_type_by_id(c.catalog, s->type.nominal);
        for (int j = 0; aggregate && j < aggregate->field_count; j++) {
            const script_catalog_field *field = &aggregate->fields[j];
            size_t bytes = strlen(s->name) + strlen(field->name) + 2;
            char *label = xmalloc(bytes);
            snprintf(label, bytes, "%s.%s", s->name, field->name);
            offer(
                &c,
                binding,
                s,
                NULL,
                field,
                field->type,
                label,
                field->unit,
                s->writable && field->writable
            );
            free(label);
        }
        if (a->owner_symbol < 0 || !script_catalog_type_assignable(
                                       c.catalog,
                                       a->symbols[a->owner_symbol].type,
                                       s->type
                                   )) {
            continue;
        }
        for (int j = 0; j < c.catalog->type_count; j++) {
            const script_catalog_type *component = &c.catalog->types[j];
            if (!component->component) {
                continue;
            }
            size_t bytes = strlen(s->name) + strlen(component->name) + 2;
            char *label = xmalloc(bytes);
            snprintf(label, bytes, "%s.%s", s->name, component->name);
            offer(
                &c,
                AUTHORING_BIND_COMPONENT,
                s,
                component,
                NULL,
                component->type,
                label,
                NULL,
                !component->derived
            );
            for (int f = 0; f < component->field_count; f++) {
                const script_catalog_field *field = &component->fields[f];
                size_t length = strlen(label) + strlen(field->name) + 2;
                char *member = xmalloc(length);
                snprintf(member, length, "%s.%s", label, field->name);
                offer(
                    &c,
                    AUTHORING_BIND_COMPONENT,
                    s,
                    component,
                    field,
                    field->type,
                    member,
                    field->unit,
                    !component->derived && field->writable
                );
                free(member);
            }
            free(label);
        }
    }
    free(visible);
    *rows = c.rows;
    *row_count = c.row_count;
    *total = c.total;
    return true;
}
