#include "flow.h"
#include "instance.h"
#include "pin_conv.h"
#include "registry.h"

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
static pin_spec for_break_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Break", PIN_FLOW, NULL, PIN_EXEC },
    { "FirstIndex", PIN_I32, "int", PIN_DATA },
    { "LastIndex", PIN_I32, "int", PIN_DATA },
};
static pin_spec while_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Condition", PIN_BOOL, "bool", PIN_DATA },
};
static pin_spec while_outputs[] = {
    { "Body", PIN_FLOW, NULL, PIN_EXEC },
    { "Completed", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec foreach_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Array",
      PIN_FLOW,
      NULL,
      PIN_DATA,
      0,
      PIN_CONTAINER_ARRAY,
      { .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY },
      0,
      PIN_FLAG_WILDCARD,
      1 },
};
static pin_spec foreach_break_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Break", PIN_FLOW, NULL, PIN_EXEC },
    { "Array",
      PIN_FLOW,
      NULL,
      PIN_DATA,
      0,
      PIN_CONTAINER_ARRAY,
      { .type = PIN_FLOW, .container = PIN_CONTAINER_ARRAY },
      0,
      PIN_FLAG_WILDCARD,
      1 },
};
static pin_spec foreach_outputs[] = {
    { "Body", PIN_FLOW, NULL, PIN_EXEC },
    { "Array Element", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
    { "Index", PIN_I32, "int", PIN_DATA },
    { "Completed", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec break_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
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
static pin_spec function_entry_outputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec function_return_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec function_call_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec function_call_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec macro_call_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec macro_call_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};

static int flow_event(node *n, flow_context *ctx)
{
    flow_inputs inputs = flow_context_inputs(ctx);
    pin_value *outputs = flow_context_output_value(ctx, 0);
    int parameter = 0;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (parameter < inputs.count &&
            pin_compatible(
                &n->output_pins[i],
                &(pin_spec){
                    .type = inputs.values[parameter].type,
                    .type_name = inputs.values[parameter].type_name,
                    .container = n->output_pins[i].container,
                }
            )) {
            outputs[i] = inputs.values[parameter];
            pin_coerce(&outputs[i], &n->output_pins[i]);
        } else {
            outputs[i] = n->output_pins[i].default_value;
        }
        parameter++;
    }
    return flow_context_continue(ctx, 0);
}

static int flow_branch(node *n, flow_context *ctx)
{
    (void)n;
    pin_value condition;
    int result = flow_context_eval_data(ctx, 1, &condition);
    if (result) {
        return result;
    }
    if (condition.type != PIN_BOOL) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Branch condition is not bool");
    }
    return flow_context_continue(ctx, condition.b ? 0 : 1);
}

static int flow_sequence(node *n, flow_context *ctx)
{
    for (int i = n->output_count - 1; i >= 0; i--) {
        if (flow_context_continue(ctx, i)) {
            return FLOW_ERR_NODE;
        }
    }
    return FLOW_OK;
}

static int flow_for_loop(node *n, flow_context *ctx)
{
    (void)n;
    pin_value first, last;
    int result = flow_context_eval_data(ctx, 1, &first);
    if (result) {
        return result;
    }
    result = flow_context_eval_data(ctx, 2, &last);
    if (result) {
        return result;
    }
    if (first.type != PIN_I32 || last.type != PIN_I32) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "ForLoop indices are not int");
    }
    return flow_context_loop(ctx, first.i, last.i);
}

static int flow_for_loop_break(node *n, flow_context *ctx)
{
    (void)n;
    pin_value first, last;
    int result = flow_context_eval_data(ctx, 2, &first);
    if (result) {
        return result;
    }
    result = flow_context_eval_data(ctx, 3, &last);
    if (result) {
        return result;
    }
    if (first.type != PIN_I32 || last.type != PIN_I32) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "ForLoop indices are not int");
    }
    return flow_context_loop_range(ctx, first.i, last.i, 0, 2, 1);
}

static int flow_while_loop(node *n, flow_context *ctx)
{
    (void)n;
    pin_value condition;
    int result = flow_context_eval_data(ctx, 1, &condition);
    if (result) {
        return result;
    }
    if (condition.type != PIN_BOOL) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "WhileLoop condition is not bool");
    }
    return flow_context_continue(ctx, condition.b ? 0 : 1);
}

