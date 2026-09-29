#include "graph/model/graph.h"

#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/value_array.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct exec_edges
{
    int first;
    int count;
} exec_edges;

typedef struct graph_edge_index
{
    int *inputs;         /* Destination pin ID -> edge slot + 1. */
    exec_edges *outputs; /* Source pin ID -> EXEC edge slots. */
    int *next;    /* EXEC edge slot -> next slot + 1, in authored edge order. */
    int *sources; /* Source node ID -> its only outgoing edge slot + 1, or -1. */
    atomic_ullong revision;
    atomic_flag building;
} graph_edge_index;

static atomic_ullong graph_lifetime;

void graph_init(graph *g)
{
    if (!g) {
        return;
    }
    memset(g, 0, sizeof *g);
    g->lifetime_id = atomic_fetch_add_explicit(&graph_lifetime, 1, memory_order_relaxed) +
                     1;
    assert(g->lifetime_id != 0);
    g->id_counter = 1;
    g->kind = GRAPH_EVENT;
    g->breakpoints_enabled = 1;
    g->topology_revision = 1;
}

const char *graph_kind_name(graph_kind kind)
{
    switch (kind) {
        case GRAPH_EVENT: return "event";
        case GRAPH_FUNCTION: return "function";
        case GRAPH_MACRO: return "macro";
    }
    return "event";
}

int graph_kind_from_name(const char *name, graph_kind *kind)
{
    if (!name || !kind) {
        return -1;
    }
    if (strcmp(name, "event") == 0) {
        *kind = GRAPH_EVENT;
    } else if (strcmp(name, "function") == 0) {
        *kind = GRAPH_FUNCTION;
    } else if (strcmp(name, "macro") == 0) {
        *kind = GRAPH_MACRO;
    } else {
        return -1;
    }
    return 0;
}

static int node_in_graph(const graph *g, const node *n)
{
    if (!g || !n) {
        return 0;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            return 1;
        }
    }
    return 0;
}

static void graph_edge_index_init(graph *g)
{
    if (!g->edge_index) {
        g->edge_index = xcalloc(1, sizeof *g->edge_index);
        atomic_init(&g->edge_index->revision, 0);
        atomic_flag_clear(&g->edge_index->building);
    }
}

static void graph_changed(graph *g)
{
    if (g) {
        graph_edge_index_init(g);
        g->topology_revision++;
    }
}

static int graph_mutation_allowed(const graph *g)
{
    return g && g->flow_pause_count == 0 &&
           (!g->owner || g->owner->graph->flow_pause_count == 0);
}

node *graph_node_by_id(const graph *g, uint64_t id);
const pin_spec *graph_pin_by_id(const node *n, uint64_t id, int *is_output, int *index);

uint64_t graph_alloc_id(graph *g)
{
    if (!g) {
        return 0;
    }
    uint64_t *counter = g->shared_id_counter ? g->shared_id_counter : &g->id_counter;
    if (*counter == 0) {
        *counter = 1;
    }
    uint64_t id = (*counter)++;
    g->id_counter = *counter;
    return id;
}

static void graph_index_node(graph *g, node *n)
{
    size_t want = (size_t)n->id + 1;
    while ((size_t)acount(g->id_table) < want) {
        apush(g->id_table, NULL);
    }
    g->id_table[n->id] = n;
}

void graph_reindex(graph *g)
{
    if (!g) {
        return;
    }
    graph_edge_index_init(g);
    afree(g->id_table);
    for (int i = 0; i < acount(g->nodes); i++) {
        graph_index_node(g, g->nodes[i]);
    }
    atomic_store_explicit(&g->edge_index->revision, 0, memory_order_relaxed);
}

static pin_binding *clone_binding(graph *g, const pin_binding *source)
{
    if (!source) {
        return NULL;
    }
    pin_binding *copy = graph_own_block(g, sizeof *copy);
    *copy = *source;
    copy->source_asset = source->source_asset ? graph_own_str(
                                                    g,
                                                    source->source_asset,
                                                    strlen(source->source_asset)
                                                )
                                              : NULL;
    copy->component_key = source->component_key ? graph_own_str(
                                                      g,
                                                      source->component_key,
                                                      strlen(source->component_key)
                                                  )
                                                : NULL;
    copy->field_name = source->field_name
                         ? graph_own_str(g, source->field_name, strlen(source->field_name))
                         : NULL;
    return copy;
}

pin_value graph_pin_initial_default(const pin_spec *template_pin)
{
    pin_value value = template_pin->default_value;
    value.type = template_pin->type;
    value.container = template_pin->container;
    value.type_name = template_pin->type_name;
    if (value.container == PIN_CONTAINER_SINGLE &&
        (value.type == PIN_STR || value.type == PIN_NAME) && !value.str) {
        value.str = "";
    }
    return value;
}

static void init_pin(pin_spec *dst, const pin_spec *src, graph *g)
{
    *dst = *src;
    dst->id = graph_alloc_id(g);
    dst->name = src->name ? graph_own_str(g, src->name, strlen(src->name)) : NULL;
    dst->field_name = src->field_name
                        ? graph_own_str(g, src->field_name, strlen(src->field_name))
                        : NULL;
    dst->binding = clone_binding(g, src->binding);
    dst->default_value = graph_pin_initial_default(src);
}

node *graph_add_node(graph *g, const node_desc *desc)
{
    if (!graph_mutation_allowed(g) || !desc || desc->input_count < 0 ||
        desc->output_count < 0 || (desc->input_count && !desc->inputs) ||
        (desc->output_count && !desc->outputs)) {
        return NULL;
    }
    node *n = (node *)xcalloc(1, sizeof *n);
    n->desc = desc;
    n->placed = true;
    n->input_count = desc->input_count;
    n->output_count = desc->output_count;
    n->id = graph_alloc_id(g);
    graph_index_node(g, n);
    if (desc->input_count) {
        n->input_pins = (pin_spec *)xcalloc((size_t)desc->input_count, sizeof *n->input_pins);
        for (int i = 0; i < desc->input_count; i++) {
            init_pin(&n->input_pins[i], &desc->inputs[i], g);
        }
    }
    if (desc->output_count) {
        n->output_pins = (pin_spec *)
            xcalloc((size_t)desc->output_count, sizeof *n->output_pins);
        for (int i = 0; i < desc->output_count; i++) {
            init_pin(&n->output_pins[i], &desc->outputs[i], g);
        }
    }
    apush(g->nodes, n);
    graph_changed(g);
    return n;
}

int pin_types_compatible(const pin_spec *a, const pin_spec *b)
{
    return pin_compatible(a, b);
}

/* Retypes one wildcard pin, keeping its default as authored data where the
 * type holds. */
static void graph_wildcard_resolve_pin(graph *g, pin_spec *p, pin_type type, const char *type_name)
{
    char *copy = type_name ? graph_own_str(g, type_name, strlen(type_name)) : NULL;
    /* Only aggregates, enums and handles are named types: a float spelled
     * "float" is the same float as an unnamed one. */
    bool named = type == PIN_STRUCT || type == PIN_ENUM || type == PIN_HANDLE;
    bool changed_type = p->type != type || (named && (p->type_name || type_name) &&
                                            (!p->type_name || !type_name ||
                                             strcmp(p->type_name, type_name)));
    /* A wildcard's default is still authored data. Keep it intact while a
     * wire temporarily supplies the resolved type when the input also
     * carries a dormant binding. Unbound wildcards retain the original retype
     * behavior used by shader graph inputs. */
    if (!p->binding || p->default_value.type != PIN_FLOW) {
        if (changed_type) {
            p->default_value = (pin_value){ 0 };
        }
        p->default_value.type = type;
        p->default_value.container = p->container;
        p->default_value.type_name = copy;
    }
    p->type = type;
    p->type_name = copy;
}

