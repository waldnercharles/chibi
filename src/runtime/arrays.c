#include "runtime/arrays.h"

#include "types/array.h"
#include "types/array_internal.h"
#include "types/map.h"
#include "types/record.h"

#include <string.h>

static pin_value scalar(pin_type type, int value)
{
    return (pin_value){ .type = type, .i = value };
}

static const pin_array *array_arg(const pin_value *inputs, int index)
{
    if (!inputs ||
        (inputs[index].container != PIN_CONTAINER_ARRAY &&
         inputs[index].container != PIN_CONTAINER_VIEW) ||
        !inputs[index].ptr) {
        return NULL;
    }
    return (const pin_array *)inputs[index].ptr;
}

static pin_value array_result(arena *a, const pin_array *src)
{
    pin_array *copy = pin_array_outer_clone(a, src);
    return (pin_value){
        .type = src ? src->element_type : PIN_FLOW,
        .container = PIN_CONTAINER_ARRAY,
        .type_name = copy ? copy->type_name : NULL,
        .record = copy ? copy->record : NULL,
        .ptr = copy,
    };
}

static pin_value owned_array(const pin_array *source, pin_array *array)
{
    return (pin_value){ .type = source->element_type,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = array ? array->type_name : NULL,
                        .record = array ? array->record : NULL,
                        .ptr = array };
}
/* Discarded values still need valid snapshot layouts, but their payloads need
 * no allocation. Retained values are validated by their actual deep copy. */
static bool snapshot_type_valid(const type_desc *type)
{
    if (!type || !type->size) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_field_type(field, (type_resolver){ 0 });
            if (!nested || nested->size != field->size || !snapshot_type_valid(nested)) {
                return false;
            }
        }
    }
    return true;
}
static bool snapshot_value_valid(const pin_value *value)
{
    if (value->container == PIN_CONTAINER_MAP && value->ptr) {
        const pin_map *map = value->ptr;
        for (int i = 0; i < map->count; i++) {
            if (!snapshot_value_valid(&map->entries[i].key) ||
                !snapshot_value_valid(&map->entries[i].value)) {
                return false;
            }
        }
        return true;
    }
    if (value->container >= PIN_CONTAINER_ARRAY && value->ptr) {
        const pin_array *array = value->ptr;
        if (pin_array_owner(array)->storage) {
            return true;
        }
        if (!pin_array_view_valid(array)) {
            return false;
        }
        for (int i = 0; i < array->count; i++) {
            pin_value item = pin_array_get(array, i);
            if (!snapshot_value_valid(&item)) {
                return false;
            }
        }
        return true;
    }
    return value->type != PIN_STRUCT || !value->ptr ||
           snapshot_type_valid(pin_value_type(value));
}

pin_value native_array_make(const pin_value *inputs, int count, arena *a)
{
    if (count == 0) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_array source = {
        .element_type = inputs[0].type,
        .type_name = inputs[0].type_name,
        .record = inputs[0].record,
        .count = count,
        .capacity = count,
        .items = (pin_value *)inputs,
    };
    return array_result(a, &source);
}