static int flow_for_each(node *n, flow_context *ctx)
{
    int input = n->input_pins[0].kind == PIN_EXEC ? 1 : 2;
    pin_value array;
    int result = flow_context_eval_data(ctx, input, &array);
    if (result) {
        return result;
    }
    return flow_context_foreach(ctx, &array, 0, 1, 2, 3);
}

static int flow_break(node *n, flow_context *ctx)
{
    (void)n;
    return flow_context_break(ctx);
}

static int flow_delay(node *n, flow_context *ctx)
{
    (void)n;
    pin_value duration;
    int result = flow_context_eval_data(ctx, 1, &duration);
    if (result) {
        return result;
    }
    if (duration.type != PIN_F64) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Delay duration is not double");
    }
    result = flow_context_pause_until(ctx, flow_context_now(ctx) + duration.d);
    if (result < 0) {
        return result;
    }
    return flow_context_continue(ctx, 0);
}

static int flow_print_string(node *n, flow_context *ctx)
{
    (void)n;
    pin_value message;
    int result = flow_context_eval_data(ctx, 1, &message);
    if (result) {
        return result;
    }
    if (message.type != PIN_STR) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Print String input is not a string");
    }
    result = flow_context_emit(ctx, message.str ? message.str : "");
    if (result) {
        return result;
    }
    return flow_context_continue(ctx, 0);
}

static int flow_set_variable(node *n, flow_context *ctx)
{
    blueprint_instance *bi = flow_context_instance(ctx);
    if (!bi) {
        return flow_context_fail(ctx, FLOW_ERR_STATE, "Set Variable requires an instance");
    }
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    if (blueprint_instance_set_variable(bi, n->reference_id, &value) != 0) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Set Variable references a lost variable");
    }
    *flow_context_output_value(ctx, 1) = value;
    return flow_context_continue(ctx, 0);
}

static int flow_function_entry(node *n, flow_context *ctx)
{
    return flow_event(n, ctx);
}

static int flow_function_return(node *n, flow_context *ctx)
{
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        pin_value value;
        int result = flow_context_eval_data(ctx, i, &value);
        if (result) {
            return result;
        }
        *flow_context_input_value(ctx, i) = value;
    }
    return FLOW_OK;
}

static int flow_execute_function(node *n, flow_context *ctx, int macro)
{
    blueprint_instance *bi = flow_context_instance(ctx);
    if (!bi) {
        return flow_context_fail(ctx, FLOW_ERR_STATE, "function call requires an instance");
    }
    graph *target = blueprint_find_graph(bi->bp, n->reference_id);
    if (!target ||
        (macro ? target->kind != GRAPH_MACRO : target->kind != GRAPH_FUNCTION)) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "function graph reference is missing");
    }
    if (target->call_depth_limit &&
        flow_context_call_depth(ctx) >= (unsigned)target->call_depth_limit) {
        return flow_context_fail(ctx, FLOW_ERR_STEP_BUDGET, "function call depth limit exceeded");
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "function graph has no entry or return");
    }
    int count = 0;
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA) {
            count++;
        }
    }
    pin_value *params = count
                          ? (pin_value *)flow_context_scratch_alloc(ctx, (size_t)count * sizeof *params)
                          : NULL;
    if (count && !params) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "function parameter allocation failed");
    }
    int at = 0;
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        int result = flow_context_eval_data(ctx, i, &params[at++]);
        if (result) {
            return result;
        }
    }
    flow_options options = {
        .now = NULL,
        .initial_time = flow_context_now(ctx),
        .sink = NULL,
        .instance = bi,
        .call_depth = flow_context_call_depth(ctx) + 1,
    };
    flow_error error = { 0 };
    flow_run *callee = flow_run_start(target, entry, (flow_inputs){ params, count, NULL }, &options, &error);
    if (!callee) {
        return flow_context_fail(
            ctx,
            error.code ? error.code : FLOW_ERR_NODE,
            error.message
        );
    }
    int result = flow_run_resume(callee);
    if (result < 0 || flow_run_status(callee) != FLOW_COMPLETED) {
        const flow_error *failure = flow_run_error(callee);
        result = failure && failure->code ? failure->code : FLOW_ERR_NODE;
        const char *message = failure && failure->message
                                ? failure->message
                                : "function call failed";
        flow_run_free(callee);
        return flow_context_fail(ctx, result, message);
    }
    at = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        while (at < n->output_count && n->output_pins[at].kind != PIN_DATA) {
            at++;
        }
        if (at >= n->output_count) {
            break;
        }
        const pin_value *value = flow_run_input_value(callee, ret, i);
        if (!value) {
            flow_run_free(callee);
            return flow_context_fail(ctx, FLOW_ERR_NODE, "function return value is unavailable");
        }
        *flow_context_output_value(ctx, at++) = *value;
    }
    if (flow_context_retain_run(ctx, callee)) {
        flow_run_free(callee);
        return flow_context_fail(ctx, FLOW_ERR_NODE, "function return lifetime unavailable");
    }
    return flow_context_continue(ctx, 0);
}