static void graph_wildcard_resolve_group(graph *g, node *n, int group, pin_type type, const char *type_name)
{
    if (group == 0) {
        return;
    }
    for (int output = 0; output <= 1; output++) {
        pin_spec *pins = output ? n->output_pins : n->input_pins;
        int count = output ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            if ((pins[i].flags & PIN_FLAG_WILDCARD) && pins[i].group == group) {
                graph_wildcard_resolve_pin(g, &pins[i], type, type_name);
            }
        }
    }
}

static edge make_edge(
    node *from,
    int from_pin,
    node *to,
    int to_pin,
    graph_edge_kind kind,
    int order,
    const node_desc *conversion
)
{
    edge e = { 0 };
    e.from_id = from ? from->id : 0;
    e.to_id = to ? to->id : 0;
    e.from_pin_id = from && from_pin >= 0 && from_pin < from->output_count
                      ? from->output_pins[from_pin].id
                      : 0;
    e.to_pin_id = to && to_pin >= 0 && to_pin < to->input_count
                    ? to->input_pins[to_pin].id
                    : 0;
    e.kind = kind;
    e.conversion = conversion;
    e.exec_order = order;
    return e;
}

static int graph_numeric_node(const node *n)
{
    const node_desc *d = n->desc;
    return d == &NODE_Add || d == &NODE_Subtract || d == &NODE_Multiply ||
           d == &NODE_Divide || d == &NODE_Min || d == &NODE_Max || d == &NODE_Clamp ||
           d == &NODE_Modulo || d == &NODE_Negate || d == &NODE_Abs;
}

static const node_desc *graph_numeric_unwrap(const pin_spec *pin)
{
    if (pin->type != PIN_STRUCT || pin->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    const type_desc *type = registry_type_by_name(pin->type_name);
    if (!type || type->field_count != 1) {
        return NULL;
    }
    const type_field *field = &type->fields[0];
    pin_spec value = { .type = field->type,
                       .type_name = field->type_name,
                       .kind = PIN_DATA,
                       .container = PIN_CONTAINER_SINGLE };
    return conversion_for_pins(pin, &value);
}

int graph_connect_data_conversion(graph *g, node *from, int from_pin, node *to, int to_pin, const node_desc *conversion)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, from) ||
        !node_in_graph(g, to)) {
        return -1;
    }
    if (from_pin < 0 || from_pin >= from->output_count || to_pin < 0 ||
        to_pin >= to->input_count) {
        return -1;
    }
    const pin_spec *out = &from->output_pins[from_pin];
    const pin_spec *in = &to->input_pins[to_pin];
    if ((out->flags | in->flags) & PIN_FLAG_SETTING) {
        return -1;
    }
    if (in->flags & PIN_FLAG_REFERENCE) {
        pin_binding binding = { 0 };
        if (conversion || graph_resolve_reference(g, from, from_pin, &binding) != 0) {
            return -1;
        }
    }
    int numeric_wildcard = (in->flags & PIN_FLAG_WILDCARD) && graph_numeric_node(to);
    if (!conversion && numeric_wildcard) {
        conversion = graph_numeric_unwrap(out);
    }
    const pin_spec *target = conversion && numeric_wildcard &&
                                     conversion->output_count == 1
                               ? &conversion->outputs[0]
                               : in;
    if (out->kind != PIN_DATA || in->kind != PIN_DATA ||
        (conversion && !conversion_matches_pins(out, target, conversion))) {
        return -1;
    }
    if (!conversion && pin_wildcard_adopts(out, in) &&
        !(in->flags & PIN_FLAG_WILDCARD)) {
        graph_wildcard_resolve_group(g, from, out->group, in->type, in->type_name);
        out = &from->output_pins[from_pin];
    }
    if (in->flags & PIN_FLAG_WILDCARD) {
        if (conversion && !numeric_wildcard && !conversion_is_vector_splat(conversion)) {
            return -1;
        }
        const pin_spec *value = conversion ? &conversion->outputs[0] : out;
        char *owned = value->type_name
                        ? graph_own_str(g, value->type_name, strlen(value->type_name))
                        : NULL;
        if (value->type_name && !owned) {
            return -1;
        }
        pin_spec *p = &to->input_pins[to_pin];
        p->container = value->container;
        /* An ungrouped wildcard resolves alone. */
        if (!p->group) {
            graph_wildcard_resolve_pin(g, p, value->type, owned);
        }
        p->type = value->type;
        p->type_name = owned;
        graph_wildcard_resolve_group(g, to, p->group, value->type, value->type_name);
        in = &to->input_pins[to_pin];
    }
    if (!conversion && !pin_compatible(out, in)) {
        return -1;
    }
    const graph_local *local = graph_input_local(g, to, to_pin);
    node *previous = NULL;
    int previous_pin = -1;
    if (local && graph_input_source(g, to, to_pin, &previous, &previous_pin) &&
        (local->source_id != from->id || local->source_pin_id != out->id)) {
        to->input_pins[to_pin].binding = NULL;
        graph_prune_locals(g);
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->to_id == to->id && old->to_pin_id == in->id) {
            if (old->kind != GRAPH_EDGE_DATA) {
                return -1;
            }
            *old = make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0, conversion);
            if (in->flags & PIN_FLAG_REFERENCE) {
                to->input_pins[to_pin].binding = NULL;
            }
            graph_changed(g);
            return 0;
        }
    }
    if (in->flags & PIN_FLAG_REFERENCE) {
        to->input_pins[to_pin].binding = NULL;
    }
    apush(g->edges, make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0, conversion));
    graph_changed(g);
    return 0;
}

int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    return graph_connect_data_conversion(g, from, from_pin, to, to_pin, NULL);
}

/* PIN-11: dropping the driver edge of a wildcard data input returns it to
 * the unresolved marker, so the pin reads as Generic again. */
static void graph_wildcard_untouch(graph *g, uint64_t to_node_id, uint64_t to_pin_id)
{
    node *to = graph_node_by_id(g, to_node_id);
    if (!to) {
        return;
    }
    int is_out = 0, index = -1;
    if (!graph_pin_by_id(to, to_pin_id, &is_out, &index) || is_out) {
        return;
    }
    pin_spec *p = &to->input_pins[index];
    if (p->kind != PIN_DATA || !(p->flags & PIN_FLAG_WILDCARD)) {
        return;
    }
    if (p->binding) {
        return;
    }
    p->type = PIN_FLOW;
    p->type_name = NULL;
    p->container = PIN_CONTAINER_SINGLE;
    p->default_value = (pin_value){ .type = PIN_FLOW };
}

int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    return graph_connect_data(g, from, from_pin, to, to_pin);
}

int graph_connect_exec_ids(
    graph *g,
    uint64_t from_node,
    uint64_t from_pin,
    uint64_t to_node,
    uint64_t to_pin,
    int exec_order
)
{
    node *from = graph_node_by_id(g, from_node), *to = graph_node_by_id(g, to_node);
    int fp = -1, tp = -1, out = 0, in = 0;
    if (!graph_pin_by_id(from, from_pin, &out, &fp) ||
        !graph_pin_by_id(to, to_pin, &in, &tp) || !out || in) {
        return -1;
    }
    return graph_connect_exec(g, from, fp, to, tp, exec_order);
}

int graph_connect_exec(graph *g, node *from, int from_pin, node *to, int to_pin, int exec_order)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, from) ||
        !node_in_graph(g, to) || exec_order < 0) {
        return -1;
    }
    if (from_pin < 0 || from_pin >= from->output_count || to_pin < 0 ||
        to_pin >= to->input_count) {
        return -1;
    }
    const pin_spec *out = &from->output_pins[from_pin];
    const pin_spec *in = &to->input_pins[to_pin];
    if (out->kind != PIN_EXEC || in->kind != PIN_EXEC) {
        return -1;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->kind == GRAPH_EDGE_EXEC && old->from_id == from->id &&
            old->from_pin_id == out->id &&
            (old->to_id != to->id || old->to_pin_id != in->id)) {
            return -1;
        }
    }
    /* Exec inputs intentionally have multiple drivers. An exact duplicate is
     * replaced, while distinct incoming edges merge at the input. */
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->kind == GRAPH_EDGE_EXEC && old->from_id == from->id &&
            old->from_pin_id == out->id && old->to_id == to->id &&
            old->to_pin_id == in->id) {
            *old = make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_EXEC, exec_order, NULL);
            graph_changed(g);
            return 0;
        }
    }
    apush(g->edges, make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_EXEC, exec_order, NULL));
    graph_changed(g);
    return 0;
}

