#include "graph/nodes/set_map_nodes.h"

#include "graph/model/native_definition.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/sets.h"
#define PORTS(KEY, NAME, SOURCE, INPUTS, OUTPUTS, VARIADIC)                    \
    static pin_spec KEY##_inputs[] = { INPUTS(GRAPH_NATIVE_PORT) };            \
    static pin_spec KEY##_outputs[] = { OUTPUTS(GRAPH_NATIVE_PORT) };
NATIVE_SET_OPERATIONS(PORTS)
#undef PORTS

node_desc NODE_MakeSet = {
    .work = NODE_WORK_SET,
    .name = NATIVE_SET_MAKE_NAME,
    .source_name = NATIVE_SET_MAKE_SOURCE,
    .input_count = sizeof make_inputs / sizeof make_inputs[0],
    .output_count = sizeof make_outputs / sizeof make_outputs[0],
    .inputs = make_inputs,
    .outputs = make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_SET_make_CALL,
    .host_call = &NATIVE_SET_make_HOST,
    .access = &NATIVE_SET_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    NATIVE_SET_make_PRESENTATION,

};
node_desc NODE_SetAdd = {
    .work = NODE_WORK_SET_COPY,
    .name = NATIVE_SET_ADD_NAME,
    .source_name = NATIVE_SET_ADD_SOURCE,
    .input_count = sizeof add_inputs / sizeof add_inputs[0],
    .output_count = sizeof add_outputs / sizeof add_outputs[0],
    .inputs = add_inputs,
    .outputs = add_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_SET_add_CALL,
    .host_call = &NATIVE_SET_add_HOST,
    .access = &NATIVE_SET_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    NATIVE_SET_add_PRESENTATION,

};
node_desc NODE_SetRemove = {
    .work = NODE_WORK_SET_COPY,
    .name = NATIVE_SET_REMOVE_NAME,
    .source_name = NATIVE_SET_REMOVE_SOURCE,
    .input_count = sizeof remove_inputs / sizeof remove_inputs[0],
    .output_count = sizeof remove_outputs / sizeof remove_outputs[0],
    .inputs = remove_inputs,
    .outputs = remove_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_SET_remove_CALL,
    .host_call = &NATIVE_SET_remove_HOST,
    .access = &NATIVE_SET_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    NATIVE_SET_remove_PRESENTATION,

};
node_desc NODE_SetContains = {
    .work = NODE_WORK_SET,
    .name = NATIVE_SET_CONTAINS_NAME,
    .source_name = NATIVE_SET_CONTAINS_SOURCE,
    .input_count = sizeof contains_inputs / sizeof contains_inputs[0],
    .output_count = sizeof contains_outputs / sizeof contains_outputs[0],
    .inputs = contains_inputs,
    .outputs = contains_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_SET_contains_CALL,
    .host_call = &NATIVE_SET_contains_HOST,
    .access = &NATIVE_SET_ACCESS,
    .display_class = NODE_DISPLAY_PURE,
    NATIVE_SET_contains_PRESENTATION,

};
node_desc NODE_SetLength = {
    .name = NATIVE_SET_LENGTH_NAME,
    .source_name = NATIVE_SET_LENGTH_SOURCE,
    .input_count = sizeof length_inputs / sizeof length_inputs[0],
    .output_count = sizeof length_outputs / sizeof length_outputs[0],
    .inputs = length_inputs,
    .outputs = length_outputs,
    .pure_kind = NODE_PURE_CALL,
    .execution_call = &NATIVE_SET_length_CALL,
    .display_class = NODE_DISPLAY_PURE,
    NATIVE_SET_length_PRESENTATION,

};

static node_desc *const SET_MAP_NODES[] = { &NODE_MakeSet,
                                            &NODE_SetAdd,
                                            &NODE_SetRemove,
                                            &NODE_SetContains,
                                            &NODE_SetLength };

const registry_pack CORE_SET_MAP_PACK = {
    .name = "core.set_map",
    .nodes = SET_MAP_NODES,
    .node_count = sizeof SET_MAP_NODES / sizeof SET_MAP_NODES[0],
};