static int flow_call_function(node *n, flow_context *ctx)
{
    return flow_execute_function(n, ctx, 0);
}

static int flow_call_macro(node *n, flow_context *ctx)
{
    return flow_execute_function(n, ctx, 1);
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
node_desc NODE_ForLoopWithBreak = {
    .name = "ForLoopWithBreak",
    .input_count = 4,
    .output_count = 3,
    .inputs = for_break_inputs,
    .outputs = for_outputs,
    .pure_kind = NODE_PURE_CONST,
    .flow_kind = NODE_FLOW_FOR_LOOP,
    .flow = flow_for_loop_break,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_WhileLoop = {
    .name = "WhileLoop",
    .input_count = 2,
    .output_count = 2,
    .inputs = while_inputs,
    .outputs = while_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_while_loop,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForEach = {
    .name = "ForEach",
    .input_count = 2,
    .output_count = 4,
    .inputs = foreach_inputs,
    .outputs = foreach_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_for_each,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForEachWithBreak = {
    .name = "ForEachWithBreak",
    .input_count = 3,
    .output_count = 4,
    .inputs = foreach_break_inputs,
    .outputs = foreach_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_for_each,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_Break = {
    .name = "Break",
    .input_count = 1,
    .output_count = 0,
    .inputs = break_inputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_break,
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
    .latent = 1,
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
node_desc NODE_FunctionEntry = {
    .name = "Function Entry",
    .input_count = 0,
    .output_count = 1,
    .outputs = function_entry_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_function_entry,
    .display_class = NODE_DISPLAY_EVENT,
};
node_desc NODE_Return = {
    .name = "Return",
    .input_count = 1,
    .output_count = 0,
    .inputs = function_return_inputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_function_return,
    .display_class = NODE_DISPLAY_EVENT,
};
node_desc NODE_CallFunction = {
    .name = "Call Function",
    .input_count = 1,
    .output_count = 1,
    .inputs = function_call_inputs,
    .outputs = function_call_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_call_function,
    .display_class = NODE_DISPLAY_FUNCTION,
};
node_desc NODE_CallPureFunction = {
    .name = "Call Pure Function",
    .input_count = 0,
    .output_count = 0,
    .pure_kind = NODE_PURE_FUNCTION,
    .display_class = NODE_DISPLAY_FUNCTION,
};
node_desc NODE_CallMacro = {
    .name = "Call Macro",
    .input_count = 1,
    .output_count = 1,
    .inputs = macro_call_inputs,
    .outputs = macro_call_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_call_macro,
    .display_class = NODE_DISPLAY_FUNCTION,
};

node_desc *const FLOW_NODES[] = {
    &NODE_Event,
    &NODE_Branch,
    &NODE_Sequence,
    &NODE_ForLoop,
    &NODE_ForLoopWithBreak,
    &NODE_WhileLoop,
    &NODE_ForEach,
    &NODE_ForEachWithBreak,
    &NODE_Break,
    &NODE_Delay,
    &NODE_PrintString,
    &NODE_GetVariable,
    &NODE_SetVariable,
    &NODE_Self,
    &NODE_FunctionEntry,
    &NODE_Return,
    &NODE_CallFunction,
    &NODE_CallPureFunction,
    &NODE_CallMacro,
};
const int FLOW_NODE_COUNT = sizeof FLOW_NODES / sizeof FLOW_NODES[0];

const registry_pack CORE_FLOW_PACK = {
    .name = "core.flow",
    .nodes = FLOW_NODES,
    .node_count = sizeof FLOW_NODES / sizeof FLOW_NODES[0],
};