unsigned long long graph_topology_revision(const graph *g)
{
    return g ? g->topology_revision : 0;
}

void graph_flow_pause(graph *g)
{
    if (g) {
        g->flow_pause_count++;
    }
}
void graph_flow_resume(graph *g)
{
    if (g && g->flow_pause_count) {
        g->flow_pause_count--;
    }
}

int graph_remove_node(graph *g, node *n)
{
    if (!graph_mutation_allowed(g)) {
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;
    }
    for (int i = acount(g->locals) - 1; i >= 0; i--) {
        if (g->locals[i].source_id == n->id) {
            int expand_local_result = graph_expand_local(g, g->locals[i].id);
            assert(expand_local_result == 0);
            (void)expand_local_result;
        }
    }
    int w = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id == n->id || e->to_id == n->id) {
            if (e->to_id != n->id) {
                graph_wildcard_untouch(g, e->to_id, e->to_pin_id);
            }
            continue;
        }
        g->edges[w++] = *e;
    }
    asetlen(g->edges, w);
    if (n->id < (uint64_t)acount(g->id_table)) {
        g->id_table[n->id] = NULL;
    }
    free(n->input_pins);
    free(n->output_pins);
    free(n);
    for (int i = idx; i < acount(g->nodes) - 1; i++) {
        g->nodes[i] = g->nodes[i + 1];
    }
    asetlen(g->nodes, acount(g->nodes) - 1);
    graph_changed(g);
    graph_prune_locals(g);
    return 0;
}

void graph_inline_tree(const graph *g, const node *n, uint64_t **ids)
{
    apush(*ids, n->id);
    for (int pin = 0; pin < n->input_count; pin++) {
        node *source = NULL;
        int source_pin = -1;
        if (graph_inline_source(g, n, pin, &source, &source_pin)) {
            graph_inline_tree(g, source, ids);
        }
    }
}

int graph_remove_node_tree(graph *g, node *n)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    uint64_t *ids = NULL;
    graph_inline_tree(g, n, &ids);
    for (int i = 0; i < acount(ids); i++) {
        int removed = graph_remove_node(g, graph_node_by_id(g, ids[i]));
        assert(removed == 0);
        (void)removed;
    }
    afree(ids);
    return 0;
}

int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    if (!graph_mutation_allowed(g) || !from || !to) {
        return -1;
    }
    uint64_t from_pin_id = from_pin >= 0 && from_pin < from->output_count
                             ? from->output_pins[from_pin].id
                             : 0;
    uint64_t to_pin_id = to_pin >= 0 && to_pin < to->input_count
                           ? to->input_pins[to_pin].id
                           : 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != from->id || e->to_id != to->id ||
            e->from_pin_id != from_pin_id || e->to_pin_id != to_pin_id) {
            continue;
        }
        uint64_t target_id = e->to_id, target_pin = e->to_pin_id;
        if (to->input_pins[to_pin].binding &&
            to->input_pins[to_pin].binding->kind == PIN_BINDING_LOCAL) {
            to->input_pins[to_pin].binding = NULL;
        }
        adel(g->edges, i);
        graph_prune_locals(g);
        graph_wildcard_untouch(g, target_id, target_pin);
        graph_changed(g);
        return 0;
    }
    return -1;
}

static node *edge_from_node(const graph *g, const edge *e, int *pin)
{
    int is_out = 0;
    node *n = graph_node_by_id(g, e->from_id);
    if (!n || !graph_pin_by_id(n, e->from_pin_id, &is_out, pin) || !is_out) {
        if (pin) {
            *pin = -1;
        }
        return NULL;
    }
    return n;
}

static node *edge_to_node(const graph *g, const edge *e, int *pin)
{
    int is_out = 0;
    node *n = graph_node_by_id(g, e->to_id);
    if (!n || !graph_pin_by_id(n, e->to_pin_id, &is_out, pin) || is_out) {
        if (pin) {
            *pin = -1;
        }
        return NULL;
    }
    return n;
}

static void index_edges(const graph *g, graph_edge_index *index)
{
    uint64_t input = 0, output = 0, source = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id > source) {
            source = e->from_id;
        }
        if (e->kind == GRAPH_EDGE_DATA && e->to_pin_id > input) {
            input = e->to_pin_id;
        }
        if (e->kind == GRAPH_EDGE_EXEC && e->from_pin_id > output) {
            output = e->from_pin_id;
        }
    }
    assert(input < INT_MAX && output < INT_MAX && source < INT_MAX);
    asetlen(index->inputs, (int)input + 1);
    asetlen(index->outputs, (int)output + 1);
    asetlen(index->next, acount(g->edges));
    asetlen(index->sources, (int)source + 1);
    memset(index->inputs, 0, (size_t)acount(index->inputs) * sizeof *index->inputs);
    memset(index->outputs, 0, (size_t)acount(index->outputs) * sizeof *index->outputs);
    memset(index->sources, 0, (size_t)acount(index->sources) * sizeof *index->sources);
    for (int i = acount(g->edges) - 1; i >= 0; i--) {
        const edge *e = &g->edges[i];
        int *only = &index->sources[e->from_id];
        *only = *only ? -1 : i + 1;
        if (e->kind == GRAPH_EDGE_DATA) {
            index->inputs[e->to_pin_id] = i + 1;
        }
        if (e->kind == GRAPH_EDGE_EXEC) {
            exec_edges *edges = &index->outputs[e->from_pin_id];
            index->next[i] = edges->first;
            edges->first = i + 1;
            edges->count++;
        }
    }
    atomic_store_explicit(&index->revision, g->topology_revision, memory_order_release);
}

static graph_edge_index *edge_index(const graph *g)
{
    if (!acount(g->edges)) {
        return NULL;
    }
    graph_edge_index *index = g->edge_index;
    assert(index);
    if (atomic_load_explicit(&index->revision, memory_order_acquire) !=
        g->topology_revision) {
        /* Graph mutation is serialized, but immutable graphs can have concurrent first readers. */
        while (atomic_flag_test_and_set_explicit(&index->building, memory_order_acquire)) {
        }
        if (atomic_load_explicit(&index->revision, memory_order_relaxed) !=
            g->topology_revision) {
            index_edges(g, index);
        }
        atomic_flag_clear_explicit(&index->building, memory_order_release);
    }
    return index;
}

static const edge *input_edge(const graph *g, const node *n, int pin)
{
    graph_edge_index *index = edge_index(g);
    uint64_t id = n->input_pins[pin].id;
    int slot = index && id < (uint64_t)acount(index->inputs) ? index->inputs[id] : 0;
    const edge *e = slot ? &g->edges[slot - 1] : NULL;
    return e && e->to_id == n->id ? e : NULL;
}

static bool vector_constructor(const node_desc *desc)
{
    if (desc->output_count != 1 ||
        desc->input_count != pin_vector_width(&desc->outputs[0])) {
        return false;
    }
    if (desc->pure_kind != NODE_PURE_MAKE && strcmp(desc->name, "Shader Vector2") &&
        strcmp(desc->name, "Shader Vector3") && strcmp(desc->name, "Shader Vector4")) {
        return false;
    }
    for (int i = 0; i < desc->input_count; i++) {
        if (desc->inputs[i].kind != PIN_DATA ||
            (desc->inputs[i].type != PIN_F32 && desc->inputs[i].type != PIN_F64)) {
            return false;
        }
    }
    return desc->input_count >= 2;
}

