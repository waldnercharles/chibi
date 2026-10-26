#include "language/signature_reader.h"

#include "language/component.h"
#include "language/unit_algebra.h"
#include "support/array.h"
#include "yyjson.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    arena *storage;
    arena scratch;
    script_signature_facts facts;
    script_catalog catalog;
    script_catalog_type *types, *own;
    script_use *uses;
    script_diagnostic *diagnostics;
    script_constructor_plan *publications;
    const char *source;
    int scope, document, initial_types, initial_uses;
} reader;

static void *copy(arena *storage, const void *source, size_t bytes)
{
    if (!bytes) {
        return NULL;
    }
    void *out = arena_alloc(storage, bytes);
    memcpy(out, source, bytes);
    return out;
}
static bool is(TSNode node, const char *kind)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), kind);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static script_range range(reader *r, TSNode node)
{
    if (ts_node_is_null(node)) {
        return (script_range){ .line = 1, .column = 1 };
    }
    TSPoint point = ts_node_start_point(node);
    return (script_range){ .start = (int)ts_node_start_byte(node),
                           .end = (int)ts_node_end_byte(node),
                           .line = (int)point.row + 1,
                           .column = (int)point.column + 1,
                           .document = r->document };
}
static void diagnostic(reader *r, TSNode node, const char *format, ...)
{
    script_diagnostic out = { .range = range(r, node) };
    va_list args;
    va_start(args, format);
    vsnprintf(out.message, sizeof out.message, format, args);
    va_end(args);
    apush(r->diagnostics, out);
}
static const char *text(reader *r, TSNode node)
{
    size_t start = ts_node_is_null(node) ? 0 : ts_node_start_byte(node);
    size_t size = ts_node_is_null(node) ? 0 : ts_node_end_byte(node) - start;
    char *out = arena_alloc(&r->scratch, size + 1);
    memcpy(out, r->source + start, size);
    out[size] = 0;
    return out;
}
static const char *name(reader *r, TSNode node)
{
    const char *raw = text(r, node);
    if (*raw != '"') {
        return raw;
    }
    yyjson_doc *document = yyjson_read(raw, strlen(raw), 0);
    yyjson_val *value = document ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_str(value) ||
        yyjson_get_len(value) != strlen(yyjson_get_str(value))) {
        diagnostic(r, node, "Invalid string or embedded NUL");
        yyjson_doc_free(document);
        return "";
    }
    const char *out = copy(&r->scratch, yyjson_get_str(value), yyjson_get_len(value) + 1);
    yyjson_doc_free(document);
    return out;
}
static script_named_facts named(reader *r)
{
    script_named_facts out = r->facts.named;
    r->catalog.types = r->types;
    r->catalog.type_count = acount(r->types);
    out.catalog = &r->catalog;
    out.own_types = r->own;
    out.own_type_count = acount(r->own);
    out.uses = r->uses;
    out.use_count = acount(r->uses);
    return out;
}
static int find_type(reader *r, const char *value)
{
    script_named_facts facts = named(r);
    return script_named_find_type(&facts, r->scope, r->document, value);
}
static script_type resolution(reader *r, script_named_resolution out)
{
    if (out.has_diagnostic) {
        apush(r->diagnostics, out.diagnostic);
    }
    if (out.has_use) {
        apush(r->uses, out.use);
    }
    return out.type;
}
static script_constructor_facts constructors(reader *r)
{
    (void)named(r);
    return (script_constructor_facts){ .catalog = &r->catalog,
                                       .symbols = r->facts.named.lexical.symbols,
                                       .functions = r->facts.functions,
                                       .function_count = r->facts.function_count };
}
static script_type publish(reader *r, script_constructor_plan plan)
{
    for (int i = 0; i < plan.diagnostic_count; i++) {
        apush(r->diagnostics, plan.diagnostics[i]);
    }
    if (plan.publish) {
        script_constructor_facts facts = constructors(r);
        script_catalog_type definition = { 0 };
        bool ok = script_constructor_materialize(r->storage, &facts, &plan, &definition);
        assert(ok);
        if (!ok) {
            return (script_type){ .flags = SCRIPT_TYPE_ERROR };
        }
        apush(r->publications, plan);
        apush(r->types, definition);
        apush(r->own, definition);
    }
    return plan.type;
}
static script_type type(reader *, TSNode);
static const script_signature_generic *generic(reader *r, TSNode node)
{
    for (int i = 0; i < r->facts.generic_count; i++) {
        const script_signature_generic *found = &r->facts.generics[i];
        if (found->start == ts_node_start_byte(node) &&
            found->end == ts_node_end_byte(node)) {
            return found;
        }
    }
    return NULL;
}
static script_type collection(reader *r, pin_container container, script_type key, script_type value)
{
    script_catalog_type definition = script_constructor_collection(
        r->storage,
        container,
        key,
        value,
        r->facts.pointer_size
    );
    (void)named(r);
    if (!script_catalog_type_by_id(&r->catalog, definition.id)) {
        apush(r->types, definition);
        apush(r->own, definition);
    }
    return definition.type;
}
/* Applies `ref`, `?` and each `[]` in order; an array of collections names
 * its element collection. */
