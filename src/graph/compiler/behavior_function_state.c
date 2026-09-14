#include "graph/compiler/behavior_lower_internal.h"
#include "graph/compiler/behavior_program.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "support/array.h"

static int constant(builder *b, pin_type type, int value)
{
    pin_value v = { .type = type };
    if (type == PIN_BOOL) {
        v.b = value != 0;
    } else {
        v.i = value;
    }
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT, .type = type, .value = v }
    );
}
static int binary(builder *b, behavior_function_op op, pin_type type, int left, int right)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = op, .type = type, .left = left, .right = right }
    );
}
static int invert(builder *b, int value)
{
    return binary(b, BF_EQ, PIN_BOOL, value, constant(b, PIN_BOOL, 0));
}
static void branch(builder *b, int condition, int target)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = target }
    );
}
static void write(builder *b, int bank, int index, size_t offset, int value)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = b->function->slots[value],
                                         .left = value,
                                         .right = bank,
                                         .offset = (size_t)index * sizeof(pin_value) + offset }
    );
}
static int read(builder *b, int bank, int index, size_t offset, pin_type type)
{
    return behavior_lower_read(b, bank, (size_t)index * sizeof(pin_value) + offset, type, false);
}
static void set(builder *b, int bank, int index, pin_type type, int value)
{
    write(b, bank, index, offsetof(pin_value, type), constant(b, PIN_I32, type));
    write(b, bank, index, offsetof(pin_value, d), value);
}
static bool output_value(builder *b, node *n, int pin, int value, const char *name)
{
    int local = behavior_lower_local(b, b->frame, n, pin);
    return local >= 0 &&
           behavior_lower_store(b, BF_SAVE, behavior_lower_named_constant(b, value, name), local);
}
/* Unevaluated inputs still expose their last per-invocation value to watches.
 * Keeping that snapshot must not cause the input expression to execute. */
static int input_local(builder *b, node *n, int pin)
{
    int local = behavior_lower_hidden_local(b, n, pin, n->input_pins[pin].type);
    b->function->locals[local].initial = n->input_pins[pin].default_value;
    return local;
}
static void capture_input(builder *b, node *n, int pin)
{
    if (!b->recording) {
        return;
    }
    int local = input_local(b, n, pin);
    int value = behavior_lower_source(b, BF_LOCAL, local, &n->input_pins[pin], n, pin);
    behavior_lower_watch(b, n, pin, false, value);
}
static void remember_input(builder *b, node *n, int pin, int value)
{
    if (b->recording) {
        bool saved = behavior_lower_store(b, BF_SAVE, value, input_local(b, n, pin));
        assert(saved);
    }
}
bool behavior_lower_state_inputs(builder *b, node *n)
{
    for (int pin = 0; pin < n->input_count; pin++) {
        if (n->input_pins[pin].kind != PIN_DATA) {
            continue;
        }
        int value = behavior_lower_input(b, n, pin);
        if (value < 0) {
            return false;
        }
        remember_input(b, n, pin, value);
    }
    return true;
}

