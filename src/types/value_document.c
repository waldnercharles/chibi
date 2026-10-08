#include "types/value_document.h"

#include "types/array.h"
#include "types/catalog.h"
#include "types/type.h"

void pin_value_write_document(
    av_doc *doc,
    av_value *obj,
    const char *key,
    pin_type t,
    const char *type_name,
    const pin_value *v
)
{
    if (v && (v->container == PIN_CONTAINER_ARRAY || v->container == PIN_CONTAINER_SET)) {
        av_value *array = av_arr(doc);
        const pin_array *items = (const pin_array *)v->ptr;
        if (items) {
            for (int i = 0; i < items->count; i++) {
                av_value *holder = av_obj(doc);
                pin_value_write_document(
                    doc,
                    holder,
                    "value",
                    items->items[i].type,
                    items->items[i].type_name,
                    &items->items[i]
                );
                av_arr_append(array, av_obj_get(holder, "value"));
            }
        }
        av_obj_set(doc, obj, key, array);
        return;
    }
    av_value *value = NULL;
    switch (t) {
        case PIN_BOOL: value = av_bool(doc, v->b); break;
        case PIN_I8: value = av_sint(doc, v->i8); break;
        case PIN_U8: value = av_uint(doc, v->u8); break;
        case PIN_I16: value = av_sint(doc, v->i16); break;
        case PIN_U16: value = av_uint(doc, v->u16); break;
        case PIN_I32: value = av_sint(doc, v->i); break;
        case PIN_U32: value = av_uint(doc, v->u); break;
        case PIN_I64: value = av_sint(doc, v->i64); break;
        case PIN_U64: value = av_uint(doc, v->u64); break;
        case PIN_F32: value = av_float(doc, v->f); break;
        case PIN_F64: value = av_real(doc, v->d); break;
        case PIN_ENUM: {
            const char *label = type_catalog_enum_label(type_name, v->e);
            value = label ? av_str(doc, label) : av_sint(doc, v->e);
            break;
        }
        case PIN_NAME:
        case PIN_STR: value = av_str(doc, v->str ? v->str : ""); break;
        case PIN_STRUCT:
            if (v->ptr && type_name) {
                const type_desc *type = type_catalog_find(type_name);
                if (type) {
                    value = av_obj(doc);
                    for (int i = 0; i < type->field_count; i++) {
                        pin_value field;
                        pin_read_field(v->ptr, &type->fields[i], &field);
                        pin_value_write_document(
                            doc,
                            value,
                            type->fields[i].name,
                            type->fields[i].type,
                            type->fields[i].type_name,
                            &field
                        );
                        if (type->fields[i].color) {
                            av_obj_set(
                                doc,
                                value,
                                type->fields[i].name,
                                av_color(doc, field.u)
                            );
                        }
                    }
                }
            }
            if (!value) {
                value = av_null(doc);
            }
            break;
        case PIN_FLOW: value = av_null(doc); break;
        case PIN_HANDLE: value = av_uint(doc, v->u64); break;
    }
    if (value) {
        av_obj_set(doc, obj, key, value);
    }
}

av_value *pin_value_to_document(av_doc *doc, const pin_value *value)
{
    av_value *holder = av_obj(doc);
    pin_value_write_document(doc, holder, "value", value->type, value->type_name, value);
    return av_obj_get(holder, "value");
}
