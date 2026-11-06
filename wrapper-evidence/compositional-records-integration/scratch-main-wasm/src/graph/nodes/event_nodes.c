#include "graph/document/events.h"

static pin_spec CALL_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC } };
static pin_spec CALL_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
node_desc NODE_CallEvent = {
    .name = "Call Event",
    .inputs = CALL_IN,
    .input_count = 1,
    .outputs = CALL_OUT,
    .output_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .display_class = NODE_DISPLAY_EVENT,
    .category = "Events",
    .keywords = "local custom event invoke",
    .description = "Queues one of this behavior's own events with the payload "
                   "inputs, then continues.",
};
static node_desc *const EVENT_NODES[] = { &NODE_CallEvent };
const registry_pack CORE_EVENT_PACK = { .name = "core.events",
                                        .nodes = EVENT_NODES,
                                        .node_count = 1 };
