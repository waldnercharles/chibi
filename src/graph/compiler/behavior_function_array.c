#include "graph/compiler/behavior_lower_internal.h"
#include "graph/nodes/array_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

/* Views borrow input/asset/instance storage for this synchronous invocation.
 * Array writes, escaping array results and mutating host calls remain excluded:
 * they require owned snapshots before these views can coexist with them. */
array_value *behavior_lower_array(builder *b, int slot)
{
    for (int i = 0; i < acount(b->arrays); i++) {
        if (b->arrays[i].slot == slot) {
            return &b->arrays[i];
        }
    }
    return NULL;
}
int behavior_lower_array_value(builder *b, int pointer, pin_type type)
{
    int slot = acount(b->function->slots);
    apush(b->function->slots, PIN_FLOW);
    apush(b->arrays, ((array_value){ slot, pointer, type }));
    return slot;
}
int behavior_lower_array_source(builder *b, behavior_function_op op, int index, const pin_spec *spec)
{
    array_value *stored = op == BF_LOCAL ? behavior_lower_array(b, index) : NULL;
    int pointer = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = stored ? BF_LOCAL : BF_ARRAY,
            .type = BF_POINTER_TYPE,
            .left = stored ? stored->pointer : index,
            .target = op,
            .value = spec->default_value,
        }
    );
    return behavior_lower_array_value(b, pointer, spec->type);
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
int behavior_lower_array_operation(builder *b, node *n)
{
    if (n->desc != &NODE_ArrayLength && n->desc != &NODE_ArrayLastIndex &&
        n->desc != &NODE_ArrayIsEmpty && n->desc != &NODE_ArrayIsNotEmpty &&
        n->desc != &NODE_ArrayIsValidIndex) {
        return -1;
    }
    int slot = behavior_lower_input(b, n, 0);
    array_value *a = behavior_lower_array(b, slot);
    if (!a) {
        return -1;
    }
    int count = behavior_lower_read(b, a->pointer, offsetof(pin_array, count), PIN_I32, true);
    if (n->desc == &NODE_ArrayLength) {
        return count;
    }
    int zero = constant(b, (pin_value){ .type = PIN_I32 });
    if (n->desc == &NODE_ArrayIsEmpty || n->desc == &NODE_ArrayIsNotEmpty) {
        return binary(b, n->desc == &NODE_ArrayIsEmpty ? BF_EQ : BF_NE, PIN_BOOL, count, zero);
    }
    if (n->desc == &NODE_ArrayLastIndex) {
        return binary(b, BF_SUB, PIN_I32, count, constant(b, (pin_value){ .type = PIN_I32, .i = 1 }));
    }
    int index = behavior_lower_input(b, n, 1);
    if (index < 0) {
        return -1;
    }
    int low = binary(b, BF_GE, PIN_BOOL, index, zero);
    int high = binary(b, BF_LT, PIN_BOOL, index, count);
    return behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = BF_SELECT,
            .type = PIN_BOOL,
            .left = low,
            .right = high,
            .target = constant(b, (pin_value){ .type = PIN_BOOL }),
        }
    );
}
int behavior_lower_array_element(builder *b, int items, int index, const pin_spec *spec)
{
    int wide = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_CAST, .type = BF_POINTER_TYPE, .left = index, .right = index }
    );
    pin_value stride = { .type = BF_POINTER_TYPE };
    if (BF_POINTER_TYPE == PIN_U64) {
        stride.u64 = sizeof(pin_value);
    } else {
        stride.u = sizeof(pin_value);
    }
    int offset = binary(b, BF_MUL, BF_POINTER_TYPE, wide, constant(b, stride));
    int item = binary(b, BF_ADD, BF_POINTER_TYPE, items, offset);
    if (behavior_lower_scalar(spec->type)) {
        return behavior_lower_read(b, item, offsetof(pin_value, d), spec->type, false);
    }
    if (spec->type != PIN_STRUCT) {
        return -1;
    }
    aggregate a = behavior_lower_shape(spec->type_name);
    if (!a.type) {
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
