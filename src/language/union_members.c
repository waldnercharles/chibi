#include "language/union_members.h"

#include "capabilities/catalog_snapshot.h"
#include "language/constant_proof.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"
#include "yyjson.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
    script_union_member_update *updates;
    const char *source;
    int scope, document, initial_uses;
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
static const char *own(reader *r, const char *source)
{
    return copy(r->storage, source, strlen(source) + 1);
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
static uint32_t named_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_named_child_count(node);
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
static const char *name(reader *r, TSNode node)
{
    size_t start = ts_node_is_null(node) ? 0 : ts_node_start_byte(node);
    size_t bytes = ts_node_is_null(node) ? 0 : ts_node_end_byte(node) - start;
    char *raw = arena_alloc(&r->scratch, bytes + 1);
    memcpy(raw, r->source + start, bytes);
    raw[bytes] = 0;
    if (*raw != '"') {
        return raw;
    }
    yyjson_doc *document = yyjson_read(raw, bytes, 0);
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
static void refresh(reader *r)
{
    r->catalog.types = r->types;
    r->catalog.type_count = acount(r->types);
    r->facts.named.catalog = &r->catalog;
    r->facts.named.own_types = r->own;
    r->facts.named.own_type_count = acount(r->own);
    r->facts.named.uses = r->uses;
    r->facts.named.use_count = acount(r->uses);
}
static int declared(reader *r, TSNode node)
{
    TSNode token = field(node, "name");
    const char *value = name(r, token);
    int symbol = script_lexical_name(&r->facts.named.lexical, r->scope, value);
    if (symbol < 0) {
        return -1;
    }
    const script_symbol *s = &r->facts.named.lexical.symbols[symbol];
    script_range origin = range(r, token);
    if (s->kind != SCRIPT_TYPE || s->type.storage != PIN_UNION ||
        s->declaration.document != origin.document ||
        s->declaration.start != origin.start) {
        return -1;
    }
    return symbol;
}
static script_type type(reader *r, TSNode node)
{
    refresh(r);
    script_signature_reading
        out = script_signature_read(r->storage, &r->facts, node, r->source, r->scope, r->document, false);
    for (int i = 0; i < out.diagnostic_count; i++) {
        apush(r->diagnostics, out.diagnostics[i]);
    }
    for (int i = 0; i < out.use_count; i++) {
        apush(r->uses, out.uses[i]);
    }
    for (int i = 0; i < out.type_count; i++) {
        apush(r->types, out.types[i]);
        apush(r->own, out.types[i]);
        apush(
            r->updates,
            ((script_union_member_update){ .append = true, .definition = out.types[i] })
        );
    }
    return out.type;
}
static void replace(reader *r, script_catalog_type definition)
{
    script_catalog input = { .types = &definition, .type_count = 1 },
                   snapshot = { 0 };
    bool ok = script_catalog_snapshot(r->storage, &input, &snapshot);
    assert(ok);
    if (!ok) {
        return;
    }
    definition = snapshot.types[0];
    for (int i = 0; i < acount(r->types); i++) {
        if (r->types[i].id == definition.id) {
            r->types[i] = definition;
        }
    }
    for (int i = 0; i < acount(r->own); i++) {
        if (r->own[i].id == definition.id) {
            r->own[i] = definition;
        }
    }
    apush(r->updates, ((script_union_member_update){ .definition = definition }));
}
static int payload_cycle(const script_catalog *catalog, script_type type, unsigned char *states, int depth)
{
    if (type.storage != PIN_UNION && type.storage != PIN_STRUCT) {
        return 0;
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (!record) {
        return 0;
    }
    int index = (int)(record - catalog->types);
    if (states[index] == 1) {
        return 1;
    }
    if (states[index] == 2) {
        return 0;
    }
    if (depth == SCRIPT_MAX_DEPTH) {
        return 2;
    }
    states[index] = 1;
    for (int i = 0; i < record->field_count; i++) {
        int error = payload_cycle(catalog, record->fields[i].type, states, depth + 1);
        if (error) {
            return error;
        }
    }
    for (int i = 0; i < record->variant_count; i++) {
        const script_catalog_variant *variant = &record->variants[i];
        if (!variant->tagged) {
            int error = payload_cycle(catalog, variant->type, states, depth + 1);
            if (error) {
                return error;
            }
        }
        for (int j = 0; j < variant->field_count; j++) {
            int error = payload_cycle(catalog, variant->fields[j].type, states, depth + 1);
            if (error) {
                return error;
            }
        }
    }
    states[index] = 2;
    return 0;
}
script_union_member_validation script_union_members_validate(
    const script_catalog *catalog,
    const script_type *roots,
    int count,
    unsigned char *states
)
{
    for (int i = 0; i < count; i++) {
        int error = payload_cycle(catalog, roots[i], states, 0);
        if (error) {
            return (script_union_member_validation){ .root = i, .error = error };
        }
    }
    return (script_union_member_validation){ .root = -1 };
}
script_union_member_result script_union_members(
    arena *storage,
    const script_signature_facts *facts,
    TSNode body,
    const char *source,
    int scope,
    int document
)
{
    reader r = { .storage = storage,
                 .facts = *facts,
                 .catalog = *facts->named.catalog,
                 .source = source,
                 .scope = scope,
                 .document = document,
                 .initial_uses = facts->named.use_count };
    for (int i = 0; i < facts->named.catalog->type_count; i++) {
        apush(r.types, facts->named.catalog->types[i]);
    }
    for (int i = 0; i < facts->named.own_type_count; i++) {
        apush(r.own, facts->named.own_types[i]);
    }
    for (int i = 0; i < r.initial_uses; i++) {
        apush(r.uses, facts->named.uses[i]);
    }
    for (uint32_t i = 0; i < named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!is(node, "union_declaration")) {
            continue;
        }
        int symbol = declared(&r, node);
        if (symbol < 0) {
            continue;
        }
        script_type union_type = facts->named.lexical.symbols[symbol].type;
        refresh(&r);
        script_catalog_type
            definition = *script_catalog_type_by_id(&r.catalog, union_type.nominal);
        script_catalog_variant *variants = NULL;
        for (uint32_t j = 0; j < named_count(node); j++) {
            TSNode variant = ts_node_named_child(node, j);
            if (!is(variant, "union_variant")) {
                continue;
            }
            TSNode literal = field(variant, "literal");
            pin_value literal_value = { .type = PIN_STR };
            const char *text;
            if (ts_node_is_null(literal)) {
                text = name(&r, field(variant, "name"));
            } else {
                size_t start = ts_node_start_byte(literal),
                       length = ts_node_end_byte(literal) - start;
                yyjson_doc *document = yyjson_read(r.source + start, length, YYJSON_READ_ALLOW_INVALID_UNICODE);
                yyjson_val *decoded = document ? yyjson_doc_get_root(document) : NULL;
                const char *error = !yyjson_is_str(decoded)
                                      ? "Invalid string literal"
                                      : pin_value_text(
                                            r.storage,
                                            &literal_value,
                                            yyjson_get_str(decoded),
                                            yyjson_get_len(decoded)
                                        );
                if (error) {
                    diagnostic(&r, literal, "%s", error);
                }
                pin_string bytes = pin_text(literal_value);
                if (bytes.length && memchr(bytes.bytes, 0, bytes.length)) {
                    char *label = arena_alloc(r.storage, length + 1);
                    memcpy(label, r.source + start, length);
                    label[length] = 0;
                    text = label;
                } else {
                    text = bytes.bytes;
                }
                yyjson_doc_free(document);
            }
            script_catalog_variant v = { .name = own(&r, text),
                                         .type = union_type,
                                         .tagged = ts_node_is_null(literal),
                                         .is_literal = !ts_node_is_null(literal) };
            if (v.is_literal) {
                pin_string bytes = pin_text(literal_value);
                v.tag = script_catalog_id_bytes(
                    "union-literal",
                    bytes.bytes,
                    bytes.length
                );
                v.literal = literal_value;
                v.type = (script_type){ .storage = PIN_STR,
                                        .flags = SCRIPT_TYPE_LITERAL,
                                        .parameter = v.tag };
            } else {
                char key[512];
                snprintf(
                    key,
                    sizeof key,
                    "%llu:%s",
                    (unsigned long long)union_type.nominal,
                    text
                );
                v.tag = script_catalog_id("union-variant", key);
                v.type.parameter = v.tag;
            }
            for (int k = 0; k < acount(variants); k++) {
                if (variants[k].tag == v.tag) {
                    diagnostic(&r, variant, "Duplicate union alternative '%s'", v.name);
                }
            }
            script_catalog_field *fields = NULL;
            TSNode parameters = field(variant, "parameters");
            for (uint32_t k = 0; k < named_count(parameters); k++) {
                TSNode parameter = ts_node_named_child(parameters, k);
                if (ts_node_is_extra(parameter)) {
                    continue;
                }
                const char *field_name = name(&r, field(parameter, "name"));
                script_catalog_field f = {
                    .id = script_catalog_id("union-field", field_name),
                    .name = own(&r, field_name),
                    .type = type(&r, field(parameter, "type")),
                    .offset = (size_t)acount(fields)
                };
                if (f.type.flags & (SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_UNTYPED)) {
                    diagnostic(&r, parameter, "Union payloads require an owned value type");
                }
                apush(fields, f);
            }
            v.field_count = acount(fields);
            v.fields = copy(storage, fields, (size_t)v.field_count * sizeof *fields);
            afree(fields);
            apush(variants, v);
        }
        definition.variant_count = acount(variants);
        if (!definition.variant_count) {
            diagnostic(&r, node, "A union needs at least one alternative");
        } else {
            definition.variants = copy(storage, variants, (size_t)definition.variant_count * sizeof *variants);
        }
        afree(variants);
        replace(&r, definition);
    }
    refresh(&r);
    unsigned char *states = xcalloc((size_t)r.catalog.type_count + 1, sizeof *states);
    for (uint32_t i = 0; i < named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (!is(node, "union_declaration")) {
            continue;
        }
        int symbol = declared(&r, node);
        if (symbol < 0) {
            continue;
        }
        script_type root = facts->named.lexical.symbols[symbol].type;
        script_union_member_validation
            validation = script_union_members_validate(&r.catalog, &root, 1, states);
        if (validation.error) {
            diagnostic(
                &r,
                node,
                validation.error == 1
                    ? "Recursive owned union payloads are not supported"
                    : "Union payload nesting exceeds the supported depth"
            );
            break;
        }
    }
    free(states);
    script_union_member_result result = { .update_count = acount(r.updates),
                                          .diagnostic_count = acount(r.diagnostics),
                                          .use_count = acount(r.uses) - r.initial_uses };
    result.updates = copy(
        storage,
        r.updates,
        (size_t)result.update_count * sizeof *r.updates
    );
    result.diagnostics = copy(
        storage,
        r.diagnostics,
        (size_t)result.diagnostic_count * sizeof *r.diagnostics
    );
    result.uses = result.use_count ? copy(
                                         storage,
                                         r.uses + r.initial_uses,
                                         (size_t)result.use_count * sizeof *r.uses
                                     )
                                   : NULL;
    afree(r.types);
    afree(r.own);
    afree(r.uses);
    afree(r.diagnostics);
    afree(r.updates);
    arena_free(&r.scratch);
    return result;
}
