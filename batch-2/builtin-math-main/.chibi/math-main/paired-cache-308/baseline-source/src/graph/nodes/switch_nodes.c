#include "graph/model/native_definition.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/selection.h"
#include "runtime/selection_definition.h"

static pin_spec switch_int_inputs[] = { NATIVE_SWITCH_INT_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec switch_string_inputs[] = { NATIVE_SWITCH_STRING_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec switch_name_inputs[] = { NATIVE_SWITCH_NAME_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec switch_outputs[] = { NATIVE_SWITCH_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec select_inputs[] = { NATIVE_SELECT_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec select_outputs[] = { NATIVE_SELECT_OUTPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_SwitchInt = {
    .name = NATIVE_SWITCH_INT_NAME,
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_int_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .host_call = &NATIVE_SWITCH_INT_CALL,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_SWITCH_Int_PRESENTATION,

};
node_desc NODE_SwitchString = {
    .name = NATIVE_SWITCH_STRING_NAME,
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_string_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .host_call = &NATIVE_SWITCH_TEXT_CALL,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_SWITCH_String_PRESENTATION,

};
node_desc NODE_SwitchName = {
    .name = NATIVE_SWITCH_NAME_NAME,
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_name_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .host_call = &NATIVE_SWITCH_TEXT_CALL,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_SWITCH_Name_PRESENTATION,

};
node_desc NODE_SwitchEnum = {
    .name = NATIVE_SWITCH_ENUM_NAME,
    .input_count = 2,
    .output_count = 3,
    .inputs = switch_int_inputs,
    .outputs = switch_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .host_call = &NATIVE_SWITCH_INT_CALL,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_SWITCH_Enum_PRESENTATION,

};
node_desc NODE_Select = {
    .name = NATIVE_SELECT_NAME,
    .input_count = 3,
    .output_count = 1,
    .inputs = select_inputs,
    .outputs = select_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_SELECT_CALL,
    .access = &NATIVE_SELECTION_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    NATIVE_SELECT_PRESENTATION,

};

static node_desc *const SWITCH_NODES[] = {
    &NODE_SwitchInt,  &NODE_SwitchString, &NODE_SwitchName,
    &NODE_SwitchEnum, &NODE_Select,
};

const registry_pack CORE_SWITCH_PACK = {
    .name = "core.switch",
    .nodes = SWITCH_NODES,
    .node_count = sizeof SWITCH_NODES / sizeof SWITCH_NODES[0],
};