bool graph_node_is_vector_constructor(const node *n)
{
    return n && n->desc && vector_constructor(n->desc);
}

const node_desc *graph_vector_constructor(const pin_spec *pin)
{
    if (!pin_vector_width(pin)) {
        return NULL;
    }
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        if (vector_constructor(desc) && pin_compatible(&desc->outputs[0], pin)) {
            return desc;
        }
    }
    return NULL;
}

/* A component node's Component or Type setting names what it reads, which its
 * chip shows, so it does not keep the node expanded once chosen. */
static bool component_setting(const node *n, int pin)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    return kind && pin == component_node_setting(n) && component_node_key(n)[0];
}

static bool inline_input(const graph *g, const node *n, int pin)
{
    const pin_spec *spec = &n->input_pins[pin];
    if (spec->kind != PIN_DATA || spec->type == PIN_FLOW) {
        return false;
    }
    if (spec->flags & PIN_FLAG_SETTING) {
        return component_setting(n, pin);
    }
    if (input_edge(g, n, pin) || spec->binding ||
        (spec->flags & (PIN_FLAG_DEFAULT_SELF | PIN_FLAG_SERVICE))) {
        return true;
    }
    return spec->type == PIN_STRUCT ? spec->default_value.ptr != NULL
         : spec->type == PIN_HANDLE ? spec->default_value.u64 != 0
                                    : true;
}

/* The single outgoing wire of a node that may fold into its consumer,
 * whatever its presentation. */
static const edge *inline_edge(const graph *g, const node *n)
{
    /* A flow node such as a pure function's Function Entry has no exec pins
     * but anchors its graph. */
    if (n->disabled || n->desc->flow_kind != NODE_FLOW_NONE ||
        node_behavior_for(n) != NODE_BEHAVIOR_PURE ||
        n->desc->pure_kind == NODE_PURE_FUNCTION ||
        n->desc->pure_kind == NODE_PURE_COPY) {
        return NULL;
    }
    for (int p = 0; p < n->input_count; p++) {
        if (!inline_input(g, n, p)) {
            return NULL;
        }
    }
    for (int p = 0; p < n->output_count; p++) {
        if ((n->output_pins[p].flags & PIN_FLAG_SETTING) ||
            graph_output_local(g, n, p)) {
            return NULL;
        }
    }
    graph_edge_index *index = edge_index(g);
    int slot = index && n->id < (uint64_t)acount(index->sources)
                 ? index->sources[n->id]
                 : 0;
    const edge *outgoing = slot > 0 ? &g->edges[slot - 1] : NULL;
    if (!outgoing || outgoing->kind != GRAPH_EDGE_DATA || outgoing->to_id == n->id) {
        return NULL;
    }
    node *target = graph_node_by_id(g, outgoing->to_id);
    int is_output = 0, is_input_output = 0;
    const pin_spec *source_pin = graph_pin_by_id(n, outgoing->from_pin_id, &is_output, NULL);
    const pin_spec *target_pin = target
                                   ? graph_pin_by_id(target, outgoing->to_pin_id, &is_input_output, NULL)
                                   : NULL;
    if (!source_pin || !target_pin || !is_output || is_input_output ||
        target_pin->kind != PIN_DATA || (target_pin->flags & PIN_FLAG_SETTING)) {
        return NULL;
    }
    return outgoing;
}

/* Watches, breakpoints and comments draw on the node's own box. */
static bool annotated(const node *n)
{
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
            if (pins[p].flags & PIN_FLAG_WATCH) {
                return true;
            }
        }
    }
    return n->breakpoint || n->comment;
}

/* Nodes in the inline tree rooted at n, or 0 when n draws its own box.
 * `forced` is treated as explicitly inline. */
static int inline_size(const graph *g, const node *n, const node *forced, int remaining)
{
    node_presentation presentation = n == forced ? NODE_PRESENTATION_INLINE
                                                 : n->presentation;
    if (!remaining || presentation == NODE_PRESENTATION_EXPANDED ||
        !inline_edge(g, n)) {
        return 0;
    }
    bool automatic = presentation == NODE_PRESENTATION_AUTO;
    if (automatic && (g->math_expression || annotated(n))) {
        return 0;
    }
    int size = 1;
    for (int p = 0; p < n->input_count; p++) {
        /* A local is a named value, like a binding. */
        const edge *e = input_edge(g, n, p);
        if (!e || graph_input_local(g, n, p)) {
            continue;
        }
        int source = inline_size(g, graph_node_by_id(g, e->from_id), forced, remaining - 1);
        if (!source) {
            return 0;
        }
        size += source;
        if (automatic && size > GRAPH_INLINE_AUTO_NODES) {
            return 0;
        }
    }
    return size;
}

static bool node_indexed(const graph *g, const node *n)
{
    return g && n && n->id < (uint64_t)acount(g->id_table) && g->id_table[n->id] == n;
}

bool graph_node_is_inline(const graph *g, const node *n)
{
    return node_indexed(g, n) && inline_size(g, n, NULL, acount(g->nodes)) > 0;
}

bool graph_node_can_inline(const graph *g, const node *n)
{
    return node_indexed(g, n) && inline_size(g, n, n, acount(g->nodes)) > 0;
}

node *graph_inline_owner(const graph *g, node *n)
{
    while (graph_node_is_inline(g, n)) {
        n = graph_node_by_id(g, inline_edge(g, n)->to_id);
    }
    return n;
}

int graph_inline_source(const graph *g, const node *target, int pin, node **src, int *src_pin)
{
    if (!node_indexed(g, target) || !src || !src_pin || pin < 0 ||
        pin >= target->input_count || target->input_pins[pin].kind != PIN_DATA) {
        return 0;
    }
    const edge *e = input_edge(g, target, pin);
    node *source = e ? edge_from_node(g, e, src_pin) : NULL;
    if (!source || !graph_node_is_inline(g, source)) {
        return 0;
    }
    *src = source;
    return 1;
}

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin)
{
    if (!g || !n || !src || !src_pin || pin < 0 || pin >= n->input_count) {
        return 0;
    }
    const edge *e = input_edge(g, n, pin);
    if (e) {
        *src = edge_from_node(g, e, src_pin);
        return *src ? 1 : 0;
    }
    return 0;
}

static int graph_append_reference_field(graph *g, const pin_binding *base, const char *field, pin_binding *out)
{
    if (!g || !base || !field || !field[0] || !out ||
        (base->kind != PIN_BINDING_COMPONENT &&
         base->kind != PIN_BINDING_COMPONENT_FIELD)) {
        return -1;
    }
    char path[256];
    int written = snprintf(
        path,
        sizeof path,
        "%s%s%s",
        base->field_name ? base->field_name : "",
        base->field_name ? "." : "",
        field
    );
    if (written < 0 || (size_t)written >= sizeof path) {
        return -1;
    }
    char *owned = graph_own_str(g, path, (size_t)written);
    if (!owned) {
        return -1;
    }
    *out = *base;
    out->kind = PIN_BINDING_COMPONENT_FIELD;
    out->field_name = owned;
    return 0;
}

