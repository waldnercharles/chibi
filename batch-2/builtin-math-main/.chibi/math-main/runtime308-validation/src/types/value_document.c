#include "types/value_document.h"

#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/schema.h"
#include "types/text.h"
#include "types/type.h"
#include "types/union.h"

#include <string.h>

#define DOCUMENT_MAX_VALUES 1000000
#define DOCUMENT_MAX_DEPTH 64

typedef struct
{
    int values;
    arena temporary;
} document_writer;

static av_value *shape(av_doc *doc, const pin_value *value)
{
    if (value->type < PIN_BOOL || value->type > PIN_QUERY ||
        value->container < PIN_CONTAINER_SINGLE || value->container > PIN_CONTAINER_MAP ||
        (value->record &&
         ((value->schema ? !pin_value_record_current(value)
                         : !pin_record_valid(value->record) ||
                               !pin_record_fields_current(value->record)) ||
          value->type != PIN_STRUCT))) {
        return NULL;
    }
    static const char *containers[] = { "value", "array", "set", "map" };
    av_value *out = av_obj(doc);
    av_obj_add_str(doc, out, "storage", pin_type_name(value->type));
    av_obj_add_str(doc, out, "container", containers[value->container]);
    if (value->type_name) {
        av_obj_add_str(doc, out, "name", value->type_name);
    }
    if (value->record && !value->record->native) {
        av_obj_add_uint(doc, out, "record_id", value->record->id);
        av_obj_add_uint(doc, out, "record_layout", value->record->layout);
    }
    return out;
}

static av_value *project(
    av_doc *,
    const pin_value *,
    pin_type,
    const char *,
    const pin_record *,
    document_writer *,
    int
);

static av_value *map_document(av_doc *doc, const pin_map *map, document_writer *writer, int depth)
{
    if (!map) {
        return av_null(doc);
    }
    if (map->count < 0 || map->count > DOCUMENT_MAX_VALUES / 2 ||
        (map->count && !map->entries) || !pin_map_key_supported(map->key_type)) {
        return NULL;
    }
    av_value *keys = shape(doc, &map->key_type),
             *values = shape(doc, &map->value_type);
    if (!keys || !values) {
        return NULL;
    }
    av_value *out = av_obj(doc), *entries = av_arr(doc);
    av_obj_set(doc, out, "key_type", keys);
    av_obj_set(doc, out, "value_type", values);
    av_obj_set(doc, out, "entries", entries);
    for (int i = 0; i < map->count; i++) {
        const pin_map_entry *entry = &map->entries[i];
        if (!pin_map_entry_valid(map, &entry->key, &entry->value)) {
            return NULL;
        }
        av_value *key = project(
            doc,
            &entry->key,
            entry->key.type,
            entry->key.type_name,
            map->key_type.record,
            writer,
            depth + 1
        );
        av_value *value = project(
            doc,
            &entry->value,
            entry->value.type,
            entry->value.type_name,
            map->value_type.record,
            writer,
            depth + 1
        );
        if (!key || !value) {
            return NULL;
        }
        av_value *pair = av_arr(doc);
        av_arr_append(pair, key);
        av_arr_append(pair, value);
        av_arr_append(entries, pair);
    }
    return out;
}

static bool record_header(const pin_value *value, const pin_record *validated)
{
    return value->type == PIN_STRUCT &&
           (value->container == PIN_CONTAINER_SINGLE ||
            value->container == PIN_CONTAINER_ARRAY ||
            value->container == PIN_CONTAINER_SET) &&
           (value->schema
                ? pin_value_record_current(value)
                : (value->record == validated || pin_record_valid(value->record)) &&
                      pin_record_fields_current(value->record)) &&
           pin_value_type(value);
}
/* NULL means invalid input. A valid document null is a distinct av_value. Build
 * each whole subtree before exposing it through the caller's destination key. */
