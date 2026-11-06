#include "graph/document/extract.h"

#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float TUNNEL_GAP = 260; /* graph units beside the copied nodes */

static bool selected(node *const *selection, int count, uint64_t id)
{
    for (int i = 0; i < count; i++) {
        if (selection[i]->id == id) {
            return true;
        }
    }
    return false;
}

static node **expand_inline_selection(const graph *g, node *const *selection, int count, int *expanded_count)
{
    int capacity = count + acount(g->nodes);
    node **expanded = xmalloc((size_t)(capacity > 0 ? capacity : 1) * sizeof *expanded);
    int total = count;
    for (int i = 0; i < count; i++) {
        expanded[i] = selection[i];
    }
    for (int i = 0; i < total; i++) {
        node *target = expanded[i];
        if (!target || graph_node_by_id(g, target->id) != target) {
            continue;
        }
        for (int pin = 0; pin < target->input_count; pin++) {
            node *source = NULL;
            int source_pin = -1;
            if (!graph_inline_source(g, target, pin, &source, &source_pin) ||
                selected(expanded, total, source->id)) {
                continue;
            }
            assert(total < capacity);
            expanded[total++] = source;
        }
    }
    *expanded_count = total;
    return expanded;
}

static bool has_split(const node *n)
{
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        int count = output ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            if (pins[i].split_parent) {
                return true;
            }
        }
    }
    return false;
}

static bool reenters_selection(const graph *g, node *const *selection, int count)
{
    uint64_t *pending = NULL;
    uint64_t *visited = NULL;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_DATA && selected(selection, count, e->from_id) &&
            !selected(selection, count, e->to_id)) {
            apush(pending, e->to_id);
        }
    }
    bool reenters = false;
    while (acount(pending) && !reenters) {
        uint64_t id = apop(pending);
        bool seen = false;
        for (int i = 0; i < acount(visited); i++) {
            seen |= visited[i] == id;
        }
        if (seen) {
            continue;
        }
        apush(visited, id);
        for (int i = 0; i < acount(g->edges); i++) {
            const edge *e = &g->edges[i];
            if (e->kind != GRAPH_EDGE_DATA || e->from_id != id) {
                continue;
            }
            if (selected(selection, count, e->to_id)) {
                reenters = true;
                break;
            }
            apush(pending, e->to_id);
        }
    }
    afree(pending);
    afree(visited);
    return reenters;
}

static bool executable(node *const *selection, int count)
{
    for (int i = 0; i < count; i++) {
        if (selection[i] && selection[i]->desc->flow_kind != NODE_FLOW_NONE) {
            return true;
        }
    }
    return false;
}

/* A single chain has an unambiguous completion and preserves execution order. */
static const char *execution_boundary(const graph *g, node *const *selection, int count)
{
    int entries = 0, exits = 0, steps = 0;
    uint64_t first = 0;
    for (int i = 0; i < count; i++) {
        const node *n = selection[i];
        if (n->desc->flow_kind == NODE_FLOW_NONE) {
            continue;
        }
        int inputs = 0, outputs = 0;
        for (int p = 0; p < n->input_count; p++) {
            inputs += n->input_pins[p].kind == PIN_EXEC;
        }
        for (int p = 0; p < n->output_count; p++) {
            outputs += n->output_pins[p].kind == PIN_EXEC;
        }
        if (inputs != 1 || outputs != 1) {
            return "Select a single execution chain; events, branches and "
                   "loops cannot be extracted yet.";
        }
        steps++;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC) {
            continue;
        }
        bool from = selected(selection, count, e->from_id);
        bool to = selected(selection, count, e->to_id);
        if (!from && to) {
            entries++;
            first = e->to_id;
        }
        if (from && !to) {
            exits++;
        }
    }
    if (entries != 1 || exits != 1) {
        return "Select one connected execution chain with one incoming and one "
               "outgoing execution wire.";
    }
    int reached = 0;
    while (selected(selection, count, first) && reached <= steps) {
        reached++;
        int outgoing = 0;
        uint64_t next = 0;
        for (int i = 0; i < acount(g->edges); i++) {
            const edge *e = &g->edges[i];
            if (e->kind == GRAPH_EDGE_EXEC && e->from_id == first) {
                outgoing++;
                next = e->to_id;
            }
        }
        if (outgoing != 1) {
            break;
        }
        first = next;
    }
    if (reached != steps || selected(selection, count, first)) {
        return "Select a connected execution chain without cycles or "
               "disconnected execution nodes.";
    }
    return NULL;
}

