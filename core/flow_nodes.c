#include "flow.h"
#include "instance.h"
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

static pin_spec get_variable_outputs[] = {
    { "Value", PIN_I32, "int", PIN_DATA },
};
static pin_spec set_variable_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_I32, "int", PIN_DATA },
};
static pin_spec set_variable_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_I32, "int", PIN_DATA },
};
static pin_spec self_outputs[] = {
    { "Self", PIN_HANDLE, NULL, PIN_DATA },
};
static pin_spec call_event_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec call_event_outputs[] = {
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

static int flow_set_variable(node *n, flow_context *ctx)
{
    blueprint_instance *bi = flow_context_instance(ctx);
    if (!bi)
        return flow_context_fail(ctx, FLOW_ERR_STATE, "Set Variable requires an instance");
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) return result;
    if (blueprint_instance_set_variable(bi, n->reference_id, &value) != 0)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Set Variable references a lost variable");
    n->outputs[1] = value;
    return flow_context_continue(ctx, 0);
}

static int flow_call_event(node *n, flow_context *ctx)
{
    blueprint_instance *bi = flow_context_instance(ctx);
    if (!bi)
        return flow_context_fail(ctx, FLOW_ERR_STATE, "Call Event requires an instance");
    node *target = graph_node_by_id(bi->bp->graph, n->reference_id);
    if (!target || target->desc->flow_kind != NODE_FLOW_EVENT)
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Call Event references a missing event");
    int count = n->input_count - 1;
    pin_value *params = count
                          ? (pin_value *)flow_context_scratch_alloc(ctx, (size_t)count * sizeof *params)
                          : NULL;
    if (count && !params)
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Call Event parameter allocation failed");
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, 1 + i, &params[i]);
        if (result) return result;
    }
    flow_error err = { 0 };
    if (!blueprint_instance_enqueue(bi, target, (flow_inputs){ params, count, NULL }, &err))
        return flow_context_fail(
            ctx,
            err.code ? err.code : FLOW_ERR_NODE,
            err.message ? err.message : "Call Event dispatch failed"
        );
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
node_desc NODE_GetVariable = {
    .name = "Get Variable",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = get_variable_outputs,
    .pure_kind = NODE_PURE_GET,
    .display_class = NODE_DISPLAY_VARIABLE,
};
node_desc NODE_SetVariable = {
    .name = "Set Variable",
    .input_count = 2,
    .output_count = 2,
    .inputs = set_variable_inputs,
    .outputs = set_variable_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_set_variable,
    .display_class = NODE_DISPLAY_VARIABLE,
};
node_desc NODE_Self = {
    .name = "Self",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = self_outputs,
    .pure_kind = NODE_PURE_SELF,
    .display_class = NODE_DISPLAY_PURE,
};
node_desc NODE_CallEvent = {
    .name = "Call Event",
    .input_count = 1,
    .output_count = 1,
    .inputs = call_event_inputs,
    .outputs = call_event_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_call_event,
    .display_class = NODE_DISPLAY_EVENT,
};

node_desc *const FLOW_NODES[] = {
    &NODE_Event, &NODE_Branch,      &NODE_Sequence,    &NODE_ForLoop,
    &NODE_Delay, &NODE_PrintString, &NODE_GetVariable, &NODE_SetVariable,
    &NODE_Self,  &NODE_CallEvent,
};
const int FLOW_NODE_COUNT = sizeof FLOW_NODES / sizeof FLOW_NODES[0];
