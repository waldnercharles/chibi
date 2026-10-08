#include "execution/log.h"
#include "graph/model/native_definition.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/core_definition.h"

static pin_spec event_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};

static pin_spec branch_inputs[] = { CORE_CONDITION_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec branch_outputs[] = { CORE_BRANCH_OUTPUTS(GRAPH_NATIVE_PORT) };

static pin_spec sequence_inputs[] = { CORE_EXEC_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec sequence_outputs[] = { CORE_SEQUENCE_OUTPUTS(GRAPH_NATIVE_PORT) };

static pin_spec for_inputs[] = { CORE_RANGE_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec for_outputs[] = { CORE_RANGE_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec for_break_inputs[] = { CORE_RANGE_BREAK_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec while_inputs[] = { CORE_CONDITION_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec while_outputs[] = { CORE_WHILE_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec foreach_inputs[] = { CORE_EACH_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec foreach_break_inputs[] = { CORE_EACH_BREAK_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec foreach_outputs[] = { CORE_EACH_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec break_inputs[] = { CORE_EXEC_INPUTS(GRAPH_NATIVE_PORT) };

static pin_spec delay_inputs[] = { CORE_DELAY_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec delay_outputs[] = { CORE_DELAY_OUTPUTS(GRAPH_NATIVE_PORT) };

static pin_spec print_text_inputs[] = { CORE_PRINT_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec print_text_outputs[] = { CORE_THEN_OUTPUTS(GRAPH_NATIVE_PORT) };

#define VARIABLE_GRAPH_PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    GRAPH_NATIVE_PORT_FLAGS(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE, PIN_FLAG_REFERENCE)
static pin_spec get_variable_outputs[] = { CORE_VARIABLE_DATA(VARIABLE_GRAPH_PORT) };
#undef VARIABLE_GRAPH_PORT
static pin_spec set_variable_inputs[] = { CORE_SET_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec set_variable_outputs[] = { CORE_SET_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec self_outputs[] = { CORE_SELF_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec function_entry_outputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static pin_spec function_return_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static pin_spec function_call_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static pin_spec function_call_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static pin_spec macro_call_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static pin_spec macro_call_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};

static const node_access control_access = { 0 };

node_desc NODE_Event = {
    .access = &control_access,
    .name = "Event",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = event_outputs,
    .flow_kind = NODE_FLOW_EVENT,
    .display_class = NODE_DISPLAY_EVENT,
    .description = "Starts a flow at Then when its named event is dispatched, "
                   "outputting the event's parameters.",
};
node_desc NODE_Branch = {
    .source_flow = CORE_BRANCH_FLOW(NODE_SOURCE_FLOW_BRANCH),
    .access = &control_access,
    .name = CORE_BRANCH_NAME,
    .source_name = CORE_BRANCH_SOURCE,
    .keywords = "if else condition true false",
    .description = "Fires True when Condition is true, otherwise False.",
    .input_count = 2,
    .output_count = 2,
    .inputs = branch_inputs,
    .outputs = branch_outputs,
    .flow_kind = NODE_FLOW_BRANCH,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_Sequence = {
    .source_flow = CORE_SEQUENCE_FLOW(NODE_SOURCE_FLOW_SEQUENCE),
    .access = &control_access,
    .name = CORE_SEQUENCE_NAME,
    .source_name = CORE_SEQUENCE_SOURCE,
    .keywords = "then order steps",
    .description = "Fires Then0, Then1 and any added outputs in order, "
                   "finishing each chain before the next starts.",
    .input_count = 1,
    .output_count = 2,
    .inputs = sequence_inputs,
    .outputs = sequence_outputs,
    .flow_kind = NODE_FLOW_SEQUENCE,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForLoop = {
    .source_flow = CORE_RANGE_FLOW(NODE_SOURCE_FLOW_RANGE, -1),
    .name = CORE_RANGE_NAME,
    .source_name = CORE_RANGE_SOURCE,
    .keywords = "repeat times count index",
    .description = "Fires Body for each Index from FirstIndex through "
                   "LastIndex, counting down if LastIndex is lower, then "
                   "Completed.",
    .input_count = 3,
    .output_count = 3,
    .inputs = for_inputs,
    .outputs = for_outputs,
    .pure_kind = NODE_PURE_CONST,
    .flow_kind = NODE_FLOW_FOR_LOOP,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForLoopWithBreak = {
    .source_flow = CORE_RANGE_FLOW(NODE_SOURCE_FLOW_RANGE, 1),
    .name = CORE_RANGE_BREAK_NAME,
    .source_name = CORE_RANGE_BREAK_SOURCE,
    .input_count = 4,
    .output_count = 3,
    .inputs = for_break_inputs,
    .outputs = for_outputs,
    .pure_kind = NODE_PURE_CONST,
    .flow_kind = NODE_FLOW_FOR_LOOP,
    .display_class = NODE_DISPLAY_FLOW,
    .description = "Fires Body for each Index from FirstIndex through "
                   "LastIndex, then Completed; Break stops before the next "
                   "pass.",
};
node_desc NODE_WhileLoop = {
    .source_flow = CORE_WHILE_FLOW(NODE_SOURCE_FLOW_WHILE),
    .name = CORE_WHILE_NAME,
    .source_name = CORE_WHILE_SOURCE,
    .keywords = "repeat until condition",
    .description = "Fires Body while Condition is true, checking it before "
                   "each pass, then fires Completed.",
    .input_count = 2,
    .output_count = 2,
    .inputs = while_inputs,
    .outputs = while_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForEach = {
    .source_flow = CORE_EACH_FLOW(NODE_SOURCE_FLOW_EACH, -1),
    .name = CORE_EACH_NAME,
    .source_name = CORE_EACH_SOURCE,
    .keywords = "each iterate array items",
    .description = "Fires Body for each Array Element with its zero-based "
                   "Index, then Completed; iterates a copy of Array.",
    .input_count = 2,
    .output_count = 4,
    .inputs = foreach_inputs,
    .outputs = foreach_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FLOW,
};
node_desc NODE_ForEachWithBreak = {
    .source_flow = CORE_EACH_FLOW(NODE_SOURCE_FLOW_EACH, 1),
    .name = CORE_EACH_BREAK_NAME,
    .source_name = CORE_EACH_BREAK_SOURCE,
    .input_count = 3,
    .output_count = 4,
    .inputs = foreach_break_inputs,
    .outputs = foreach_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FLOW,
    .description = "Fires Body for each Array Element and zero-based Index, "
                   "then Completed; Break stops before the next pass.",
};
node_desc NODE_Break = {
    .source_flow = CORE_BREAK_FLOW(NODE_SOURCE_FLOW_BREAK),
    .name = CORE_BREAK_NAME,
    .source_name = CORE_BREAK_SOURCE,
    .input_count = 1,
    .output_count = 0,
    .inputs = break_inputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FLOW,
    .description = "Stops the innermost running loop before its next pass, "
                   "which then fires Completed.",
};
/* Temporary proof node: this validates yielding and host-time resume. */
node_desc NODE_Delay = {
    .name = CORE_DELAY_NAME,
    .source_name = CORE_DELAY_SOURCE,
    .execution_intrinsic = BF_DELAY,
    CORE_DELAY_PRESENTATION,

    .input_count = 2,
    .output_count = 1,
    .inputs = delay_inputs,
    .outputs = delay_outputs,
    .flow_kind = NODE_FLOW_DELAY,
    .latent = 1,
    .display_class = NODE_DISPLAY_FLOW,
};
static const node_access print_access = { .flags = NODE_EFFECT_HOST_SINK };
node_desc NODE_PrintString = {
    .name = CORE_PRINT_NAME,
    .source_name = CORE_PRINT_SOURCE,
    CORE_PRINT_PRESENTATION,

    .input_count = 2,
    .output_count = 1,
    .inputs = print_text_inputs,
    .outputs = print_text_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .execution_call = &EXECUTION_PrintString,
    .access = &print_access,
    .display_class = NODE_DISPLAY_FUNCTION,
};
node_desc NODE_GetVariable = {
    .name = CORE_GET_NAME,
    .source_name = CORE_GET_SOURCE,
    .source_state_access = NODE_SOURCE_STATE_READ,
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = get_variable_outputs,
    .pure_kind = NODE_PURE_GET,
    .display_class = NODE_DISPLAY_VARIABLE,
    .description = "Returns the current value of a behavior variable.",
};
node_desc NODE_SetVariable = {
    .name = CORE_SET_NAME,
    .source_name = CORE_SET_SOURCE,
    .source_state_access = NODE_SOURCE_STATE_WRITE,
    .input_count = 2,
    .output_count = 2,
    .inputs = set_variable_inputs,
    .outputs = set_variable_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_VARIABLE,
    .description = "Stores Value in a behavior variable, outputs the stored "
                   "Value, then fires Then.",
};
node_desc NODE_Self = {
    .name = CORE_SELF_NAME,
    .source_name = CORE_SELF_SOURCE,
    .execution_intrinsic = BF_OWNER,
    CORE_SELF_PRESENTATION,

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
    .display_class = NODE_DISPLAY_EVENT,
    .description = "Starts a function or macro graph with the caller's "
                   "arguments; a macro enters at the exec matching the call's "
                   "input.",
};
node_desc NODE_Return = {
    .access = &control_access,
    .name = "Return",
    .input_count = 1,
    .output_count = 0,
    .inputs = function_return_inputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_EVENT,
    .description = "Ends the function or macro graph, passing its inputs back "
                   "as results; a macro leaves by the matching call output.",
};
node_desc NODE_CallFunction = {
    .access = &control_access,
    .name = "Call Function",
    .input_count = 1,
    .output_count = 1,
    .inputs = function_call_inputs,
    .outputs = function_call_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FUNCTION,
    .description = "Runs a function graph with these inputs as arguments, then "
                   "fires Then with its results once it returns.",
};
node_desc NODE_CallPureFunction = {
    .access = &control_access,
    .name = "Call Pure Function",
    .input_count = 0,
    .output_count = 0,
    .pure_kind = NODE_PURE_FUNCTION,
    .display_class = NODE_DISPLAY_FUNCTION,
    .description = "Evaluates a pure function graph whenever its outputs are "
                   "read.",
};
node_desc NODE_BehaviorExpression = {
    .access = &control_access,
    .name = "Behavior Expression",
    .input_count = 0,
    .output_count = 0,
    .pure_kind = NODE_PURE_FUNCTION,
    .display_class = NODE_DISPLAY_FUNCTION,
    .category = "Math",
    .keywords = "behavior expression formula assignment condition branch",
    .description = "Evaluates a formula as a value, an action run on Exec, or "
                   "a branch that fires True or False from Result.",
};
node_desc NODE_CallMacro = {
    .access = &control_access,
    .name = "Call Macro",
    .input_count = 1,
    .output_count = 1,
    .inputs = macro_call_inputs,
    .outputs = macro_call_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FUNCTION,
    .description = "Runs a macro graph from the entry matching the triggered "
                   "input and continues at the output its Return picks.",
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
