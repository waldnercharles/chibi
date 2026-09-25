#include "graph/document/events.h"

#include "graph/runtime/instance.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

static const pin_spec TICK_PAYLOAD[] = {
    { .name = "DeltaSeconds", .type = PIN_F32, .type_name = "float" },
};
static const pin_spec INPUT_PAYLOAD[] = {
    { .name = "Direction", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static const pin_spec CONTACT_PAYLOAD[] = {
    { .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
};

const graph_host_event_desc GRAPH_HOST_EVENTS[GRAPH_HOST_EVENT_COUNT] = {
    [GRAPH_HOST_EVENT_BEGIN] = { "Begin", "start spawn create init ready" },
    [GRAPH_HOST_EVENT_END] = { "End", "stop finish despawn" },
    [GRAPH_HOST_EVENT_TICK] = { "Tick", "update frame every loop delta time", TICK_PAYLOAD, 1 },
    [GRAPH_HOST_EVENT_FIRE] = { "Fire", "shoot attack weapon" },
    [GRAPH_HOST_EVENT_DASH] = { "Dash", "dodge boost" },
    [GRAPH_HOST_EVENT_INPUT] = { "Input", "move direction stick controls player", INPUT_PAYLOAD, 1 },
    [GRAPH_HOST_EVENT_CONTACT_BEGIN] = { "Contact Begin", "hit collide collision touch overlap enter", CONTACT_PAYLOAD, 2 },
};

const char *graph_host_event_name(graph_host_event event)
{
    assert(event >= 0 && event < GRAPH_HOST_EVENT_COUNT);
    return GRAPH_HOST_EVENTS[event].name;
}

bool graph_host_event_is(const char *name, graph_host_event event)
{
    return name && !strcmp(name, graph_host_event_name(event));
}

bool graph_event_host_name(const char *name)
{
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        if (graph_host_event_is(name, i)) {
            return true;
        }
    }
    return false;
}

int graph_host_event_apply(graph *g, node *n)
{
    assert(n->desc == &NODE_Event);
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        const graph_host_event_desc *event = &GRAPH_HOST_EVENTS[i];
        if (!graph_host_event_is(n->reference_name, i)) {
            continue;
        }
        /* Then precedes the payload. */
        for (int p = n->output_count - 1; p < event->payload_count; p++) {
            if (graph_add_pin(g, n, 1, &event->payload[p]) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

static bool receiver(const node *n)
{
    return n->desc->flow_kind == NODE_FLOW_EVENT;
}

static bool field_matches(const pin_spec *pin, const behavior_variable *field)
{
    return pin->type == field->type && pin->container == field->container &&
           ((!pin->type_name && !field->type_name) ||
            (pin->type_name && field->type_name &&
             !strcmp(pin->type_name, field->type_name)));
}

static int field_index(const node *n, uint64_t id)
{
    const pin_spec *pins = receiver(n) ? n->output_pins : n->input_pins;
    int count = receiver(n) ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].declaration_id == id) {
            return i;
        }
    }
    return -1;
}

bool graph_event_matches(const node *n, const behavior_asset *definition)
{
    if (!definition || definition->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION ||
        graph_event_host_name(definition->name) || !n->reference_name ||
        strcmp(n->reference_name, definition->name)) {
        return false;
    }
    int first = receiver(n) || n->desc == &NODE_CallEvent ? 1 : 2;
    int count = 0;
    const pin_spec *pins = receiver(n) ? n->output_pins : n->input_pins;
    int total = receiver(n) ? n->output_count : n->input_count;
    for (int i = 0; i < total; i++) {
        count += pins[i].declaration_id != 0;
    }
    if (count != acount(definition->variables) || total != first + count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const behavior_variable *field = &definition->variables[i];
        int at = field_index(n, field->id);
        if (at != first + i || !field_matches(&pins[at], field) ||
            strcmp(pins[at].name, field->name)) {
            return false;
        }
    }
    return true;
}

int graph_event_apply(graph *g, node *n, const behavior_asset *definition)
{
    if (!definition || definition->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION ||
        graph_event_host_name(definition->name) || g->flow_pause_count) {
        return -1;
    }
    bool output = receiver(n);
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int total = output ? n->output_count : n->input_count;
    int first = output || n->desc == &NODE_CallEvent ? 1 : 2;
    for (int i = first; i < total; i++) {
        const behavior_variable
            *field = behavior_asset_find_variable_by_id(definition, pins[i].declaration_id);
        if (field && field_matches(&pins[i], field)) {
            continue;
        }
        for (int j = 0; j < acount(g->edges); j++) {
            const edge *e = &g->edges[j];
            if ((output && e->from_id == n->id && e->from_pin_id == pins[i].id) ||
                (!output && e->to_id == n->id && e->to_pin_id == pins[i].id)) {
                return -1;
            }
        }
    }
    for (int i = total - 1; i >= first; i--) {
        pins = output ? n->output_pins : n->input_pins;
        if (!pins[i].declaration_id ||
            !behavior_asset_find_variable_by_id(definition, pins[i].declaration_id)) {
            graph_remove_pin(g, n, output, i);
        }
    }
    for (int i = 0; i < acount(definition->variables); i++) {
        const behavior_variable *field = &definition->variables[i];
        int at = field_index(n, field->id);
        if (at < 0) {
            pin_spec pin = { .name = field->name,
                             .type = field->type,
                             .type_name = field->type_name,
                             .container = field->container,
                             .default_value = field->default_value,
                             .declaration_id = field->id };
            at = graph_add_pin(g, n, output, &pin);
            graph_set_pin_type(g, n, output, at, field->type, field->type_name);
            graph_set_pin_default(g, n, output, at, &field->default_value);
        } else {
            graph_set_pin_name(g, n, output, at, field->name);
            pins = output ? n->output_pins : n->input_pins;
            if (!field_matches(&pins[at], field)) {
                graph_set_pin_type(g, n, output, at, field->type, field->type_name);
                graph_set_pin_container(g, n, output, at, field->container);
                graph_set_pin_default(g, n, output, at, &field->default_value);
            }
        }
        if (at != first + i) {
            graph_move_pin(g, n, output, at, first + i);
        }
    }
    return graph_set_node_reference(g, n, n->reference_id, definition->name);
}

static int call_event(node *n, flow_context *ctx)
{
    behavior_instance *instance = flow_context_instance(ctx);
    if (!instance || !n->reference_name) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Choose a local event");
    }
    node *entry = NULL;
    for (int i = 0; i < acount(instance->asset->graph->nodes); i++) {
        node *candidate = instance->asset->graph->nodes[i];
        if (receiver(candidate) && candidate->reference_name &&
            !strcmp(candidate->reference_name, n->reference_name)) {
            if (entry) {
                return flow_context_fail(ctx, FLOW_ERR_NODE, "Duplicate local event");
            }
            entry = candidate;
        }
    }
    if (!entry) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Local event is missing");
    }
    int count = n->input_count - 1;
    pin_value *values = flow_context_scratch_alloc(ctx, sizeof *values * (size_t)(count + 1));
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    flow_error error = { 0 };
    if (!behavior_instance_enqueue(instance, entry, (flow_inputs){ .values = values, .count = count }, &error)) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Local event payload does not match its receiver");
    }
    return flow_context_continue(ctx, 0);
}

static pin_spec CALL_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC } };
static pin_spec CALL_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
node_desc NODE_CallEvent = {
    .name = "Call Event",
    .inputs = CALL_IN,
    .input_count = 1,
    .outputs = CALL_OUT,
    .output_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = call_event,
    .display_class = NODE_DISPLAY_EVENT,
    .category = "Events",
    .keywords = "local custom event invoke",
};
static node_desc *const EVENT_NODES[] = { &NODE_CallEvent };
const registry_pack CORE_EVENT_PACK = { .name = "core.events",
                                        .nodes = EVENT_NODES,
                                        .node_count = 1 };
