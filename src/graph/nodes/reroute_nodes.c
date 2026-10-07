#include "graph/model/registry.h"
#include "graph/nodes/snapshot_nodes.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/value_array.h"

static pin_spec data_reroute_inputs[] = {
    { "In", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
};
static pin_spec data_reroute_outputs[] = {
    { "Out", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
};
static pin_spec exec_reroute_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec exec_reroute_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};

static pin_value reroute_value(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    return inputs[0];
}

static int reroute_exec(node *n, flow_context *ctx)
{
    (void)n;
    return flow_context_continue(ctx, 0);
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
    .flow = reroute_exec,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Utilities",
    .keywords = "reroute wire exec",
    .description = "Continues at once, to route an execution wire.",
};

static pin_spec snapshot_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
};
static pin_spec snapshot_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 },
};
static const node_access snapshot_access = { 0 };

static int snapshot_value(node *n, flow_context *ctx)
{
    (void)n;
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    if (pin_value_snapshot_to_arena(flow_context_scratch(ctx), flow_context_output_value(ctx, 1), &value)) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Cannot retain local value");
    }
    return flow_context_continue(ctx, 0);
}

node_desc NODE_Snapshot = {
    .name = "Snapshot",
    .input_count = 2,
    .output_count = 2,
    .inputs = snapshot_inputs,
    .outputs = snapshot_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = snapshot_value,
    .lower = behavior_lower_snapshot,
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
