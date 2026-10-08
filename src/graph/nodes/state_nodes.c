#include "graph/nodes/state_nodes.h"

#include "graph/model/native_definition.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/state_definition.h"

#define PORTS(KEY, NAME, SOURCE, STATE, INPUTS, OUTPUTS)                       \
    static pin_spec KEY##_inputs[] = { INPUTS(GRAPH_NATIVE_PORT) };            \
    static pin_spec KEY##_outputs[] = { OUTPUTS(GRAPH_NATIVE_PORT) };
NATIVE_STATE_OPERATIONS(PORTS)
#undef PORTS

node_desc NODE_DoOnce = {
    .name = NATIVE_STATE_ONCE_NAME,
    .input_count = sizeof once_inputs / sizeof once_inputs[0],
    .output_count = sizeof once_outputs / sizeof once_outputs[0],
    .inputs = once_inputs,
    .outputs = once_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_STATE_once_PRESENTATION,

};
node_desc NODE_DoN = {
    .name = NATIVE_STATE_COUNT_NAME,
    .input_count = sizeof count_inputs / sizeof count_inputs[0],
    .output_count = sizeof count_outputs / sizeof count_outputs[0],
    .inputs = count_inputs,
    .outputs = count_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_STATE_count_PRESENTATION,

};
node_desc NODE_FlipFlop = {
    .name = NATIVE_STATE_FLIP_NAME,
    .input_count = sizeof flip_inputs / sizeof flip_inputs[0],
    .output_count = sizeof flip_outputs / sizeof flip_outputs[0],
    .inputs = flip_inputs,
    .outputs = flip_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_STATE_flip_PRESENTATION,

};
node_desc NODE_Gate = {
    .name = NATIVE_STATE_GATE_NAME,
    .input_count = sizeof gate_inputs / sizeof gate_inputs[0],
    .output_count = sizeof gate_outputs / sizeof gate_outputs[0],
    .inputs = gate_inputs,
    .outputs = gate_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_STATE_gate_PRESENTATION,

};
node_desc NODE_MultiGate = {
    .name = NATIVE_STATE_MULTI_NAME,
    .input_count = sizeof multi_inputs / sizeof multi_inputs[0],
    .output_count = sizeof multi_outputs / sizeof multi_outputs[0],
    .inputs = multi_inputs,
    .outputs = multi_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_STATE_multi_PRESENTATION,

};
node_desc NODE_RetriggerableDelay = {
    .name = NATIVE_STATE_RETRIGGER_NAME,
    .input_count = sizeof retrigger_inputs / sizeof retrigger_inputs[0],
    .output_count = sizeof retrigger_outputs / sizeof retrigger_outputs[0],
    .inputs = retrigger_inputs,
    .outputs = retrigger_outputs,
    .flow_kind = NODE_FLOW_DELAY,
    .latent = 1,
    .display_class = NODE_DISPLAY_FLOW,
    NATIVE_STATE_retrigger_PRESENTATION,

};

node_desc *const STATE_NODES[] = {
    &NODE_DoOnce, &NODE_DoN,       &NODE_FlipFlop,
    &NODE_Gate,   &NODE_MultiGate, &NODE_RetriggerableDelay,
};
const int STATE_NODE_COUNT = sizeof STATE_NODES / sizeof STATE_NODES[0];

const registry_pack CORE_STATE_PACK = {
    .name = "core.state",
    .nodes = STATE_NODES,
    .node_count = sizeof STATE_NODES / sizeof STATE_NODES[0],
};
