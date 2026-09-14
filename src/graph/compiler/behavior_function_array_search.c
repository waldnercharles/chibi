#include "graph/compiler/behavior_lower_internal.h"
#include "graph/nodes/array_nodes.h"
#include "graph/nodes/set_map_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

static int constant(builder *b, int value)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32, .i = value } }
    );
}
static int binary(builder *b, behavior_function_op op, pin_type type, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op, .type = type, .left = left, .right = right }
    );
}
static void branch(builder *b, int condition, int target)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = target }
    );
}
static void jump(builder *b, int target)
{
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = target });
}
static int local(builder *b, int index)
{
    return behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_LOCAL, .type = b->function->locals[index].initial.type, .left = index }
    );
}
static int text_differs(builder *b, int left, int right)
{
    int equal = binary(b, BF_STRING_EQUAL, PIN_BOOL, left, right);
    int no = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL } }
    );
    return binary(b, BF_EQ, PIN_BOOL, equal, no);
}
static int temporary(builder *b)
{
    /* Every inlined invocation owns its loop state, including repeated calls
     * to the same pure graph within one expression. */
    int index = acount(b->function->locals);
    apush(
        b->function->locals,
        ((behavior_function_local){ .initial = { .type = PIN_I32 },
                                    .frame = b->frame,
                                    .pin = -1 })
    );
    return index;
}
int behavior_lower_array_search(builder *b, node *n, int pin)
{
    if (n->input_pins[1].type == PIN_STRUCT &&
        n->input_pins[1].container == PIN_CONTAINER_SINGLE) {
        return behavior_lower_host_call(b, n, pin);
    }
    pin_type type = n->input_pins[1].type;
    if ((!behavior_lower_scalar(type) && !behavior_scalar_string(type)) ||
        n->input_pins[1].container != PIN_CONTAINER_SINGLE) {
        return behavior_lower_unsupported(b, n, pin, true, "aggregate or nested-container array search");
    }
    int array_slot = behavior_lower_input(b, n, 0);
    array_value *array = behavior_lower_array(b, array_slot);
    if (!array) {
        return -1;
    }
    int pointer = array->pointer;
    int needle = behavior_lower_input(b, n, 1);
    if (needle < 0) {
        return -1;
    }
    scalar_value *scalar = behavior_lower_scalar_value(b, needle);
    int name = scalar ? scalar->name : behavior_lower_static_name(b, NULL);
    int indexed_result = -1, indexed_end = -1;
    if (n->desc == &NODE_SetContains) {
        indexed_result = temporary(b);
        b->function->locals[indexed_result].initial.type = PIN_BOOL;
        indexed_end = behavior_lower_label(b);
        int flat = behavior_lower_label(b);
        int table = behavior_lower_read(b, pointer, offsetof(pin_array, buckets), BF_POINTER_TYPE, true);
        int zero = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = BF_POINTER_TYPE,
                                             .value = { .type = BF_POINTER_TYPE } }
        );
        branch(b, binary(b, BF_EQ, PIN_BOOL, table, zero), flat);
        int found = behavior_lower_host_call(b, n, pin);
        if (found < 0) {
            return -1;
        }
        behavior_lower_store(b, BF_SAVE, found, indexed_result);
        jump(b, indexed_end);
        behavior_lower_bind_label(b, flat);
    }
    int count = behavior_lower_read(b, pointer, offsetof(pin_array, count), PIN_I32, true);
    int items = behavior_lower_read(b, pointer, offsetof(pin_array, items), BF_POINTER_TYPE, true);
    int index_local = temporary(b), found_local = temporary(b);
    behavior_lower_store(b, BF_SAVE, constant(b, 0), index_local);
    behavior_lower_store(b, BF_SAVE, constant(b, -1), found_local);
    int loop = behavior_lower_label(b), next = behavior_lower_label(b),
        end = behavior_lower_label(b);
    behavior_lower_bind_label(b, loop);
    int index = local(b, index_local);
    branch(b, binary(b, BF_GE, PIN_BOOL, index, count), end);
    int item = behavior_lower_array_item(b, items, index);
    int item_type = behavior_lower_read(b, item, offsetof(pin_value, type), PIN_I32, false);
    branch(b, binary(b, BF_NE, PIN_BOOL, item_type, constant(b, type)), next);
    int container = behavior_lower_read(b, item, offsetof(pin_value, container), PIN_I32, false);
    branch(b, binary(b, BF_NE, PIN_BOOL, container, constant(b, PIN_CONTAINER_SINGLE)), next);
    int value = behavior_lower_read(b, item, offsetof(pin_value, d), type, false);
    if (behavior_scalar_string(type)) {
        branch(b, text_differs(b, value, needle), next);
    } else {
        branch(b, binary(b, BF_NE, PIN_BOOL, value, needle), next);
        if (type == PIN_ENUM || type == PIN_HANDLE) {
            int item_name = behavior_lower_read(b, item, offsetof(pin_value, type_name), BF_POINTER_TYPE, false);
            branch(b, text_differs(b, item_name, name), next);
        }
    }
    behavior_lower_store(b, BF_SAVE, index, found_local);
    jump(b, end);
    behavior_lower_bind_label(b, next);
    behavior_lower_store(b, BF_SAVE, binary(b, BF_ADD, PIN_I32, index, constant(b, 1)), index_local);
    jump(b, loop);
    behavior_lower_bind_label(b, end);
    int result = local(b, found_local);
    if (n->desc == &NODE_ArrayContains || n->desc == &NODE_SetContains) {
        result = binary(b, BF_GE, PIN_BOOL, result, constant(b, 0));
    }
    if (indexed_result >= 0) {
        behavior_lower_store(b, BF_SAVE, result, indexed_result);
        behavior_lower_bind_label(b, indexed_end);
        result = local(b, indexed_result);
    }
    return behavior_lower_named_constant(b, result, NULL);
}
