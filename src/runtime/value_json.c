#include "runtime/value_json.h"

#include "support/asset_json.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/type.h"
#include "types/union.h"
#include "types/value_document.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    size_t bytes;
    int values;
    const char *error;
} budget;

static bool charge(budget *b, size_t bytes)
{
    if (bytes > VALUE_JSON_MAX_BYTES - b->bytes) {
        b->error = "JSON output exceeds the 1 MiB budget";
        return false;
    }
    b->bytes += bytes;
    return true;
}

static bool string_budget(budget *b, const char *text)
{
    if (!charge(b, 2)) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
        size_t bytes = *p < 0x20 ? 6 : *p == '"' || *p == '\\' ? 2 : 1;
        if (!charge(b, bytes)) {
            return false;
        }
    }
    return true;
}

static bool validate(budget *b, const pin_value *v, int depth)
{
    if (depth > VALUE_JSON_MAX_DEPTH || ++b->values > VALUE_JSON_MAX_VALUES) {
        b->error = "JSON value exceeds the nesting or value-count budget";
        return false;
    }
    if (v->container == PIN_CONTAINER_MAP) {
        b->error = "JSON encoding does not support maps";
        return false;
    }
    if (v->container == PIN_CONTAINER_ARRAY || v->container == PIN_CONTAINER_SET) {
        const pin_array *array = v->ptr;
        if (!charge(b, 2)) {
            return false;
        }
        if (!array) {
            return true;
        }
        if (array->count < 0 || array->count > VALUE_JSON_MAX_VALUES - b->values) {
            b->error = "JSON array exceeds the value-count budget";
            return false;
        }
        for (int i = 0; i < array->count; i++) {
            if (!charge(b, i ? 1 : 0) || !validate(b, &array->items[i], depth + 1)) {
                return false;
            }
        }
        return true;
    }
    switch (v->type) {
        case PIN_STR:
        case PIN_NAME: return string_budget(b, v->str);
        case PIN_F32:
        case PIN_F64:
            if (!isfinite(v->type == PIN_F32 ? (double)v->f : v->d)) {
                b->error = "JSON numbers must be finite";
                return false;
            }
            return charge(b, 32);
        case PIN_STRUCT: {
            if (!v->ptr) {
                return charge(b, 4);
            }
            const type_desc *type = v->type_name ? type_catalog_find(v->type_name)
                                                 : NULL;
            if (!type) {
                b->error = "JSON record requires a reflected type layout";
                return false;
            }
            if (!charge(b, 2)) {
                return false;
            }
            for (int i = 0; i < type->field_count; i++) {
                pin_value field;
                pin_read_field(v->ptr, &type->fields[i], &field);
                if (!charge(b, i ? 2 : 1) || !string_budget(b, type->fields[i].name) ||
                    !validate(b, &field, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        case PIN_ENUM: {
            const char *label = type_catalog_enum_label(v->type_name, v->e);
            return label ? string_budget(b, label) : charge(b, 12);
        }
        case PIN_UNION: {
            const pin_union *tagged = v->ptr;
            if (!tagged) {
                return charge(b, 4);
            }
            if (tagged->count < 0 || tagged->count > VALUE_JSON_MAX_VALUES - b->values) {
                b->error = "JSON union exceeds the value-count budget";
                return false;
            }
            if (!charge(b, 48)) {
                return false;
            }
            for (int i = 0; i < tagged->count; i++) {
                if (!charge(b, i ? 1 : 0) ||
                    !validate(b, &tagged->values[i], depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        case PIN_TASK:
            b->error = "Runtime tasks cannot be encoded as JSON";
            return false;
        case PIN_FLOW: return charge(b, 4);
        case PIN_BOOL: return charge(b, 5);
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_I64:
        case PIN_U64:
        case PIN_HANDLE: return charge(b, 24);
    }
    b->error = "Unsupported JSON value type";
    return false;
}

bool value_to_json(const pin_value *value, char **text, char *error, size_t error_size)
{
    *text = NULL;
    if (error_size) {
        error[0] = 0;
    }
    budget b = { 0 };
    if (validate(&b, value, 0)) {
        av_doc *doc = av_doc_new();
        av_value *root = pin_value_to_document(doc, value);
        *text = root ? av_json_write(root) : NULL;
        av_doc_free(doc);
        if (*text && strlen(*text) <= VALUE_JSON_MAX_BYTES) {
            return true;
        }
        free(*text);
        *text = NULL;
        b.error = "Cannot encode this value as valid UTF-8 JSON within the "
                  "byte budget";
    }
    if (error_size) {
        snprintf(error, error_size, "%s", b.error);
    }
    return false;
}
