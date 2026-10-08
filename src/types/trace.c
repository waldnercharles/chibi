#include "types/trace.h"

#include "types/catalog.h"

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
        if (field->type == PIN_STR || field->type == PIN_NAME) {
            const char *value;
            memcpy(&value, member, sizeof value);
            mark(user, value);
        }
    }
}
void pin_value_trace(const pin_value *value, arena_marker mark, void *user)
{
    mark(user, value->type_name);
    if (value->container != PIN_CONTAINER_SINGLE || value->type == PIN_STRUCT ||
        value->type == PIN_STR || value->type == PIN_NAME) {
        mark(user, value->ptr);
    }
    if (value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE &&
        value->ptr) {
        const type_desc *type = type_catalog_find(value->type_name);
        assert(type);
        struct_fields(type, value->ptr, mark, user, 0);
    }
}
static void trace_struct(const void *data, const void *shape, arena_marker mark, void *user)
{
    struct_fields(shape, data, mark, user, 0);
}
static void trace_array(const void *data, const void *, arena_marker mark, void *user)
{
    const pin_array *array = data;
    mark(user, array->type_name);
    mark(user, array->items);
    mark(user, array->buckets);
    for (int i = 0; i < array->count; i++) {
        pin_value_trace(&array->items[i], mark, user);
    }
}
void pin_arena_trace_struct(arena *storage, void *data, const type_desc *type)
{
    arena_set_trace(storage, data, trace_struct, type);
}
void pin_arena_trace_array(arena *storage, pin_array *array)
{
    arena_set_trace(storage, array, trace_array, NULL);
}