int graph_resolve_reference(graph *g, const node *source, int source_pin, pin_binding *binding)
{
    if (!g || !source || !binding || source_pin < 0 ||
        source_pin >= source->output_count || !node_in_graph(g, source)) {
        return -1;
    }
    const pin_spec *pin = &source->output_pins[source_pin];
    if (pin->kind != PIN_DATA || !(pin->flags & PIN_FLAG_REFERENCE)) {
        return -1;
    }
    if (pin->field_source && !pin->split_parent) {
        int parent_pin = -1;
        const type_field *field = graph_output_field(source, source_pin, &parent_pin);
        if (!field || parent_pin == source_pin) {
            return -1;
        }
        pin_binding base = { 0 };
        if (graph_resolve_reference(g, source, parent_pin, &base) != 0) {
            return -1;
        }
        return graph_append_reference_field(g, &base, field->name, binding);
    }
    if (pin->split_parent && pin->split_type_name && pin->name &&
        source->desc->component_kind == NODE_COMPONENT_GET_SELF &&
        source->desc->component && source->desc->component->key) {
        const type_desc *type = registry_type_by_name(pin->split_type_name);
        const type_field *field = type ? pin_type_field_path(type, pin->name) : NULL;
        if (!field || field->type != pin->type ||
            strcmp(field->type_name ? field->type_name : "", pin->type_name ? pin->type_name : "")) {
            return -1;
        }
        pin_binding base = { 0 };
        if (pin->field_source) {
            int parent_pin = -1;
            for (int i = 0; i < source->output_count; i++) {
                if (source->output_pins[i].id == pin->field_source) {
                    parent_pin = i;
                    break;
                }
            }
            if (parent_pin < 0 ||
                graph_resolve_reference(g, source, parent_pin, &base) != 0) {
                return -1;
            }
            const pin_spec *parent = &source->output_pins[parent_pin];
            if (!parent->field_source && pin->field_name) {
                pin_binding projected = { 0 };
                if (graph_append_reference_field(g, &base, pin->field_name, &projected) !=
                    0) {
                    return -1;
                }
                base = projected;
            }
        } else {
            base.kind = PIN_BINDING_COMPONENT;
            base.component_key = source->desc->component->key;
        }
        return graph_append_reference_field(g, &base, field->name, binding);
    }
    memset(binding, 0, sizeof *binding);
    if (source->desc->pure_kind == NODE_PURE_GET) {
        if (!source->reference_id) {
            return -1;
        }
        binding->kind = PIN_BINDING_VARIABLE;
        binding->variable_id = source->reference_id;
        binding->source_asset = g->owner ? g->owner->name : NULL;
        return 0;
    }
    if (source->desc->component_kind == NODE_COMPONENT_GET_SELF &&
        source->desc->component && source->desc->component->key &&
        source->desc->component->key[0]) {
        binding->kind = PIN_BINDING_COMPONENT;
        binding->component_key = source->desc->component->key;
        return 0;
    }
    const node_desc *getter = component_node_getter(g, source);
    if (getter && getter != source->desc) {
        binding->kind = PIN_BINDING_COMPONENT;
        binding->component_key = getter->component->key;
        return 0;
    }
    return -1;
}

const node_desc *graph_input_conversion(const graph *g, const node *n, int pin)
{
    if (!g || !n || pin < 0 || pin >= n->input_count) {
        return NULL;
    }
    const edge *e = input_edge(g, n, pin);
    return e ? e->conversion : NULL;
}

int graph_exec_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin, int *order)
{
    if (!g || !n || !src || !src_pin || !order || pin < 0 ||
        pin >= n->input_count || n->input_pins[pin].kind != PIN_EXEC) {
        return 0;
    }
    uint64_t id = n->input_pins[pin].id;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_EXEC && e->to_id == n->id && e->to_pin_id == id) {
            *src = edge_from_node(g, e, src_pin);
            if (!*src) {
                return 0;
            }
            *order = e->exec_order;
            return 1;
        }
    }
    return 0;
}

int graph_exec_input_count(const graph *g, const node *n, int pin)
{
    if (!g || !n || pin < 0 || pin >= n->input_count ||
        n->input_pins[pin].kind != PIN_EXEC) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].kind == GRAPH_EDGE_EXEC && g->edges[i].to_id == n->id &&
            g->edges[i].to_pin_id == n->input_pins[pin].id) {
            count++;
        }
    }
    return count;
}

int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{
    if (!g || !n || !dst || !dst_pin || pin < 0 || pin >= n->output_count ||
        n->output_pins[pin].kind != PIN_EXEC || order < 0) {
        return 0;
    }
    graph_edge_index *index = edge_index(g);
    uint64_t id = n->output_pins[pin].id;
    int slot = index && id < (uint64_t)acount(index->outputs)
                 ? index->outputs[id].first
                 : 0;
    for (; slot; slot = index->next[slot - 1]) {
        const edge *e = &g->edges[slot - 1];
        if (e->from_id == n->id && e->exec_order == order) {
            *dst = edge_to_node(g, e, dst_pin);
            return *dst ? 1 : 0;
        }
    }
    return 0;
}

int graph_exec_output_edges(const graph *g, const node *n, int pin, const edge **first)
{
    if (!g || !n || !first || pin < 0 || pin >= n->output_count ||
        n->output_pins[pin].kind != PIN_EXEC) {
        return 0;
    }
    *first = NULL;
    graph_edge_index *index = edge_index(g);
    uint64_t id = n->output_pins[pin].id;
    if (!index || id >= (uint64_t)acount(index->outputs) || !index->outputs[id].first) {
        return 0;
    }
    const edge *e = &g->edges[index->outputs[id].first - 1];
    if (e->from_id != n->id) {
        return 0;
    }
    *first = e;
    return index->outputs[id].count;
}

int graph_exec_target_at(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{
    return graph_exec_output_target(g, n, pin, order, dst, dst_pin);
}

node *graph_node_by_id(const graph *g, uint64_t id)
{
    if (!g || id == 0 || id >= (uint64_t)acount(g->id_table)) {
        return NULL;
    }
    return g->id_table[id];
}

const pin_spec *graph_pin_by_id(const node *n, uint64_t id, int *is_output, int *index)
{
    if (!n || id == 0) {
        return NULL;
    }
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].id == id) {
            if (is_output) {
                *is_output = 0;
            }
            if (index) {
                *index = i;
            }
            return &n->input_pins[i];
        }
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].id == id) {
            if (is_output) {
                *is_output = 1;
            }
            if (index) {
                *index = i;
            }
            return &n->output_pins[i];
        }
    }
    return NULL;
}

int graph_set_pin_default(graph *g, node *n, int output, int index, const pin_value *value)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || !value) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || p[index].kind != PIN_DATA) {
        return -1;
    }
    component_node_kind component = component_node_kind_of(n->desc);
    if (!output && component && index == component_node_setting(n)) {
        if (value->type != PIN_NAME || component_node_retarget(g, n, value->str)) {
            return -1;
        }
        graph_changed(g);
        return 0;
    }
    if ((p[index].flags & PIN_FLAG_WILDCARD) && p[index].type == PIN_FLOW) {
        if (graph_set_pin_type(g, n, output, index, value->type, value->type_name) != 0) {
            return -1;
        }
        p = output ? n->output_pins : n->input_pins;
    }
    if (value->type != p[index].type) {
        return -1;
    }
    pin_value owned = *value;
    owned.container = p[index].container;
    owned.type_name = p[index].type_name;
    pin_value source = owned;
    if (owned.container == PIN_CONTAINER_SINGLE && owned.type == PIN_STRUCT &&
        owned.ptr && pin_value_copy_to_graph(g, &owned, &source)) {
        return -1;
    }
    p[index].default_value = owned;
    p[index].default_value.container = p[index].container;
    p[index].default_value.type_name = p[index].type_name;
    if (p[index].container >= PIN_CONTAINER_ARRAY && value->ptr) {
        p[index].default_value.ptr = pin_array_clone_to_graph(
            g,
            (const pin_array *)value->ptr
        );
    }
    if (p[index].container == PIN_CONTAINER_SINGLE &&
        (value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        char *s = graph_own_str(g, value->str, strlen(value->str));
        if (!s) {
            return -1;
        }
        p[index].default_value.str = s;
    }
    graph_changed(g);
    return 0;
}

int graph_set_pin_name(graph *g, node *n, int output, int index, const char *name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || !name) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count) {
        return -1;
    }
    char *copy = graph_own_str(g, name, strlen(name));
    if (!copy) {
        return -1;
    }
    p[index].name = copy;
    graph_changed(g);
    return 0;
}

/* Whether an edge can still carry out to in after a retype, through its
   conversion or directly. */
