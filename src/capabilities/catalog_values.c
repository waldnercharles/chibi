#include "capabilities/catalog.h"
#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <string.h>

/* Bounded exact comparison evidence, rather than a collision-prone content
 * hash or borrowed type descriptor. This is process-local cache metadata. */
enum
{
    DEFAULT_KEY_BYTES = 1024 * 1024,
    DEFAULT_KEY_DEPTH = 64
};
static bool key_bytes(unsigned char **key, const void *data, size_t size)
{
    size_t before = (size_t)acount(*key);
    if (size > DEFAULT_KEY_BYTES - before) {
        return false;
    }
    asetlen(*key, (int)(before + size));
    if (size) {
        memcpy(*key + before, data, size);
    }
    return true;
}
static bool key_word(unsigned char **key, uint64_t word)
{
    return key_bytes(key, &word, sizeof word);
}
static bool key_text(unsigned char **key, const char *text)
{
    size_t size = text ? strlen(text) : 0;
    return key_word(key, text ? (uint64_t)size : UINT64_MAX) &&
           key_bytes(key, text, size);
}
static bool key_value(unsigned char **key, const pin_value *value, int depth);
static bool key_layout(unsigned char **key, const type_desc *type, int depth)
{
    if (!type || !type->size || type->field_count < 0 || depth >= DEFAULT_KEY_DEPTH ||
        !key_word(key, type->size) || !key_word(key, (uint64_t)type->field_count) ||
        !key_text(key, type->name) || !key_text(key, type->alias_of)) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        size_t width = 0;
        const type_desc *nested = NULL;
        switch (field->type) {
            case PIN_FLOW: break;
            case PIN_BOOL: width = sizeof(bool); break;
            case PIN_I8:
            case PIN_U8: width = 1; break;
            case PIN_I16:
            case PIN_U16: width = 2; break;
            case PIN_I32:
            case PIN_U32:
            case PIN_F32: width = 4; break;
            case PIN_I64:
            case PIN_U64:
            case PIN_F64:
            case PIN_HANDLE:
            case PIN_TASK: width = 8; break;
            case PIN_NAME:
            case PIN_STR:
            case PIN_UNION: width = sizeof(void *); break;
            case PIN_ENUM:
                if (field->size != 1 && field->size != 2 && field->size != 4) {
                    return false;
                }
                width = field->size;
                break;
            case PIN_STRUCT:
                nested = type_catalog_find(field->type_name);
                if (!nested) {
                    return false;
                }
                width = nested->size;
                break;
            default: return false;
        }
        if (field->size != width || field->offset > type->size ||
            width > type->size - field->offset || !key_word(key, field->type) ||
            !key_text(key, field->type_name) || !key_text(key, field->name) ||
            !key_word(key, field->offset) || !key_word(key, field->size) ||
            !key_word(key, field->range_contract) ||
            !key_word(key, field->text_descriptor) ||
            !key_bytes(key, &field->min, sizeof field->min) ||
            !key_bytes(key, &field->max, sizeof field->max) ||
            !key_bytes(key, &field->range_default, sizeof field->range_default) ||
            !key_text(key, field->unit) ||
            (nested && !key_layout(key, nested, depth + 1))) {
            return false;
        }
    }
    return true;
}
static bool key_struct(unsigned char **key, const pin_value *value, int depth)
{
    if (!key_word(key, value->ptr != NULL)) {
        return false;
    }
    if (!value->ptr) {
        return true;
    }
    const type_desc *type = pin_value_type(value);
    /* Capture every allocated byte, including opaque/partially reflected
     * fields. The descriptor is the one used by this construction's snapshot,
     * never a later descriptor or the independent catalog-declared size.
     * Raw copying handles packed scalar fields without alignment assumptions.
     * Separately snapshotted managed pointers and padding may conservatively
     * miss; their addresses cannot coincide between two live owned snapshots. */
    return key_layout(key, type, depth) && key_bytes(key, value->ptr, type->size);
}
static bool key_value(unsigned char **key, const pin_value *value, int depth)
{
    if (depth >= DEFAULT_KEY_DEPTH || !key_word(key, value->type) ||
        !key_word(key, value->container) || !key_text(key, value->type_name)) {
        return false;
    }
    if (value->container == PIN_CONTAINER_MAP) {
        const pin_map *map = value->ptr;
        if (!key_word(key, map != NULL)) {
            return false;
        }
        if (!map) {
            return true;
        }
        if (map->count < 0 || (map->count && !map->entries) ||
            !key_value(key, &map->key_type, depth + 1) ||
            !key_value(key, &map->value_type, depth + 1) ||
            !key_word(key, (uint64_t)map->count)) {
            return false;
        }
        for (int i = 0; i < map->count; i++) {
            if (!key_value(key, &map->entries[i].key, depth + 1) ||
                !key_value(key, &map->entries[i].value, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (value->container) {
        const pin_array *array = value->ptr;
        if (!key_word(key, array != NULL)) {
            return false;
        }
        if (!array) {
            return true;
        }
        if (!pin_array_valid(array) || !key_word(key, array->element_type) ||
            !key_text(key, array->type_name) ||
            !key_word(key, (uint64_t)array->count)) {
            return false;
        }
        for (int i = 0; i < array->count; i++) {
            pin_value item = pin_array_get(array, i);
            if (!key_value(key, &item, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    switch (value->type) {
        case PIN_FLOW: return true;
        case PIN_BOOL: return key_word(key, value->b);
        case PIN_I8: return key_bytes(key, &value->i8, sizeof value->i8);
        case PIN_U8: return key_bytes(key, &value->u8, sizeof value->u8);
        case PIN_I16: return key_bytes(key, &value->i16, sizeof value->i16);
        case PIN_U16: return key_bytes(key, &value->u16, sizeof value->u16);
        case PIN_I32: return key_bytes(key, &value->i, sizeof value->i);
        case PIN_U32: return key_bytes(key, &value->u, sizeof value->u);
        case PIN_I64: return key_bytes(key, &value->i64, sizeof value->i64);
        case PIN_TASK:
        case PIN_HANDLE:
        case PIN_U64: return key_bytes(key, &value->u64, sizeof value->u64);
        case PIN_ENUM: return key_bytes(key, &value->e, sizeof value->e);
        case PIN_F32: return key_bytes(key, &value->f, sizeof value->f);
        case PIN_F64: return key_bytes(key, &value->d, sizeof value->d);
        case PIN_NAME: return key_text(key, value->str);
        case PIN_STR: {
            pin_string text = pin_text(*value);
            return key_word(key, text.length) &&
                   key_bytes(key, text.bytes, text.length);
        }
        case PIN_STRUCT: return key_struct(key, value, depth);
        case PIN_POINTER: return !value->ptr && key_word(key, 0);
        case PIN_UNION: {
            const pin_union *tagged = value->ptr;
            if (!key_word(key, tagged != NULL)) {
                return false;
            }
            if (!tagged) {
                return true;
            }
            if (!key_word(key, tagged->tag) || !key_word(key, (uint64_t)tagged->count)) {
                return false;
            }
            for (int i = 0; i < tagged->count; i++) {
                if (!key_value(key, &tagged->values[i], depth + 1)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}
bool script_catalog_values_key(const script_catalog *catalog, unsigned char **out)
{
    assert(out && !*out);
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        for (int j = 0; j < type->field_count; j++) {
            if (type->fields[j].has_default &&
                !key_value(out, &type->fields[j].default_value, 0)) {
                goto unavailable;
            }
        }
        for (int j = 0; j < type->variant_count; j++) {
            const script_catalog_variant *variant = &type->variants[j];
            if (variant->is_literal && !key_value(out, &variant->literal, 0)) {
                goto unavailable;
            }
            for (int k = 0; k < variant->field_count; k++) {
                if (variant->fields[k].has_default &&
                    !key_value(out, &variant->fields[k].default_value, 0)) {
                    goto unavailable;
                }
            }
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        for (int result = 0; result < 2; result++) {
            const script_catalog_parameter *parameters = result ? c->results
                                                                : c->parameters;
            int count = result ? c->result_count : c->parameter_count;
            for (int j = 0; j < count; j++) {
                if (parameters[j].has_default &&
                    !key_value(out, &parameters[j].default_value, 0)) {
                    goto unavailable;
                }
            }
        }
    }
    for (int i = 0; i < catalog->specialization_count; i++) {
        const script_catalog_specialization *specialization = &catalog->specializations[i];
        if (!specialization->result.is_type &&
            !key_value(out, &specialization->result.value, 0)) {
            goto unavailable;
        }
        for (int j = 0; j < specialization->binding_count; j++) {
            if (!specialization->bindings[j].value.is_type &&
                !key_value(out, &specialization->bindings[j].value.value, 0)) {
                goto unavailable;
            }
        }
    }
    return true;
unavailable:
    afree(*out);
    return false;
}
