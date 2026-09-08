#include "graph/nodes/struct_nodes.h"

#include "graph/model/registry.h"
#include "graph/runtime/flow.h"

static pin_spec set_members_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Struct", PIN_STRUCT, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_STRUCT }, 0, PIN_FLAG_REFERENCE, 0 },
};
static pin_spec set_members_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};

static int flow_set_members(node *n, flow_context *ctx)
{
    (void)n;
    return flow_context_continue(ctx, 0);
}

node_desc NODE_SetMembers = {
    .name = "Set Members in Struct",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_members_inputs,
    .outputs = set_members_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_set_members,
    .display_class = NODE_DISPLAY_FUNCTION,
    .category = "Struct",
    .keywords = "struct set members split reference",
};

node_desc *const STRUCT_NODES[] = { &NODE_SetMembers };
const int STRUCT_NODE_COUNT = sizeof STRUCT_NODES / sizeof STRUCT_NODES[0];

const registry_pack CORE_STRUCT_PACK = {
    .name = "core.struct",
    .nodes = STRUCT_NODES,
    .node_count = sizeof STRUCT_NODES / sizeof STRUCT_NODES[0],
};
