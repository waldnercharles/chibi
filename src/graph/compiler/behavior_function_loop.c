#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
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
    bool counted = n->desc != &NODE_WhileLoop;
    for (int at = b->loop; at >= 0; at = b->loops[at].parent) {
        if (b->loops[at].node == n) {
            return behavior_lower_unsupported(b, n, -1, false, "recursive loop activation");
        }
    }
    int completed = behavior_lower_successor(b, n, counted ? 2 : 1, t.after);
    if (completed < -1) {
        return -2;
    }
    lowered_loop loop = { .node = n,
                          .parent = b->loop,
                          .completed = completed,
                          .broken = behavior_lower_hidden_local(b, n, 0, PIN_BOOL) };
    if (counted) {
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
        branch(b, condition, body);
        return loop.completed;
    }
    if (t.continuation) {
        int current = local(b, loop.current, PIN_I32),
            last = local(b, loop.last, PIN_I32);
        branch(b, binary(b, BF_EQ, current, last, PIN_BOOL), loop.completed);
        int step = local(b, loop.step, PIN_I32);
        int increment = binary(b, BF_ADD, current, step, PIN_I32);
        behavior_lower_store(b, BF_SAVE, increment, loop.current);
    }
    /* Inclusive ranges run once even when the endpoints are equal. Check the
     * endpoint before stepping, so INT_MIN/INT_MAX never overflow. */
    return body;
}
