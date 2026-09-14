#include "graph/compiler/behavior_lower_internal.h"
#include "graph/nodes/state_nodes.h"
#include "support/array.h"

static int context(builder *b, size_t offset, pin_type type)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONTEXT, .type = type, .offset = offset }
    );
}

static void retrigger(builder *b, task t, int duration)
{
    int callback = context(b, offsetof(behavior_function_context, retrigger), BF_POINTER_TYPE);
    int zero = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = BF_POINTER_TYPE,
                                         .value = { .type = BF_POINTER_TYPE } }
    );
    int available = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_NE, .type = PIN_BOOL, .left = callback, .right = zero }
    );
    int ready = behavior_lower_label(b);
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = available, .target = ready }
    );
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = BF_FAIL,
            .left = BEHAVIOR_FUNCTION_STATE,
            .node_id = t.node->id,
            .pin = -1,
            .message = "Retriggerable Delay requires an instance" }
    );
    behavior_lower_bind_label(b, ready);
    int now = context(b, offsetof(behavior_function_context, now), PIN_F64);
    int deadline = behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_ADD, .type = PIN_F64, .left = now, .right = duration }
    );
    int index = 0;
    while (index < acount(b->graph->nodes) && b->graph->nodes[index] != t.node) {
        index++;
    }
    assert(index < acount(b->graph->nodes));
    int found = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_RETRIGGER,
                                         .type = PIN_BOOL,
                                         .left = deadline,
                                         .target = index,
                                         .node_id = t.node->id }
    );
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = found, .target = t.after }
    );
}

int behavior_lower_delay(builder *b, task t)
{
    node *n = t.node;
    int duration = behavior_lower_input(b, n, 1);
    if (duration < 0) {
        return -2;
    }
    if (b->function->slots[duration] != PIN_F64) {
        return behavior_lower_unsupported(b, n, 1, false, "Delay duration is not double");
    }
    b->function->waits = true;
    behavior_lower_capture(b, n, false);
    if (n->desc == &NODE_RetriggerableDelay) {
        retrigger(b, t, duration);
    }
    behavior_lower_emit(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_DELAY, .type = PIN_F64, .left = duration, .node_id = n->id, .pin = 1 }
    );
    return behavior_lower_successor(b, n, 0, t.after);
}
