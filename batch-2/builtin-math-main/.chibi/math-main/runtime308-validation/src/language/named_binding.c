#include "language/named_binding.h"

#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static int type_index(const script_named_facts *facts, uint64_t id)
{
    for (int i = 0; i < facts->catalog->type_count; i++) {
        if (facts->catalog->types[i].id == id) {
            return i;
        }
    }
    return -1;
}
static const script_package_import *namespace(
    const script_named_facts *facts,
    int scope,
    int document,
    const char *name,
    const char **member
)
{
    if (!facts->document_count) {
        return NULL;
    }
    const char *dot = strchr(name, '.');
    if (!dot || dot == name) {
        return NULL;
    }
    size_t length = (size_t)(dot - name);
    for (int i = 0; i < facts->import_count; i++) {
        const script_package_import *import = &facts->imports[i];
        if (import->document != document || !import->alias ||
            strlen(import->alias) != length ||
            strncmp(import->alias, name, length) || !strcmp(import->alias, "_")) {
            continue;
        }
        int local = script_lexical_name(&facts->lexical, scope, import->alias);
        if (local < 0) {
            local = script_lexical_callable(&facts->lexical, scope, import->alias);
        }
        if (local >= 0 && facts->lexical.symbols[local].scope > facts->package_scope) {
            return NULL;
        }
        *member = dot + 1;
        return import;
    }
    return NULL;
}
int script_named_find_type(const script_named_facts *facts, int scope, int document, const char *name)
{
    const char *member;
    const script_package_import *import = namespace(facts, scope, document, name, &member);
    if (import) {
        if (!strcmp(import->identity, facts->lexical.identity)) {
            for (int i = 0; i < facts->lexical.symbol_count; i++) {
                const script_symbol *symbol = &facts->lexical.symbols[i];
                if (symbol->scope == facts->package_scope &&
                    symbol->kind == SCRIPT_TYPE && !strcmp(symbol->name, member)) {
                    return type_index(facts, symbol->id);
                }
            }
            return -1;
        }
        for (int i = 0; import->exports && i < import->exports->member_count; i++) {
            const script_package_export *export = &import->exports->members[i];
            if (export->kind == SCRIPT_PACKAGE_TYPE && !strcmp(export->name, member)) {
                return type_index(facts, export->id);
            }
        }
        return -1;
    }
    if (!facts->native) {
        return script_catalog_find_type(facts->catalog, name);
    }
    script_catalog own = { .types = facts->own_types,
                           .type_count = facts->own_type_count };
    int found = script_catalog_find_type(&own, name);
    if (found >= 0) {
        return type_index(facts, own.types[found].id);
    }
    if (found == -2) {
        return found;
    }
    found = script_catalog_find_type(facts->native, name);
    return found >= 0 ? type_index(facts, facts->native->types[found].id) : found;
}
static void error(
    const script_named_facts *facts,
    script_named_request request,
    const char *name,
    int index,
    script_named_resolution *out
)
{
    out->has_diagnostic = true;
    out->diagnostic.range = request.range;
    const char *member;
    const script_package_import
        *import = namespace(facts, request.scope, request.range.document, name, &member);
    if (import && script_package_private(import->exports, member)) {
        snprintf(out->diagnostic.message, sizeof out->diagnostic.message, SCRIPT_PRIVATE_MEMBER, name);
        return;
    }
    snprintf(
        out->diagnostic.message,
        sizeof out->diagnostic.message,
        "%s type '%s'",
        index == -2 ? "Ambiguous" : "Unknown",
        name
    );
}
static void type_use(
    const script_named_facts *facts,
    script_named_request request,
    script_named_resolution *out,
    int symbol,
    uint64_t nominal
)
{
    if (symbol < 0 && !nominal) {
        return;
    }
    for (int i = 0; i < facts->use_count; i++) {
        const script_use *use = &facts->uses[i];
        if (use->kind == SCRIPT_TYPE_USE && use->symbol == symbol &&
            use->member == nominal && use->range.document == request.range.document &&
            use->range.start == request.range.start &&
            use->range.end == request.range.end) {
            return;
        }
    }
    out->has_use = true;
    out->use = (script_use){ request.range, symbol, SCRIPT_TYPE_USE, nominal };
}
static script_type optional_storage(const script_named_facts *facts, script_type type)
{
    const script_catalog_type *definition = script_catalog_type_by_id(facts->catalog, type.nominal);
    if ((type.flags & SCRIPT_TYPE_OPTIONAL) && (!definition || !definition->component)) {
        type.flags |= SCRIPT_TYPE_BOXED_OPTIONAL;
    }
    return type;
}
script_named_resolution script_named_parsed(
    const script_named_facts *facts,
    script_named_request request,
    const char *name,
    const char *const *tokens,
    int token_count
)
{
    script_named_resolution out = { .type = script_builtin_type(name) };
    int symbol = script_lexical_name(&facts->lexical, request.scope, name);
    if (symbol >= 0 && (facts->lexical.symbols[symbol].kind == SCRIPT_TYPE_PARAMETER ||
                        facts->lexical.symbols[symbol].kind == SCRIPT_TYPE)) {
        out.type = facts->lexical.symbols[symbol].type;
    } else if (out.type.flags & SCRIPT_TYPE_ERROR) {
        int index = script_named_find_type(facts, request.scope, request.range.document, name);
        if (index >= 0) {
            out.type = facts->catalog->types[index].type;
            out.type.nominal = facts->catalog->types[index].id;
        } else {
            error(facts, request, name, index, &out);
        }
    }
    if (!strcmp(name, "entity") && facts->catalog->entity_type) {
        out.type.nominal = facts->catalog->entity_type;
    }
    if (!(out.type.flags & SCRIPT_TYPE_ERROR)) {
        bool generic = symbol >= 0 && facts->lexical.symbols[symbol].kind ==
                                          SCRIPT_TYPE_PARAMETER;
        type_use(
            facts,
            request,
            &out,
            generic ? symbol : -1,
            generic ? 0 : out.type.nominal
        );
    }
    for (int i = 0; i < token_count; i++) {
        if (!strcmp(tokens[i], "?")) {
            out.type.flags |= SCRIPT_TYPE_OPTIONAL;
        } else if (!strcmp(tokens[i], "[") || !strcmp(tokens[i], "[]")) {
            out.type.container = PIN_CONTAINER_ARRAY;
        }
    }
    out.type = optional_storage(facts, out.type);
    return out;
}
script_named_resolution script_named_text(
    const script_named_facts *facts,
    script_named_request request,
    const char *source
)
{
    size_t length = strlen(source);
    char *name = xmalloc(length + 1);
    int count = 0;
    for (size_t i = 0; i < length; i++) {
        if (!isspace((unsigned char)source[i])) {
            name[count++] = source[i];
        }
    }
    name[count] = 0;
    unsigned flags = 0;
    if (count && name[count - 1] == '?') {
        name[--count] = 0;
        flags |= SCRIPT_TYPE_OPTIONAL;
    }
    pin_container container = PIN_CONTAINER_SINGLE;
    if (count >= 2 && !strcmp(name + count - 2, "[]")) {
        name[count -= 2] = 0;
        container = PIN_CONTAINER_ARRAY;
    }
    if (*name == '"') {
        yyjson_doc *document = yyjson_read(name, strlen(name), 0);
        if (document && yyjson_is_str(yyjson_doc_get_root(document))) {
            char *decoded = xstrdup(yyjson_get_str(yyjson_doc_get_root(document)));
            free(name);
            name = decoded;
        }
        yyjson_doc_free(document);
    }
    script_named_resolution out = { .type = script_builtin_type(name) };
    if (out.type.flags & SCRIPT_TYPE_ERROR) {
        int symbol = script_lexical_name(&facts->lexical, request.scope, name);
        if (symbol >= 0 && (facts->lexical.symbols[symbol].kind == SCRIPT_TYPE_PARAMETER ||
                            facts->lexical.symbols[symbol].kind == SCRIPT_TYPE)) {
            out.type = facts->lexical.symbols[symbol].type;
        } else {
            int index = script_named_find_type(
                facts,
                request.scope,
                request.range.document,
                name
            );
            if (index >= 0) {
                out.type = facts->catalog->types[index].type;
                out.type.nominal = facts->catalog->types[index].id;
            } else {
                error(facts, request, name, index, &out);
            }
        }
    }
    if (!strcmp(name, "entity") && facts->catalog->entity_type) {
        out.type.nominal = facts->catalog->entity_type;
    }
    out.type.container = container;
    out.type.flags |= flags;
    int symbol = script_lexical_name(&facts->lexical, request.scope, name);
    if (symbol < 0 || facts->lexical.symbols[symbol].kind != SCRIPT_TYPE_PARAMETER) {
        symbol = -1;
    }
    if (!(out.type.flags & SCRIPT_TYPE_ERROR)) {
        type_use(facts, request, &out, symbol, symbol < 0 ? out.type.nominal : 0);
    }
    free(name);
    out.type = optional_storage(facts, out.type);
    return out;
}
