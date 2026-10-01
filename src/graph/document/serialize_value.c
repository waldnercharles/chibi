#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *serialize_pin_type_name(pin_type type)
{
    const char *name = pin_type_name(type);
    return name ? name : "flow";
}

const char *serialize_implied_type_name(pin_type type, const char *template_name)
{
    bool primitive = (type >= PIN_BOOL && type <= PIN_F64) ||
                     type == PIN_NAME || type == PIN_STR;
    return primitive ? pin_type_name(type) : template_name;
}

pin_value serialize_zero_value(pin_type type, pin_container container, const char *type_name)
{
    pin_value value = { .type = type, .container = container, .type_name = type_name };
    if (container == PIN_CONTAINER_SINGLE && (type == PIN_STR || type == PIN_NAME)) {
        value.str = "";
    }
    return value;
}

bool serialize_read_pin_type(yyjson_val *value, pin_type *out)
{
    if (yyjson_is_int(value) && yyjson_get_sint(value) >= PIN_FLOW &&
        yyjson_get_sint(value) <= PIN_ENUM) {
        *out = (pin_type)yyjson_get_sint(value);
        return true;
    }
    for (int type = PIN_FLOW; yyjson_is_str(value) && type <= PIN_ENUM; type++) {
        if (!strcmp(yyjson_get_str(value), serialize_pin_type_name((pin_type)type))) {
            *out = (pin_type)type;
            return true;
        }
    }
    return false;
}

void serialize_write_value(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    pin_type t,
    const char *type_name,
    const pin_value *v
)
{
    if (v && (v->container == PIN_CONTAINER_ARRAY || v->container == PIN_CONTAINER_SET)) {
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
        case PIN_F32: value = yyjson_mut_float(doc, v->f); break;
        case PIN_F64: value = yyjson_mut_real(doc, v->d); break;
        case PIN_ENUM: {
            const char *label = registry_enum_label(type_name, v->e);
            value = label ? yyjson_mut_strcpy(doc, label)
                          : yyjson_mut_sint(doc, v->e);
            break;
        }
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
        case PIN_FLOW: value = yyjson_mut_null(doc); break;
        case PIN_HANDLE: value = yyjson_mut_uint(doc, v->u64); break;
    }
    if (value) {
        yyjson_mut_obj_add(obj, yyjson_mut_strcpy(doc, key), value);
    }
}

char *serialize_value_json(const pin_value *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *holder = yyjson_mut_obj(doc);
    serialize_write_value(doc, holder, "value", value->type, value->type_name, value);
    char *json = yyjson_mut_val_write(yyjson_mut_obj_get(holder, "value"), 0, NULL);
    yyjson_mut_doc_free(doc);
    assert(json);
    return json;
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

static int read_unsigned(yyjson_val *v, uint64_t *out)
{
    if (yyjson_is_uint(v)) {
        *out = yyjson_get_uint(v);
        return 0;
    }
    if (yyjson_is_sint(v) && yyjson_get_sint(v) >= 0) {
        *out = (uint64_t)yyjson_get_sint(v);
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
    if (!yyjson_is_arr(json) || yyjson_arr_size(json) > INT_MAX) {
        serialize_error_set("invalid array default");
        return -1;
    }
    int count = (int)yyjson_arr_size(json);
    pin_array *array = graph_own_block(g, sizeof *array + (size_t)count * sizeof(pin_value));
    *array = (pin_array){
        .element_type = element_type,
        .type_name = type_name ? graph_own_str(g, type_name, strlen(type_name)) : NULL,
        .count = count,
        .capacity = count,
        .items = (pin_value *)(array + 1)
    };
    for (int i = 0; i < count; i++) {
        if (serialize_read_value(
                element_type,
                array->type_name,
                yyjson_arr_get(json, (size_t)i),
                &array->items[i],
                g
            )) {
            if (element_type != PIN_STRUCT) {
                serialize_error_set("invalid array item %d", i);
            }
            return -1;
        }
    }
    *out = (pin_value){ .type = element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array->type_name,
                        .ptr = array };
    return 0;
}

static int read_struct(const char *type_name, yyjson_val *json, pin_value *out, graph *g)
{
    const type_desc *type = registry_type_by_name(type_name);
    if (!type || !type->size || !yyjson_is_obj(json)) {
        serialize_error_set("invalid struct default for %s", type_name ? type_name : "unknown type");
        return -1;
    }
    void *block = graph_own_block(g, type->size);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        yyjson_val *value = yyjson_obj_get(json, field->name);
        if (!value) {
            serialize_error_set("missing struct field %s.%s", type->name, field->name);
            return -1;
        }
        if (field->type == PIN_STRUCT && !yyjson_is_obj(value)) {
            serialize_error_set("invalid struct field %s.%s", type->name, field->name);
            return -1;
        }
        pin_value member;
        if (serialize_read_value(field->type, field->type_name, value, &member, g)) {
            if (field->type != PIN_STRUCT) {
                serialize_error_set(
                    "invalid struct field %s.%s",
                    type->name,
                    field->name
                );
            }
            return -1;
        }
        pin_write_field(block, field, member);
    }
    *out = (pin_value){ .type = PIN_STRUCT, .type_name = type_name, .ptr = block };
    return 0;
}

int serialize_read_value(pin_type t, const char *type_name, yyjson_val *v, pin_value *out, graph *g)
{
    memset(out, 0, sizeof *out);
    out->type = t;
    out->type_name = type_name;
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
        case PIN_ENUM: {
            int label_value = 0;
            if (yyjson_is_str(v) &&
                registry_enum_value(type_name, yyjson_get_str(v), &label_value)) {
                out->e = label_value;
            } else {
                int64_t value;
                if (read_signed(v, &value) || value < INT32_MIN || value > INT32_MAX) {
                    return -1;
                }
                out->e = (int32_t)value;
            }
            break;
        }
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
            return yyjson_is_null(v) ? 0 : read_struct(type_name, v, out, g);
        case PIN_FLOW:
            if (!yyjson_is_null(v)) {
                return -1;
            }
            break;
        case PIN_HANDLE:
            if (yyjson_is_null(v)) {
                break;
            }
            if (read_unsigned(v, &out->u64)) {
                return -1;
            }
            break;
    }
    return 0;
}
