#include "runtime/arrays.h"

#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"

#include <string.h>

static pin_value scalar(pin_type type, int value)
{
    return (pin_value){ .type = type, .i = value };
}

static const pin_array *array_arg(const pin_value *inputs, int index)
{
    if (!inputs || inputs[index].container != PIN_CONTAINER_ARRAY ||
        !inputs[index].ptr) {
        return NULL;
    }
    return (const pin_array *)inputs[index].ptr;
}

static pin_value array_result(arena *a, const pin_array *src)
{
    pin_array *copy = pin_array_snapshot_to_arena(a, src);
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
            const type_desc *nested = type_catalog_find(field->type_name);
            if (!nested || nested->size != field->size || !snapshot_type_valid(nested)) {
                return false;
            }
        }
    }
    return true;
}
static bool snapshot_value_valid(const pin_value *value)
{
    if (value->container >= PIN_CONTAINER_ARRAY && value->ptr) {
        const pin_array *array = value->ptr;
        for (int i = 0; i < array->count; i++) {
            if (!snapshot_value_valid(&array->items[i])) {
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
    if (!array || index < 0 || index >= array->count) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_value value;
    if (pin_value_snapshot_to_arena(a, &value, &array->items[index])) {
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
    int found = 0;
    if (array) {
        for (int i = 0; i < array->count; i++) {
            if (pin_value_equal(&array->items[i], &inputs[1])) {
                found = 1;
                break;
            }
        }
    }
    return (pin_value){ .type = PIN_BOOL, .b = found };
}

pin_value native_array_find(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    int found = -1;
    if (array) {
        for (int i = 0; i < array->count; i++) {
            if (pin_value_equal(&array->items[i], &inputs[1])) {
                found = i;
                break;
            }
        }
    }
    return scalar(PIN_I32, found);
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
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count + 1);
    if (!array ||
        pin_array_snapshot_items(
            a,
            array->record,
            array->items,
            source->items,
            source->count
        ) ||
        pin_array_snapshot_items(a, array->record, &array->items[source->count], &inputs[1], 1)) {
        return owned_array(source, NULL);
    }
    return owned_array(source, array);
}

pin_value native_array_add_unique(const pin_value *inputs, int count, arena *a)
{
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    for (int i = 0; i < source->count; i++) {
        if (pin_value_equal(&source->items[i], &inputs[1])) {
            return array_result(a, source);
        }
    }
    return native_array_add(inputs, count, a);
}

pin_value native_array_append(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *left = array_arg(inputs, 0);
    const pin_array *right = array_arg(inputs, 1);
    if (!left || !right || !pin_record_same(left->record, right->record)) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int total = left->count + right->count;
    pin_array *array = pin_array_snapshot_like(a, left, total);
    if (!array ||
        pin_array_snapshot_items(a, array->record, array->items, left->items, left->count) ||
        pin_array_snapshot_items(
            a,
            array->record,
            array->items + left->count,
            right->items,
            right->count
        )) {
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
    if (!snapshot_value_valid(&source->items[index])) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count);
    if (!array ||
        pin_array_snapshot_items(a, array->record, array->items, source->items, index) ||
        pin_array_snapshot_items(a, array->record, &array->items[index], &inputs[2], 1) ||
        pin_array_snapshot_items(
            a,
            array->record,
            array->items + index + 1,
            source->items + index + 1,
            source->count - index - 1
        )) {
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
    if (!array ||
        pin_array_snapshot_items(a, array->record, array->items, source->items, index) ||
        pin_array_snapshot_items(a, array->record, &array->items[index], &inputs[2], 1) ||
        pin_array_snapshot_items(
            a,
            array->record,
            array->items + index + 1,
            index < source->count ? source->items + index : NULL,
            source->count - index
        )) {
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
    if (!snapshot_value_valid(&source->items[index])) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array *array = pin_array_snapshot_like(a, source, source->count - 1);
    if (!array ||
        pin_array_snapshot_items(a, array->record, array->items, source->items, index) ||
        pin_array_snapshot_items(
            a,
            array->record,
            array->items + index,
            source->items + index + 1,
            source->count - index - 1
        )) {
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
            pin_value temp = array->items[i];
            array->items[i] = array->items[array->count - i - 1];
            array->items[array->count - i - 1] = temp;
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
        pin_value temp = array->items[inputs[1].i];
        array->items[inputs[1].i] = array->items[inputs[2].i];
        array->items[inputs[2].i] = temp;
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
    if (source->record && size > source->count) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_array *array = pin_array_snapshot_like(a, source, size);
    if (!array) {
        return (pin_value){ .type = PIN_FLOW };
    }
    int retained = size < source->count ? size : source->count;
    if (pin_array_snapshot_items(a, array->record, array->items, source->items, retained)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    for (int i = retained; i < size; i++) {
        array->items[i] = (pin_value){ .type = source->element_type,
                                       .type_name = array->type_name };
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
    pin_array *array = pin_array_snapshot_like(a, source, size);
    if (!array ||
        pin_array_snapshot_items(a, array->record, array->items, size ? source->items + start : NULL, size)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return owned_array(source, array);
}

static const char *resize_error(const pin_value *inputs)
{
    pin_array source = array_or_empty(inputs, 0);
    return source.record && inputs[1].i > source.count
             ? "Cannot grow an authored record array without initialized rows"
             : NULL;
}

#define CALL(KERNEL, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                                                   \
    static const char *KERNEL##_owned(arena *storage, const pin_value *inputs, int count, pin_value *result)    \
    {                                                                                                           \
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
        pin_value value;                                                                                        \
        const char *error = KERNEL##_owned(storage, inputs, count, &value);                                     \
        if (error) return error;                                                                                \
        /* These kernels already snapshot every item into storage. */                                           \
        if (native_array_##KERNEL == native_array_add ||                                                        \
            native_array_##KERNEL == native_array_append) {                                                     \
            *result = value;                                                                                    \
            return NULL;                                                                                        \
        }                                                                                                       \
        return pin_value_snapshot_to_arena(storage, result, &value)                                             \
                 ? "Cannot retain native value result"                                                          \
                 : NULL;                                                                                        \
    }                                                                                                           \
    const node_host_call NATIVE_ARRAY_##KERNEL##_HOST = { .value = KERNEL##_owned,                              \
                                                          .variadic = VARIADIC };                               \
    const node_host_call NATIVE_ARRAY_##KERNEL##_CALL = { .value = KERNEL##_snapshot,                           \
                                                          .variadic = VARIADIC };
NATIVE_ARRAY_OPERATIONS(CALL)
#undef CALL
const node_access NATIVE_ARRAY_ACCESS = { .flags = NODE_EFFECT_PURE };