static bool edge_carries(edge *e, const pin_spec *out, const pin_spec *in)
{
    if (e->conversion && conversion_matches_pins(out, in, e->conversion)) {
        return true;
    }
    e->conversion = NULL;
    return pin_compatible(out, in);
}

int graph_set_pin_type(graph *g, node *n, int output, int index, pin_type type, const char *type_name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count) {
        return -1;
    }
    char *owned_type_name = type_name
                              ? graph_own_str(g, type_name, strlen(type_name))
                              : NULL;
    if (type_name && !owned_type_name) {
        return -1;
    }
    p[index].type = type;
    p[index].type_name = owned_type_name;
    p[index].default_value = (pin_value){ .type = type,
                                          .container = p[index].container,
                                          .type_name = owned_type_name };
    if (p[index].flags & PIN_FLAG_WILDCARD) {
        graph_wildcard_resolve_group(g, n, p[index].group, type, type_name);
    }

    /* Retyping invalidates only edges that can no longer carry the new pin. */
    for (int i = 0; i < acount(g->edges);) {
        edge *e = &g->edges[i];
        int attached = (output && e->from_id == n->id &&
                        e->from_pin_id == p[index].id) ||
                       (!output && e->to_id == n->id && e->to_pin_id == p[index].id);
        if (attached) {
            const pin_spec *other = NULL;
            if (output) {
                int tp = -1;
                node *to = edge_to_node(g, e, &tp);
                other = to ? node_input_pin(to, tp) : NULL;
            } else {
                int fp = -1;
                node *from = edge_from_node(g, e, &fp);
                other = from ? node_output_pin(from, fp) : NULL;
            }
            if (!other ||
                !edge_carries(e, output ? p + index : other, output ? other : p + index)) {
                /* A retyped source can strand a wildcard target; return it
                 * to Generic (PIN-11). The retyped pin itself is not a
                 * wildcard victim: the user retyped it on purpose. */
                if (output) {
                    graph_wildcard_untouch(g, e->to_id, e->to_pin_id);
                }
                adel(g->edges, i);
                continue;
            }
        }
        i++;
    }
    graph_changed(g);
    return 0;
}

int graph_set_pin_container(graph *g, node *n, int output, int index, pin_container container)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || container < PIN_CONTAINER_SINGLE ||
        container > PIN_CONTAINER_MAP) {
        return -1;
    }
    if (p[index].container == container) {
        return 0;
    }
    p[index].container = container;
    p[index].default_value = (pin_value){ .type = p[index].type,
                                          .container = container,
                                          .type_name = p[index].type_name };
    graph_changed(g);
    return 0;
}

const pin_binding *graph_pin_binding(const graph *g, const node *n, int output, int index)
{
    if (!g || !n || output || index < 0 || index >= n->input_count ||
        n->input_pins[index].kind != PIN_DATA) {
        return NULL;
    }
    return n->input_pins[index].binding;
}

static const char *graph_binding_str(graph *g, const char *value)
{
    return value ? graph_own_str(g, value, strlen(value)) : NULL;
}

int graph_set_pin_binding(graph *g, node *n, int output, int index, const pin_binding *binding)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || output || index < 0 ||
        index >= n->input_count || n->input_pins[index].kind != PIN_DATA) {
        return -1;
    }
    if (binding && binding->kind != PIN_BINDING_LOCAL && g->owner &&
        (g->owner->domain == BEHAVIOR_DOMAIN_LIBRARY ||
         g->owner->domain == BEHAVIOR_DOMAIN_MATERIAL ||
         g->owner->domain == BEHAVIOR_DOMAIN_SHAPE ||
         g->owner->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION ||
         g->owner->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY)) {
        return -1;
    }
    if (!binding) {
        pin_spec *p = &n->input_pins[index];
        if (p->binding && (p->flags & PIN_FLAG_WILDCARD) &&
            p->type != PIN_FLOW && p->default_value.type == PIN_FLOW) {
            p->default_value = (pin_value){
                .type = p->type,
                .container = p->container,
                .type_name = p->type_name,
            };
        }
        p->binding = NULL;
        graph_changed(g);
        return 0;
    }
    if (binding->kind != PIN_BINDING_VARIABLE &&
        binding->kind != PIN_BINDING_COMPONENT_FIELD &&
        binding->kind != PIN_BINDING_DELTA_TIME &&
        binding->kind != PIN_BINDING_COMPONENT && binding->kind != PIN_BINDING_LOCAL &&
        binding->kind != PIN_BINDING_EVENT_OUTPUT) {
        return -1;
    }
    if (binding->kind == PIN_BINDING_LOCAL && !graph_local_by_id(g, binding->local_id)) {
        return -1;
    }
    if (binding->kind == PIN_BINDING_VARIABLE && !binding->variable_id) {
        return -1;
    }
    if (binding->kind == PIN_BINDING_COMPONENT_FIELD &&
        (!binding->component_key || !binding->component_key[0] ||
         !binding->field_name || !binding->field_name[0])) {
        return -1;
    }
    if (binding->kind == PIN_BINDING_COMPONENT &&
        (!binding->component_key || !binding->component_key[0] || binding->field_name)) {
        return -1;
    }
    if (binding->kind == PIN_BINDING_EVENT_OUTPUT &&
        (!binding->event_node_id || !binding->event_pin_id)) {
        return -1;
    }
    pin_binding *copy = graph_own_block(g, sizeof *copy);
    if (!copy) {
        return -1;
    }
    *copy = *binding;
    const char *source_asset = binding->source_asset;
    if (!source_asset && binding->kind == PIN_BINDING_VARIABLE && g->owner) {
        source_asset = g->owner->name;
    }
    copy->source_asset = graph_binding_str(g, source_asset);
    copy->component_key = graph_binding_str(g, binding->component_key);
    copy->field_name = graph_binding_str(g, binding->field_name);
    if ((binding->source_asset && !copy->source_asset) ||
        (binding->component_key && !copy->component_key) ||
        (binding->field_name && !copy->field_name)) {
        return -1;
    }
    n->input_pins[index].binding = copy;
    graph_changed(g);
    return 0;
}

int graph_move_pin(graph *g, node *n, int output, int index, int target_index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || target_index < 0 || target_index >= count) {
        return -1;
    }
    if (index == target_index) {
        return index;
    }
    pin_spec moved_pin = pins[index];
    if (index < target_index) {
        memmove(pins + index, pins + index + 1, (size_t)(target_index - index) * sizeof *pins);
    } else {
        memmove(pins + target_index + 1, pins + target_index, (size_t)(index - target_index) * sizeof *pins);
    }
    pins[target_index] = moved_pin;

    graph_changed(g);
    return target_index;
}

int graph_set_node_disabled(graph *g, node *n, int disabled)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    n->disabled = disabled != 0;
    graph_changed(g);
    return 0;
}

int graph_set_node_presentation(graph *g, node *n, node_presentation presentation)
{
    assert(presentation >= NODE_PRESENTATION_AUTO && presentation <= NODE_PRESENTATION_EXPANDED);
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    n->presentation = presentation;
    graph_changed(g);
    return 0;
}

int graph_presentation_by_name(const char *name)
{
    for (int i = NODE_PRESENTATION_AUTO; name && i <= NODE_PRESENTATION_EXPANDED; i++) {
        if (!strcmp(name, node_presentation_name(i))) {
            return i;
        }
    }
    return -1;
}

int graph_inline_node(graph *g, node *n)
{
    if (!graph_node_can_inline(g, n) ||
        graph_set_node_presentation(g, n, NODE_PRESENTATION_AUTO) != 0) {
        return -1;
    }
    return graph_node_is_inline(g, n)
             ? 0
             : graph_set_node_presentation(g, n, NODE_PRESENTATION_INLINE);
}

int graph_set_breakpoint(graph *g, node *n, int enabled)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    n->breakpoint = enabled != 0;
    n->breakpoint_enabled = 1;
    graph_changed(g);
    return 0;
}

