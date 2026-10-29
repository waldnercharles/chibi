#include "graph/document/events.h"

#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

pin_spec graph_event_payload_pin(const event_field *field)
{
    return (pin_spec){ .name = field->name,
                       .type = field->type,
                       .type_name = field->type_name,
                       .container = field->container };
}

int graph_host_event_apply(graph *g, node *n)
{
    assert(n->desc == &NODE_Event);
    for (int i = 0; i < HOST_EVENT_COUNT; i++) {
        const host_event_desc *event = &HOST_EVENTS[i];
        if (!host_event_is(n->reference_name, i)) {
            continue;
        }
        /* Then precedes the payload. */
        for (int p = n->output_count - 1; p < event->payload_count; p++) {
            pin_spec pin = graph_event_payload_pin(&event->payload[p]);
            if (graph_add_pin(g, n, 1, &pin) < 0) {
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
    /* A number's type names it; only named types compare their names. */
    bool named = pin->type == PIN_STRUCT || pin->type == PIN_ENUM ||
                 pin->type == PIN_HANDLE;
    return pin->type == field->type && pin->container == field->container &&
           (!named || (!pin->type_name && !field->type_name) ||
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
        host_event_reserved(definition->name) || !n->reference_name ||
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
        host_event_reserved(definition->name) || g->flow_pause_count) {
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
