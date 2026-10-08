#include "authoring/source_literal.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

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

typedef struct
{
    const script_catalog *catalog;
    char *text, *error;
    size_t error_size;
    int values;
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
static bool string(literal *l, const char *text)
{
    if (!text) {
        return token(l, "null");
    }
    if (!token(l, "\"")) {
        return false;
    }
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
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
        if (!bytes) {
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
        } else {
            memcpy(&item.d, data, size);
        }
        if ((i && !token(l, ", ")) || !string(l, field->name) ||
            !token(l, ": ") || !value(l, field->type, &item, depth + 1)) {
            return false;
        }
    }
    return token(l, "}");
}
static bool array(literal *l, script_type expected, const pin_value *v, int depth)
{
    const pin_array *items = v->ptr;
    if (!items || items->count < 0 || items->count > LITERAL_MAX_VALUES - l->values ||
        items->element_type != expected.storage || (items->count && !items->items)) {
        return fail(l, "Array literal has an invalid payload shape or element limit");
    }
    if ((items->type_name || v->type_name) &&
        (!items->type_name || !v->type_name || strcmp(items->type_name, v->type_name))) {
        return fail(l, "Array literal nominal header does not match its payload");
    }
    if (!token(l, "[")) {
        return false;
    }
    expected.container = PIN_CONTAINER_SINGLE;
    for (int i = 0; i < items->count; ++i) {
        if ((i && !token(l, ", ")) ||
            !value(l, expected, &items->items[i], depth + 1)) {
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
    if (expected.nominal) {
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
    if (expected.container == PIN_CONTAINER_SET ||
        expected.container == PIN_CONTAINER_MAP) {
        return fail(l, "Set and map values have no supported source literal contract");
    }
    if (expected.container == PIN_CONTAINER_ARRAY) {
        return array(l, expected, v, depth);
    }
    switch (v->type) {
        case PIN_BOOL: return token(l, v->b ? "true" : "false");
        case PIN_NAME:
        case PIN_STR: return string(l, v->str);
        case PIN_STRUCT: return aggregate(l, expected, v, depth);
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
        return NULL;
    }
    int length = acount(l.text);
    char *result = xmalloc((size_t)length + 1);
    memcpy(result, l.text, (size_t)length);
    result[length] = 0;
    afree(l.text);
    return result;
}
