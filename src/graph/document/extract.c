#include "graph/document/extract.h"

#include "graph/document/serialize.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool selected(node *const *selection, int count, uint64_t id)
{
    for (int i = 0; i < count; i++) {
        if (selection[i]->id == id) {
            return true;
        }
    }
    return false;
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

const char *behavior_asset_can_extract_function(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
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
        if (n->disabled ||
            (!exec &&
             (n->desc->flow_kind != NODE_FLOW_NONE || n->desc->latent ||
              n->desc->pure_kind == NODE_PURE_GET || n->desc->pure_kind == NODE_PURE_SELF ||
              n->desc->pure_kind == NODE_PURE_READ || n->desc->pure_kind == NODE_PURE_FUNCTION ||
              n->desc->pure_kind == NODE_PURE_DELTA || n->desc->pure_kind == NODE_PURE_VOID_CALL ||
              (n->desc->access && (n->desc->access->flags & ~NODE_EFFECT_PURE))))) {
            return "Select pure calculations. Keep events, state reads and "
                   "function calls outside the selection.";
        }
        if (has_split(n)) {
            return "Recombine split pins before extracting a function.";
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
        const char *error = execution_boundary(source, selection, count);
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

typedef struct boundary_port
{
    uint64_t node_id;
    uint64_t pin_id;
    int index;
} boundary_port;

static int port_index(boundary_port *ports, const edge *e)
{
    for (int i = 0; i < acount(ports); i++) {
        if (ports[i].node_id == e->from_id && ports[i].pin_id == e->from_pin_id) {
            return i;
        }
    }
    return -1;
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
    return graph_add_pin(g, boundary, output, &pin);
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
    assert(graph_pin_by_id(n, id, NULL, &index));
    return index;
}

static const char *build_function(
    behavior_asset *asset,
    graph *source,
    node **ordered,
    int count,
    const char *name,
    uint64_t *call_id
)
{
    char *json = graph_save_nodes_json(source, ordered, count);
    if (!json) {
        return "Could not copy the selected calculations.";
    }
    bool exec = executable(ordered, count);
    graph *function = behavior_asset_add_function(asset, name, !exec);
    if (!function) {
        free(json);
        return "Could not create the function.";
    }
    int appended = graph_append_json(function, json, 0, 0);
    free(json);
    if (appended < 0 || acount(function->nodes) != count + 2) {
        return "Could not copy the selected calculations.";
    }
    node *entry = graph_node_by_id(function, function->entry_id);
    node *result = graph_node_by_id(function, function->return_id);
    boundary_port *inputs = NULL, *outputs = NULL;
    edge *boundary = NULL;
    edge exec_in = { 0 }, exec_out = { 0 };
    for (int i = 0; i < acount(source->edges); i++) {
        edge e = source->edges[i];
        bool from = selected(ordered, count, e.from_id),
             to = selected(ordered, count, e.to_id);
        if (e.kind == GRAPH_EDGE_EXEC && from != to) {
            if (to) {
                exec_in = e;
            }
            if (from) {
                exec_out = e;
            }
        }
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
    node *call = behavior_asset_add_function_call_node_to_graph(asset, source, function);
    const char *error = call ? NULL : "Could not create the function call.";
    for (int i = 0; !error && i < acount(inputs); i++) {
        boundary_port p = inputs[i];
        node *producer = graph_node_by_id(source, p.node_id);
        if (graph_connect_data(source, producer, pin_index(producer, p.pin_id), call, p.index) !=
            0) {
            error = "Could not preserve a function input.";
        }
    }
    for (int i = 0; !error && i < acount(outputs); i++) {
        boundary_port p = outputs[i];
        node *original = graph_node_by_id(source, p.node_id);
        node *copy = copied_node(function, ordered, count, p.node_id);
        if (graph_connect_data(function, copy, pin_index(original, p.pin_id), result, p.index) !=
            0) {
            error = "Could not preserve a function result.";
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
            rc = graph_connect_data_conversion(
                function,
                entry,
                inputs[port_index(inputs, &e)].index,
                copy,
                pin_index(consumer, e.to_pin_id),
                e.conversion
            );
        }
        if (rc != 0) {
            error = "Could not preserve a connection across the function "
                    "boundary.";
        }
    }
    if (!error && exec) {
        node *before = graph_node_by_id(source, exec_in.from_id);
        node *after = graph_node_by_id(source, exec_out.to_id);
        node *first = copied_node(function, ordered, count, exec_in.to_id);
        node *last = copied_node(function, ordered, count, exec_out.from_id);
        node *original_first = graph_node_by_id(source, exec_in.to_id);
        node *original_last = graph_node_by_id(source, exec_out.from_id);
        if (graph_connect_exec(
                source,
                before,
                pin_index(before, exec_in.from_pin_id),
                call,
                0,
                exec_in.exec_order
            ) ||
            graph_connect_exec(
                source,
                call,
                0,
                after,
                pin_index(after, exec_out.to_pin_id),
                exec_out.exec_order
            ) ||
            graph_connect_exec(
                function,
                entry,
                0,
                first,
                pin_index(original_first, exec_in.to_pin_id),
                0
            ) ||
            graph_connect_exec(function, last, pin_index(original_last, exec_out.from_pin_id), result, 0, 0)) {
            error = "Could not preserve execution across the function "
                    "boundary.";
        }
    }
    if (!error) {
        float x = 0, y = 0;
        for (int i = 0; i < count; i++) {
            x += ordered[i]->x;
            y += ordered[i]->y;
        }
        call->x = x / count;
        call->y = y / count;
        entry->x = call->x - 260;
        entry->y = call->y;
        result->x = call->x + 260;
        result->y = call->y;
        for (int i = 0; i < count; i++) {
            assert(graph_remove_node(source, ordered[i]) == 0);
        }
        *call_id = call->id;
    }
    afree(inputs);
    afree(outputs);
    afree(boundary);
    return error;
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
    assert(out_asset && out_call_id);
    *out_asset = NULL;
    *out_call_id = 0;
    const char *error = behavior_asset_can_extract_function(asset, source, selection, count);
    if (error) {
        return error;
    }
    bool visible = false;
    if (!name) {
        return "Enter a function name.";
    }
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        if (iscntrl(*c)) {
            return "Use a function name without control characters.";
        }
        visible |= !isspace(*c);
    }
    if (!visible) {
        return "Enter a function name.";
    }
    if (behavior_asset_find_graph_by_name(asset, name)) {
        return "A graph with that name already exists.";
    }
    char *json = behavior_asset_save_json(asset);
    behavior_asset *copy = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!copy) {
        return "Could not copy the behavior asset.";
    }
    graph *caller = behavior_asset_find_graph(copy, source->graph_id);
    node **ordered = xmalloc((size_t)count * sizeof *ordered);
    for (int i = 0; i < count; i++) {
        ordered[i] = graph_node_by_id(caller, selection[i]->id);
    }
    qsort(ordered, (size_t)count, sizeof *ordered, compare_node_ids);
    error = build_function(copy, caller, ordered, count, name, out_call_id);
    free(ordered);
    if (error) {
        behavior_asset_free(copy);
        *out_call_id = 0;
        return error;
    }
    *out_asset = copy;
    return NULL;
}