bool behavior_lower_is_state(const node_desc *desc)
{
    return desc == &NODE_DoOnce || desc == &NODE_DoN || desc == &NODE_Gate ||
           desc == &NODE_FlipFlop || desc == &NODE_MultiGate;
}
static int flag(builder *b, int condition, int bit)
{
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_SELECT,
                                         .type = PIN_I32,
                                         .left = condition,
                                         .right = constant(b, PIN_I32, bit),
                                         .target = constant(b, PIN_I32, 0) }
    );
}
static int multi_gate(builder *b, task t, int index)
{
    node *n = t.node;
    int start = constant(b, PIN_I32, 0),
        flags = constant(b, PIN_I32, BEHAVIOR_STATE_RESET);
    if (t.input != 1) {
        int values[5] = { 0 };
        /* The language evaluates Start Index, Random and Loop in that order. */
        for (int pin = 4; pin >= 2; pin--) {
            values[pin] = behavior_lower_input(b, n, pin);
            if (values[pin] < 0) {
                return -2;
            }
            if (b->function->slots[values[pin]] != (pin == 4 ? PIN_I32 : PIN_BOOL)) {
                return behavior_lower_unsupported(b, n, pin, false, "MultiGate input type");
            }
            remember_input(b, n, pin, values[pin]);
        }
        start = values[4];
        flags = binary(
            b,
            BF_ADD,
            PIN_I32,
            flag(b, values[2], BEHAVIOR_STATE_LOOP),
            flag(b, values[3], BEHAVIOR_STATE_RANDOM)
        );
    }
    int selection = acount(b->function->state_selections);
    apush(b->function->state_selections, ((behavior_state_selection){ index, n->output_count }));
    int selected = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_STATE_SELECT,
                                         .type = PIN_I32,
                                         .left = start,
                                         .right = flags,
                                         .target = selection }
    );
    for (int pin = 2; pin < 5; pin++) {
        capture_input(b, n, pin);
    }
    if (t.input == 1) {
        return t.after;
    }
    for (int output = 0; output < n->output_count; output++) {
        int next = behavior_lower_successor(b, n, output, t.after);
        if (next < -1) {
            return -2;
        }
        branch(b, binary(b, BF_EQ, PIN_BOOL, selected, constant(b, PIN_I32, output)), next);
    }
    return t.after;
}
int behavior_lower_state(builder *b, task t)
{
    node *n = t.node;
    int index = behavior_asset_state_index(b->asset, n->id);
    assert(index >= 0);
    b->function->checkpoints = true;
    int bank = behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .offset = offsetof(behavior_function_context, states) }
    );
    int ready = behavior_lower_label(b);
    branch(b, binary(b, BF_NE, PIN_BOOL, bank, constant(b, BF_POINTER_TYPE, 0)), ready);
    behavior_lower_emit(
        b,
        (behavior_function_instruction){
            .op = BF_FAIL,
            .left = BEHAVIOR_FUNCTION_STATE,
            .node_id = n->id,
            .pin = -1,
            .message = "stateful flow node requires a Behavior Asset instance" }
    );
    behavior_lower_bind_label(b, ready);

    if (n->desc == &NODE_MultiGate) {
        return multi_gate(b, t, index);
    }
    if ((n->desc == &NODE_DoOnce || n->desc == &NODE_DoN) && t.input == 1) {
        pin_type type = n->desc == &NODE_DoOnce ? PIN_BOOL : PIN_I32;
        set(b, bank, index, type, constant(b, type, 0));
        capture_input(b, n, 2);
        behavior_lower_capture(b, n, true);
        return t.after;
    }
    if (n->desc == &NODE_FlipFlop) {
        int value = invert(b, read(b, bank, index, offsetof(pin_value, b), PIN_BOOL));
        set(b, bank, index, PIN_BOOL, value);
        if (!output_value(b, n, 2, value, NULL)) {
            return -2;
        }
        behavior_lower_capture(b, n, true);
        int yes = behavior_lower_successor(b, n, 0, t.after);
        int no = behavior_lower_successor(b, n, 1, t.after);
        if (yes < -1 || no < -1) {
            return -2;
        }
        branch(b, value, yes);
        return no;
    }
    if (n->desc == &NODE_DoN) {
        int limit = behavior_lower_input(b, n, 2);
        if (limit < 0 || b->function->slots[limit] != PIN_I32) {
            return behavior_lower_unsupported(b, n, 2, false, "DoN limit is not int");
        }
        remember_input(b, n, 2, limit);
        int initialized = behavior_lower_label(b);
        int type = read(b, bank, index, offsetof(pin_value, type), PIN_I32);
        branch(b, binary(b, BF_EQ, PIN_BOOL, type, constant(b, PIN_I32, PIN_I32)), initialized);
        set(b, bank, index, PIN_I32, constant(b, PIN_I32, 0));
        behavior_lower_bind_label(b, initialized);
        int value = read(b, bank, index, offsetof(pin_value, i), PIN_I32);
        if (!output_value(b, n, 1, value, "int")) {
            return -2;
        }
        behavior_lower_capture(b, n, false);
        branch(b, binary(b, BF_GE, PIN_BOOL, value, limit), t.after);
        write(
            b,
            bank,
            index,
            offsetof(pin_value, i),
            binary(b, BF_ADD, PIN_I32, value, constant(b, PIN_I32, 1))
        );
        return behavior_lower_successor(b, n, 0, t.after);
    }

    int initialized = behavior_lower_label(b);
    int type = read(b, bank, index, offsetof(pin_value, type), PIN_I32);
    branch(
        b,
        binary(
            b,
            n->desc == &NODE_DoOnce ? BF_NE : BF_EQ,
            PIN_BOOL,
            type,
            constant(b, PIN_I32, n->desc == &NODE_DoOnce ? PIN_FLOW : PIN_BOOL)
        ),
        initialized
    );
    int closed = behavior_lower_input(b, n, n->desc == &NODE_DoOnce ? 2 : 4);
    if (closed < 0 || b->function->slots[closed] != PIN_BOOL) {
        return behavior_lower_unsupported(b, n, n->desc == &NODE_DoOnce ? 2 : 4, false, "Start Closed is not bool");
    }
    set(b, bank, index, PIN_BOOL, n->desc == &NODE_DoOnce ? closed : invert(b, closed));
    remember_input(b, n, n->desc == &NODE_DoOnce ? 2 : 4, closed);
    behavior_lower_bind_label(b, initialized);
    capture_input(b, n, n->desc == &NODE_DoOnce ? 2 : 4);
    int value = read(b, bank, index, offsetof(pin_value, b), PIN_BOOL);
    if (n->desc == &NODE_DoOnce) {
        branch(b, value, t.after);
        write(b, bank, index, offsetof(pin_value, b), constant(b, PIN_BOOL, 1));
        return behavior_lower_successor(b, n, 0, t.after);
    }
    if (t.input) {
        int next = t.input == 3 ? invert(b, value)
                                : constant(b, PIN_BOOL, t.input == 1);
        write(b, bank, index, offsetof(pin_value, b), next);
        return t.after;
    }
    int next = behavior_lower_successor(b, n, 0, t.after);
    if (next < -1) {
        return -2;
    }
    branch(b, value, next);
    return t.after;
}