int graph_set_node_comment(graph *g, node *n, const char *comment, bool hidden)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    bool empty = !comment || !comment[0];
    n->comment = empty ? NULL : graph_own_str(g, comment, strlen(comment));
    n->comment_hidden = !empty && hidden;
    graph_changed(g);
    return 0;
}

int graph_set_all_breakpoints(graph *g, int enabled)
{
    if (!graph_mutation_allowed(g)) {
        return -1;
    }
    g->breakpoints_enabled = enabled != 0;
    graph_changed(g);
    return 0;
}

int graph_set_pin_watch(graph *g, node *n, int output, int index, int enabled)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || pins[index].kind != PIN_DATA) {
        return -1;
    }
    if (enabled) {
        pins[index].flags |= PIN_FLAG_WATCH;
    } else {
        pins[index].flags &= ~PIN_FLAG_WATCH;
    }
    graph_changed(g);
    return 0;
}

graph_comment_box *graph_add_comment_box(graph *g, const char *title, float x, float y, float width, float height)
{
    if (!graph_mutation_allowed(g) || !title || width < 0.0f || height < 0.0f) {
        return NULL;
    }
    graph_comment_box box = {
        .id = graph_alloc_id(g),
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .title = graph_own_str(g, title, strlen(title)),
    };
    apush(g->comment_boxes, box);
    graph_changed(g);
    return &g->comment_boxes[acount(g->comment_boxes) - 1];
}

int graph_remove_comment_box(graph *g, uint64_t id)
{
    if (!graph_mutation_allowed(g)) {
        return -1;
    }
    for (int i = 0; i < acount(g->comment_boxes); i++) {
        if (g->comment_boxes[i].id == id) {
            adel(g->comment_boxes, i);
            graph_changed(g);
            return 0;
        }
    }
    return -1;
}

int graph_set_comment_box(graph *g, graph_comment_box *box, const char *title, uint32_t color)
{
    if (!graph_mutation_allowed(g) || !box || !title) {
        return -1;
    }
    int found = 0;
    for (int i = 0; i < acount(g->comment_boxes); i++) {
        found |= &g->comment_boxes[i] == box;
    }
    if (!found) {
        return -1;
    }
    box->title = graph_own_str(g, title, strlen(title));
    box->color = color;
    graph_changed(g);
    return 0;
}

int graph_set_node_reference(graph *g, node *n, uint64_t reference_id, const char *reference_name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    n->reference_id = reference_id;
    if (reference_name) {
        char *copy = graph_own_str(g, reference_name, strlen(reference_name));
        if (!copy) {
            return -1;
        }
        n->reference_name = copy;
    } else {
        n->reference_name = NULL;
    }
    graph_changed(g);
    return 0;
}

int graph_add_pin(graph *g, node *n, int output, const pin_spec *template_pin)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || !template_pin) {
        return -1;
    }
    int *count = output ? &n->output_count : &n->input_count;
    pin_spec **pins = output ? &n->output_pins : &n->input_pins;
    int next = *count + 1;
    pin_spec *grown_pins = (pin_spec *)xmalloc((size_t)next * sizeof *grown_pins);
    if (*count) {
        memcpy(grown_pins, *pins, (size_t)*count * sizeof *grown_pins);
    }
    free(*pins);
    *pins = grown_pins;
    init_pin(&(*pins)[*count], template_pin, g);
    (*count)++;
    graph_changed(g);
    return *count - 1;
}

static const type_desc *graph_find_type(const char *name)
{
    return registry_type_by_name(name);
}

const type_desc *graph_output_struct(const node *n, int index)
{
    if (!n || index < 0 || index >= n->output_count) {
        return NULL;
    }
    const node_desc *desc = n->desc;
    bool signature = desc == &NODE_Event || desc == &NODE_FunctionEntry ||
                     desc == &NODE_CallFunction || desc == &NODE_CallPureFunction ||
                     desc == &NODE_BehaviorExpression || desc == &NODE_CallMacro;
    if (!signature && index >= desc->output_count && !n->output_pins[index].field_source) {
        return NULL;
    }
    const pin_spec *pin = &n->output_pins[index];
    if (pin->kind != PIN_DATA || pin->type != PIN_STRUCT ||
        pin->container != PIN_CONTAINER_SINGLE || pin->split_parent) {
        return NULL;
    }
    return pin->type_name ? registry_type_by_name(pin->type_name) : NULL;
}

const type_field *graph_output_field(const node *n, int index, int *source_index)
{
    if (!n || index < 0 || index >= n->output_count) {
        return NULL;
    }
    const pin_spec *pin = &n->output_pins[index];
    if (!pin->field_source || !pin->field_name || !pin->field_name[0] ||
        pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE ||
        pin->split_parent) {
        return NULL;
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].id != pin->field_source) {
            continue;
        }
        const type_desc *type = graph_output_struct(n, i);
        if (!type) {
            return NULL;
        }
        for (int f = 0; f < type->field_count; f++) {
            const type_field *field = &type->fields[f];
            if (strcmp(field->name, pin->field_name) != 0) {
                continue;
            }
            if (pin->type != field->type || strcmp(
                                                pin->type_name ? pin->type_name : "",
                                                field->type_name ? field->type_name : ""
                                            ) != 0) {
                return NULL;
            }
            if (source_index) {
                *source_index = i;
            }
            return field;
        }
    }
    return NULL;
}

pin_spec graph_field_pin(const node *n, int source_index, const type_field *field)
{
    const pin_spec *source = &n->output_pins[source_index];
    return (pin_spec){ .name = field->name,
                       .type = field->type,
                       .type_name = field->type_name,
                       .kind = PIN_DATA,
                       .field_source = source->id,
                       .field_name = field->name,
                       .flags = source->flags & PIN_FLAG_REFERENCE };
}

int graph_expose_output_fields(graph *g, node *n, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    const type_desc *type = graph_output_struct(n, index);
    if (!type || !type->field_count) {
        return -1;
    }
    uint64_t source = n->output_pins[index].id;
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        bool exists = false;
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            exists |= pin->field_source == source && pin->field_name &&
                      strcmp(pin->field_name, field->name) == 0;
        }
        if (exists) {
            continue;
        }
        pin_spec pin = graph_field_pin(n, index, field);
        if (graph_add_pin(g, n, 1, &pin) < 0) {
            return -1;
        }
    }
    return 0;
}

static bool graph_output_has_children(const node *n, uint64_t source)
{
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].field_source == source) {
            return true;
        }
    }
    return false;
}

int graph_hide_output_fields(graph *g, node *n, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) ||
        !graph_output_struct(n, index)) {
        return -1;
    }
    uint64_t source = n->output_pins[index].id;
    bool removed;
    do {
        removed = false;
        for (int i = n->output_count - 1; i >= 0; i--) {
            pin_spec *pin = &n->output_pins[i];
            if (pin->field_source != source || graph_output_has_children(n, pin->id) ||
                graph_output_local(g, n, i)) {
                continue;
            }
            bool connected = false;
            for (int e = 0; e < acount(g->edges); e++) {
                connected |= g->edges[e].from_id == n->id &&
                             g->edges[e].from_pin_id == pin->id;
            }
            if (connected) {
                continue;
            }
            if (graph_remove_pin(g, n, 1, i)) {
                return -1;
            }
            removed = true;
            break;
        }
    } while (removed);
    return 0;
}

