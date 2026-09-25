#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/instance.h"

#include <string.h>

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
    { "Value", PIN_I32, "int", PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_I32 }, 0, PIN_FLAG_REFERENCE },
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
        if (n->output_pins[i].kind != PIN_DATA || n->output_pins[i].field_source) {
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
    return flow_context_continue(
        ctx,
        n->desc == &NODE_FunctionEntry ? flow_context_entry_output(ctx) : 0
    );
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
    if (flow_context_input_pin(ctx) == 1) {
        return flow_context_break(ctx);
    }
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
    return flow_context_while(ctx, 1, 0, 1);
}

static int flow_for_each(node *n, flow_context *ctx)
{
    int input = n->input_count == 3 ? 2 : 1;
    if (input == 2 && flow_context_input_pin(ctx) == 1) {
        return flow_context_break(ctx);
    }
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
    behavior_instance *bi = flow_context_instance(ctx);
    if (!bi) {
        return flow_context_fail(ctx, FLOW_ERR_STATE, "Set Variable requires an instance");
    }
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    if (behavior_instance_set_variable(bi, n->reference_id, &value) != 0) {
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
    return flow_context_return(ctx);
}

static int flow_execute_function(node *n, flow_context *ctx, int macro)
{
    behavior_instance *bi = flow_context_instance(ctx);
    if (!bi) {
        return flow_context_fail(ctx, FLOW_ERR_STATE, "function call requires an instance");
    }
    graph *target = behavior_asset_find_graph(bi->asset, n->reference_id);
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
    return flow_context_call(ctx, target, (flow_inputs){ params, count, NULL });
}

static int flow_call_function(node *n, flow_context *ctx)
{
    return flow_execute_function(n, ctx, 0);
}

static int flow_behavior_expression(node *n, flow_context *ctx)
{
    int true_pin = -1, false_pin = -1, value_pin = -1;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind == PIN_EXEC && n->output_pins[i].name) {
            if (!strcmp(n->output_pins[i].name, "True")) {
                true_pin = i;
            } else if (!strcmp(n->output_pins[i].name, "False")) {
                false_pin = i;
            }
        } else if (
            n->output_pins[i].kind == PIN_DATA && n->output_pins[i].name &&
            !strcmp(n->output_pins[i].name, "Result")
        ) {
            value_pin = i;
        }
    }
    if (true_pin >= 0 || false_pin >= 0) {
        if (true_pin < 0 || false_pin < 0 || value_pin < 0) {
            return flow_context_fail(ctx, FLOW_ERR_INVALID_NODE, "branch expression signature is incomplete");
        }
        pin_value value;
        int result = flow_context_eval_output(ctx, value_pin, &value);
        if (result) {
            return result;
        }
        if (value.type != PIN_BOOL) {
            return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "branch expression Result is not bool");
        }
        return flow_context_continue(ctx, value.b ? true_pin : false_pin);
    }
    return flow_execute_function(n, ctx, 0);
}

static int flow_call_macro(node *n, flow_context *ctx)
{
    return flow_execute_function(n, ctx, 1);
}

static const node_access control_access = { 0 };

node_desc NODE_Event = {
    .access = &control_access,
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
    .access = &control_access,
    .name = "Branch",
    .keywords = "if else condition true false",
    .input_count = 2,
    .output_count = 2,
    .inputs = branch_inputs,
    .outputs = branch_outputs,
    .flow_kind = NODE_FLOW_BRANCH,
    .flow = flow_branch,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_Sequence = {
    .access = &control_access,
    .name = "Sequence",
    .keywords = "then order steps",
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
    .keywords = "repeat times count index",
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
    .keywords = "repeat until condition",
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
    .keywords = "each iterate array items",
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
    .keywords = "wait timer seconds after later",
    .input_count = 2,
    .output_count = 1,
    .inputs = delay_inputs,
    .outputs = delay_outputs,
    .flow_kind = NODE_FLOW_DELAY,
    .flow = flow_delay,
    .latent = 1,
    .display_class = NODE_DISPLAY_FLOW,
};
static const node_access print_access = { .flags = NODE_EFFECT_HOST_SINK };
node_desc NODE_PrintString = {
    .name = "Print String",
    .keywords = "log debug message",
    .input_count = 2,
    .output_count = 1,
    .inputs = print_text_inputs,
    .outputs = print_text_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_print_string,
    .access = &print_access,
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
    .keywords = "owner this entity",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = self_outputs,
    .pure_kind = NODE_PURE_SELF,
    .display_class = NODE_DISPLAY_PURE,
};
node_desc NODE_FunctionEntry = {
    .access = &control_access,
    .name = "Function Entry",
    .input_count = 0,
    .output_count = 1,
    .outputs = function_entry_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_function_entry,
    .display_class = NODE_DISPLAY_EVENT,
};
node_desc NODE_Return = {
    .access = &control_access,
    .name = "Return",
    .input_count = 1,
    .output_count = 0,
    .inputs = function_return_inputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_function_return,
    .display_class = NODE_DISPLAY_EVENT,
};
node_desc NODE_CallFunction = {
    .access = &control_access,
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
    .access = &control_access,
    .name = "Call Pure Function",
    .input_count = 0,
    .output_count = 0,
    .pure_kind = NODE_PURE_FUNCTION,
    .display_class = NODE_DISPLAY_FUNCTION,
};
node_desc NODE_BehaviorExpression = {
    .access = &control_access,
    .name = "Behavior Expression",
    .input_count = 0,
    .output_count = 0,
    .pure_kind = NODE_PURE_FUNCTION,
    .flow = flow_behavior_expression,
    .display_class = NODE_DISPLAY_FUNCTION,
    .category = "Math",
    .keywords = "behavior expression formula assignment condition branch",
};
node_desc NODE_CallMacro = {
    .access = &control_access,
    .name = "Call Macro",
    .input_count = 1,
    .output_count = 1,
    .inputs = macro_call_inputs,
    .outputs = macro_call_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_call_macro,
    .display_class = NODE_DISPLAY_FUNCTION,
};

static node_desc *const FLOW_NODES[] = {
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
    &NODE_BehaviorExpression,
    &NODE_CallMacro,
};

const registry_pack CORE_FLOW_PACK = {
    .name = "core.flow",
    .nodes = FLOW_NODES,
    .node_count = sizeof FLOW_NODES / sizeof FLOW_NODES[0],
};
