#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

void serialize_write_value(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    pin_type t,
    const char *type_name,
    const pin_value *v
)
{
    if (v && v->container == PIN_CONTAINER_ARRAY) {
        yyjson_mut_val *array = yyjson_mut_arr(doc);
        const pin_array *items = (const pin_array *)v->ptr;
        if (items) {
            for (int i = 0; i < items->count; i++) {
                yyjson_mut_val *holder = yyjson_mut_obj(doc);
                serialize_write_value(
                    doc,
                    holder,
                    "value",
                    items->items[i].type,
                    items->items[i].type_name,
                    &items->items[i]
                );
                yyjson_mut_arr_add_val(array, yyjson_mut_obj_get(holder, "value"));
            }
        }
        yyjson_mut_obj_add_val(doc, obj, key, array);
        return;
    }
    yyjson_mut_val *value = NULL;
    switch (t) {
        case PIN_BOOL: value = yyjson_mut_bool(doc, v->b); break;
        case PIN_I8: value = yyjson_mut_sint(doc, v->i8); break;
        case PIN_U8: value = yyjson_mut_uint(doc, v->u8); break;
        case PIN_I16: value = yyjson_mut_sint(doc, v->i16); break;
        case PIN_U16: value = yyjson_mut_uint(doc, v->u16); break;
        case PIN_I32: value = yyjson_mut_sint(doc, v->i); break;
        case PIN_U32: value = yyjson_mut_uint(doc, v->u); break;
        case PIN_I64: value = yyjson_mut_sint(doc, v->i64); break;
        case PIN_U64: value = yyjson_mut_uint(doc, v->u64); break;
        case PIN_F32: value = yyjson_mut_real(doc, v->f); break;
        case PIN_F64: value = yyjson_mut_real(doc, v->d); break;
        case PIN_ENUM: value = yyjson_mut_sint(doc, v->e); break;
        case PIN_NAME:
        case PIN_STR:
            value = yyjson_mut_strcpy(doc, v->str ? v->str : "");
            break;
        case PIN_STRUCT:
            if (v->ptr && type_name) {
                const type_desc *type = registry_type_by_name(type_name);
                if (type) {
                    value = yyjson_mut_obj(doc);
                    for (int i = 0; i < type->field_count; i++) {
                        pin_value field;
                        pin_read_field(v->ptr, &type->fields[i], &field);
                        serialize_write_value(
                            doc,
                            value,
                            type->fields[i].name,
                            type->fields[i].type,
                            type->fields[i].type_name,
                            &field
                        );
                    }
                }
            }
            if (!value) {
                value = yyjson_mut_null(doc);
            }
            break;
        case PIN_FLOW:
        case PIN_HANDLE: value = yyjson_mut_null(doc); break;
    }
    if (value) {
        yyjson_mut_obj_add(obj, yyjson_mut_strcpy(doc, key), value);
    }
}

static int read_signed(yyjson_val *v, int64_t *out)
{
    if (yyjson_is_sint(v)) {
        *out = yyjson_get_sint(v);
        return 0;
    }
    if (yyjson_is_uint(v) && yyjson_get_uint(v) <= INT64_MAX) {
        *out = (int64_t)yyjson_get_uint(v);
        return 0;
    }
    return -1;
}

int serialize_read_array(
    pin_type element_type,
    const char *type_name,
    yyjson_val *json,
    pin_value *out,
    graph *g
)
{
    if (!yyjson_is_arr(json)) {
        return -1;
    }
    int count = (int)yyjson_arr_size(json);
    pin_array *array = pin_array_new(element_type, type_name, count);
    yyjson_arr_iter it;
    yyjson_arr_iter_init(json, &it);
    yyjson_val *item;
    int i = 0;
    while ((item = yyjson_arr_iter_next(&it))) {
        if (serialize_read_value(element_type, item, &array->items[i], g)) {
            free(array->items);
            free(array);
            return -1;
        }
        array->items[i].container = PIN_CONTAINER_SINGLE;
        i++;
    }
    array->count = count;
    pin_array *owned = pin_array_clone_to_graph(g, array);
    free(array->items);
    free(array);
    if (!owned) {
        return -1;
    }
    *out = (pin_value){ .type = element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = type_name,
                        .ptr = owned };
    return 0;
}

int serialize_read_value(pin_type t, yyjson_val *v, pin_value *out, graph *g)
{
    memset(out, 0, sizeof *out);
    out->type = t;
    switch (t) {
        case PIN_BOOL:
            if (!yyjson_is_bool(v)) {
                return -1;
            }
            out->b = yyjson_get_bool(v);
            break;
        case PIN_I8: {
            int64_t value;
            if (read_signed(v, &value) || value < INT8_MIN || value > INT8_MAX) {
                return -1;
            }
            out->i8 = (int8_t)value;
            break;
        }
        case PIN_U8:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT8_MAX) {
                return -1;
            }
            out->u8 = (uint8_t)yyjson_get_uint(v);
            break;
        case PIN_I16: {
            int64_t value;
            if (read_signed(v, &value) || value < INT16_MIN || value > INT16_MAX) {
                return -1;
            }
            out->i16 = (int16_t)value;
            break;
        }
        case PIN_U16:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT16_MAX) {
                return -1;
            }
            out->u16 = (uint16_t)yyjson_get_uint(v);
            break;
        case PIN_I32: {
            int64_t value;
            if (read_signed(v, &value) || value < INT32_MIN || value > INT32_MAX) {
                return -1;
            }
            out->i = (int32_t)value;
            break;
        }
        case PIN_U32:
            if (!yyjson_is_uint(v) || yyjson_get_uint(v) > UINT32_MAX) {
                return -1;
            }
            out->u = (uint32_t)yyjson_get_uint(v);
            break;
        case PIN_I64:
            if (read_signed(v, &out->i64)) {
                return -1;
            }
            break;
        case PIN_U64:
            if (!yyjson_is_uint(v)) {
                return -1;
            }
            out->u64 = yyjson_get_uint(v);
            break;
        case PIN_F32:
            if (!yyjson_is_num(v)) {
                return -1;
            }
            out->f = (float)yyjson_get_num(v);
            break;
        case PIN_F64:
            if (!yyjson_is_num(v)) {
                return -1;
            }
            out->d = yyjson_get_num(v);
            break;
        case PIN_ENUM:
            if (yyjson_is_sint(v)) {
                out->e = (int32_t)yyjson_get_sint(v);
            } else if (yyjson_is_uint(v) && yyjson_get_uint(v) <= INT32_MAX) {
                out->e = (int32_t)yyjson_get_uint(v);
            } else {
                return -1;
            }
            break;
        case PIN_NAME:
        case PIN_STR:
            if (!yyjson_is_str(v)) {
                return -1;
            }
            out->str = graph_own_str(g, yyjson_get_str(v), yyjson_get_len(v));
            if (!out->str) {
                return -1;
            }
            break;
        case PIN_STRUCT:
        case PIN_FLOW:
        case PIN_HANDLE:
            if (!yyjson_is_null(v)) {
                return -1;
            }
            break;
    }
    return 0;
}
