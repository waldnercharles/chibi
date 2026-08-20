#include "flow.h"
#include "pin_conv.h"

static pin_spec event_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};

static pin_spec branch_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Condition", PIN_BOOL, "bool", PIN_DATA },
};
static pin_spec branch_outputs[] = {
    { "True", PIN_FLOW, NULL, PIN_EXEC },
    { "False", PIN_FLOW, NULL, PIN_EXEC },
};

static pin_spec sequence_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec sequence_outputs[] = {
    { "Then0", PIN_FLOW, NULL, PIN_EXEC },
    { "Then1", PIN_FLOW, NULL, PIN_EXEC },
};

static pin_spec for_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "FirstIndex", PIN_I32, "int", PIN_DATA },
    { "LastIndex", PIN_I32, "int", PIN_DATA },
};
static pin_spec for_outputs[] = {
    { "Body", PIN_FLOW, NULL, PIN_EXEC },
    { "Index", PIN_I32, "int", PIN_DATA },
    { "Completed", PIN_FLOW, NULL, PIN_EXEC },
};

static pin_spec delay_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Duration", PIN_F64, "double", PIN_DATA },
};
static pin_spec delay_outputs[] = {
    { "Completed", PIN_FLOW, NULL, PIN_EXEC },
};

static pin_spec print_text_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "InString", PIN_STR, "const char *", PIN_DATA },
};
static pin_spec print_text_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};

static int flow_event(node *n, flow_context *ctx)
{
    flow_inputs inputs = flow_context_inputs(ctx);
    int parameter = 0;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_DATA) continue;
        if (parameter < inputs.count &&
            pin_compatible(
                &n->output_pins[i],
                &(pin_spec){
                    .type = inputs.values[parameter].type,
                    .type_name = inputs.values[parameter].type_name,
                    .container = n->output_pins[i].container,
                }
            )) {
            n->outputs[i] = inputs.values[parameter];
            pin_coerce(&n->outputs[i], &n->output_pins[i]);
        } else n->outputs[i] = n->output_pins[i].default_value;
        parameter++;
    }
    return flow_context_continue(ctx, 0);
}

static int flow_branch(node *n, flow_context *ctx)
{
    (void)n;
    pin_value condition;
    int result = flow_context_eval_data(ctx, 1, &condition);
    if (result) return result;
    if (condition.type != PIN_BOOL)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Branch condition is not bool");
    return flow_context_continue(ctx, condition.b ? 0 : 1);
}

static int flow_sequence(node *n, flow_context *ctx)
{
    for (int i = n->output_count - 1; i >= 0; i--) {
        if (flow_context_continue(ctx, i)) return FLOW_ERR_NODE;
    }
    return FLOW_OK;
}

static int flow_for_loop(node *n, flow_context *ctx)
{
    (void)n;
    pin_value first, last;
    int result = flow_context_eval_data(ctx, 1, &first);
    if (result) return result;
    result = flow_context_eval_data(ctx, 2, &last);
    if (result) return result;
    if (first.type != PIN_I32 || last.type != PIN_I32)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "ForLoop indices are not int");
    return flow_context_loop(ctx, first.i, last.i);
}

static int flow_delay(node *n, flow_context *ctx)
{
    (void)n;
    pin_value duration;
    int result = flow_context_eval_data(ctx, 1, &duration);
    if (result) return result;
    if (duration.type != PIN_F64)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Delay duration is not double");
    result = flow_context_pause_until(ctx, flow_context_now(ctx) + duration.d);
    if (result < 0) return result;
    return flow_context_continue(ctx, 0);
}

static int flow_print_string(node *n, flow_context *ctx)
{
    (void)n;
    pin_value message;
    int result = flow_context_eval_data(ctx, 1, &message);
    if (result) return result;
    if (message.type != PIN_STR)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Print String input is not a string");
    result = flow_context_emit(ctx, message.str ? message.str : "");
    if (result) return result;
    return flow_context_continue(ctx, 0);
}

node_desc NODE_Event = {
    .name = "Event",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = event_outputs,
    .flow_kind = NODE_FLOW_EVENT,
    .flow = flow_event,
    .display_class = NODE_DISPLAY_EVENT,
};
node_desc NODE_Branch = {
    .name = "Branch",
    .input_count = 2,
    .output_count = 2,
    .inputs = branch_inputs,
    .outputs = branch_outputs,
    .flow_kind = NODE_FLOW_BRANCH,
    .flow = flow_branch,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_Sequence = {
    .name = "Sequence",
    .input_count = 1,
    .output_count = 2,
    .inputs = sequence_inputs,
    .outputs = sequence_outputs,
    .flow_kind = NODE_FLOW_SEQUENCE,
    .flow = flow_sequence,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForLoop = {
    .name = "ForLoop",
    .input_count = 3,
    .output_count = 3,
    .inputs = for_inputs,
    .outputs = for_outputs,
    .pure_kind = NODE_PURE_CONST,
    .flow_kind = NODE_FLOW_FOR_LOOP,
    .flow = flow_for_loop,
    .display_class = NODE_DISPLAY_FLOW,
};
/* Temporary proof node: this validates yielding and host-time resume. */
node_desc NODE_Delay = {
    .name = "Delay",
    .input_count = 2,
    .output_count = 1,
    .inputs = delay_inputs,
    .outputs = delay_outputs,
    .flow_kind = NODE_FLOW_DELAY,
    .flow = flow_delay,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_PrintString = {
    .name = "Print String",
    .input_count = 2,
    .output_count = 1,
    .inputs = print_text_inputs,
    .outputs = print_text_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_print_string,
    .display_class = NODE_DISPLAY_FUNCTION,
};

node_desc *const FLOW_NODES[] = {
    &NODE_Event,   &NODE_Branch, &NODE_Sequence,
    &NODE_ForLoop, &NODE_Delay,  &NODE_PrintString,
};
const int FLOW_NODE_COUNT = sizeof FLOW_NODES / sizeof FLOW_NODES[0];
