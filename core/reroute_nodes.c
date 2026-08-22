#include "reroute_nodes.h"

#include "flow.h"

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
    .pure_kind = NODE_PURE_CALL,
    .fn = reroute_value,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Utilities",
    .keywords = "reroute wire data",
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
};

node_desc *const REROUTE_NODES[] = { &NODE_RerouteData, &NODE_RerouteExec };
const int REROUTE_NODE_COUNT = sizeof REROUTE_NODES / sizeof REROUTE_NODES[0];
