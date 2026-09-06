#include "graph.h"

#include "conv_nodes.h"
#include "pin_conv.h"
#include "registry.h"
#include "value_array.h"

#include <stdlib.h>
#include <string.h>

void graph_init(graph *g)
{
    if (!g) {
        return;
    }
    memset(g, 0, sizeof *g);
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

static void graph_changed(graph *g)
{
    if (g) {
        g->topology_revision++;
    }
}

static int graph_mutation_allowed(const graph *g)
{
    return g && g->flow_pause_count == 0;
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
    afree(g->id_table);
    for (int i = 0; i < acount(g->nodes); i++) {
        graph_index_node(g, g->nodes[i]);
    }
}

static void init_pin(pin_spec *dst, const pin_spec *src, graph *g)
{
    *dst = *src;
    dst->id = graph_alloc_id(g);
    dst->name = src->name ? graph_own_str(g, src->name, strlen(src->name)) : NULL;
    dst->field_name = src->field_name
                        ? graph_own_str(g, src->field_name, strlen(src->field_name))
                        : NULL;
    dst->default_value.type = dst->type;
    dst->default_value.container = dst->container;
    dst->default_value.type_name = dst->type_name;
    if ((dst->type == PIN_STR || dst->type == PIN_NAME) && !dst->default_value.str) {
        dst->default_value.str = "";
    }
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

static void graph_wildcard_resolve_group(graph *g, node *n, int group, pin_type type, const char *type_name)
{
    if (group == 0) {
        return;
    }
    for (int output = 0; output <= 1; output++) {
        pin_spec *pins = output ? n->output_pins : n->input_pins;
        int count = output ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            pin_spec *p = &pins[i];
            if (!(p->flags & PIN_FLAG_WILDCARD) || p->group != group) {
                continue;
            }
            char *copy = type_name ? graph_own_str(g, type_name, strlen(type_name))
                                   : NULL;
            p->type = type;
            p->type_name = copy;
            p->default_value.type = type;
            p->default_value.container = p->container;
            p->default_value.type_name = copy;
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
    if (out->flags & PIN_FLAG_WILDCARD && !(in->flags & PIN_FLAG_WILDCARD)) {
        graph_wildcard_resolve_group(g, from, out->group, in->type, in->type_name);
        out = &from->output_pins[from_pin];
    }
    if (in->flags & PIN_FLAG_WILDCARD) {
        if (conversion && !numeric_wildcard) {
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
        p->type = value->type;
        p->type_name = owned;
        p->container = value->container;
        p->default_value = (pin_value){ .type = value->type,
                                        .container = value->container,
                                        .type_name = owned };
        graph_wildcard_resolve_group(g, to, p->group, value->type, value->type_name);
        in = &to->input_pins[to_pin];
    }
    if (!conversion && !pin_compatible(out, in)) {
        return -1;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->to_id == to->id && old->to_pin_id == in->id) {
            if (old->kind != GRAPH_EDGE_DATA) {
                return -1;
            }
            *old = make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0, conversion);
            graph_changed(g);
            return 0;
        }
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
    p->type = PIN_FLOW;
    p->type_name = NULL;
    p->container = PIN_CONTAINER_SINGLE;
    p->default_value = (pin_value){ .type = PIN_FLOW };
}

int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    return graph_connect_data(g, from, from_pin, to, to_pin);
}

int graph_connect_data_ids(graph *g, uint64_t from_node, uint64_t from_pin, uint64_t to_node, uint64_t to_pin)
{
    node *from = graph_node_by_id(g, from_node), *to = graph_node_by_id(g, to_node);
    int fp = -1, tp = -1, out = 0, in = 0;
    if (!graph_pin_by_id(from, from_pin, &out, &fp) ||
        !graph_pin_by_id(to, to_pin, &in, &tp) || !out || in) {
        return -1;
    }
    return graph_connect_data(g, from, fp, to, tp);
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
        adel(g->edges, i);
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

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin)
{
    if (!g || !n || !src || !src_pin || pin < 0 || pin >= n->input_count) {
        return 0;
    }
    uint64_t id = n->input_pins[pin].id;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_DATA && e->to_id == n->id && e->to_pin_id == id) {
            *src = edge_from_node(g, e, src_pin);
            return *src ? 1 : 0;
        }
    }
    return 0;
}

