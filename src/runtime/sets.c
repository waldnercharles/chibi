#include "runtime/sets.h"

#include "support/arena.h"
#include "types/array.h"

#include <string.h>

static const pin_array *set_value(const pin_value *v)
{
    return v && v->container == PIN_CONTAINER_SET ? v->ptr : NULL;
}

static pin_value set_result(arena *a, const pin_array *source)
{
    pin_array *copy = pin_array_snapshot_to_arena(a, source);
    if (!copy) {
        return (pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_SET };
    }
    pin_array_index(a, copy);
    return (pin_value){ .type = source->element_type,
                        .type_name = copy->type_name,
                        .container = PIN_CONTAINER_SET,
                        .ptr = copy };
}
pin_value native_set_make(const pin_value *inputs, int count, arena *a)
{
    pin_value *unique = arena_alloc(a, (size_t)count * sizeof *unique);
    int length = pin_set_unique(a, unique, inputs, count);
    pin_array source = {
        .element_type = count ? inputs[0].type : PIN_FLOW,
        .type_name = count ? inputs[0].type_name : NULL,
        .count = length,
        .capacity = length,
        .items = unique,
    };
    return set_result(a, &source);
}

pin_value native_set_contains(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *set = set_value(&inputs[0]);
    return (pin_value){ .type = PIN_BOOL, .b = pin_array_find(set, &inputs[1]) >= 0 };
}

pin_value native_set_length(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *set = set_value(&inputs[0]);
    return (pin_value){ .type = PIN_I32, .i = set ? set->count : 0 };
}

pin_value native_set_add(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *set = set_value(&inputs[0]);
    if (native_set_contains(inputs, count, a).b) {
        return set_result(a, set);
    }
    int length = set ? set->count : 0;
    pin_value *items = arena_alloc(a, ((size_t)length + 1) * sizeof *items);
    if (length) {
        memcpy(items, set->items, (size_t)length * sizeof *items);
    }
    items[length] = inputs[1];
    pin_array source = { .element_type = set ? set->element_type : inputs[1].type,
                         .type_name = set ? set->type_name : inputs[1].type_name,
                         .count = length + 1,
                         .capacity = length + 1,
                         .items = items };
    return set_result(a, &source);
}

pin_value native_set_remove(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *set = set_value(&inputs[0]);
    int removed = pin_array_find(set, &inputs[1]);
    int length = set ? set->count : 0;
    int result_count = length - (removed >= 0);
    pin_value *items = arena_alloc(a, (size_t)result_count * sizeof *items);
    if (result_count && !items) {
        return (pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_SET };
    }
    if (removed < 0 && length) {
        memcpy(items, set->items, (size_t)length * sizeof *items);
    } else if (removed >= 0) {
        if (removed) {
            memcpy(items, set->items, (size_t)removed * sizeof *items);
        }
        if (removed + 1 < length) {
            memcpy(
                items + removed,
                set->items + removed + 1,
                (size_t)(length - removed - 1) * sizeof *items
            );
        }
    }
    pin_array source = {
        .element_type = set ? set->element_type : inputs[0].type,
        .type_name = set ? set->type_name : inputs[0].type_name,
        .count = result_count,
        .capacity = result_count,
        .items = items,
    };
    return set_result(a, &source);
}

#define CALL(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                                                   \
    static const char *KERNEL##_owned(arena *storage, const pin_value *inputs, int count, pin_value *result)    \
    {                                                                                                           \
        *result = native_set_##KERNEL(inputs, count, storage);                                                  \
        if (result->type == PIN_FLOW)                                                                           \
            return "data function produced an invalid result";                                                  \
        if (result->type == PIN_STRUCT && !result->ptr)                                                         \
            return "arena allocation failed";                                                                   \
        return NULL;                                                                                            \
    }                                                                                                           \
    static const char *KERNEL##_snapshot(arena *storage, const pin_value *inputs, int count, pin_value *result) \
    {                                                                                                           \
        pin_value value = native_set_##KERNEL(inputs, count, storage);                                          \
        if (value.type == PIN_FLOW ||                                                                           \
            (value.type == PIN_STRUCT &&                                                                        \
             value.container == PIN_CONTAINER_SINGLE && !value.ptr))                                            \
            return "Native value operation failed";                                                             \
        return pin_value_snapshot_to_arena(storage, result, &value)                                             \
                 ? "Cannot retain native value result"                                                          \
                 : NULL;                                                                                        \
    }                                                                                                           \
    const node_host_call NATIVE_SET_##KERNEL##_HOST = { .value = KERNEL##_owned,                                \
                                                        .variadic = VARIADIC };                                 \
    const node_host_call NATIVE_SET_##KERNEL##_CALL = { .value = KERNEL##_snapshot,                             \
                                                        .variadic = VARIADIC };
NATIVE_SET_OPERATIONS(CALL)
#undef CALL
const node_access NATIVE_SET_ACCESS = { .flags = NODE_EFFECT_PURE };
