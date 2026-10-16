#include "types/value_document.h"

#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/type.h"
#include "types/union.h"

#include <string.h>

static bool record_header(const pin_value *value, const pin_record *validated)
{
    return value->type == PIN_STRUCT &&
           (value->container == PIN_CONTAINER_SINGLE ||
            value->container == PIN_CONTAINER_ARRAY ||
            value->container == PIN_CONTAINER_SET) &&
           (value->record == validated || pin_record_valid(value->record)) &&
           pin_value_type(value);
}
/* NULL means invalid input. A valid document null is a distinct av_value. Build
 * each whole subtree before exposing it through the caller's destination key. */
static av_value *project(av_doc *doc, const pin_value *v, pin_type t, const char *type_name, const pin_record *validated)
{
    if (v->record && (!record_header(v, validated) || t != v->type ||
                      !type_name || strcmp(type_name, v->type_name))) {
        return NULL;
    }
    if (v->container == PIN_CONTAINER_ARRAY || v->container == PIN_CONTAINER_SET) {
        av_value *array = av_arr(doc);
        const pin_array *items = v->ptr;
        if (!items) {
            return array;
        }
        if (items->count < 0 || items->capacity < items->count ||
            (items->count && !items->items)) {
            return NULL;
        }
        if (items->record || v->record) {
            if (!v->record || !items->record ||
                (items->record != v->record && !pin_record_valid(items->record)) ||
                !pin_record_same(items->record, v->record) ||
                items->element_type != PIN_STRUCT || !items->type_name ||
                strcmp(items->type_name, v->type_name)) {
                return NULL;
            }
        }
        for (int i = 0; i < items->count; i++) {
            const pin_value *item = &items->items[i];
            if ((v->record || item->record) &&
                (!v->record || !item->record || !record_header(item, v->record) ||
                 item->container != PIN_CONTAINER_SINGLE ||
                 !pin_record_same(item->record, v->record))) {
                return NULL;
            }
            av_value *child = project(doc, item, item->type, item->type_name, v->record);
            if (!child) {
                return NULL;
            }
            av_arr_append(array, child);
        }
        return array;
    }
    av_value *value = NULL;
    switch (t) {
        case PIN_BOOL: return av_bool(doc, v->b);
        case PIN_I8: return av_sint(doc, v->i8);
        case PIN_U8: return av_uint(doc, v->u8);
        case PIN_I16: return av_sint(doc, v->i16);
        case PIN_U16: return av_uint(doc, v->u16);
        case PIN_I32: return av_sint(doc, v->i);
        case PIN_U32: return av_uint(doc, v->u);
        case PIN_I64: return av_sint(doc, v->i64);
        case PIN_U64: return av_uint(doc, v->u64);
        case PIN_F32: return av_float(doc, v->f);
        case PIN_F64: return av_real(doc, v->d);
        case PIN_ENUM: {
            const char *label = type_catalog_enum_label(type_name, v->e);
            return label ? av_str(doc, label) : av_sint(doc, v->e);
        }
        case PIN_NAME:
        case PIN_STR: return av_str(doc, v->str ? v->str : "");
        case PIN_STRUCT:
            if (v->ptr && type_name) {
                const type_desc *type = v->record ? pin_value_type(v)
                                                  : type_catalog_find(type_name);
                if (type) {
                    value = av_obj(doc);
                    for (int i = 0; i < type->field_count; i++) {
                        pin_value field;
                        pin_read_field(v->ptr, &type->fields[i], &field);
                        av_value *child = project(doc, &field, field.type, field.type_name, NULL);
                        if (!child) {
                            return NULL;
                        }
                        av_obj_set(
                            doc,
                            value,
                            type->fields[i].name,
                            type->fields[i].color ? av_color(doc, field.u) : child
                        );
                    }
                }
            }
            return value ? value : av_null(doc);
        case PIN_FLOW: return av_null(doc);
        case PIN_HANDLE:
        case PIN_TASK: return av_uint(doc, v->u64);
        case PIN_UNION: {
            const pin_union *tagged = v->ptr;
            if (!tagged) {
                return av_null(doc);
            }
            if (tagged->count < 0) {
                return NULL;
            }
            value = av_obj(doc);
            av_value *payload = av_arr(doc);
            for (int i = 0; i < tagged->count; i++) {
                const pin_value *item = &tagged->values[i];
                av_value *child = project(doc, item, item->type, item->type_name, NULL);
                if (!child) {
                    return NULL;
                }
                av_arr_append(payload, child);
            }
            av_obj_set(doc, value, "tag", av_uint(doc, tagged->tag));
            av_obj_set(doc, value, "values", payload);
            return value;
        }
    }
    return NULL;
}
void pin_value_write_document(
    av_doc *doc,
    av_value *obj,
    const char *key,
    pin_type type,
    const char *type_name,
    const pin_value *v
)
{
    av_value *value = project(doc, v, type, type_name, NULL);
    if (value) {
        av_obj_set(doc, obj, key, value);
    }
}
av_value *pin_value_to_document(av_doc *doc, const pin_value *value)
{
    return project(doc, value, value->type, value->type_name, NULL);
}