const node_desc *graph_input_conversion(const graph *g, const node *n, int pin)
{
    if (!g || !n || pin < 0 || pin >= n->input_count) {
        return NULL;
    }
    uint64_t id = n->input_pins[pin].id;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_DATA && e->to_id == n->id && e->to_pin_id == id) {
            return e->conversion;
        }
    }
    return NULL;
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

int graph_exec_input_source_at(
    const graph *g,
    const node *n,
    int pin,
    int ordinal,
    node **src,
    int *src_pin,
    int *order
)
{
    if (!g || !n || !src || !src_pin || !order || ordinal < 0 || pin < 0 ||
        pin >= n->input_count || n->input_pins[pin].kind != PIN_EXEC) {
        return 0;
    }
    int at = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->to_id != n->id ||
            e->to_pin_id != n->input_pins[pin].id) {
            continue;
        }
        if (at++ != ordinal) {
            continue;
        }
        *src = edge_from_node(g, e, src_pin);
        if (!*src) {
            return 0;
        }
        *order = e->exec_order;
        return 1;
    }
    return 0;
}

int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{
    if (!g || !n || !dst || !dst_pin || pin < 0 || pin >= n->output_count ||
        n->output_pins[pin].kind != PIN_EXEC || order < 0) {
        return 0;
    }
    uint64_t id = n->output_pins[pin].id;
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].kind == GRAPH_EDGE_EXEC && g->edges[i].from_id == n->id &&
            g->edges[i].from_pin_id == id && g->edges[i].exec_order == order) {
            *dst = edge_to_node(g, &g->edges[i], dst_pin);
            return *dst ? 1 : 0;
        }
    }
    return 0;
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
    if (index < 0 || index >= count || p[index].kind != PIN_DATA ||
        value->type != p[index].type) {
        return -1;
    }
    p[index].default_value = *value;
    p[index].default_value.container = p[index].container;
    p[index].default_value.type_name = p[index].type_name;
    if (p[index].container >= PIN_CONTAINER_ARRAY && value->ptr) {
        p[index].default_value.ptr = pin_array_clone_to_graph(
            g,
            (const pin_array *)value->ptr
        );
    }
    if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
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
        .color = 0xff808080u,
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
    memcpy(grown_pins, *pins, (size_t)*count * sizeof *grown_pins);
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
    bool signature = desc == &NODE_FunctionEntry || desc == &NODE_CallFunction ||
                     desc == &NODE_CallPureFunction || desc == &NODE_CallMacro;
    if (!signature && index >= desc->output_count) {
        return NULL;
    }
    const pin_spec *pin = &n->output_pins[index];
    if (pin->kind != PIN_DATA || pin->type != PIN_STRUCT ||
        pin->container != PIN_CONTAINER_SINGLE || pin->field_source ||
        pin->split_parent) {
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
        pin_spec pin = { .name = field->name,
                         .type = field->type,
                         .type_name = field->type_name,
                         .kind = PIN_DATA,
                         .field_source = source,
                         .field_name = field->name };
        if (graph_add_pin(g, n, 1, &pin) < 0) {
            return -1;
        }
    }
    return 0;
}

int graph_hide_output_fields(graph *g, node *n, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) ||
        !graph_output_struct(n, index)) {
        return -1;
    }
    uint64_t source = n->output_pins[index].id;
    for (int i = n->output_count - 1; i >= 0; i--) {
        if (n->output_pins[i].field_source != source) {
            continue;
        }
        bool connected = false;
        for (int e = 0; e < acount(g->edges); e++) {
            connected |= g->edges[e].from_id == n->id &&
                         g->edges[e].from_pin_id == n->output_pins[i].id;
        }
        if (!connected && graph_remove_pin(g, n, 1, i)) {
            return -1;
        }
    }
    return 0;
}

int graph_split_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) {
        return -1;
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
            .split_type_name = type->name,
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
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || !pins[index].split_parent) {
        return -1;
    }
    uint64_t parent_id = pins[index].split_parent;
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
    for (int i = count - 1; i >= 0; i--) {
        pins = output ? n->output_pins : n->input_pins;
        if (pins[i].split_parent == parent_id && graph_remove_pin(g, n, output, i)) {
            return -1;
        }
    }
    pin_spec parent = {
        .name = type->name,
        .type = PIN_STRUCT,
        .type_name = type->name,
        .kind = PIN_DATA,
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
