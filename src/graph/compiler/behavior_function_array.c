#include "graph/compiler/behavior_lower_internal.h"
#include "graph/nodes/array_nodes.h"
#include "graph/nodes/set_map_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

/* Reads remain typed loads. Mutable sources are copied at retention boundaries;
 * immutable arguments and generation-owned defaults can share their storage. */
array_value *behavior_lower_array(builder *b, int slot)
{
    for (int i = 0; i < acount(b->arrays); i++) {
        if (b->arrays[i].slot == slot) {
            return &b->arrays[i];
        }
    }
    return NULL;
}
int behavior_lower_array_value(builder *b, int pointer, int name, const pin_spec *spec)
{
    int slot = acount(b->function->slots);
    apush(b->function->slots, PIN_FLOW);
    apush(
        b->arrays,
        ((array_value){ .slot = slot,
                        .pointer = pointer,
                        .name = name,
                        .type = spec->type,
                        .container = spec->container })
    );
    return slot;
}
int behavior_lower_array_source(builder *b, behavior_function_op op, int index, const pin_spec *spec)
{
    array_value *stored = op == BF_LOCAL ? behavior_lower_array(b, index) : NULL;
    int default_index = (op == BF_ARGUMENT || op == BF_CONSTANT) &&
                                spec->default_value.ptr
                          ? behavior_lower_array_default(b, spec->default_value) + 1
                          : 0;
    int pointer = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = stored ? BF_LOCAL : BF_ARRAY,
            .type = BF_POINTER_TYPE,
            .left = stored ? stored->pointer : index,
            .target = op,
            .value = spec->default_value,
            .offset = (size_t)default_index,
        }
    );
    int name = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = stored ? BF_LOCAL : BF_TYPE_NAME,
            .type = BF_POINTER_TYPE,
            .left = stored ? stored->name : index,
            .target = op,
            .value = { .type = spec->type, .str = spec->default_value.type_name } }
    );
    int slot = behavior_lower_array_value(b, pointer, name, spec);
    behavior_lower_array(b, slot)->stable = op != BF_VARIABLE;
    return slot;
}
int behavior_lower_array_default(builder *b, pin_value value)
{
    for (int i = 0; i < acount(b->function->data_values); i++) {
        const pin_value *initial = &b->function->data_values[i].initial;
        if (initial->container == value.container && initial->ptr == value.ptr) {
            return i;
        }
    }
    apush(b->function->data_values, ((behavior_function_data_value){ .initial = value }));
    return acount(b->function->data_values) - 1;
}
int behavior_lower_array_copy(builder *b, int pointer, bool loop)
{
    b->function->managed = true;
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_COPY_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .left = pointer,
                                         .target = loop }
    );
}
int behavior_lower_read(builder *b, int pointer, size_t offset, pin_type type, bool nullable)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = nullable ? BF_READ : BF_LOAD,
                                         .type = type,
                                         .left = pointer,
                                         .offset = offset }
    );
}
static int constant(builder *b, pin_value value)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT, .type = value.type, .value = value }
    );
}
static int binary(builder *b, behavior_function_op op, pin_type type, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op, .type = type, .left = left, .right = right }
    );
}
int behavior_lower_array_get(builder *b, node *n, int pin)
{
    assert(pin == 0);
    int input = behavior_lower_input(b, n, 0);
    array_value *array = behavior_lower_array(b, input);
    if (!array) {
        return -1;
    }
    int pointer = array->pointer;
    int index = behavior_lower_input(b, n, 1);
    if (index < 0) {
        return -1;
    }
    int count = behavior_lower_read(b, pointer, offsetof(pin_array, count), PIN_I32, true);
    int zero = constant(b, (pin_value){ .type = PIN_I32 });
    int low = binary(b, BF_GE, PIN_BOOL, index, zero);
    int high = binary(b, BF_LT, PIN_BOOL, index, count);
    int valid = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = BF_SELECT,
            .type = PIN_BOOL,
            .left = low,
            .right = high,
            .target = constant(b, (pin_value){ .type = PIN_BOOL }) }
    );
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = BF_ASSERT,
            .left = valid,
            .target = BEHAVIOR_FUNCTION_HOST_DATA,
            .node_id = n->id,
            .pin = 0,
            .message = "data function produced an invalid result" }
    );
    int items = behavior_lower_read(b, pointer, offsetof(pin_array, items), BF_POINTER_TYPE, false);
    int value = behavior_lower_array_element(b, items, index, &n->output_pins[0], false);
    aggregate *record = behavior_lower_aggregate(b, value);
    if (record) {
        behavior_lower_emit(
            b,
            (behavior_function_instruction){
                .op = BF_ASSERT,
                .left = record->present,
                .target = BEHAVIOR_FUNCTION_HOST_DATA,
                .node_id = n->id,
                .pin = 0,
                .message = "arena allocation failed" }
        );
    }
    return value;
}
int behavior_lower_array_operation(builder *b, node *n)
{
    if (n->desc != &NODE_ArrayLength && n->desc != &NODE_ArrayLastIndex &&
        n->desc != &NODE_ArrayIsEmpty && n->desc != &NODE_ArrayIsNotEmpty &&
        n->desc != &NODE_ArrayIsValidIndex && n->desc != &NODE_SetLength) {
        return -1;
    }
    int slot = behavior_lower_input(b, n, 0);
    array_value *a = behavior_lower_array(b, slot);
    if (!a) {
        return -1;
    }
    int count = behavior_lower_read(b, a->pointer, offsetof(pin_array, count), PIN_I32, true);
    if (n->desc == &NODE_ArrayLength || n->desc == &NODE_SetLength) {
        return behavior_lower_named_constant(b, count, NULL);
    }
    int zero = constant(b, (pin_value){ .type = PIN_I32 });
    if (n->desc == &NODE_ArrayIsEmpty || n->desc == &NODE_ArrayIsNotEmpty) {
        return behavior_lower_named_constant(
            b,
            binary(b, n->desc == &NODE_ArrayIsEmpty ? BF_EQ : BF_NE, PIN_BOOL, count, zero),
            NULL
        );
    }
    if (n->desc == &NODE_ArrayLastIndex) {
        return behavior_lower_named_constant(
            b,
            binary(b, BF_SUB, PIN_I32, count, constant(b, (pin_value){ .type = PIN_I32, .i = 1 })),
            NULL
        );
    }
    int index = behavior_lower_input(b, n, 1);
    if (index < 0) {
        return -1;
    }
    int low = binary(b, BF_GE, PIN_BOOL, index, zero);
    int high = binary(b, BF_LT, PIN_BOOL, index, count);
    return behavior_lower_named_constant(
        b,
        behavior_lower_value(
            b,
            (behavior_function_instruction){
                .op = BF_SELECT,
                .type = PIN_BOOL,
                .left = low,
                .right = high,
                .target = constant(b, (pin_value){ .type = PIN_BOOL }),
            }
        ),
        NULL
    );
}
int behavior_lower_array_item(builder *b, int items, int index)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_INDEX,
                                         .type = BF_POINTER_TYPE,
                                         .left = items,
                                         .right = index,
                                         .offset = sizeof(pin_value) }
    );
}
int behavior_lower_array_element(builder *b, int items, int index, const pin_spec *spec, bool entity_snapshot)
{
    int item = behavior_lower_array_item(b, items, index);
    if (behavior_lower_scalar(spec->type) || behavior_scalar_string(spec->type)) {
        int value = behavior_lower_read(b, item, offsetof(pin_value, d), spec->type, false);
        if (entity_snapshot) {
            return behavior_lower_named_constant(b, value, "ecs.entity");
        }
        int name = behavior_lower_read(b, item, offsetof(pin_value, type_name), BF_POINTER_TYPE, false);
        return behavior_lower_named_scalar(b, value, name, false);
    }
    if (spec->type != PIN_STRUCT) {
        return -1;
    }
    aggregate a = behavior_lower_shape(spec->type_name);
    if (!a.type) {
        afree(a.fields);
        return -1;
    }
    int pointer = behavior_lower_read(b, item, offsetof(pin_value, ptr), BF_POINTER_TYPE, false);
    int zero = constant(b, (pin_value){ .type = BF_POINTER_TYPE });
    a.present = binary(b, BF_NE, PIN_BOOL, pointer, zero);
    a.name = behavior_lower_read(b, item, offsetof(pin_value, type_name), BF_POINTER_TYPE, false);
    for (int i = 0; i < acount(a.fields); i++) {
        aggregate_field *f = &a.fields[i];
        f->slot = behavior_lower_read(b, pointer, f->offset, f->type, true);
    }
    return behavior_lower_aggregate_value(b, a);
}
