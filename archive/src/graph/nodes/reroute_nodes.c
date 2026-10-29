#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "types/array.h"

static pin_spec data_reroute_inputs[] = {
    { .name = "In",
      .type = PIN_FLOW,
      .kind = PIN_DATA,
      .container = PIN_CONTAINER_SINGLE,
      .default_value = { .type = PIN_FLOW },
      .flags = PIN_FLAG_WILDCARD,
      .group = 1 },
};
static pin_spec data_reroute_outputs[] = {
    { .name = "Out",
      .type = PIN_FLOW,
      .kind = PIN_DATA,
      .container = PIN_CONTAINER_SINGLE,
      .default_value = { .type = PIN_FLOW },
      .flags = PIN_FLAG_WILDCARD,
      .group = 1 },
};
static pin_spec exec_reroute_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static pin_spec exec_reroute_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};

static pin_value reroute_value(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    return inputs[0];
}

node_desc NODE_RerouteData = {
    .name = "Reroute Data",
    .input_count = 1,
    .output_count = 1,
    .inputs = data_reroute_inputs,
    .outputs = data_reroute_outputs,
    .pure_kind = NODE_PURE_COPY,
    .fn = reroute_value,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Utilities",
    .keywords = "reroute wire data",
    .description = "Passes its value through unchanged, to route a data wire.",
};
node_desc NODE_RerouteExec = {
    .name = "Reroute Exec",
    .input_count = 1,
    .output_count = 1,
    .inputs = exec_reroute_inputs,
    .outputs = exec_reroute_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Utilities",
    .keywords = "reroute wire exec",
    .description = "Continues at once, to route an execution wire.",
};

static pin_spec snapshot_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Value",
      .type = PIN_FLOW,
      .kind = PIN_DATA,
      .container = PIN_CONTAINER_SINGLE,
      .default_value = { .type = PIN_FLOW },
      .flags = PIN_FLAG_WILDCARD,
      .group = 1 },
};
static pin_spec snapshot_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Value",
      .type = PIN_FLOW,
      .kind = PIN_DATA,
      .container = PIN_CONTAINER_SINGLE,
      .default_value = { .type = PIN_FLOW },
      .flags = PIN_FLAG_WILDCARD,
      .group = 1 },
};
static const node_access snapshot_access = { 0 };

node_desc NODE_Snapshot = {
    .name = "Snapshot",
    .input_count = 2,
    .output_count = 2,
    .inputs = snapshot_inputs,
    .outputs = snapshot_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .access = &snapshot_access,
    .display_class = NODE_DISPLAY_FUNCTION,
    .category = "Variables",
    .keywords = "local value capture snapshot",
    .description = "Evaluates and retains a local value for this invocation.",
};

static node_desc *const REROUTE_NODES[] = { &NODE_RerouteData,
                                            &NODE_RerouteExec,
                                            &NODE_Snapshot };

const registry_pack CORE_REROUTE_PACK = {
    .name = "core.reroute",
    .nodes = REROUTE_NODES,
    .node_count = sizeof REROUTE_NODES / sizeof REROUTE_NODES[0],
};
