#include "types/trace.h"

#include "types/catalog.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/schema.h"

#include <assert.h>
#include <string.h>

static void struct_fields(const type_desc *type, const void *data, arena_marker mark, void *user, int depth)
{
    assert(type && data && depth < 16);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        const unsigned char *member = (const unsigned char *)data + field->offset;
        if (field->type == PIN_STRUCT) {
            struct_fields(type_catalog_find(field->type_name), member, mark, user, depth + 1);
        }
        if (field->type == PIN_STR || field->type == PIN_NAME || field->type == PIN_UNION ||
            field->type == PIN_POINTER || field->type == PIN_QUERY) {
            const char *value;
            memcpy(&value, member, sizeof value);
            mark(user, value);
            if (field->type == PIN_STR && field->text_descriptor && value) {
                mark(user, ((const pin_string *)value)->bytes);
            }
        }
    }
}
void pin_value_trace(const pin_value *value, arena_marker mark, void *user)
{
    mark(user, value->type_name);
    mark(user, value->record);
    mark(user, value->schema);
    if (value->container == PIN_CONTAINER_SINGLE && value->type == PIN_STR &&
        value->text) {
        mark(user, value->text->bytes);
    }
    if (value->container != PIN_CONTAINER_SINGLE || value->type == PIN_STRUCT ||
        value->type == PIN_STR || value->type == PIN_NAME || value->type == PIN_UNION ||
        value->type == PIN_POINTER || value->type == PIN_QUERY) {
        mark(user, value->ptr);
    }
    if (value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE &&
        value->ptr) {
        const type_desc *type = pin_value_type(value);
        assert(type);
        if (value->schema) {
            pin_schema_trace_fields(value->schema, type, value->ptr, mark, user);
        } else {
            struct_fields(type, value->ptr, mark, user, 0);
        }
    }
}
static void trace_struct(const void *data, const void *shape, arena_marker mark, void *user)
{
    mark(user, shape);
    struct_fields(shape, data, mark, user, 0);
}
static void trace_array(const void *data, const void *, arena_marker mark, void *user)
{
    const pin_array *array = data;
    mark(user, array->type_name);
    mark(user, array->record);
    mark(user, array->schema);
    mark(user, array->items);
    mark(user, array->data);
    mark(user, array->buckets);
    /* Only authored records hold pointers, and their layouts travel with the
     * array; native layouts may leave the catalog while a payload holds them. */
    bool managed = !array->stride || array->element_type == PIN_POINTER ||
                   array->element_type == PIN_QUERY ||
                   (array->element_type == PIN_STRUCT && array->record &&
                    !type_is_plain(&array->record->type));
    for (int i = 0; managed && i < array->count; i++) {
        pin_value item = pin_array_get(array, i);
        pin_value_trace(&item, mark, user);
    }
}
static void trace_union(const void *data, const void *, arena_marker mark, void *user)
{
    const pin_union *value = data;
    for (int i = 0; i < value->count; i++) {
        pin_value_trace(&value->values[i], mark, user);
    }
}
static void trace_pointer(const void *data, const void *, arena_marker mark, void *user)
{
    const pin_pointer *pointer = data;
    mark(user, pointer->base);
    mark(user, pointer->path);
    pin_value_trace(&pointer->value, mark, user);
}
void pin_arena_trace_pointer(arena *storage, pin_pointer *value)
{
    arena_set_trace(storage, value, trace_pointer, NULL);
}
void pin_arena_trace_union(arena *storage, pin_union *value)
{
    arena_set_trace(storage, value, trace_union, NULL);
}
void pin_arena_trace_struct(arena *storage, void *data, const type_desc *type)
{
    arena_set_trace(storage, data, trace_struct, type);
}
void pin_arena_trace_array(arena *storage, pin_array *array)
{
    arena_set_trace(storage, array, trace_array, NULL);
}

static bool boundary_scalar_width(const type_field *field)
{
    if (field->type == PIN_ENUM) {
        return field->size > 0 && field->size <= sizeof(int32_t);
    }
    return field->type >= PIN_BOOL && field->type <= PIN_ENUM &&
           field->type != PIN_STRUCT &&
           field->size == behavior_scalar_size(field->type);
}

/* Validate physical access before the fallback reads any aggregate field.
 * This permits dynamic pointer/union fields but never trusts their offsets. */
static bool boundary_layout_valid(const type_desc *type, int depth, type_resolver resolver)
{
    if (!type || !type->size || type->field_count < 0 ||
        (type->field_count && !type->fields) || depth >= 64) {
        return false;
    }
    if (type->alias_of) {
        const type_desc *nested = type_resolve(resolver, type->alias_of);
        return nested && nested->size == type->size &&
               boundary_layout_valid(nested, depth + 1, resolver);
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_resolve(resolver, field->type_name);
            if (!nested || nested->size != field->size ||
                !boundary_layout_valid(nested, depth + 1, resolver)) {
                return false;
            }
        } else if (field->type == PIN_UNION || field->type == PIN_POINTER || field->type == PIN_QUERY) {
            if (field->size != sizeof(void *)) {
                return false;
            }
        } else if (!boundary_scalar_width(field)) {
            return false;
        }
    }
    return true;
}

/* A complete physical layout proves that every array element is free of
 * managed pointers. Union fields remain dynamic and require a payload walk. */
