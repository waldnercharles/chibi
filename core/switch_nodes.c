#include "switch_nodes.h"

#include "flow.h"
#include "registry.h"

#include <string.h>

static pin_spec switch_exec_input[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec switch_int_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Selection", PIN_I32, "int", PIN_DATA },
};
static pin_spec switch_string_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Selection", PIN_STR, "const char *", PIN_DATA },
};
static pin_spec switch_name_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Selection", PIN_NAME, "name", PIN_DATA },
};
static pin_spec switch_outputs[] = {
    { "Case0", PIN_FLOW, NULL, PIN_EXEC },
    { "Case1", PIN_FLOW, NULL, PIN_EXEC },
    { "Default", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec select_inputs[] = {
    { "Index", PIN_I32, "int", PIN_DATA },
    { "Option0", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
};
static pin_spec select_outputs[] = {
    { "Return Value", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
};

static int flow_switch_int(node *n, flow_context *ctx)
{
    pin_value selection;
    int result = flow_context_eval_data(ctx, 1, &selection);
    if (result) {
        return result;
    }
    if (selection.type != PIN_I32) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Switch selection is not int");
    }
    int cases = n->output_count - 1;
    int output = selection.i >= 0 && selection.i < cases ? selection.i : cases;
    return flow_context_continue(ctx, output);
}

static int flow_switch_text(node *n, flow_context *ctx)
{
    pin_value selection;
    int result = flow_context_eval_data(ctx, 1, &selection);
    if (result) {
        return result;
    }
    const char *value = selection.str ? selection.str : "";
    for (int i = 0; i + 1 < n->output_count; i++) {
        const char *name = n->output_pins[i].name ? n->output_pins[i].name : "";
        if (strncmp(name, "Case:", 5) == 0 && strcmp(name + 5, value) == 0) {
            return flow_context_continue(ctx, i);
        }
    }
    return flow_context_continue(ctx, n->output_count - 1);
}

static pin_value select_value(const pin_value *inputs, int count, arena *a)
{
    (void)a;
    if (count < 2 || inputs[0].type != PIN_I32 || inputs[0].i < 0 ||
        inputs[0].i + 1 >= count) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return inputs[inputs[0].i + 1];
}

node_desc NODE_SwitchInt = {
    .name = "Switch on Int",
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_int_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_switch_int,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "switch int cases default",
};
node_desc NODE_SwitchString = {
    .name = "Switch on String",
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_string_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_switch_text,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "switch string cases default",
};
node_desc NODE_SwitchName = {
    .name = "Switch on Name",
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_name_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_switch_text,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "switch name cases default",
};
node_desc NODE_SwitchEnum = {
    .name = "Switch on Enum",
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_int_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_switch_int,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "switch enum cases default",
};
node_desc NODE_Select = {
    .name = "Select",
    .input_count = 2,
    .output_count = 1,
    .inputs = select_inputs,
    .outputs = select_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = select_value,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Utilities",
    .keywords = "select ternary option",
};

node_desc *const SWITCH_NODES[] = {
    &NODE_SwitchInt,  &NODE_SwitchString, &NODE_SwitchName,
    &NODE_SwitchEnum, &NODE_Select,
};
const int SWITCH_NODE_COUNT = sizeof SWITCH_NODES / sizeof SWITCH_NODES[0];

const registry_pack CORE_SWITCH_PACK = {
    .name = "core.switch",
    .nodes = SWITCH_NODES,
    .node_count = sizeof SWITCH_NODES / sizeof SWITCH_NODES[0],
};