/* A macro takes any execution wires: each inside pin a wire enters or leaves
   by becomes one of its tunnel pins (Unreal's Collapse to Macro). */
static const char *macro_boundary(const graph *g, node *const *selection, int count)
{
    for (int i = 0; i < count; i++) {
        const node *n = selection[i];
        if (node_behavior_for(n) == NODE_BEHAVIOR_ENTRY || n->desc == &NODE_Return) {
            return "Leave events, Function Entry and Return out.";
        }
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_EXEC && !selected(selection, count, e->from_id) &&
            selected(selection, count, e->to_id)) {
            return NULL;
        }
    }
    return "Connect an execution wire into the selection first.";
}

static const char *can_extract_selection(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    graph_kind kind
)
{
    if (!asset || !source || source->owner != asset ||
        behavior_asset_find_graph(asset, source->graph_id) != source) {
        return "Choose nodes in a behavior graph.";
    }
    if (count <= 0 || !selection) {
        return "Select calculations or a connected execution chain to extract.";
    }
    bool exec = executable(selection, count);
    if (kind == GRAPH_MACRO && !exec) {
        return "Select nodes with execution wires to collapse to a macro.";
    }
    for (int i = 0; i < count; i++) {
        const node *n = selection[i];
        if (!n || graph_node_by_id(source, n->id) != n) {
            return "The selection contains a node from another graph.";
        }
        for (int j = 0; j < i; j++) {
            if (selection[j] == n) {
                return "The selection contains the same node twice.";
            }
        }
        bool inline_source = graph_node_is_inline(source, n);
        if (n->disabled ||
            (!exec && !inline_source &&
             (n->desc->flow_kind != NODE_FLOW_NONE || n->desc->latent ||
              n->desc->pure_kind == NODE_PURE_GET || n->desc->pure_kind == NODE_PURE_SELF ||
              n->desc->pure_kind == NODE_PURE_READ || n->desc->pure_kind == NODE_PURE_FUNCTION ||
              n->desc->pure_kind == NODE_PURE_VOID_CALL ||
              (n->desc->access && (n->desc->access->flags & ~NODE_EFFECT_PURE))))) {
            return "Select pure calculations. Keep events, state reads and "
                   "function calls outside the selection.";
        }
        if (has_split(n)) {
            return "Recombine split pins first.";
        }
    }
    bool output = false;
    for (int i = 0; i < acount(source->edges); i++) {
        const edge *e = &source->edges[i];
        output |= e->kind == GRAPH_EDGE_DATA &&
                  selected(selection, count, e->from_id) &&
                  !selected(selection, count, e->to_id);
    }
    if (exec) {
        const char *error = kind == GRAPH_MACRO
                              ? macro_boundary(source, selection, count)
                              : execution_boundary(source, selection, count);
        if (error) {
            return error;
        }
    }
    if (!exec && !output) {
        return "Connect a result to a node outside the selection first.";
    }
    if (reenters_selection(source, selection, count)) {
        return "Include the calculations between the selected nodes to avoid a "
               "circular function call.";
    }
    return NULL;
}

static const char *can_extract(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    graph_kind kind
)
{
    if (!asset || !source || source->owner != asset ||
        behavior_asset_find_graph(asset, source->graph_id) != source ||
        count <= 0 || !selection) {
        return can_extract_selection(asset, source, selection, count, kind);
    }
    int expanded_count = count;
    node **expanded = expand_inline_selection(source, selection, count, &expanded_count);
    const char *error = can_extract_selection(asset, source, expanded, expanded_count, kind);
    free(expanded);
    return error;
}

const char *behavior_asset_can_extract_function(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
)
{
    return can_extract(asset, source, selection, count, GRAPH_FUNCTION);
}

const char *behavior_asset_can_extract_macro(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
)
{
    return can_extract(asset, source, selection, count, GRAPH_MACRO);
}