static av_value *project(
    av_doc *doc,
    const pin_value *v,
    pin_type t,
    const char *type_name,
    const pin_record *validated,
    document_writer *writer,
    int depth
)
{
    if (depth >= DOCUMENT_MAX_DEPTH || ++writer->values > DOCUMENT_MAX_VALUES) {
        return NULL;
    }
    if (v->container == PIN_CONTAINER_VIEW) {
        return NULL;
    }
    if (v->container == PIN_CONTAINER_MAP) {
        return map_document(doc, v->ptr, writer, depth);
    }
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
        if (!pin_array_valid(items)) {
            return NULL;
        }
        if (items->record || v->record) {
            pin_value element = { .type = items->element_type,
                                  .type_name = items->type_name,
                                  .record = items->record,
                                  .schema = items->schema };
            if (!v->record || !items->record || !record_header(&element, v->record) ||
                !pin_record_same(items->record, v->record) ||
                items->element_type != PIN_STRUCT || !items->type_name ||
                strcmp(items->type_name, v->type_name)) {
                return NULL;
            }
        }
        av_value *result = array;
        bool nested = items->count &&
                      pin_array_get(items, 0).container != PIN_CONTAINER_SINGLE;
        pin_value element_shape = nested ? pin_array_get(items, 0) : (pin_value){ 0 };
        if (nested) {
            av_value *element_type = shape(doc, &element_shape);
            if (!element_type) {
                return NULL;
            }
            result = av_obj(doc);
            av_obj_set(doc, result, "element_type", element_type);
            av_obj_set(doc, result, "items", array);
        }
        for (int i = 0; i < items->count; i++) {
            pin_value element = pin_array_get(items, i);
            const pin_value *item = &element;
            if (nested && (item->type != element_shape.type ||
                           item->container != element_shape.container ||
                           strcmp(
                               item->type_name ? item->type_name : "",
                               element_shape.type_name ? element_shape.type_name : ""
                           ) ||
                           !pin_record_same(item->record, element_shape.record))) {
                return NULL;
            }
            bool nested_descriptor = nested && !v->record && v->type_name &&
                                     !strncmp(v->type_name, "__collection_", 13);
            if (!nested_descriptor && (v->record || item->record) &&
                (!v->record || !item->record || !record_header(item, v->record) ||
                 item->container != PIN_CONTAINER_SINGLE ||
                 !pin_record_same(item->record, v->record))) {
                return NULL;
            }
            av_value *child = project(doc, item, item->type, item->type_name, v->record, writer, depth + 1);
            if (!child) {
                return NULL;
            }
            av_arr_append(array, child);
        }
        return result;
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
        case PIN_NAME: return av_str(doc, v->str ? v->str : "");
        case PIN_STR: {
            pin_string text = pin_text(*v);
            if (pin_text_valid_utf8(text)) {
                return av_strn(doc, text.bytes, text.length);
            }
            if (text.length > (unsigned)(DOCUMENT_MAX_VALUES - writer->values)) {
                return NULL;
            }
            writer->values += (int)text.length;
            av_value *encoded = av_obj(doc), *bytes = av_arr(doc);
            for (uint32_t i = 0; i < text.length; i++) {
                av_arr_append(bytes, av_uint(doc, (unsigned char)text.bytes[i]));
            }
            av_obj_set(doc, encoded, "bytes", bytes);
            return encoded;
        }
        case PIN_STRUCT:
            if (v->ptr && type_name) {
                const type_desc *type = v->record || v->schema
                                          ? pin_value_type(v)
                                          : type_catalog_find(type_name);
                const type_desc *layout = type;
                type_resolver resolver = v->schema ? pin_schema_resolver(v->schema)
                                                   : (type_resolver){ 0 };
                for (int depth = 0; type && type->alias_of && depth < TYPE_MAX_NESTING;
                     depth++) {
                    type = type_alias_type(type, resolver);
                }
                if (layout && (!type || type->alias_of || type->size != layout->size)) {
                    return NULL;
                }
                if (type) {
                    value = av_obj(doc);
                    for (int i = 0; i < type->field_count; i++) {
                        pin_value field;
                        if (pin_read_field(&writer->temporary, v->ptr, &type->fields[i], &field)) {
                            return NULL;
                        }
                        if (field.type == PIN_STRUCT) {
                            field.schema = v->schema;
                        }
                        av_value *child = project(
                            doc,
                            &field,
                            field.type,
                            field.type_name,
                            NULL,
                            writer,
                            depth + 1
                        );
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
        case PIN_QUERY:
        case PIN_POINTER: return v->ptr ? NULL : av_null(doc);
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
                av_value *child = project(doc, item, item->type, item->type_name, NULL, writer, depth + 1);
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
    document_writer writer = { 0 };
    av_value *value = project(doc, v, type, type_name, NULL, &writer, 0);
    if (value) {
        av_obj_set(doc, obj, key, value);
    }
    arena_free(&writer.temporary);
}
av_value *pin_value_to_document(av_doc *doc, const pin_value *value)
{
    document_writer writer = { 0 };
    av_value *result = project(doc, value, value->type, value->type_name, NULL, &writer, 0);
    arena_free(&writer.temporary);
    return result;
}
