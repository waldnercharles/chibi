#include "array_nodes.h"

#include "arena.h"
#include "value_array.h"

#include <string.h>

#define ARRAY_INPUT                                                            \
    { "Array",                                                                 \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_DATA,                                                                \
      0,                                                                       \
      PIN_CONTAINER_ARRAY,                                                     \
      { .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY },                  \
      0,                                                                       \
      PIN_FLAG_WILDCARD,                                                       \
      1 }
#define ELEMENT_INPUT                                                          \
    { "Item", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 }
#define ARRAY_OUTPUT                                                           \
    { "Array",                                                                 \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_DATA,                                                                \
      0,                                                                       \
      PIN_CONTAINER_ARRAY,                                                     \
      { .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY },                  \
      0,                                                                       \
      PIN_FLAG_WILDCARD,                                                       \
      1 }
#define ELEMENT_OUTPUT                                                         \
    { "Item", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 }
#define INDEX_INPUT { "Index", PIN_I32, "int", PIN_DATA }
static pin_spec bool_output = { "Return Value", PIN_BOOL, "bool", PIN_DATA };
static pin_spec int_output = { "Return Value", PIN_I32, "int", PIN_DATA };

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
    pin_array *copy = pin_array_clone_to_arena(a, src);
    return (pin_value){
        .type = src ? src->element_type : PIN_FLOW,
        .container = PIN_CONTAINER_ARRAY,
        .type_name = src ? src->type_name : NULL,
        .ptr = copy,
    };
}

static pin_value array_make(const pin_value *inputs, int count, arena *a)
{
    if (count == 0) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_array source = {
        .element_type = inputs[0].type,
        .type_name = inputs[0].type_name,
        .count = count,
        .capacity = count,
        .items = (pin_value *)inputs,
    };
    return array_result(a, &source);
}

static pin_value array_get(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *array = array_arg(inputs, 0);
    int index = inputs[1].i;
    if (!array || index < 0 || index >= array->count) {
        return (pin_value){ .type = PIN_FLOW };
    }
    pin_value value;
    pin_value_copy_to_arena(a, &value, &array->items[index]);
    return value;
}

static pin_value array_length(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    return scalar(PIN_I32, array_arg(inputs, 0) ? array_arg(inputs, 0)->count : 0);
}

static pin_value array_last_index(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return scalar(PIN_I32, array && array->count ? array->count - 1 : -1);
}

static pin_value array_empty(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return (pin_value){ .type = PIN_BOOL, .b = !array || array->count == 0 };
}

static pin_value array_not_empty(const pin_value *inputs, int count, arena *a)
{
    pin_value value = array_empty(inputs, count, a);
    value.b = !value.b;
    return value;
}

static pin_value array_valid_index(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *array = array_arg(inputs, 0);
    return (pin_value){
        .type = PIN_BOOL,
        .b = array && inputs[1].i >= 0 && inputs[1].i < array->count,
    };
}

static pin_value array_contains(const pin_value *inputs, int count, arena *a)
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

static pin_value array_find(const pin_value *inputs, int count, arena *a)
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

static pin_value array_identical(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    return (pin_value){
        .type = PIN_BOOL,
        .b = pin_array_equal(array_arg(inputs, 0), array_arg(inputs, 1)),
    };
}

static pin_value array_add(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_value result = array_result(a, source);
    pin_array *array = (pin_array *)result.ptr;
    if (array->count < array->capacity) {
        array->items[array->count++] = inputs[1];
    }
    return result;
}

static pin_value array_add_unique(const pin_value *inputs, int count, arena *a)
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
    return array_add(inputs, count, a);
}

static pin_value array_append(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *left = array_arg(inputs, 0);
    const pin_array *right = array_arg(inputs, 1);
    if (!left || !right) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int total = left->count + right->count;
    pin_array source = *left;
    source.count = total;
    source.capacity = total;
    source.items = (pin_value *)arena_alloc(a, (size_t)total * sizeof *source.items);
    if (!source.items) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    memcpy(source.items, left->items, (size_t)left->count * sizeof *source.items);
    memcpy(
        source.items + left->count,
        right->items,
        (size_t)right->count * sizeof *source.items
    );
    return array_result(a, &source);
}

static pin_value array_set_elem(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_value result = array_result(a, source);
    pin_array *array = (pin_array *)result.ptr;
    if (inputs[1].i >= 0 && inputs[1].i < array->count) {
        array->items[inputs[1].i] = inputs[2];
    }
    return result;
}