typedef struct boundary_port
{
    uint64_t node_id;
    uint64_t pin_id;
    int index;
} boundary_port;

typedef struct binding_port
{
    uint64_t node_id;
    uint64_t pin_id;
    int index;
} binding_port;

static int same_binding(
    const pin_binding *left,
    const pin_binding *right,
    const pin_spec *expected_left,
    const pin_spec *expected_right
)
{
    return left && right && left->kind == right->kind &&
           left->variable_id == right->variable_id &&
           left->local_id == right->local_id &&
           left->event_node_id == right->event_node_id &&
           left->event_pin_id == right->event_pin_id &&
           !strcmp(
               left->source_asset ? left->source_asset : "",
               right->source_asset ? right->source_asset : ""
           ) &&
           !strcmp(
               left->component_key ? left->component_key : "",
               right->component_key ? right->component_key : ""
           ) &&
           !strcmp(
               left->field_name ? left->field_name : "",
               right->field_name ? right->field_name : ""
           ) &&
           expected_left->type == expected_right->type &&
           expected_left->container == expected_right->container &&
           !strcmp(
               expected_left->type_name ? expected_left->type_name : "",
               expected_right->type_name ? expected_right->type_name : ""
           );
}

static bool same_optional_binding(
    const pin_binding *left,
    const pin_binding *right,
    const pin_spec *expected_left,
    const pin_spec *expected_right
)
{
    if (left && left->kind == PIN_BINDING_LOCAL) {
        left = NULL;
    }
    if (right && right->kind == PIN_BINDING_LOCAL) {
        right = NULL;
    }
    if (!left || !right) {
        return !left && !right;
    }
    return same_binding(left, right, expected_left, expected_right);
}

static int port_of(const boundary_port *ports, uint64_t node_id, uint64_t pin_id)
{
    for (int i = 0; i < acount(ports); i++) {
        if (ports[i].node_id == node_id && ports[i].pin_id == pin_id) {
            return i;
        }
    }
    return -1;
}

static int port_index(const boundary_port *ports, const edge *e)
{
    return port_of(ports, e->from_id, e->from_pin_id);
}

static bool generic_label(const char *name)
{
    return !name || !name[0] || strcmp(name, "A") == 0 || strcmp(name, "B") == 0 ||
           strcmp(name, "Value") == 0 || strcmp(name, "Result") == 0;
}

static const char *input_label(const pin_spec *producer, const pin_spec *consumer)
{
    if (generic_label(consumer->name) && !generic_label(producer->name)) {
        return producer->name;
    }
    return consumer->name;
}

static int add_port(graph *g, node *boundary, int output, const pin_spec *source, const char *label)
{
    char name[256];
    const char *base = label && label[0] ? label : "Value";
    snprintf(name, sizeof name, "%s", base);
    int suffix = 2;
    for (;;) {
        bool duplicate = false;
        pin_spec *pins = output ? boundary->output_pins : boundary->input_pins;
        int count = output ? boundary->output_count : boundary->input_count;
        for (int i = 0; i < count; i++) {
            duplicate |= strcmp(pins[i].name, name) == 0;
        }
        if (!duplicate) {
            break;
        }
        snprintf(name, sizeof name, "%.230s %d", base, suffix++);
    }
    pin_spec pin = *source;
    pin.id = 0;
    pin.name = name;
    pin.group = 0;
    pin.flags &= ~PIN_FLAG_WILDCARD;
    pin.split_parent = 0;
    pin.split_type_name = NULL;
    pin.field_source = 0;
    pin.field_name = NULL;
    pin.binding = NULL;
    return graph_add_pin(g, boundary, output, &pin);
}

/* The tunnel pin for execution crossing the boundary at an inside pin; the
   first on each side is the pin Function Entry or Return starts with. Macro
   tunnels take the inside pin's name (Unreal). */
static void exec_tunnel(
    graph *made,
    node *tunnel,
    bool entry,
    boundary_port **ports,
    uint64_t node_id,
    const pin_spec *inside,
    bool named
)
{
    if (port_of(*ports, node_id, inside->id) >= 0) {
        return;
    }
    int index = 0;
    if (acount(*ports)) {
        index = add_port(made, tunnel, entry, inside, inside->name);
        assert(index >= 0);
    } else if (named) {
        int renamed = graph_set_pin_name(made, tunnel, entry, 0, inside->name);
        assert(renamed == 0);
        (void)renamed;
    }
    apush(*ports, ((boundary_port){ node_id, inside->id, index }));
}