int graph_split_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    /* Component nodes split into their field pins: Set writes each field it
     * shows, and Get exposes fields beside Value. */
    component_node_kind component = component_node_kind_of(n->desc);
    if (component) {
        if (index < 0 || index >= (output ? n->output_count : n->input_count)) {
            return -1;
        }
        const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
        if (pin->type != PIN_STRUCT || pin->field_source) {
            return -1;
        }
        int result = output ? graph_expose_output_fields(g, n, index)
                            : component_node_split_value(g, n, true);
        return result ? -1 : index;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || pins[index].kind != PIN_DATA ||
        pins[index].split_parent) {
        return -1;
    }
    const pin_spec parent = pins[index];
    const type_desc *type = parent.type == PIN_STRUCT
                              ? graph_find_type(parent.type_name)
                              : NULL;
    if (!type || !type->field_count) {
        return -1;
    }
    uint64_t parent_id = parent.id;
    int first = index;
    if (graph_remove_pin(g, n, output, index)) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        pin_spec child = {
            .name = type->fields[i].name,
            .type = type->fields[i].type,
            .type_name = type->fields[i].type_name,
            .kind = PIN_DATA,
            .container = PIN_CONTAINER_SINGLE,
            .split_parent = parent_id,
            .field_source = parent.field_source,
            .field_name = parent.field_name,
            .split_type_name = type->name,
            .flags = parent.flags & PIN_FLAG_REFERENCE,
        };
        int at = graph_add_pin(g, n, output, &child);
        if (at < 0) {
            return -1;
        }
        if (output) {
            n->output_pins[at].split_parent = parent_id;
        } else {
            n->input_pins[at].split_parent = parent_id;
        }
    }
    int last = output ? n->output_count - 1 : n->input_count - 1;
    for (int i = last; i >= first; i--) {
        graph_move_pin(g, n, output, i, first);
    }
    return first;
}

static const type_desc *graph_split_type(const pin_spec *pin)
{
    for (int i = 0; i < registry_type_count(); i++) {
        const type_desc *type = registry_type(i);
        for (int field = 0; field < type->field_count; field++) {
            if (strcmp(type->fields[field].name, pin->name ? pin->name : "") == 0 &&
                type->fields[field].type == pin->type &&
                strcmp(
                    type->fields[field].type_name ? type->fields[field].type_name : "",
                    pin->type_name ? pin->type_name : ""
                ) == 0) {
                return type;
            }
        }
    }
    return NULL;
}

int graph_recombine_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    component_node_kind component = component_node_kind_of(n->desc);
    if (component) {
        /* Set's field inputs, and Value, follow Exec, Component and Entity. */
        int first = component_node_setting(n) + 2;
        return !output && component_node_split(n) && index >= first &&
                       index < n->input_count &&
                       !component_node_split_value(g, n, false)
                 ? first
                 : -1;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || !pins[index].split_parent) {
        return -1;
    }
    uint64_t parent_id = pins[index].split_parent;
    uint64_t field_source = pins[index].field_source;
    const char *field_name = pins[index].field_name;
    const char *parent_name = pins[index].field_source ? pins[index].field_name : NULL;
    const type_desc *type = pins[index].split_type_name
                              ? graph_find_type(pins[index].split_type_name)
                              : graph_split_type(&pins[index]);
    if (!type) {
        return -1;
    }
    int first = index;
    while (first > 0 && pins[first - 1].split_parent == parent_id) {
        first--;
    }
    bool reference = false;
    for (int i = first; i < count; i++) {
        if (pins[i].split_parent == parent_id && (pins[i].flags & PIN_FLAG_REFERENCE)) {
            reference = true;
            break;
        }
    }
    for (int i = count - 1; i >= 0; i--) {
        pins = output ? n->output_pins : n->input_pins;
        if (pins[i].split_parent == parent_id && graph_remove_pin(g, n, output, i)) {
            return -1;
        }
    }
    pin_spec parent = {
        .name = parent_name ? parent_name : type->name,
        .type = PIN_STRUCT,
        .type_name = type->name,
        .kind = PIN_DATA,
        .field_source = field_source,
        .field_name = field_name,
        .flags = reference ? PIN_FLAG_REFERENCE : 0,
    };
    int at = graph_add_pin(g, n, output, &parent);
    if (at < 0) {
        return -1;
    }
    pins = output ? n->output_pins : n->input_pins;
    pins[at].id = parent_id;
    graph_move_pin(g, n, output, at, first < (output ? n->output_count : n->input_count) ? first : at);
    return first;
}

int graph_remove_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
    }
    int *count = output ? &n->output_count : &n->input_count;
    pin_spec **pins = output ? &n->output_pins : &n->input_pins;
    if (index < 0 || index >= *count) {
        return -1;
    }
    uint64_t pin_id = (*pins)[index].id;
    if (output) {
        for (int i = acount(g->locals) - 1; i >= 0; i--) {
            if (g->locals[i].source_id == n->id && g->locals[i].source_pin_id == pin_id) {
                graph_expand_local(g, g->locals[i].id);
            }
        }
    }
    if (output) {
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].field_source == pin_id) {
                return -1;
            }
        }
    }
    int w = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *e = &g->edges[i];
        if ((output && e->from_id == n->id && e->from_pin_id == pin_id) ||
            (!output && e->to_id == n->id && e->to_pin_id == pin_id)) {
            continue;
        }
        g->edges[w++] = *e;
    }
    asetlen(g->edges, w);
    memmove(*pins + index, *pins + index + 1, (size_t)(*count - index - 1) * sizeof **pins);
    (*count)--;
    if (*count == 0) {
        free(*pins);
        *pins = NULL;
    } else {
        *pins = (pin_spec *)xrealloc(*pins, (size_t)*count * sizeof **pins);
    }
    graph_changed(g);
    graph_prune_locals(g);
    return 0;
}

void *graph_own_block(graph *g, size_t size)
{
    if (!g || size == 0) {
        return NULL;
    }
    void *block = xcalloc(1, size);
    apush(g->blocks, block);
    return block;
}

void graph_free(graph *g)
{
    if (!g) {
        return;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        free(g->nodes[i]->input_pins);
        free(g->nodes[i]->output_pins);
        free(g->nodes[i]);
    }
    afree(g->nodes);
    afree(g->id_table);
    afree(g->edges);
    afree(g->locals);
    if (g->edge_index) {
        afree(g->edge_index->inputs);
        afree(g->edge_index->outputs);
        afree(g->edge_index->next);
        afree(g->edge_index->sources);
        free(g->edge_index);
    }
    afree(g->comment_boxes);
    for (int i = 0; i < acount(g->blocks); i++) {
        free(g->blocks[i]);
    }
    afree(g->blocks);
    for (int i = 0; i < acount(g->strings); i++) {
        free(g->strings[i]);
    }
    afree(g->strings);
    memset(g, 0, sizeof *g);
}

int graph_set_event_name(graph *g, node *event_node, const char *name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, event_node) ||
        !event_node->desc || event_node->desc->flow_kind != NODE_FLOW_EVENT ||
        !name || !name[0]) {
        return -1;
    }
    char *copy = graph_own_str(g, name, strlen(name));
    if (!copy) {
        return -1;
    }
    event_node->reference_name = copy;
    graph_changed(g);
    return 0;
}

const char *graph_event_name(const node *event_node)
{
    return event_node ? event_node->reference_name : NULL;
}

int graph_set_metadata(graph *g, const char *name, const char *description)
{
    if (!graph_mutation_allowed(g) || !name || !description) {
        return -1;
    }
    char *n = graph_own_str(g, name, strlen(name));
    char *d = graph_own_str(g, description, strlen(description));
    if (!n || !d) {
        return -1;
    }
    g->name = n;
    g->description = d;
    graph_changed(g);
    return 0;
}

char *graph_own_str(graph *g, const char *s, size_t len)
{
    if (!g || !s) {
        return NULL;
    }
    char *copy = (char *)xmalloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    apush(g->strings, copy);
    return copy;
}

const char *graph_node_name_literal(const graph *g, const node *n)
{
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *p = &n->input_pins[i];
        node *source = NULL;
        int pin = -1;
        if (p->name && !strcmp(p->name, "Name") &&
            (p->type == PIN_STR || p->type == PIN_NAME) &&
            p->default_value.str && p->default_value.str[0] && !p->binding &&
            !graph_input_source(g, n, i, &source, &pin)) {
            return p->default_value.str;
        }
    }
    return NULL;
}