static bool boundary_layout_pointer_free(const type_desc *type, int depth, type_resolver resolver)
{
    if (!type || !type->size || type->field_count <= 0 || !type->fields || depth >= 64) {
        return false;
    }
    if (type->alias_of) {
        const type_desc *nested = type_resolve(resolver, type->alias_of);
        return nested && nested->size == type->size &&
               boundary_layout_pointer_free(nested, depth + 1, resolver);
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_resolve(resolver, field->type_name);
            if (!nested || nested->size != field->size ||
                !boundary_layout_pointer_free(nested, depth + 1, resolver)) {
                return false;
            }
        } else if (field->type == PIN_POINTER || field->type == PIN_QUERY || field->type == PIN_UNION || !boundary_scalar_width(field)) {
            return false;
        }
    }
    return true;
}
static bool boundary_array_pointer_free(const pin_array *array, int depth)
{
    if (!array || array->element_type != PIN_STRUCT || !pin_array_valid(array) ||
        (array->record && !pin_record_current(array->record))) {
        return false;
    }
    pin_value shape = { .type = PIN_STRUCT,
                        .type_name = array->type_name,
                        .record = array->record,
                        .schema = array->schema };
    const type_desc *type = pin_value_type(&shape);
    return type && (!array->stride || type->size == array->stride) &&
           boundary_layout_pointer_free(
               type,
               depth,
               array->schema ? pin_schema_resolver(array->schema) : (type_resolver){ 0 }
           );
}
static bool boundary_array_layout_same(const pin_array *array, const pin_value *item)
{
    if (!array->schema && !item->schema) {
        return true;
    }
    pin_value shape = { .type = PIN_STRUCT,
                        .type_name = array->type_name,
                        .record = array->record,
                        .schema = array->schema };
    const type_desc *expected = pin_value_type(&shape),
                    *actual = pin_value_type(item);
    uint64_t left = array->schema ? pin_schema_layout(array->schema, array->type_name)
                                  : type_layout_hash(expected);
    uint64_t right = item->schema ? pin_schema_layout(item->schema, item->type_name)
                                  : type_layout_hash(actual);
    return expected && actual && expected->size == actual->size && left && left == right;
}

/* Examine values before publication, without following managed pointees.
 * Ordinary value graphs have finite depth; pointer cycles stop at their first
 * pointer. No native callback has run when this rejects a value. */
static const char *managed_boundary_error(const pin_value *value, int depth)
{
    if (value->type == PIN_POINTER || value->type == PIN_QUERY) {
        return value->type == PIN_QUERY
                 ? "Native calls cannot retain query arguments"
                 : "Native calls cannot retain managed pointer arguments";
    }
    if (depth >= 64) {
        return "Cannot retain language value";
    }
    if (value->container == PIN_CONTAINER_MAP) {
        const pin_map *map = value->ptr;
        for (int i = 0; map && i < map->count; i++) {
            const char *error = managed_boundary_error(&map->entries[i].key, depth + 1);
            if (!error) {
                error = managed_boundary_error(&map->entries[i].value, depth + 1);
            }
            if (error) {
                return error;
            }
        }
        return NULL;
    }
    if (value->container != PIN_CONTAINER_SINGLE) {
        const pin_array *array = value->ptr;
        if (array && !pin_array_valid(array)) {
            return "Cannot retain language value";
        }
        bool pointer_free = boundary_array_pointer_free(array, depth);
        if (pointer_free && array->stride) {
            return NULL;
        }
        if (array && array->stride && array->element_type != PIN_STRUCT &&
            array->element_type != PIN_UNION && array->element_type != PIN_POINTER &&
            array->element_type != PIN_QUERY) {
            return NULL;
        }
        for (int i = 0; array && i < array->count; i++) {
            pin_value item = pin_array_get(array, i);
            /* Boxed arrays can contain mismatched dynamic values. The schema
             * proof applies only to elements with the exact declared header. */
            if (pointer_free && item.type == PIN_STRUCT &&
                item.container == PIN_CONTAINER_SINGLE && item.type_name &&
                array->type_name && !strcmp(item.type_name, array->type_name) &&
                pin_record_same(item.record, array->record) &&
                boundary_array_layout_same(array, &item)) {
                continue;
            }
            const char *error = managed_boundary_error(&item, depth + 1);
            if (error) {
                return error;
            }
        }
        return NULL;
    }
    if (value->type == PIN_UNION) {
        const pin_union *variant = value->ptr;
        for (int i = 0; variant && i < variant->count; i++) {
            const char *error = managed_boundary_error(&variant->values[i], depth + 1);
            if (error) {
                return error;
            }
        }
    }
    if (value->type == PIN_STRUCT && value->ptr) {
        const type_desc *type = pin_value_type(value);
        type_resolver resolver = value->schema ? pin_schema_resolver(value->schema)
                                               : (type_resolver){ 0 };
        if (!boundary_layout_valid(type, depth, resolver)) {
            return "Cannot retain language value";
        }
        /* Validation already proves this alias chain finite and size-matched. */
        while (type->alias_of) {
            type = type_resolve(resolver, type->alias_of);
        }
        for (int i = 0; i < type->field_count; i++) {
            const type_field *field = &type->fields[i];
            if (field->type != PIN_STRUCT && field->type != PIN_UNION &&
                field->type != PIN_POINTER && field->type != PIN_QUERY) {
                continue;
            }
            const char *member = (const char *)value->ptr + field->offset;
            pin_value item = { .type = field->type,
                               .type_name = field->type_name,
                               .schema = field->type == PIN_STRUCT ? value->schema
                                                                   : NULL };
            if (field->type == PIN_STRUCT) {
                item.ptr = (void *)member;
            } else {
                memcpy(&item.ptr, member, sizeof item.ptr);
            }
            const char *error = managed_boundary_error(&item, depth + 1);
            if (error) {
                return error;
            }
        }
    }
    return NULL;
}
const char *pin_value_managed_boundary_error(const pin_value *value)
{
    return managed_boundary_error(value, 0);
}
bool pin_value_has_managed_pointer(const pin_value *value)
{
    return pin_value_managed_boundary_error(value) != NULL;
}