static int compare_node_ids(const void *a, const void *b)
{
    uint64_t x = (*(node *const *)a)->id, y = (*(node *const *)b)->id;
    return x < y ? -1 : x > y;
}

static node *copied_node(graph *function, node **ordered, int count, uint64_t original)
{
    for (int i = 0; i < count; i++) {
        if (ordered[i]->id == original) {
            return function->nodes[2 + i];
        }
    }
    assert(!"missing selected node");
    return NULL;
}

static int pin_index(const node *n, uint64_t id)
{
    int index = -1;
    bool found = graph_pin_by_id(n, id, NULL, &index);
    assert(found);
    (void)found;
    return index;
}

static const char *build_graph(
    behavior_asset *asset,
    graph *source,
    node **ordered,
    int count,
    graph_kind kind,
    const char *name,
    uint64_t *call_id
)
{
    av_doc *document = av_doc_new();
    av_value *selection = graph_nodes_to_value(document, source, ordered, count);
    if (!selection) {
        av_doc_free(document);
        return "Could not copy the selected calculations.";
    }
    bool exec = executable(ordered, count);
    graph *function = kind == GRAPH_MACRO
                        ? behavior_asset_add_macro(asset, name)
                        : behavior_asset_add_function(asset, name, !exec);
    if (!function) {
        av_doc_free(document);
        return "Could not create the graph.";
    }
    int appended = graph_append_value(function, selection, 0, 0);
    av_doc_free(document);
    if (appended < 0 || acount(function->nodes) != count + 2) {
        return "Could not copy the selected calculations.";
    }
    node *entry = graph_node_by_id(function, function->entry_id);
    node *result = graph_node_by_id(function, function->return_id);
    boundary_port *inputs = NULL, *outputs = NULL, *entries = NULL, *exits = NULL;
    binding_port *bindings = NULL;
    edge *boundary = NULL, *crossings = NULL;
    node *call = NULL;
    const char *error = NULL;
    for (int i = 0; i < acount(source->edges); i++) {
        edge e = source->edges[i];
        bool to = selected(ordered, count, e.to_id);
        if (e.kind != GRAPH_EDGE_EXEC || selected(ordered, count, e.from_id) == to) {
            continue;
        }
        apush(crossings, e);
        node *inside = graph_node_by_id(source, to ? e.to_id : e.from_id);
        const pin_spec *pin = graph_pin_by_id(inside, to ? e.to_pin_id : e.from_pin_id, NULL, NULL);
        exec_tunnel(
            function,
            to ? entry : result,
            to,
            to ? &entries : &exits,
            inside->id,
            pin,
            kind == GRAPH_MACRO
        );
    }
    /* A macro whose execution ends inside it has no exit. */
    if (kind == GRAPH_MACRO && !acount(exits)) {
        int removed = graph_remove_pin(function, result, 0, 0);
        assert(removed == 0);
        (void)removed;
    }
    for (int i = 0; i < acount(source->edges); i++) {
        edge e = source->edges[i];
        bool from = selected(ordered, count, e.from_id),
             to = selected(ordered, count, e.to_id);
        if (e.kind != GRAPH_EDGE_DATA || from == to) {
            continue;
        }
        apush(boundary, e);
        node *producer = graph_node_by_id(source, e.from_id);
        node *consumer = graph_node_by_id(source, e.to_id);
        const pin_spec *pin = graph_pin_by_id(producer, e.from_pin_id, NULL, NULL);
        boundary_port **ports = from ? &outputs : &inputs;
        if (port_index(*ports, &e) >= 0) {
            continue;
        }
        const char *label = from
                              ? pin->name
                              : input_label(pin, graph_pin_by_id(consumer, e.to_pin_id, NULL, NULL));
        int at = add_port(function, from ? result : entry, !from, pin, label);
        assert(at >= 0);
        apush(*ports, ((boundary_port){ e.from_id, e.from_pin_id, at }));
    }
    if (!exec) {
        /* A single function input can represent one dormant binding. Reject a
         * producer fanout whose selected consumers authored different fallbacks
         * rather than silently applying one fallback to all consumers. */
        for (int i = 0; i < acount(inputs) && !error; i++) {
            boundary_port p = inputs[i];
            const pin_binding *binding = NULL;
            const pin_spec *binding_pin = NULL;
            bool first = true;
            for (int b = 0; b < acount(boundary); b++) {
                const edge *crossing = &boundary[b];
                if (crossing->kind != GRAPH_EDGE_DATA ||
                    selected(ordered, count, crossing->from_id) ||
                    crossing->from_id != p.node_id ||
                    crossing->from_pin_id != p.pin_id) {
                    continue;
                }
                node *consumer = graph_node_by_id(source, crossing->to_id);
                int consumer_pin_index = consumer
                                           ? pin_index(consumer, crossing->to_pin_id)
                                           : -1;
                const pin_spec *consumer_pin = consumer && consumer_pin_index >= 0
                                                 ? &consumer->input_pins[consumer_pin_index]
                                                 : NULL;
                if (!consumer_pin) {
                    error = "Could not inspect a function input boundary.";
                    break;
                }
                if (first) {
                    binding = consumer_pin->binding;
                    binding_pin = consumer_pin;
                    first = false;
                } else if (!same_optional_binding(binding, consumer_pin->binding, binding_pin, consumer_pin)) {
                    error = "Cannot extract a fanout with different input "
                            "bindings.";
                    break;
                }
            }
        }
    }
    for (int i = 0; i < count && !error; i++) {
        const node *original = ordered[i];
        for (int p = 0; p < original->input_count; p++) {
            const pin_spec *pin = &original->input_pins[p];
            node *driver = NULL;
            int driver_pin = -1;
            if (pin->kind != PIN_DATA || !pin->binding ||
                graph_input_source(source, original, p, &driver, &driver_pin) ||
                (exec && pin->binding->kind != PIN_BINDING_EVENT_OUTPUT)) {
                continue;
            }
            int existing = -1;
            for (int b = 0; b < acount(bindings); b++) {
                const binding_port *candidate = &bindings[b];
                const node *candidate_node = graph_node_by_id(source, candidate->node_id);
                int candidate_pin_index = candidate_node ? pin_index(
                                                               candidate_node,
                                                               candidate->pin_id
                                                           )
                                                         : -1;
                const pin_spec *candidate_pin = candidate_node &&
                                                        candidate_pin_index >= 0
                                                  ? &candidate_node->input_pins[candidate_pin_index]
                                                  : NULL;
                if (candidate_pin &&
                    same_binding(pin->binding, candidate_pin->binding, pin, candidate_pin)) {
                    existing = candidate->index;
                    break;
                }
            }
            if (existing < 0) {
                existing = add_port(function, entry, 1, pin, pin->name);
                if (existing < 0) {
                    error = "Could not preserve an input binding.";
                    break;
                }
                apush(bindings, ((binding_port){ original->id, pin->id, existing }));
            }
        }
    }
    if (!error) {
        call = kind == GRAPH_MACRO
                 ? behavior_asset_add_macro_call_node_to_graph(asset, source, function)
                 : behavior_asset_add_function_call_node_to_graph(asset, source, function);
        error = call ? NULL : "Could not create the call.";
    }
    for (int i = 0; !error && i < acount(inputs); i++) {
        boundary_port p = inputs[i];
        node *producer = graph_node_by_id(source, p.node_id);
        if (graph_connect_data(source, producer, pin_index(producer, p.pin_id), call, p.index) !=
            0) {
            error = "Could not preserve an input.";
        }
        if (!error && !exec) {
            for (int b = 0; b < acount(boundary); b++) {
                const edge *crossing = &boundary[b];
                if (crossing->kind != GRAPH_EDGE_DATA ||
                    selected(ordered, count, crossing->from_id) ||
                    crossing->from_id != p.node_id ||
                    crossing->from_pin_id != p.pin_id) {
                    continue;
                }
                node *consumer = graph_node_by_id(source, crossing->to_id);
                const pin_spec
                    *consumer_pin = consumer
                                      ? &consumer->input_pins
                                             [pin_index(consumer, crossing->to_pin_id)]
                                      : NULL;
                if (consumer_pin && consumer_pin->binding &&
                    consumer_pin->binding->kind != PIN_BINDING_LOCAL &&
                    graph_set_pin_binding(source, call, 0, p.index, consumer_pin->binding) !=
                        0) {
                    error = "Could not preserve a dormant input binding.";
                }
                break;
            }
        }
    }
    for (int i = 0; !error && i < acount(bindings); i++) {
        const binding_port *port = &bindings[i];
        node *original = graph_node_by_id(source, port->node_id);
        node *copy = copied_node(function, ordered, count, port->node_id);
        int original_pin = pin_index(original, port->pin_id);
        const pin_binding *binding = original->input_pins[original_pin].binding;
        if (graph_connect_data(function, entry, port->index, copy, original_pin) != 0 ||
            graph_set_pin_binding(function, copy, 0, original_pin, NULL) != 0 ||
            graph_set_pin_binding(source, call, 0, port->index, binding) != 0) {
            error = "Could not preserve an input binding.";
        }
    }
    for (int i = 0; !error && i < acount(outputs); i++) {
        boundary_port p = outputs[i];
        node *original = graph_node_by_id(source, p.node_id);
        node *copy = copied_node(function, ordered, count, p.node_id);
        if (graph_connect_data(function, copy, pin_index(original, p.pin_id), result, p.index) !=
            0) {
            error = "Could not preserve a result.";
        }
    }
    /* Remove original boundary wires before connecting the replacement call. */
    for (int i = acount(source->edges) - 1; i >= 0; i--) {
        edge *e = &source->edges[i];
        if (selected(ordered, count, e->from_id) || selected(ordered, count, e->to_id)) {
            adel(source->edges, i);
        }
    }
    for (int i = 0; !error && i < acount(boundary); i++) {
        edge e = boundary[i];
        bool outgoing = selected(ordered, count, e.from_id);
        int rc;
        if (outgoing) {
            node *consumer = graph_node_by_id(source, e.to_id);
            rc = graph_connect_data_conversion(
                source,
                call,
                outputs[port_index(outputs, &e)].index,
                consumer,
                pin_index(consumer, e.to_pin_id),
                e.conversion
            );
        } else {
            node *consumer = graph_node_by_id(source, e.to_id);
            node *copy = copied_node(function, ordered, count, e.to_id);
            int consumer_pin = pin_index(consumer, e.to_pin_id);
            rc = graph_connect_data_conversion(
                function,
                entry,
                inputs[port_index(inputs, &e)].index,
                copy,
                consumer_pin,
                e.conversion
            );
            if (!exec && rc == 0 &&
                graph_set_pin_binding(function, copy, 0, consumer_pin, NULL) != 0) {
                rc = -1;
            }
        }
        if (rc != 0) {
            error = "Could not preserve a connection across the boundary.";
        }
    }
    for (int i = 0; !error && i < acount(crossings); i++) {
        const edge *e = &crossings[i];
        bool into = selected(ordered, count, e->to_id);
        node *outside = graph_node_by_id(source, into ? e->from_id : e->to_id);
        int pin = pin_index(outside, into ? e->from_pin_id : e->to_pin_id);
        int rc = into
                   ? graph_connect_exec(
                         source,
                         outside,
                         pin,
                         call,
                         entries[port_of(entries, e->to_id, e->to_pin_id)].index,
                         e->exec_order
                     )
                   : graph_connect_exec(
                         source,
                         call,
                         exits[port_of(exits, e->from_id, e->from_pin_id)].index,
                         outside,
                         pin,
                         e->exec_order
                     );
        if (rc != 0) {
            error = "Could not preserve execution across the boundary.";
        }
    }
    for (int side = 0; !error && side < 2; side++) {
        const boundary_port *ports = side ? exits : entries;
        for (int i = 0; !error && i < acount(ports); i++) {
            node *copy = copied_node(function, ordered, count, ports[i].node_id);
            int pin = pin_index(
                graph_node_by_id(source, ports[i].node_id),
                ports[i].pin_id
            );
            int rc = side
                       ? graph_connect_exec(
                             function,
                             copy,
                             pin,
                             result,
                             ports[i].index,
                             0
                         )
                       : graph_connect_exec(function, entry, ports[i].index, copy, pin, 0);
            if (rc != 0) {
                error = "Could not preserve execution across the boundary.";
            }
        }
    }
    if (!error) {
        /* The call takes the selection's center; Function Entry and Return
           sit clear of the copied nodes on either side. */
        float x = 0, y = 0, left = INFINITY, right = -INFINITY;
        for (int i = 0; i < count; i++) {
            x += ordered[i]->x;
            y += ordered[i]->y;
            left = fminf(left, ordered[i]->x);
            right = fmaxf(right, ordered[i]->x);
        }
        call->x = x / count;
        call->y = y / count;
        entry->x = left - TUNNEL_GAP;
        entry->y = call->y;
        result->x = right + TUNNEL_GAP;
        result->y = call->y;
        uint64_t *ordered_ids = xmalloc((size_t)count * sizeof *ordered_ids);
        for (int i = 0; i < count; i++) {
            ordered_ids[i] = ordered[i]->id;
        }
        for (int i = 0; i < count; i++) {
            node *original = graph_node_by_id(source, ordered_ids[i]);
            if (original) {
                int removed = graph_remove_node(source, original);
                assert(removed == 0);
                (void)removed;
            }
        }
        free(ordered_ids);
        *call_id = call->id;
    }
    afree(inputs);
    afree(outputs);
    afree(entries);
    afree(exits);
    afree(bindings);
    afree(boundary);
    afree(crossings);
    return error;
}

