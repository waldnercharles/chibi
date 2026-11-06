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
                        .record = copy->record,
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
    pin_array source = { .element_type = set ? set->element_type : inputs[1].type,
                         .type_name = set ? set->type_name : inputs[1].type_name,
                         .record = set ? set->record : inputs[1].record };
    pin_array *out = pin_array_snapshot_like(a, &source, length + 1);
    if (!out || (length && pin_array_snapshot_copy(a, out, 0, set, 0, length)) ||
        pin_array_snapshot_set(a, out, length, &inputs[1])) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array_index(a, out);
    return (pin_value){ .type = source.element_type,
                        .type_name = out->type_name,
                        .record = out->record,
                        .container = PIN_CONTAINER_SET,
                        .ptr = out };
}

pin_value native_set_remove(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *set = set_value(&inputs[0]);
    int removed = pin_array_find(set, &inputs[1]);
    int length = set ? set->count : 0;
    int result_count = length - (removed >= 0);
    pin_array source = {
        .element_type = set ? set->element_type : inputs[0].type,
        .type_name = set ? set->type_name : inputs[0].type_name,
        .record = set ? set->record : inputs[0].record,
    };
    pin_array *out = pin_array_snapshot_like(a, &source, result_count);
    if (!out ||
        (removed < 0 && length && pin_array_snapshot_copy(a, out, 0, set, 0, length)) ||
        (removed >= 0 &&
         (pin_array_snapshot_copy(a, out, 0, set, 0, removed) ||
          pin_array_snapshot_copy(a, out, removed, set, removed + 1, length - removed - 1)))) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array_index(a, out);
    return (pin_value){ .type = source.element_type,
                        .type_name = out->type_name,
                        .record = out->record,
                        .container = PIN_CONTAINER_SET,
                        .ptr = out };
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