static script_type traits(reader *r, TSNode node, script_type out)
{
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        const char *token = text(r, ts_node_child(node, i));
        if (!strcmp(token, "ref")) {
            out.flags |= SCRIPT_TYPE_REFERENCE;
        } else if (!strcmp(token, "?")) {
            out.flags |= SCRIPT_TYPE_OPTIONAL;
        } else if (!strcmp(token, "[") || !strcmp(token, "[]")) {
            if (out.collection || out.container != PIN_CONTAINER_SINGLE) {
                out = collection(r, PIN_CONTAINER_ARRAY, (script_type){ 0 }, out);
            }
            out.container = PIN_CONTAINER_ARRAY;
        }
    }
    (void)named(r);
    const script_catalog_type *definition = script_catalog_type_by_id(&r->catalog, out.nominal);
    if ((out.flags & SCRIPT_TYPE_OPTIONAL) &&
        (out.storage != PIN_POINTER || out.container != PIN_CONTAINER_SINGLE) &&
        (!definition || !definition->component)) {
        out.flags |= SCRIPT_TYPE_BOXED_OPTIONAL;
    }
    return out;
}
/* The result type `task(T)` names, or a null node for another application. */
static TSNode task_result(const char *source, TSNode node)
{
    TSNode function = field(node, "function"), arguments = field(node, "arguments");
    bool task = ts_node_end_byte(function) - ts_node_start_byte(function) == 4 &&
                !strncmp(source + ts_node_start_byte(function), "task", 4);
    TSNode result = { 0 };
    int count = 0;
    for (uint32_t i = 0; task && i < ts_node_named_child_count(arguments); i++) {
        TSNode child = ts_node_named_child(arguments, i);
        if (!ts_node_is_extra(child)) {
            result = child;
            count++;
        }
    }
    return count == 1 && !is(result, "named_argument") && !is(result, "typed_argument")
             ? result
             : (TSNode){ 0 };
}
bool script_signature_task(const char *source, TSNode application)
{
    return !ts_node_is_null(task_result(source, application));
}
static script_type application(reader *r, TSNode node)
{
    TSNode result = task_result(r->source, node);
    if (!ts_node_is_null(result)) {
        script_constructor_facts facts = constructors(r);
        return publish(r, script_constructor_task(r->storage, &facts, type(r, result), 0));
    }
    const script_signature_generic *found = generic(r, node);
    if (!found) {
        diagnostic(r, node, "Type expression must construct a compile-time type");
        return (script_type){ .flags = SCRIPT_TYPE_ERROR };
    }
    return found->type;
}
static script_type map(reader *r, TSNode node)
{
    script_type key = type(r, field(node, "key"));
    script_type value = type(r, field(node, "value"));
    bool supported = key.container == PIN_CONTAINER_SINGLE &&
                     !(key.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_TASK |
                                    SCRIPT_TYPE_UNION)) &&
                     (key.storage == PIN_BOOL ||
                      (key.storage >= PIN_I8 && key.storage <= PIN_F64) ||
                      key.storage == PIN_STR || key.storage == PIN_NAME ||
                      key.storage == PIN_ENUM || key.storage == PIN_HANDLE);
    if (!supported) {
        diagnostic(r, node, "Unsupported map key type");
    }
    return collection(r, PIN_CONTAINER_MAP, key, value);
}
static script_type type(reader *r, TSNode node)
{
    if (ts_node_is_null(node)) {
        return (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
    }
    if (is(node, "type_application")) {
        return application(r, node);
    }
    if (is(node, "type")) {
        for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (is(child, "pointer_type")) {
                script_type target = type(r, field(child, "target"));
                script_catalog_type definition = script_constructor_pointer(
                    r->storage,
                    target,
                    r->facts.pointer_size
                );
                (void)named(r);
                if (!script_catalog_type_by_id(&r->catalog, definition.id)) {
                    apush(r->types, definition);
                    apush(r->own, definition);
                    apush(
                        r->publications,
                        ((script_constructor_plan){
                            .type = definition.type,
                            .publish = true,
                            .id = definition.id,
                            .name = definition.name,
                            .definition_type = definition.type,
                            .element_type = definition.element_type,
                            .size = definition.size,
                            .internal = definition.internal })
                    );
                }
                return traits(r, node, definition.type);
            }
            if (is(child, "parenthesized_type")) {
                return traits(r, node, type(r, field(child, "type")));
            }
            if (is(child, "union_type")) {
                return type(r, child);
            }
            if (is(child, "type_application")) {
                return traits(r, node, application(r, child));
            }
            if (is(child, "map_type")) {
                return traits(r, node, map(r, child));
            }
        }
        TSNode name_node = field(node, "name");
        if (is(name_node, "type_binder")) {
            name_node = field(name_node, "name");
        }
        const char *value = name(r, name_node);
        script_type out;
        const script_signature_generic *member = generic(r, name_node);
        if (member) {
            out = member->type;
        } else {
            script_named_facts facts = named(r);
            out = resolution(
                r,
                script_named_parsed(
                    &facts,
                    (script_named_request){ r->scope, range(r, name_node) },
                    value,
                    NULL,
                    0
                )
            );
        }
        return traits(r, node, out);
    }
    if (is(node, "union_type")) {
        script_type *members = NULL;
        for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (!ts_node_is_extra(child)) {
                apush(members, type(r, child));
            }
        }
        script_constructor_facts facts = constructors(r);
        script_type out = publish(
            r,
            script_constructor_union(
                r->storage,
                &facts,
                members,
                acount(members),
                r->facts.pointer_size
            )
        );
        afree(members);
        return out;
    }
    script_named_facts facts = named(r);
    return resolution(
        r,
        script_named_text(&facts, (script_named_request){ r->scope, range(r, node) }, text(r, node))
    );
}
static const char *unit(reader *r, TSNode node)
{
    char *buffer = NULL;
    for (uint32_t i = 0, count = ts_node_is_null(node) ? 0 : ts_node_child_count(node);
         i < count;
         i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_is_extra(child)) {
            continue;
        }
        const char *part = text(r, child);
        if (is(child, "identifier") &&
            (!(script_builtin_type(part).flags & SCRIPT_TYPE_ERROR) ||
             find_type(r, part) >= 0)) {
            diagnostic(r, child, "'%s' is a type, not a unit", part);
        }
        for (const char *c = part; *c; c++) {
            apush(buffer, *c);
        }
    }
    apush(buffer, 0);
    const char *out = copy(&r->scratch, buffer, (size_t)acount(buffer));
    afree(buffer);
    if (script_unit_parse(out).kind == SCRIPT_UNIT_INVALID) {
        diagnostic(r, node, "Invalid unit '%s'", out);
    }
    return out;
}
static script_type signature(reader *r, TSNode node, const char **unit_text)
{
    if (is(node, "unit")) {
        *unit_text = unit(r, node);
        return script_builtin_type("number");
    }
    if (is(node, "measured_type")) {
        script_type out = type(r, field(node, "base"));
        *unit_text = unit(r, field(node, "unit"));
        if (!script_type_numeric(out)) {
            diagnostic(r, node, "A measured signature requires a numeric storage type");
        }
        return out;
    }
    if (is(node, "type")) {
        const char *value = name(r, field(node, "name"));
        int symbol = script_lexical_name(&r->facts.named.lexical, r->scope, value);
        bool named_type = symbol >= 0 &&
                          (r->facts.named.lexical.symbols[symbol].kind == SCRIPT_TYPE ||
                           r->facts.named.lexical.symbols[symbol].kind ==
                               SCRIPT_TYPE_PARAMETER);
        if (script_unit_shorthand(value) && !named_type &&
            find_type(r, value) == -1 && ts_node_child_count(node) == 1) {
            *unit_text = value;
            return script_builtin_type("number");
        }
    }
    return type(r, node);
}
script_signature_reading script_signature_read(
    arena *storage,
    const script_signature_facts *facts,
    TSNode node,
    const char *source,
    int scope,
    int document,
    bool measured
)
{
    reader r = { .storage = storage,
                 .facts = *facts,
                 .catalog = *facts->named.catalog,
                 .source = source,
                 .scope = scope,
                 .document = document,
                 .initial_types = facts->named.catalog->type_count,
                 .initial_uses = facts->named.use_count };
    for (int i = 0; i < r.initial_types; i++) {
        apush(r.types, facts->named.catalog->types[i]);
    }
    for (int i = 0; i < facts->named.own_type_count; i++) {
        apush(r.own, facts->named.own_types[i]);
    }
    for (int i = 0; i < r.initial_uses; i++) {
        apush(r.uses, facts->named.uses[i]);
    }
    const char *unit_text = NULL;
    script_type out = measured ? signature(&r, node, &unit_text) : type(&r, node);
    script_signature_reading result = {
        .type = out,
        .unit = unit_text ? copy(storage, unit_text, strlen(unit_text) + 1) : NULL,
        .diagnostic_count = acount(r.diagnostics),
        .use_count = acount(r.uses) - r.initial_uses,
        .publication_count = acount(r.publications),
        .type_count = acount(r.types) - r.initial_types
    };
    result.diagnostics = copy(
        storage,
        r.diagnostics,
        (size_t)result.diagnostic_count * sizeof *result.diagnostics
    );
    result.uses = result.use_count
                    ? copy(
                          storage,
                          r.uses + r.initial_uses,
                          (size_t)result.use_count * sizeof *result.uses
                      )
                    : NULL;
    result.publications = copy(
        storage,
        r.publications,
        (size_t)result.publication_count * sizeof *result.publications
    );
    result.types = result.type_count
                     ? copy(
                           storage,
                           r.types + r.initial_types,
                           (size_t)result.type_count * sizeof *result.types
                       )
                     : NULL;
    afree(r.types);
    afree(r.own);
    afree(r.uses);
    afree(r.diagnostics);
    afree(r.publications);
    arena_free(&r.scratch);
    return result;
}