static const char *extract(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    graph_kind kind,
    const char *name,
    behavior_asset **out_asset,
    uint64_t *out_call_id
)
{
    assert(out_asset && out_call_id);
    *out_asset = NULL;
    *out_call_id = 0;
    int expanded_count = count;
    node **expanded = NULL;
    if (asset && source && source->owner == asset && count > 0 && selection) {
        expanded = expand_inline_selection(source, selection, count, &expanded_count);
    }
    const char *error = can_extract_selection(
        asset,
        source,
        expanded ? expanded : selection,
        expanded ? expanded_count : count,
        kind
    );
    if (error) {
        free(expanded);
        return error;
    }
    bool visible = false;
    if (!name) {
        free(expanded);
        return "Enter a name.";
    }
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        if (iscntrl(*c)) {
            free(expanded);
            return "Use a name without control characters.";
        }
        visible |= !isspace(*c);
    }
    if (!visible) {
        free(expanded);
        return "Enter a name.";
    }
    if (behavior_asset_find_graph_by_name(asset, name)) {
        free(expanded);
        return "A graph with that name already exists.";
    }
    behavior_asset *copy = behavior_asset_clone(asset);
    if (!copy) {
        free(expanded);
        return "Could not copy the behavior asset.";
    }
    graph *caller = behavior_asset_find_graph(copy, source->graph_id);
    int selected_count = expanded ? expanded_count : count;
    node *const *effective_selection = expanded ? expanded : selection;
    node **ordered = xmalloc((size_t)selected_count * sizeof *ordered);
    for (int i = 0; i < selected_count; i++) {
        ordered[i] = graph_node_by_id(caller, effective_selection[i]->id);
    }
    free(expanded);
    expanded = NULL;
    qsort(ordered, (size_t)selected_count, sizeof *ordered, compare_node_ids);
    error = build_graph(copy, caller, ordered, selected_count, kind, name, out_call_id);
    free(ordered);
    if (error) {
        behavior_asset_free(copy);
        *out_call_id = 0;
        return error;
    }
    *out_asset = copy;
    return NULL;
}

const char *behavior_asset_extract_function(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    const char *name,
    behavior_asset **out_asset,
    uint64_t *out_call_id
)
{
    return extract(asset, source, selection, count, GRAPH_FUNCTION, name, out_asset, out_call_id);
}

const char *behavior_asset_extract_macro(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    const char *name,
    behavior_asset **out_asset,
    uint64_t *out_call_id
)
{
    return extract(asset, source, selection, count, GRAPH_MACRO, name, out_asset, out_call_id);
}