static pin_value array_insert(const pin_value *inputs, int count, arena *a)
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
    pin_array *array = pin_array_clone_to_arena(a, source);
    if (!array) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    if (array->count == array->capacity) {
        size_t size = sizeof *array + (size_t)(array->count + 1) * sizeof *array->items;
        pin_array *grown = (pin_array *)arena_alloc(a, size);
        if (!grown) {
            return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
        }
        *grown = *array;
        grown->capacity = array->count + 1;
        grown->items = (pin_value *)(grown + 1);
        memcpy(grown->items, array->items, (size_t)array->count * sizeof *array->items);
        array = grown;
    }
    memmove(
        array->items + index + 1,
        array->items + index,
        (size_t)(array->count - index) * sizeof *array->items
    );
    array->items[index] = inputs[2];
    array->count++;
    return (pin_value){ .type = array->element_type,
                        .type_name = array->type_name,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array };
}

static pin_value array_remove_index(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    pin_value result = array_result(a, source);
    pin_array *array = (pin_array *)result.ptr;
    if (inputs[1].i >= 0 && inputs[1].i < array->count) {
        memmove(
            array->items + inputs[1].i,
            array->items + inputs[1].i + 1,
            (size_t)(array->count - inputs[1].i - 1) * sizeof *array->items
        );
        array->count--;
    }
    return result;
}

static pin_value array_remove_item(const pin_value *inputs, int count, arena *a)
{
    pin_value index = array_find(inputs, count, a);
    if (index.i < 0) {
        return array_result(a, array_arg(inputs, 0));
    }
    pin_value args[] = { inputs[0], index };
    return array_remove_index(args, 2, a);
}

static pin_value array_clear(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    pin_value result = array_result(a, array_arg(inputs, 0));
    if (result.ptr) {
        ((pin_array *)result.ptr)->count = 0;
    }
    return result;
}

static pin_value array_reverse(const pin_value *inputs, int count, arena *a)
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

static pin_value array_swap(const pin_value *inputs, int count, arena *a)
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

static pin_value array_resize(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *source = array_arg(inputs, 0);
    if (!source) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY };
    }
    int size = inputs[1].i < 0 ? 0 : inputs[1].i;
    pin_array *array = pin_array_new(source->element_type, source->type_name, size);
    array->count = size;
    for (int i = 0; i < size && i < source->count; i++) {
        array->items[i] = source->items[i];
    }
    pin_array *arena_array = pin_array_clone_to_arena(a, array);
    pin_array_free(array);
    return (pin_value){ .type = source->element_type,
                        .type_name = source->type_name,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = arena_array };
}

#define PURE(name, in_count, out_count, in_pins, out_pins, fn)                 \
    node_desc name = { .name = #name,                                          \
                       .input_count = in_count,                                \
                       .output_count = out_count,                              \
                       .inputs = in_pins,                                      \
                       .outputs = out_pins,                                    \
                       .pure_kind = NODE_PURE_CALL,                            \
                       .fn = fn,                                               \
                       .category = "Array",                                    \
                       .keywords = "array collection" }

static pin_spec make_inputs[] = { ELEMENT_INPUT };
static pin_spec make_outputs[] = { ARRAY_OUTPUT };
static pin_spec array_element_inputs[] = { ARRAY_INPUT, INDEX_INPUT };
static pin_spec array_element_outputs[] = { ELEMENT_OUTPUT };
static pin_spec array_only_inputs[] = { ARRAY_INPUT };
static pin_spec array_only_outputs[] = { ARRAY_OUTPUT };
static pin_spec array_bool_inputs[] = { ARRAY_INPUT, ELEMENT_INPUT };
static pin_spec array_bool_index_inputs[] = { ARRAY_INPUT, INDEX_INPUT };
static pin_spec array_array_item_inputs[] = { ARRAY_INPUT, ELEMENT_INPUT };
static pin_spec array_two_inputs[] = { ARRAY_INPUT, ARRAY_INPUT };
static pin_spec array_set_inputs[] = { ARRAY_INPUT, INDEX_INPUT, ELEMENT_INPUT };
static pin_spec array_insert_inputs[] = { ARRAY_INPUT, INDEX_INPUT, ELEMENT_INPUT };
static pin_spec array_swap_inputs[] = { ARRAY_INPUT, INDEX_INPUT, INDEX_INPUT };
static pin_spec array_resize_inputs[] = { ARRAY_INPUT, INDEX_INPUT };

