#include "language/record_members.h"

#include "support/array.h"
#include "yyjson.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    arena *storage;
    const char *source;
    int document;
    script_diagnostic *diagnostics;
} discovery;

static void *copy(arena *storage, const void *source, size_t bytes)
{
    if (!bytes) {
        return NULL;
    }
    void *out = arena_alloc(storage, bytes);
    memcpy(out, source, bytes);
    return out;
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static script_range range(const discovery *d, TSNode node)
{
    if (ts_node_is_null(node)) {
        return (script_range){ .line = 1, .column = 1 };
    }
    script_range out = script_node_range(node);
    out.document = d->document;
    return out;
}
static void diagnostic(discovery *d, TSNode node, const char *format, ...)
{
    script_diagnostic out = { .range = range(d, node) };
    va_list args;
    va_start(args, format);
    vsnprintf(out.message, sizeof out.message, format, args);
    va_end(args);
    apush(d->diagnostics, out);
}
static const char *name(discovery *d, TSNode node)
{
    size_t start = ts_node_is_null(node) ? 0 : ts_node_start_byte(node);
    size_t bytes = ts_node_is_null(node) ? 0 : ts_node_end_byte(node) - start;
    char *raw = arena_alloc(d->storage, bytes + 1);
    memcpy(raw, d->source + start, bytes);
    raw[bytes] = 0;
    if (*raw != '"') {
        return raw;
    }
    yyjson_doc *document = yyjson_read(raw, bytes, 0);
    yyjson_val *value = document ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_str(value) ||
        yyjson_get_len(value) != strlen(yyjson_get_str(value))) {
        diagnostic(d, node, "Invalid string or embedded NUL");
        yyjson_doc_free(document);
        return copy(d->storage, "", 1);
    }
    const char *out = copy(d->storage, yyjson_get_str(value), yyjson_get_len(value) + 1);
    yyjson_doc_free(document);
    return out;
}
static const script_diagnostic *finish(discovery *d, int *count)
{
    *count = acount(d->diagnostics);
    const script_diagnostic *out = copy(d->storage, d->diagnostics, (size_t)*count * sizeof *out);
    afree(d->diagnostics);
    return out;
}
script_record_selection script_record_members_select(
    arena *storage,
    const script_signature_facts *facts,
    TSNode node,
    const char *source,
    int scope,
    int document
)
{
    assert(storage && facts && facts->named.catalog && source);
    discovery d = { .storage = storage, .source = source, .document = document };
    script_record_selection out = { .symbol = -1, .definition = -1 };
    if (!ts_node_is_null(node) && !strcmp(ts_node_type(node), "record_declaration")) {
        TSNode token = field(node, "name");
        int symbol = script_lexical_name(&facts->named.lexical, scope, name(&d, token));
        if (symbol >= 0) {
            const script_symbol *s = &facts->named.lexical.symbols[symbol];
            const script_catalog_type
                *found = script_catalog_type_by_id(facts->named.catalog, s->type.nominal);
            script_range origin = range(&d, token);
            if (found && found->authored_record &&
                s->declaration.document == origin.document &&
                s->declaration.start == origin.start) {
                out.symbol = symbol;
                out.definition = (int)(found - facts->named.catalog->types);
            }
        }
    }
    out.diagnostics = finish(&d, &out.diagnostic_count);
    return out;
}
script_record_member script_record_members_read(
    arena *storage,
    const script_signature_facts *facts,
    TSNode member,
    const char *source,
    int scope,
    int document,
    const char *const *previous_names,
    int previous_count
)
{
    assert(storage && facts && source && previous_count >= 0);
    assert(!previous_count || previous_names);
    assert(!ts_node_is_null(member) && !ts_node_is_extra(member));
    discovery d = { .storage = storage, .source = source, .document = document };
    script_record_member out = { .admissible = true };
    out.field.name = name(&d, field(member, "name"));
    out.field.id = script_catalog_id("record-field", out.field.name);
    out.field.writable = true;
    out.annotation = script_signature_read(storage, facts, field(member, "type"), source, scope, document, false);
    out.field.type = out.annotation.type;
    for (int i = 0; i < out.annotation.diagnostic_count; i++) {
        apush(d.diagnostics, out.annotation.diagnostics[i]);
    }
    script_type type = out.field.type;
    if (!*out.field.name || type.nominal || type.flags ||
        type.container != PIN_CONTAINER_SINGLE ||
        (type.storage != PIN_STR && type.storage != PIN_I32 && type.storage != PIN_BOOL)) {
        diagnostic(&d, member, "Record fields require nonoptional string, int or bool values");
        out.admissible = false;
    }
    for (int i = 0; i < previous_count; i++) {
        if (!strcmp(previous_names[i], out.field.name)) {
            diagnostic(&d, member, "Duplicate record field '%s'", out.field.name);
            out.admissible = false;
        }
    }
    out.default_node = field(member, "value");
    out.field.has_default = !ts_node_is_null(out.default_node);
    out.diagnostics = finish(&d, &out.diagnostic_count);
    return out;
}
