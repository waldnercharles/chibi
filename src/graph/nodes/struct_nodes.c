#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"

static pin_spec set_members_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Struct",
      .type = PIN_STRUCT,
      .kind = PIN_DATA,
      .container = PIN_CONTAINER_SINGLE,
      .default_value = { .type = PIN_STRUCT },
      .flags = PIN_FLAG_REFERENCE | PIN_FLAG_WILDCARD },
};
static pin_spec set_members_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};

node_desc NODE_SetMembers = {
    .name = "Set Members in Struct",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_members_inputs,
    .outputs = set_members_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_FUNCTION,
    .category = "Struct",
    .keywords = "struct set members split reference",
    .description = "Writes the member inputs it shows into the struct Struct "
                   "refers to.",
};

static node_desc *const STRUCT_NODES[] = { &NODE_SetMembers };

const registry_pack CORE_STRUCT_PACK = {
    .name = "core.struct",
    .nodes = STRUCT_NODES,
    .node_count = sizeof STRUCT_NODES / sizeof STRUCT_NODES[0],
};