node_desc NODE_MakeArray = { .name = "Make Array",
                             .input_count = 1,
                             .output_count = 1,
                             .inputs = make_inputs,
                             .outputs = make_outputs,
                             .pure_kind = NODE_PURE_CALL,
                             .fn = array_make,
                             .category = "Array",
                             .keywords = "array make collection" };
node_desc NODE_ArrayGet = { .name = "Get",
                            .input_count = 2,
                            .output_count = 1,
                            .inputs = array_element_inputs,
                            .outputs = array_element_outputs,
                            .pure_kind = NODE_PURE_CALL,
                            .fn = array_get,
                            .category = "Array",
                            .keywords = "array get index" };
node_desc NODE_ArrayLength = { .name = "Length",
                               .input_count = 1,
                               .output_count = 1,
                               .inputs = array_only_inputs,
                               .outputs = &int_output,
                               .pure_kind = NODE_PURE_CALL,
                               .fn = array_length,
                               .category = "Array",
                               .keywords = "array length" };
node_desc NODE_ArrayLastIndex = { .name = "Last Index",
                                  .input_count = 1,
                                  .output_count = 1,
                                  .inputs = array_only_inputs,
                                  .outputs = &int_output,
                                  .pure_kind = NODE_PURE_CALL,
                                  .fn = array_last_index,
                                  .category = "Array",
                                  .keywords = "array last index" };
node_desc NODE_ArrayIsEmpty = { .name = "Is Empty",
                                .input_count = 1,
                                .output_count = 1,
                                .inputs = array_only_inputs,
                                .outputs = &bool_output,
                                .pure_kind = NODE_PURE_CALL,
                                .fn = array_empty,
                                .category = "Array",
                                .keywords = "array empty" };
node_desc NODE_ArrayIsNotEmpty = { .name = "Is Not Empty",
                                   .input_count = 1,
                                   .output_count = 1,
                                   .inputs = array_only_inputs,
                                   .outputs = &bool_output,
                                   .pure_kind = NODE_PURE_CALL,
                                   .fn = array_not_empty,
                                   .category = "Array",
                                   .keywords = "array nonempty" };
node_desc NODE_ArrayIsValidIndex = { .name = "Is Valid Index",
                                     .input_count = 2,
                                     .output_count = 1,
                                     .inputs = array_bool_index_inputs,
                                     .outputs = &bool_output,
                                     .pure_kind = NODE_PURE_CALL,
                                     .fn = array_valid_index,
                                     .category = "Array",
                                     .keywords = "array index valid" };
node_desc NODE_ArrayContains = { .name = "Contains",
                                 .input_count = 2,
                                 .output_count = 1,
                                 .inputs = array_bool_inputs,
                                 .outputs = &bool_output,
                                 .pure_kind = NODE_PURE_CALL,
                                 .fn = array_contains,
                                 .category = "Array",
                                 .keywords = "array contains" };
node_desc NODE_ArrayFind = { .name = "Find",
                             .input_count = 2,
                             .output_count = 1,
                             .inputs = array_bool_inputs,
                             .outputs = &int_output,
                             .pure_kind = NODE_PURE_CALL,
                             .fn = array_find,
                             .category = "Array",
                             .keywords = "array find" };
node_desc NODE_ArrayIdentical = { .name = "Identical",
                                  .input_count = 2,
                                  .output_count = 1,
                                  .inputs = array_two_inputs,
                                  .outputs = &bool_output,
                                  .pure_kind = NODE_PURE_CALL,
                                  .fn = array_identical,
                                  .category = "Array",
                                  .keywords = "array identical equality" };
node_desc NODE_ArrayAdd = { .name = "Array Add",
                            .input_count = 2,
                            .output_count = 1,
                            .inputs = array_array_item_inputs,
                            .outputs = array_only_outputs,
                            .pure_kind = NODE_PURE_CALL,
                            .fn = array_add,
                            .category = "Array",
                            .keywords = "array add" };
node_desc NODE_ArrayAddUnique = { .name = "Add Unique",
                                  .input_count = 2,
                                  .output_count = 1,
                                  .inputs = array_array_item_inputs,
                                  .outputs = array_only_outputs,
                                  .pure_kind = NODE_PURE_CALL,
                                  .fn = array_add_unique,
                                  .category = "Array",
                                  .keywords = "array add unique" };
