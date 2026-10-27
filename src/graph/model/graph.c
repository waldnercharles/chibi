#include "graph/model/graph.h"

#include "graph/model/behavior_asset.h"
#include "graph/model/graph_internal.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/value_copy.h"
#include "support/arena.h"
#include "types/catalog.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int graph_contains_node(const graph *g, const node *n)
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

void graph_changed(graph *g)
{
    if (g) {
        graph_edge_index_init(g);
        g->topology_revision++;
    }
}

int graph_mutation_allowed(const graph *g)
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
    if (value.container == PIN_CONTAINER_SINGLE && value.type == PIN_NAME &&
        !value.str) {
        value.str = "";
    }
    return value;
}

static void init_pin(pin_spec *dst, const pin_spec *src, graph *g)
{
    *dst = *src;
    dst->id = graph_alloc_id(g);
    dst->name = src->name ? graph_own_str(g, src->name, strlen(src->name)) : NULL;
    dst->display_name = src->display_name
                          ? graph_own_str(g, src->display_name, strlen(src->display_name))
                          : NULL;
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
void graph_wildcard_resolve_pin(graph *g, pin_spec *p, pin_type type, const char *type_name)
{
    char *copy = type_name ? graph_own_str(g, type_name, strlen(type_name)) : NULL;
    /* Only aggregates, enums and handles are named types: a number spelled
     * "number" is the same number as an unnamed one. */
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

void graph_wildcard_resolve_group(graph *g, node *n, int group, pin_type type, const char *type_name)
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

edge graph_make_edge(
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, from) ||
        !graph_contains_node(g, to) || exec_order < 0) {
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
            *old = graph_make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_EXEC, exec_order, NULL);
            graph_changed(g);
            return 0;
        }
    }
    apush(g->edges, graph_make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_EXEC, exec_order, NULL));
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

node *graph_edge_source(const graph *g, const edge *e, int *pin)
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

graph_edge_index *graph_edges_index(const graph *g)
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

const edge *graph_input_edge(const graph *g, const node *n, int pin)
{
    graph_edge_index *index = graph_edges_index(g);
    uint64_t id = n->input_pins[pin].id;
    int slot = index && id < (uint64_t)acount(index->inputs) ? index->inputs[id] : 0;
    const edge *e = slot ? &g->edges[slot - 1] : NULL;
    return e && e->to_id == n->id ? e : NULL;
}

bool graph_vector_descriptor(const node_desc *desc)
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
    return n && n->desc && graph_vector_descriptor(n->desc);
}

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin)
{
    if (!g || !n || !src || !src_pin || pin < 0 || pin >= n->input_count) {
        return 0;
    }
    const edge *e = graph_input_edge(g, n, pin);
    if (e) {
        *src = graph_edge_source(g, e, src_pin);
        return *src ? 1 : 0;
    }
    return 0;
}

const node_desc *graph_input_conversion(const graph *g, const node *n, int pin)
{
    if (!g || !n || pin < 0 || pin >= n->input_count) {
        return NULL;
    }
    const edge *e = graph_input_edge(g, n, pin);
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
            *src = graph_edge_source(g, e, src_pin);
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
    graph_edge_index *index = graph_edges_index(g);
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
    graph_edge_index *index = graph_edges_index(g);
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

int graph_set_pin_name(graph *g, node *n, int output, int index, const char *name)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n) || !name) {
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

int graph_set_pin_display_name(graph *g, node *n, int output, int index, const char *name)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count) {
        return -1;
    }
    if (name && !name[0]) {
        name = NULL;
    }
    if ((!p[index].display_name && !name) ||
        (p[index].display_name && name && !strcmp(p[index].display_name, name))) {
        return 0;
    }
    char *copy = name ? graph_own_str(g, name, strlen(name)) : NULL;
    if (name && !copy) {
        return -1;
    }
    p[index].display_name = copy;
    graph_changed(g);
    return 0;
}

int graph_set_pin_type(graph *g, node *n, int output, int index, pin_type type, const char *type_name)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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
                node *from = graph_edge_source(g, e, &fp);
                other = from ? node_output_pin(from, fp) : NULL;
            }
            if (!other ||
                !pin_compatible(output ? p + index : other, output ? other : p + index)) {
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || container < PIN_CONTAINER_SINGLE ||
        container > PIN_CONTAINER_VIEW) {
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n) || output ||
        index < 0 || index >= n->input_count || n->input_pins[index].kind != PIN_DATA) {
        return -1;
    }
    if (binding && binding->kind != PIN_BINDING_LOCAL && g->owner &&
        (g->owner->domain == BEHAVIOR_DOMAIN_LIBRARY ||
         g->owner->domain == BEHAVIOR_DOMAIN_MATERIAL ||
         g->owner->domain == BEHAVIOR_DOMAIN_SHAPE ||
         g->owner->domain == BEHAVIOR_DOMAIN_MODEL ||
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    n->disabled = disabled != 0;
    graph_changed(g);
    return 0;
}

int graph_set_node_presentation(graph *g, node *n, node_presentation presentation)
{
    assert(presentation >= NODE_PRESENTATION_AUTO && presentation <= NODE_PRESENTATION_EXPANDED);
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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

int graph_set_breakpoint(graph *g, node *n, int enabled)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    n->breakpoint = enabled != 0;
    n->breakpoint_enabled = 1;
    graph_changed(g);
    return 0;
}

int graph_set_node_comment(graph *g, node *n, const char *comment, bool hidden)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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

int graph_set_node_reference_asset(graph *g, node *n, const char *asset_name)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    n->reference_asset = asset_name
                           ? graph_own_str(g, asset_name, strlen(asset_name))
                           : NULL;
    graph_changed(g);
    return 0;
}

int graph_add_pin(graph *g, node *n, int output, const pin_spec *template_pin)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n) || !template_pin) {
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

pin_spec graph_field_pin(const node *n, int source_index, const type_field *field)
{
    const pin_spec *source = &n->output_pins[source_index];
    return (pin_spec){ .name = field->name,
                       .type = field->type,
                       .type_name = field->type_name,
                       .kind = PIN_DATA,
                       .field_source = source->id,
                       .field_name = field->name,
                       .flags = (source->flags & PIN_FLAG_REFERENCE) |
                                (field->color ? PIN_FLAG_COLOR : 0) };
}

int graph_remove_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
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
    if (g->value_storage) {
        arena_free(g->value_storage);
        free(g->value_storage);
    }
    for (int i = 0; i < acount(g->strings); i++) {
        free(g->strings[i]);
    }
    afree(g->strings);
    memset(g, 0, sizeof *g);
}

int graph_set_event_name(graph *g, node *event_node, const char *name)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, event_node) ||
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
            (p->type == PIN_STR || p->type == PIN_NAME) && !p->binding &&
            !graph_input_source(g, n, i, &source, &pin)) {
            const char *text = graph_text_cstring(g, p->default_value);
            if (text && *text) {
                return text;
            }
        }
    }
    return NULL;
}
