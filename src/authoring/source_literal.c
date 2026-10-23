#include "authoring/source_literal.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/text.h"

#include <assert.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LITERAL_MAX_DEPTH 64
#define LITERAL_MAX_VALUES 16384
#define LITERAL_MAX_BYTES (1024 * 1024)

char *authoring_source_identifier(const char *name, char *error, size_t size)
{
    if (!name || !*name || strlen(name) > 4096) {
        if (size) {
            snprintf(error, size, "Source symbols need a nonempty identifier within 4096 bytes");
        }
        return NULL;
    }
    char *out = xstrdup(name);
    bool first = true;
    for (char *p = out; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '-') {
            *p = '_';
            c = '_';
        }
        if (c == '.' && !first && p[1]) {
            first = true;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
              (!first && c >= '0' && c <= '9'))) {
            free(out);
            if (size) {
                snprintf(error, size, "Source symbols must use identifier names");
            }
            return NULL;
        }
        first = false;
    }
    return out;
}

typedef struct
{
    const script_catalog *catalog;
    char *text, *error;
    size_t error_size;
    int values;
    arena storage;
} literal;

static bool fail(literal *l, const char *message)
{
    if (l->error_size) {
        snprintf(l->error, l->error_size, "%s", message);
    }
    return false;
}
static bool append(literal *l, const char *text, size_t size)
{
    if (size > LITERAL_MAX_BYTES - (size_t)acount(l->text)) {
        return fail(l, "Source literal exceeds output limit");
    }
    int first = acount(l->text);
    asetlen(l->text, first + (int)size);
    memcpy(l->text + first, text, size);
    return true;
}
static bool token(literal *l, const char *text)
{
    return append(l, text, strlen(text));
}
static bool string(literal *l, pin_string text)
{
    if (!token(l, "\"")) {
        return false;
    }
    const unsigned char *p = (const unsigned char *)text.bytes;
    const unsigned char *end = p + text.length;
    while (p < end) {
        if (*p == '"' || *p == '\\') {
            char escape[] = { '\\', (char)*p++, 0 };
            if (!token(l, escape)) {
                return false;
            }
            continue;
        }
        if (*p < 32) {
            char escape[7];
            snprintf(escape, sizeof escape, "\\u%04x", *p++);
            if (!token(l, escape)) {
                return false;
            }
            continue;
        }
        int bytes = *p < 0x80                ? 1
                  : *p >= 0xc2 && *p <= 0xdf ? 2
                  : *p >= 0xe0 && *p <= 0xef ? 3
                  : *p >= 0xf0 && *p <= 0xf4 ? 4
                                             : 0;
        if (!bytes || (size_t)(end - p) < (size_t)bytes) {
            return fail(l, "Source literal string is not valid UTF-8");
        }
        for (int i = 1; i < bytes; ++i) {
            if ((p[i] & 0xc0) != 0x80) {
                return fail(l, "Source literal string is not valid UTF-8");
            }
        }
        if ((bytes == 3 &&
             ((*p == 0xe0 && p[1] < 0xa0) || (*p == 0xed && p[1] >= 0xa0))) ||
            (bytes == 4 &&
             ((*p == 0xf0 && p[1] < 0x90) || (*p == 0xf4 && p[1] >= 0x90)))) {
            return fail(l, "Source literal string is not valid UTF-8");
        }
        if (!append(l, (const char *)p, (size_t)bytes)) {
            return false;
        }
        p += bytes;
    }
    return token(l, "\"");
}
static size_t width(literal *l, script_type type)
{
    if (type.container != PIN_CONTAINER_SINGLE) {
        return sizeof(void *);
    }
    switch (type.storage) {
        case PIN_BOOL: return sizeof(bool);
        case PIN_I8:
        case PIN_U8: return 1;
        case PIN_I16:
        case PIN_U16: return 2;
        case PIN_I32:
        case PIN_U32:
        case PIN_F32:
        case PIN_ENUM: return 4;
        case PIN_I64:
        case PIN_U64:
        case PIN_F64:
        case PIN_HANDLE: return 8;
        case PIN_NAME:
        case PIN_STR: return sizeof(char *);
        case PIN_STRUCT: {
            const script_catalog_type *record = script_catalog_type_by_id(l->catalog, type.nominal);
            return record ? record->size : 0;
        }
        default: return 0;
    }
}
static bool value(literal *, script_type, const pin_value *, int);
static bool aggregate(literal *l, script_type expected, const pin_value *v, int depth)
{
    const script_catalog_type
        *type = script_catalog_type_by_id(l->catalog, expected.nominal);
    if (!type || !type->size || !v->ptr || type->field_count < 0 ||
        (type->field_count && !type->fields)) {
        return fail(l, "Struct literal has no reflected payload layout");
    }
    if (!token(l, "{")) {
        return false;
    }
    for (int i = 0; i < type->field_count; ++i) {
        const script_catalog_field *field = &type->fields[i];
        size_t size = width(l, field->type);
        if (!field->name || !size || field->offset > type->size ||
            size > type->size - field->offset) {
            return fail(l, "Struct literal field exceeds reflected layout");
        }
        const script_catalog_type
            *nominal = script_catalog_type_by_id(l->catalog, field->type.nominal);
        pin_value item = { .type = field->type.storage,
                           .container = field->type.container,
                           .type_name = nominal ? nominal->name : NULL };
        const char *data = (const char *)v->ptr + field->offset;
        if (item.type == PIN_STRUCT && item.container == PIN_CONTAINER_SINGLE) {
            item.ptr = (void *)data;
        } else if (
            item.type == PIN_STR && item.container == PIN_CONTAINER_SINGLE &&
            field->c_string
        ) {
            const char *text;
            memcpy(&text, data, sizeof text);
            const char *error = pin_value_text(&l->storage, &item, text, text ? strlen(text) : 0);
            if (error) {
                return fail(l, error);
            }
            item.type_name = nominal ? nominal->name : NULL;
        } else {
            memcpy(&item.d, data, size);
        }
        char *name = authoring_source_identifier(field->name, l->error, l->error_size);
        bool valid = name && (!i || token(l, ", ")) && token(l, name) &&
                     token(l, ": ") && value(l, field->type, &item, depth + 1);
        free(name);
        if (!valid) {
            return false;
        }
    }
    return token(l, "}");
}
static bool array(literal *l, script_type expected, const pin_value *v, int depth)
{
    const pin_array *items = v->ptr;
    if (!items || items->count < 0 || items->count > LITERAL_MAX_VALUES - l->values ||
        items->element_type != expected.storage || !pin_array_valid(items)) {
        return fail(l, "Array literal has an invalid payload shape or element limit");
    }
    if ((items->type_name || v->type_name) &&
        (!items->type_name || !v->type_name || strcmp(items->type_name, v->type_name))) {
        return fail(l, "Array literal nominal header does not match its payload");
    }
    if (!token(l, "[")) {
        return false;
    }
    expected = script_catalog_element_type(l->catalog, expected);
    for (int i = 0; i < items->count; ++i) {
        pin_value item = pin_array_get(items, i);
        if ((i && !token(l, ", ")) || !value(l, expected, &item, depth + 1)) {
            return false;
        }
    }
    return token(l, "]");
}
static bool map(literal *l, script_type expected, const pin_value *v, int depth)
{
    const pin_map *items = v->ptr;
    const script_catalog_type
        *descriptor = script_catalog_type_by_id(l->catalog, expected.collection);
    if (!descriptor || descriptor->container != PIN_CONTAINER_MAP ||
        (items && (items->count < 0 || items->count > LITERAL_MAX_VALUES - l->values))) {
        return fail(l, "Map literal requires its complete key/value descriptor");
    }
    if (!items || !items->count) {
        return token(l, "[:]");
    }
    if (!token(l, "[")) {
        return false;
    }
    for (int i = 0; i < items->count; i++) {
        const pin_map_entry *entry = &items->entries[i];
        if (!pin_map_key_supported(entry->key) || (i && !token(l, ", ")) ||
            !value(l, descriptor->key_type, &entry->key, depth + 1) || !token(l, ": ") ||
            !value(l, descriptor->element_type, &entry->value, depth + 1)) {
            return false;
        }
    }
    return token(l, "]");
}
static bool number(literal *l, const pin_value *v)
{
    char text[128];
    switch (v->type) {
        case PIN_I8: snprintf(text, sizeof text, "%" PRId8, v->i8); break;
        case PIN_U8: snprintf(text, sizeof text, "%" PRIu8, v->u8); break;
        case PIN_I16: snprintf(text, sizeof text, "%" PRId16, v->i16); break;
        case PIN_U16: snprintf(text, sizeof text, "%" PRIu16, v->u16); break;
        case PIN_I32: snprintf(text, sizeof text, "%" PRId32, v->i); break;
        case PIN_ENUM: snprintf(text, sizeof text, "%" PRId32, v->e); break;
        case PIN_U32: snprintf(text, sizeof text, "%" PRIu32, v->u); break;
        case PIN_I64: snprintf(text, sizeof text, "%" PRId64, v->i64); break;
        case PIN_U64: snprintf(text, sizeof text, "%" PRIu64, v->u64); break;
        case PIN_F32:
            if (!isfinite(v->f)) {
                return fail(l, "Source numeric literal must be finite");
            }
            snprintf(text, sizeof text, "%.9g", (double)v->f);
            break;
        case PIN_F64:
            if (!isfinite(v->d)) {
                return fail(l, "Source numeric literal must be finite");
            }
            snprintf(text, sizeof text, "%.17g", v->d);
            break;
        default: return fail(l, "Value has no numeric source representation");
    }
    const char *decimal = localeconv()->decimal_point;
    if (decimal && *decimal && strcmp(decimal, ".")) {
        char *position = strstr(text, decimal);
        if (position) {
            size_t size = strlen(decimal);
            *position = '.';
            memmove(position + 1, position + size, strlen(position + size) + 1);
        }
    }
    return token(l, text);
}
static bool reference(literal *l, script_reference_kind kind, const pin_value *v)
{
    const char *reference_text = NULL;
    if (kind == SCRIPT_REFERENCE_PROTOTYPE || kind == SCRIPT_REFERENCE_COMPONENT) {
        const char *error = pin_text_cstring(&l->storage, *v, &reference_text, NULL);
        if (error) {
            return fail(l, error);
        }
    }
    const char *name = NULL;
    if (kind == SCRIPT_REFERENCE_PROTOTYPE) {
        int index = reference_text
                      ? script_catalog_find_capability(l->catalog, reference_text, SCRIPT_CAPABILITY_PROTOTYPE)
                      : -1;
        if (index >= 0) {
            name = l->catalog->capabilities[index].name;
        }
    } else if (kind == SCRIPT_REFERENCE_COMPONENT) {
        int index = reference_text
                      ? script_catalog_find_type(l->catalog, reference_text)
                      : -1;
        if (index >= 0 && l->catalog->types[index].component) {
            const script_catalog_type *type = &l->catalog->types[index];
            name = type->source_name ? type->source_name : type->name;
        }
    } else if (v->u64 == SCRIPT_CHILD_OF) {
        name = "ChildOf";
    } else {
        for (int i = 0; i < l->catalog->relationship_count; i++) {
            if (l->catalog->relationships[i].id == v->u64) {
                name = l->catalog->relationships[i].name;
                break;
            }
        }
    }
    if (!name) {
        return fail(l, "Source reference must resolve to a symbol of its declared kind");
    }
    char *spelling = authoring_source_identifier(name, l->error, l->error_size);
    if (!spelling) {
        return false;
    }
    bool written = token(l, spelling);
    free(spelling);
    return written;
}
static bool value(literal *l, script_type expected, const pin_value *v, int depth)
{
    if (depth >= LITERAL_MAX_DEPTH || ++l->values > LITERAL_MAX_VALUES) {
        return fail(l, "Source literal exceeds nesting or value limit");
    }
    if (expected.flags & (SCRIPT_TYPE_ERROR | SCRIPT_TYPE_REFERENCE |
                          SCRIPT_TYPE_RESULTS | SCRIPT_TYPE_UNTYPED)) {
        return fail(l, "Source literal needs a resolved value type");
    }
    bool text = (expected.storage == PIN_NAME || expected.storage == PIN_STR) &&
                (v->type == PIN_NAME || v->type == PIN_STR);
    if ((!text && expected.storage != v->type) || expected.container != v->container) {
        return fail(l, "Source literal does not match the declared value type");
    }
    script_type element = expected;
    element.container = PIN_CONTAINER_SINGLE;
    script_reference_kind reference_kind = script_reference_kind_for_type(element);
    if (expected.nominal && reference_kind == SCRIPT_REFERENCE_NONE) {
        const script_catalog_type
            *type = script_catalog_type_by_id(l->catalog, expected.nominal);
        int actual = v->type_name ? script_catalog_find_type(l->catalog, v->type_name)
                                  : -1;
        script_type actual_type = actual >= 0
                                    ? l->catalog->types[actual].type
                                    : (script_type){ .flags = SCRIPT_TYPE_ERROR };
        actual_type.container = v->container;
        if (!type || actual < 0 ||
            !script_catalog_type_assignable(l->catalog, expected, actual_type) ||
            (expected.storage == PIN_STRUCT &&
             type->size != l->catalog->types[actual].size)) {
            return fail(l, "Source literal nominal type does not match its reflected layout");
        }
    }
    if (expected.container == PIN_CONTAINER_MAP) {
        return map(l, expected, v, depth);
    }
    if (expected.container == PIN_CONTAINER_SET) {
        return fail(l, "Set values have no supported source literal contract");
    }
    if (expected.container == PIN_CONTAINER_ARRAY) {
        return array(l, expected, v, depth);
    }
    if (reference_kind != SCRIPT_REFERENCE_NONE) {
        return reference(l, reference_kind, v);
    }
    switch (v->type) {
        case PIN_BOOL: return token(l, v->b ? "true" : "false");
        case PIN_NAME:
        case PIN_STR: return string(l, pin_text(*v));
        case PIN_STRUCT: return aggregate(l, expected, v, depth);
        case PIN_POINTER:
            return fail(l, "Managed pointers cannot be written as source literals");
        case PIN_FLOW:
        case PIN_HANDLE:
            return fail(l, "Runtime handles and services cannot be written as source literals");
        default: return number(l, v);
    }
}
char *authoring_source_literal(
    const script_catalog *catalog,
    script_type expected,
    const pin_value *v,
    char *error,
    size_t size
)
{
    assert(catalog && v && (!size || error));
    if (size) {
        error[0] = 0;
    }
    literal l = { .catalog = catalog, .error = error, .error_size = size };
    if (!value(&l, expected, v, 0)) {
        afree(l.text);
        arena_free(&l.storage);
        return NULL;
    }
    int length = acount(l.text);
    char *result = xmalloc((size_t)length + 1);
    memcpy(result, l.text, (size_t)length);
    result[length] = 0;
    afree(l.text);
    arena_free(&l.storage);
    return result;
}