node_desc NODE_ArrayAppend = { .name = "Append",
                               .input_count = 2,
                               .output_count = 1,
                               .inputs = array_two_inputs,
                               .outputs = array_only_outputs,
                               .pure_kind = NODE_PURE_CALL,
                               .fn = array_append,
                               .category = "Array",
                               .keywords = "array append" };
node_desc NODE_ArraySetElem = { .name = "Set Array Elem",
                                .input_count = 3,
                                .output_count = 1,
                                .inputs = array_set_inputs,
                                .outputs = array_only_outputs,
                                .pure_kind = NODE_PURE_CALL,
                                .fn = array_set_elem,
                                .category = "Array",
                                .keywords = "array set element" };
node_desc NODE_ArrayInsert = { .name = "Insert",
                               .input_count = 3,
                               .output_count = 1,
                               .inputs = array_insert_inputs,
                               .outputs = array_only_outputs,
                               .pure_kind = NODE_PURE_CALL,
                               .fn = array_insert,
                               .category = "Array",
                               .keywords = "array insert" };
node_desc NODE_ArrayRemoveIndex = { .name = "Remove Index",
                                    .input_count = 2,
                                    .output_count = 1,
                                    .inputs = array_bool_index_inputs,
                                    .outputs = array_only_outputs,
                                    .pure_kind = NODE_PURE_CALL,
                                    .fn = array_remove_index,
                                    .category = "Array",
                                    .keywords = "array remove index" };
node_desc NODE_ArrayRemoveItem = { .name = "Remove Item",
                                   .input_count = 2,
                                   .output_count = 1,
                                   .inputs = array_bool_inputs,
                                   .outputs = array_only_outputs,
                                   .pure_kind = NODE_PURE_CALL,
                                   .fn = array_remove_item,
                                   .category = "Array",
                                   .keywords = "array remove item" };
node_desc NODE_ArrayClear = { .name = "Clear",
                              .input_count = 1,
                              .output_count = 1,
                              .inputs = array_only_inputs,
                              .outputs = array_only_outputs,
                              .pure_kind = NODE_PURE_CALL,
                              .fn = array_clear,
                              .category = "Array",
                              .keywords = "array clear" };
node_desc NODE_ArrayReverse = { .name = "Reverse",
                                .input_count = 1,
                                .output_count = 1,
                                .inputs = array_only_inputs,
                                .outputs = array_only_outputs,
                                .pure_kind = NODE_PURE_CALL,
                                .fn = array_reverse,
                                .category = "Array",
                                .keywords = "array reverse" };
node_desc NODE_ArraySwap = { .name = "Swap",
                             .input_count = 3,
                             .output_count = 1,
                             .inputs = array_swap_inputs,
                             .outputs = array_only_outputs,
                             .pure_kind = NODE_PURE_CALL,
                             .fn = array_swap,
                             .category = "Array",
                             .keywords = "array swap" };
node_desc NODE_ArrayResize = { .name = "Resize",
                               .input_count = 2,
                               .output_count = 1,
                               .inputs = array_resize_inputs,
                               .outputs = array_only_outputs,
                               .pure_kind = NODE_PURE_CALL,
                               .fn = array_resize,
                               .category = "Array",
                               .keywords = "array resize" };

node_desc *const ARRAY_NODES[] = {
    &NODE_MakeArray,      &NODE_ArrayGet,         &NODE_ArraySetElem,
    &NODE_ArrayAdd,       &NODE_ArrayAddUnique,   &NODE_ArrayAppend,
    &NODE_ArrayClear,     &NODE_ArrayContains,    &NODE_ArrayFind,
    &NODE_ArrayIdentical, &NODE_ArrayLength,      &NODE_ArrayLastIndex,
    &NODE_ArrayResize,    &NODE_ArrayRemoveIndex, &NODE_ArrayRemoveItem,
    &NODE_ArrayIsEmpty,   &NODE_ArrayIsNotEmpty,  &NODE_ArrayIsValidIndex,
    &NODE_ArrayReverse,   &NODE_ArraySwap,
};
const int ARRAY_NODE_COUNT = sizeof ARRAY_NODES / sizeof ARRAY_NODES[0];
