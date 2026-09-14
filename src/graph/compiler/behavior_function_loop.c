#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

static int constant(builder *b, pin_value value)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT, .type = value.type, .value = value }
    );
}
static int local(builder *b, int index, pin_type type)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_LOCAL, .type = type, .left = index }
    );
}
static int binary(builder *b, behavior_function_op op, int left, int right, pin_type type)
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
static int break_loop(builder *b, task t)
{
    if (b->loop < 0) {
        behavior_lower_emit(
            b,
            (behavior_function_instruction){
                .op = BF_FAIL,
                .left = BEHAVIOR_FUNCTION_STATE,
                .node_id = t.node->id,
                .pin = -1,
                .message = "Break is outside a loop" }
        );
        return -1;
    }
    int flag = constant(b, (pin_value){ .type = PIN_BOOL, .b = true });
    behavior_lower_store(b, BF_SAVE, flag, b->loops[b->loop].broken);
    /* Sequence tails already queued in this body still run before the loop
     * continuation observes the flag. A function frame has its own loop scope. */
    return t.after;
}
static int begin(builder *b, task t)
{
    node *n = t.node;
    bool each = n->desc == &NODE_ForEach || n->desc == &NODE_ForEachWithBreak;
    bool counted = n->desc != &NODE_WhileLoop;
    for (int at = b->loop; at >= 0; at = b->loops[at].parent) {
        if (b->loops[at].node == n) {
            return behavior_lower_unsupported(b, n, -1, false, "recursive loop activation");
        }
    }
    int completed = behavior_lower_successor(
        b,
        n,
        each      ? 3
        : counted ? 2
                  : 1,
        t.after
    );
    if (completed < -1) {
        return -2;
    }
    lowered_loop loop = { .node = n,
                          .parent = b->loop,
                          .completed = completed,
                          .broken = behavior_lower_hidden_local(b, n, 0, PIN_BOOL) };
    if (each) {
        int input = n->desc == &NODE_ForEach ? 1 : 2;
        int source = behavior_lower_input(b, n, input);
        array_value *array = behavior_lower_array(b, source);
        if (!array) {
            return -2;
        }
        loop.entity_snapshot = array->temporary;
        int pointer = array->stable || array->temporary
                        ? array->pointer
                        : behavior_lower_array_copy(b, array->pointer);
        int zero_pointer = constant(b, (pin_value){ .type = BF_POINTER_TYPE });
        int present = binary(b, BF_NE, pointer, zero_pointer, PIN_BOOL);
        behavior_lower_emit(
            b,
            (behavior_function_instruction){
                .op = BF_ASSERT,
                .left = present,
                .target = BEHAVIOR_FUNCTION_DATA,
                .node_id = n->id,
                .pin = -1,
                .message = "ForEach input is not an array",
            }
        );
        int count = behavior_lower_read(b, pointer, offsetof(pin_array, count), PIN_I32, false);
        int zero = constant(b, (pin_value){ .type = PIN_I32 });
        behavior_lower_capture(b, n, false);
        branch(b, binary(b, BF_EQ, count, zero, PIN_BOOL), completed);
        loop.current = behavior_lower_local(b, b->frame, n, 2);
        loop.last = behavior_lower_hidden_local(b, n, 1, PIN_I32);
        loop.items = behavior_lower_hidden_local(b, n, 3, BF_POINTER_TYPE);
        int one = constant(b, (pin_value){ .type = PIN_I32, .i = 1 });
        int last = binary(b, BF_SUB, count, one, PIN_I32);
        int items = behavior_lower_read(b, pointer, offsetof(pin_array, items), BF_POINTER_TYPE, false);
        behavior_lower_store(b, BF_SAVE, items, loop.items);
        behavior_lower_store(b, BF_SAVE, zero, loop.current);
        behavior_lower_store(b, BF_SAVE, last, loop.last);
    } else if (counted) {
        int first_pin = n->desc == &NODE_ForLoop ? 1 : 2;
        int first = behavior_lower_input(b, n, first_pin);
        int last = behavior_lower_input(b, n, first_pin + 1);
        if (first < 0 || last < 0 || b->function->slots[first] != PIN_I32 ||
            b->function->slots[last] != PIN_I32) {
            return -2;
        }
        loop.current = behavior_lower_local(b, b->frame, n, 1);
        loop.last = behavior_lower_hidden_local(b, n, 1, PIN_I32);
        loop.step = behavior_lower_hidden_local(b, n, 2, PIN_I32);
        int ascending = binary(b, BF_LT, first, last, PIN_BOOL);
        int one = constant(b, (pin_value){ .type = PIN_I32, .i = 1 });
        int minus_one = constant(b, (pin_value){ .type = PIN_I32, .i = -1 });
        int step = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_SELECT,
                                             .type = PIN_I32,
                                             .left = ascending,
                                             .right = one,
                                             .target = minus_one }
        );
        behavior_lower_store(b, BF_SAVE, step, loop.step);
        if (!behavior_lower_store(b, BF_SAVE, first, loop.current) ||
            !behavior_lower_store(b, BF_SAVE, last, loop.last)) {
            return -2;
        }
    }
    int clear = constant(b, (pin_value){ .type = PIN_BOOL });
    behavior_lower_store(b, BF_SAVE, clear, loop.broken);
    b->loop = acount(b->loops);
    apush(b->loops, loop);
    int next = behavior_lower_schedule(
        b,
        (task){ .node = n, .after = t.after, .frame = b->frame, .loop = b->loop, .continuation = true }
    );
    b->loops[b->loop].next = next;
    return next < -1 ? -2 : 0;
}
int behavior_lower_loop(builder *b, task t)
{
    node *n = t.node;
    if (n->desc == &NODE_Break || t.input == 1) {
        return break_loop(b, t);
    }
    if (!t.continuation && begin(b, t) < 0) {
        return -2;
    }
    lowered_loop loop = b->loops[b->loop];
    if (t.continuation) {
        behavior_lower_capture(b, n, true);
        branch(b, local(b, loop.broken, PIN_BOOL), loop.completed);
    }
    int body = behavior_lower_successor(b, n, 0, loop.next);
    if (body < -1) {
        return -2;
    }
    if (n->desc == &NODE_WhileLoop) {
        int condition = behavior_lower_input(b, n, 1);
        if (condition < 0 || b->function->slots[condition] != PIN_BOOL) {
            return -2;
        }
        behavior_lower_capture(b, n, t.continuation);
        branch(b, condition, body);
        return loop.completed;
    }
    if (t.continuation) {
        int current = local(b, loop.current, PIN_I32),
            last = local(b, loop.last, PIN_I32);
        branch(b, binary(b, BF_EQ, current, last, PIN_BOOL), loop.completed);
        bool each = n->desc == &NODE_ForEach || n->desc == &NODE_ForEachWithBreak;
        int step = each ? constant(b, (pin_value){ .type = PIN_I32, .i = 1 })
                        : local(b, loop.step, PIN_I32);
        int increment = binary(b, BF_ADD, current, step, PIN_I32);
        behavior_lower_store(b, BF_SAVE, increment, loop.current);
    }
    if (n->desc == &NODE_ForEach || n->desc == &NODE_ForEachWithBreak) {
        int items = local(b, loop.items, BF_POINTER_TYPE);
        int index = local(b, loop.current, PIN_I32);
        int value = behavior_lower_array_element(b, items, index, &n->output_pins[1], loop.entity_snapshot);
        int target = behavior_lower_local(b, b->frame, n, 1);
        if (!behavior_lower_store(b, BF_SAVE, value, target)) {
            return behavior_lower_unsupported(b, n, 1, true, "managed array element");
        }
    }
    /* Inclusive ranges run once even when the endpoints are equal. Check the
     * endpoint before stepping, so INT_MIN/INT_MAX never overflow. */
    return body;
}
