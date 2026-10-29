#include "authoring/source_refactor_text.h"

#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool source_refactor_fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
void source_refactor_append(char **text, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    int start = acount(*text);
    asetlen(*text, start + count + 1);
    vsnprintf(*text + start, (size_t)count + 1, format, args);
    asetlen(*text, start + count);
    va_end(args);
}
char *source_refactor_take(char **text)
{
    source_refactor_append(text, "%c", '\0');
    char *copy = xstrdup(*text);
    afree(*text);
    return copy;
}
bool source_refactor_contains(script_range outer, script_range inner)
{
    return inner.start >= outer.start && inner.end <= outer.end;
}
char *source_refactor_quote(const script_catalog *catalog, const char *name)
{
    (void)catalog;
    return authoring_source_identifier(name, NULL, 0);
}
char *source_refactor_slice(const char *text, script_range range)
{
    int count = range.end - range.start;
    char *copy = xmalloc((size_t)count + 1);
    memcpy(copy, text + range.start, (size_t)count);
    copy[count] = '\0';
    return copy;
}
static bool type_text(
    const script_catalog *catalog,
    script_type type,
    bool reference,
    char **text,
    char *error,
    size_t size,
    int depth
)
{
    if (depth == 64) {
        return source_refactor_fail(error, size, "Refactor type nesting exceeds 64 levels");
    }
    if (type.flags & (SCRIPT_TYPE_RESULTS | SCRIPT_TYPE_GENERIC |
                      SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_ERROR)) {
        return source_refactor_fail(error, size, "Refactor requires a concrete declared value type");
    }
    if (reference) {
        bool compound = type.container != PIN_CONTAINER_SINGLE ||
                        (type.flags & SCRIPT_TYPE_OPTIONAL);
        source_refactor_append(text, compound ? "^(" : "^");
        if (!type_text(catalog, type, false, text, error, size, depth + 1)) {
            return false;
        }
        source_refactor_append(text, compound ? ")" : "");
        return true;
    }
    if (type.container == PIN_CONTAINER_MAP || type.container == PIN_CONTAINER_ARRAY) {
        bool map = type.container == PIN_CONTAINER_MAP;
        if (map) {
            source_refactor_append(text, "[");
            if (!type_text(catalog, script_catalog_key_type(catalog, type), false, text, error, size, depth + 1)) {
                return false;
            }
            source_refactor_append(text, ": ");
        }
        if (!type_text(catalog, script_catalog_element_type(catalog, type), false, text, error, size, depth + 1)) {
            return false;
        }
        source_refactor_append(text, "%s%s", map ? "]" : "[]", type.flags & SCRIPT_TYPE_OPTIONAL ? "?" : "");
        return true;
    }
    if (type.container != PIN_CONTAINER_SINGLE) {
        return source_refactor_fail(error, size, "This container has no callable source annotation");
    }
    if (type.storage == PIN_POINTER) {
        return source_refactor_fail(error, size, "Pointer type annotations are not supported by this refactor");
    }
    const char *name = script_reference_type_name(script_reference_kind_for_type(type));
    if (!name && script_type_char(type)) {
        name = "char";
    } else if (!name && type.nominal) {
        const script_catalog_type *nominal = script_catalog_type_by_id(catalog, type.nominal);
        if (nominal) {
            name = nominal->source_name ? nominal->source_name : nominal->name;
        }
    } else if (!name) {
        if ((type.storage >= PIN_BOOL && type.storage <= PIN_NAME) ||
            type.storage == PIN_STR) {
            name = script_scalar_name(type.storage);
        }
    }
    if (!name) {
        return source_refactor_fail(error, size, "Refactor type is absent from the authoritative catalog");
    }
    char *quoted = strchr(name, '(') ? xstrdup(name)
                                     : source_refactor_quote(catalog, name);
    if (!quoted) {
        return source_refactor_fail(error, size, "Refactor type has no identifier source spelling");
    }
    source_refactor_append(text, "%s%s", quoted, type.flags & SCRIPT_TYPE_OPTIONAL ? "?" : "");
    free(quoted);
    return true;
}
bool source_refactor_type(
    const script_catalog *catalog,
    script_type type,
    bool reference,
    char **text,
    char *error,
    size_t size
)
{
    return type_text(catalog, type, reference, text, error, size, 0);
}
