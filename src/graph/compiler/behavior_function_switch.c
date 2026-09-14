#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"

#include <string.h>

bool behavior_lower_is_switch(const node_desc *desc)
{
    return desc == &NODE_SwitchInt || desc == &NODE_SwitchEnum ||
           desc == &NODE_SwitchString || desc == &NODE_SwitchName;
}

static const char *text_case(const node *n, int pin)
{
    const char *name = n->output_pins[pin].name;
    if (!name || strncmp(name, "Case:", 5)) {
        return NULL;
    }
    /* The first matching case wins. Later duplicates cannot execute. */
    for (int i = 0; i < pin; i++) {
        const char *previous = n->output_pins[i].name;
        if (previous && !strcmp(previous, name)) {
            return NULL;
        }
    }
    return name + 5;
}

int behavior_lower_switch(builder *b, task t)
{
    node *n = t.node;
    bool text = n->desc == &NODE_SwitchString || n->desc == &NODE_SwitchName;
    int selection = behavior_lower_input(b, n, 1);
    if (selection < 0 || n->output_count < 1 ||
        (text ? !behavior_scalar_string(b->function->slots[selection])
              : b->function->slots[selection] != PIN_I32)) {
        return behavior_lower_unsupported(b, n, 1, false, "switch selection or cases");
    }
    b->function->checkpoints = true;
    behavior_lower_capture(b, n, false);
    for (int pin = 0; pin + 1 < n->output_count; pin++) {
        const char *label = text ? text_case(n, pin) : NULL;
        if (text && !label) {
            continue;
        }
        pin_value value = text ? (pin_value){ .type = PIN_STR, .str = label }
                               : (pin_value){ .type = PIN_I32, .i = pin };
        int candidate = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = value.type,
                                             .value = value }
        );
        int equal = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = text ? BF_STRING_EQUAL : BF_EQ,
                                             .type = PIN_BOOL,
                                             .left = selection,
                                             .right = candidate }
        );
        int target = behavior_lower_successor(b, n, pin, t.after);
        if (target < -1) {
            return -2;
        }
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = equal, .target = target }
        );
    }
    return behavior_lower_successor(b, n, n->output_count - 1, t.after);
}