pin_value native_array_get(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *array = array_arg(inputs, 0);
    int index = inputs[1].i;
    if (!array || !pin_array_valid(array) || index < 0 || index >= array->count) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_value value, item = pin_array_get(array, index);
    if (pin_value_collection_copy(a, &value, &item)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return value;
}

pin_value native_array_length(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return scalar(PIN_I32, array ? array->count : 0);
}

pin_value native_array_last_index(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return scalar(PIN_I32, array && array->count ? array->count - 1 : -1);
}

pin_value native_array_empty(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return (pin_value){ .type = PIN_BOOL, .b = !array || array->count == 0 };
}

pin_value native_array_not_empty(const pin_value *inputs, int count, arena *a)
{
    pin_value value = native_array_empty(inputs, count, a);
    value.b = !value.b;
    return value;
}

pin_value native_array_valid_index(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return (pin_value){
        .type = PIN_BOOL,
        .b = array && inputs[1].i >= 0 && inputs[1].i < array->count,
    };
}

pin_value native_array_contains(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return (pin_value){ .type = PIN_BOOL, .b = pin_array_find(array, &inputs[1]) >= 0 };
}

pin_value native_array_find(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return scalar(PIN_I32, pin_array_find(array, &inputs[1]));
}

pin_value native_array_identical(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    return (pin_value){
        .type = PIN_BOOL,
        .b = pin_array_equal(array_arg(inputs, 0), array_arg(inputs, 1)),
    };
}

pin_value native_array_add(const pin_value *inputs, int count, arena *a)
{
    return pin_array_add(inputs, count, a);
}

pin_value native_array_add_unique(const pin_value *inputs, int count, arena *a)
{
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    if (pin_array_find(source, &inputs[1]) >= 0) {
        return array_result(a, source);
    }
    return native_array_add(inputs, count, a);
}

static int set_item_value(arena *storage, pin_array *out, int index, const pin_value *value)
{
    return pin_array_snapshot_set(storage, out, index, value);
}

static int copy_items(arena *storage, pin_array *out, int to, const pin_array *source, int from, int count)
{
    return pin_array_snapshot_copy(storage, out, to, source, from, count);
}

pin_value native_array_extend(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *left = array_arg(inputs, 0);
    const pin_array *right = array_arg(inputs, 1);
    if (!left || !right || !pin_value_layout_same(&inputs[0], &inputs[1])) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int total = left->count + right->count;
    pin_array *array = pin_array_snapshot_like(a, left, total);
    if (!array || copy_items(a, array, 0, left, 0, left->count) ||
        copy_items(a, array, left->count, right, 0, right->count)) {
        return owned_array(left, NULL);
    }
    return owned_array(left, array);
}

pin_value native_array_set_elem(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int index = inputs[1].i;
    if (index < 0 || index >= source->count) {
        return array_result(a, source);
    }
    pin_value previous = pin_array_get(source, index);
    if (!snapshot_value_valid(&previous)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count);
    if (!array || copy_items(a, array, 0, source, 0, index) ||
        set_item_value(a, array, index, &inputs[2]) ||
        copy_items(a, array, index + 1, source, index + 1, source->count - index - 1)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return owned_array(source, array);
}

pin_value native_array_insert(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int index = inputs[1].i;
    if (index < 0) {
        index = 0;
    }
    if (index > source->count) {
        index = source->count;
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count + 1);
    if (!array || copy_items(a, array, 0, source, 0, index) ||
        set_item_value(a, array, index, &inputs[2]) ||
        copy_items(a, array, index + 1, source, index, source->count - index)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return owned_array(source, array);
}

pin_value native_array_remove_index(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int index = inputs[1].i;
    if (index < 0 || index >= source->count) {
        return array_result(a, source);
    }
    pin_value previous = pin_array_get(source, index);
    if (!snapshot_value_valid(&previous)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count - 1);
    if (!array || copy_items(a, array, 0, source, 0, index) ||
        copy_items(a, array, index, source, index + 1, source->count - index - 1)) {
        return owned_array(source, NULL);
    }
    return owned_array(source, array);
}

pin_value native_array_remove_item(const pin_value *inputs, int count, arena *a)
{
    pin_value index = native_array_find(inputs, count, a);
    if (index.i < 0) {
        return array_result(a, array_arg(inputs, 0));
    }
    pin_value args[] = { inputs[0], index };
    return native_array_remove_index(args, 2, a);
}

pin_value native_array_clear(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_array empty = { .element_type = source->element_type,
                        .type_name = source->type_name,
                        .record = source->record };
    return array_result(a, &empty);
}

pin_value native_array_reverse(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    pin_value result = array_result(a, array_arg(inputs, 0));
    pin_array *array = (pin_array *)result.ptr;
    if (array) {
        for (int i = 0; i < array->count / 2; i++) {
            pin_array_swap(array, i, array->count - i - 1);
        }
    }
    return result;
}

pin_value native_array_swap(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    pin_value result = array_result(a, array_arg(inputs, 0));
    pin_array *array = (pin_array *)result.ptr;
    if (array && inputs[1].i >= 0 && inputs[2].i >= 0 &&
        inputs[1].i < array->count && inputs[2].i < array->count) {
        pin_array_swap(array, inputs[1].i, inputs[2].i);
    }
    return result;
}

/* An array input's items; an empty array may hold none, and keeps the
   input's element type. */
static pin_array array_or_empty(const pin_value *inputs, int index)
{
    const pin_array *array = array_arg(inputs, index);
    return array ? *array
                 : (pin_array){ .element_type = inputs[index].type,
                                .type_name = inputs[index].type_name,
                                .record = inputs[index].record };
}

pin_value native_array_resize(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    pin_array held = array_or_empty(inputs, 0);
    const pin_array *source = &held;
    if (source->element_type == PIN_FLOW) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int size = inputs[1].i < 0 ? 0 : inputs[1].i;
    if (source->record && !source->record->native && size > source->count) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array
        *array = source->element_type == PIN_STRUCT && !source->record &&
                         size > source->count
                   ? pin_array_snapshot_allocate(a, source->element_type, source->type_name, size)
                   : pin_array_snapshot_like(a, source, size);
    if (!array) {
        return (pin_value){ .type = PIN_FLOW };
    }
    int retained = size < source->count ? size : source->count;
    if (copy_items(a, array, 0, source, 0, retained)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    for (int i = retained; i < size; i++) {
        if (array->stride && array->record && array->record->native) {
            continue;
        }
        pin_value value = { .type = source->element_type,
                            .type_name = array->type_name,
                            .record = array->record };
        if (pin_array_set(array, i, &value)) {
            return (pin_value){ .type = PIN_FLOW };
        }
    }
    return owned_array(source, array);
}

/* The Count elements from Start, clamped to the array, as JavaScript's
   slice(start, start + count). */
pin_value native_array_slice(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    pin_array held = array_or_empty(inputs, 0);
    const pin_array *source = &held;
    if (source->element_type == PIN_FLOW) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int start = inputs[1].i < 0             ? 0
              : inputs[1].i > source->count ? source->count
                                            : inputs[1].i;
    int size = inputs[2].i < 0 ? 0 : inputs[2].i;
    size = size > source->count - start ? source->count - start : size;
    pin_array *array = pin_array_window_copy(a, source, start, size);
    if (!array) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return owned_array(source, array);
}

static const char *resize_error(const pin_value *inputs)
{
    pin_array source = array_or_empty(inputs, 0);
    return source.record && !source.record->native && inputs[1].i > source.count
             ? "Cannot grow an authored record array without initialized rows"
             : NULL;
}

static bool get_result_matches(pin_value expected, pin_value actual)
{
    if (expected.type == PIN_FLOW) {
        return true; /* Unresolved generic C caller. */
    }
    if (expected.type != actual.type || expected.container != actual.container) {
        return false;
    }
    if (expected.type != PIN_STRUCT && expected.type != PIN_UNION &&
        expected.type != PIN_POINTER) {
        return true;
    }
    if (expected.record || actual.record) {
        return pin_value_layout_same(&expected, &actual);
    }
    if (!expected.type_name || !actual.type_name) {
        return expected.type == PIN_UNION && !expected.type_name && !actual.type_name;
    }
    return !strcmp(expected.type_name, actual.type_name);
}
static const char *select_owned(arena *storage, const pin_value *inputs, int count, pin_value *result)
{
    if (count != 2) {
        return "Array selection requires an array and index";
    }
    const pin_array *array = array_arg(inputs, 0);
    int index = inputs[1].i;
    if (!array || !pin_array_valid(array) || index < 0 || index >= array->count) {
        return "Array index out of range";
    }
    pin_value item = pin_array_get(array, index);
    if (!get_result_matches(*result, item)) {
        return "Array element does not match result type";
    }
    return pin_value_collection_copy(storage, result, &item)
             ? "Cannot retain array element"
             : NULL;
}

#define CALL(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                                                   \
    static const char *KERNEL##_owned(arena *storage, const pin_value *inputs, int count, pin_value *result)    \
    {                                                                                                           \
        if (native_array_##KERNEL == native_array_get)                                                          \
            return select_owned(storage, inputs, count, result);                                                \
        if (native_array_##KERNEL == native_array_resize) {                                                     \
            const char *error = resize_error(inputs);                                                           \
            if (error) return error;                                                                            \
        }                                                                                                       \
        pin_value value = native_array_##KERNEL(inputs, count, storage);                                        \
        if (value.type == PIN_FLOW)                                                                             \
            return "data function produced an invalid result";                                                  \
        if ((value.container >= PIN_CONTAINER_ARRAY || value.type == PIN_STRUCT) &&                             \
            !value.ptr)                                                                                         \
            return "arena allocation failed";                                                                   \
        *result = value;                                                                                        \
        return NULL;                                                                                            \
    }                                                                                                           \
    static const char *KERNEL##_snapshot(arena *storage, const pin_value *inputs, int count, pin_value *result) \
    {                                                                                                           \
        pin_value value = *result;                                                                              \
        const char *error = KERNEL##_owned(storage, inputs, count, &value);                                     \
        if (error) return error;                                                                                \
        /* These kernels already snapshot every item into storage. */                                           \
        if (native_array_##KERNEL == native_array_extend) {                                                     \
            *result = value;                                                                                    \
            return NULL;                                                                                        \
        }                                                                                                       \
        return pin_value_snapshot_to_arena(storage, result, &value)                                             \
                 ? "Cannot retain native value result"                                                          \
                 : NULL;                                                                                        \
    }                                                                                                           \
    const node_host_call NATIVE_ARRAY_##KERNEL##_HOST = { .value = KERNEL##_owned,                              \
                                                          .variadic = VARIADIC,                                 \
                                                          .language_values = true,                              \
                                                          .owned_result = true };                               \
    const node_host_call NATIVE_ARRAY_##KERNEL##_CALL = { .value = KERNEL##_snapshot,                           \
                                                          .variadic = VARIADIC,                                 \
                                                          .language_values = true,                              \
                                                          .owned_result = true,                                 \
                                                          .borrows = true };
#define ADD(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                    \
    const node_host_call NATIVE_ARRAY_add_HOST = { .value = pin_array_add_call, \
                                                   .language_values = true,     \
                                                   .owned_result = true };      \
    const node_host_call NATIVE_ARRAY_add_CALL = { .value = pin_array_add_call, \
                                                   .language_values = true,     \
                                                   .owned_result = true,        \
                                                   .borrows = true };
NATIVE_ARRAY_OPERATIONS_WITH_ADD(CALL, ADD)
#undef ADD
#undef CALL
/* Argument-only value operations: no service, world or invocation state.
 * Results have independent logical owners; explicit pointer targets keep their
 * identity and are never dereferenced here. Reading an argument's live view
 * observes its backing storage, which the compiler records at the call site.
 * language_values/owned_result describe the value boundary, not these effects. */
const node_access NATIVE_ARRAY_ACCESS = { .flags = NODE_EFFECT_PURE };
