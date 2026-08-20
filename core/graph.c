#include "graph.h"

#include "pin_conv.h"

#include <stdlib.h>
#include <string.h>

void graph_init(graph *g)
{
    if (!g) return;
    memset(g, 0, sizeof *g);
    g->id_counter = 1;
    g->topology_revision = 1;
}

static int node_in_graph(const graph *g, const node *n)
{
    if (!g || !n) return 0;
    for (int i = 0; i < acount(g->nodes); i++)
        if (g->nodes[i] == n) return 1;
    return 0;
}

static void graph_changed(graph *g)
{
    if (g) g->topology_revision++;
}

static int graph_mutation_allowed(const graph *g)
{ return g && g->flow_pause_count == 0; }

node *graph_node_by_id(const graph *g, uint64_t id);
const pin_spec *graph_pin_by_id(const node *n, uint64_t id, int *is_output, int *index);

uint64_t graph_alloc_id(graph *g)
{
    if (!g) return 0;
    if (g->id_counter == 0) g->id_counter = 1;
    return g->id_counter++;
}

static void graph_index_node(graph *g, node *n)
{
    size_t want = (size_t)n->id + 1;
    while (acount(g->id_table) < want) apush(g->id_table, NULL);
    g->id_table[n->id] = n;
}

void graph_reindex(graph *g)
{
    if (!g) return;
    afree(g->id_table);
    for (int i = 0; i < acount(g->nodes); i++) graph_index_node(g, g->nodes[i]);
}

static void init_pin(pin_spec *dst, const pin_spec *src, graph *g)
{
    *dst = *src;
    dst->id = graph_alloc_id(g);
    dst->default_value.type = dst->type;
    dst->default_value.type_name = dst->type_name;
    if ((dst->type == PIN_STR || dst->type == PIN_NAME) && !dst->default_value.str)
        dst->default_value.str = "";
}

node *graph_add_node(graph *g, const node_desc *desc)
{
    if (!graph_mutation_allowed(g) || !desc || desc->input_count < 0 ||
        desc->output_count < 0 || (desc->input_count && !desc->inputs) ||
        (desc->output_count && !desc->outputs))
        return NULL;
    node *n = (node *)xcalloc(1, sizeof *n);
    n->desc = desc;
    n->input_count = desc->input_count;
    n->output_count = desc->output_count;
    n->id = graph_alloc_id(g);
    graph_index_node(g, n);
    if (desc->input_count) {
        n->input_pins = (pin_spec *)xcalloc((size_t)desc->input_count, sizeof *n->input_pins);
        n->inputs = (pin_value *)xcalloc((size_t)desc->input_count, sizeof *n->inputs);
        for (int i = 0; i < desc->input_count; i++) {
            init_pin(&n->input_pins[i], &desc->inputs[i], g);
            n->inputs[i] = n->input_pins[i].default_value;
        }
    }
    if (desc->output_count) {
        n->output_pins = (pin_spec *)
            xcalloc((size_t)desc->output_count, sizeof *n->output_pins);
        n->outputs = (pin_value *)xcalloc((size_t)desc->output_count, sizeof *n->outputs);
        for (int i = 0; i < desc->output_count; i++) {
            init_pin(&n->output_pins[i], &desc->outputs[i], g);
            n->outputs[i] = n->output_pins[i].default_value;
        }
    }
    apush(g->nodes, n);
    graph_changed(g);
    return n;
}

int pin_types_compatible(const pin_spec *a, const pin_spec *b)
{ return pin_compatible(a, b); }

static edge make_edge(node *from, int from_pin, node *to, int to_pin, graph_edge_kind kind, int order)
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
    e.exec_order = order;
    return e;
}

int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, from) || !node_in_graph(g, to))
        return -1;
    if (from_pin < 0 || from_pin >= from->output_count || to_pin < 0 ||
        to_pin >= to->input_count)
        return -1;
    const pin_spec *out = &from->output_pins[from_pin];
    const pin_spec *in = &to->input_pins[to_pin];
    if (out->kind != PIN_DATA || in->kind != PIN_DATA || !pin_compatible(out, in))
        return -1;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->to_id == to->id && old->to_pin_id == in->id) {
            if (old->kind != GRAPH_EDGE_DATA) return -1;
            *old = make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0);
            graph_changed(g);
            return 0;
        }
    }
    apush(g->edges, make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0));
    graph_changed(g);
    return 0;
}

int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin)
{ return graph_connect_data(g, from, from_pin, to, to_pin); }

int graph_connect_data_ids(graph *g, uint64_t from_node, uint64_t from_pin, uint64_t to_node, uint64_t to_pin)
{
    node *from = graph_node_by_id(g, from_node), *to = graph_node_by_id(g, to_node);
    int fp = -1, tp = -1, out = 0, in = 0;
    if (!graph_pin_by_id(from, from_pin, &out, &fp) ||
        !graph_pin_by_id(to, to_pin, &in, &tp) || !out || in)
        return -1;
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
        !graph_pin_by_id(to, to_pin, &in, &tp) || !out || in)
        return -1;
    return graph_connect_exec(g, from, fp, to, tp, exec_order);
}

int graph_connect_exec(graph *g, node *from, int from_pin, node *to, int to_pin, int exec_order)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, from) ||
        !node_in_graph(g, to) || exec_order < 0)
        return -1;
    if (from_pin < 0 || from_pin >= from->output_count || to_pin < 0 ||
        to_pin >= to->input_count)
        return -1;
    const pin_spec *out = &from->output_pins[from_pin];
    const pin_spec *in = &to->input_pins[to_pin];
    if (out->kind != PIN_EXEC || in->kind != PIN_EXEC) return -1;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->kind == GRAPH_EDGE_EXEC && old->from_id == from->id &&
            old->from_pin_id == out->id &&
            (old->to_id != to->id || old->to_pin_id != in->id))
            return -1;
    }
    /* Exec inputs intentionally have multiple drivers. An exact duplicate is
     * replaced, while distinct incoming edges merge at the input. */
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->kind == GRAPH_EDGE_EXEC && old->from_id == from->id &&
            old->from_pin_id == out->id && old->to_id == to->id &&
            old->to_pin_id == in->id) {
            *old = make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_EXEC, exec_order);
            graph_changed(g);
            return 0;
        }
    }
    apush(g->edges, make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_EXEC, exec_order));
    graph_changed(g);
    return 0;
}

unsigned long long graph_topology_revision(const graph *g)
{ return g ? g->topology_revision : 0; }

void graph_flow_pause(graph *g)
{
    if (g) g->flow_pause_count++;
}
void graph_flow_resume(graph *g)
{
    if (g && g->flow_pause_count) g->flow_pause_count--;
}

int graph_remove_node(graph *g, node *n)
{
    if (!graph_mutation_allowed(g)) return -1;
    int idx = -1;
    for (int i = 0; i < acount(g->nodes); i++)
        if (g->nodes[i] == n) {
            idx = i;
            break;
        }
    if (idx < 0) return -1;
    int w = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id == n->id || e->to_id == n->id) continue;
        g->edges[w++] = *e;
    }
    asetlen(g->edges, w);
    if (n->id < (uint64_t)acount(g->id_table)) g->id_table[n->id] = NULL;
    free(n->input_pins);
    free(n->output_pins);
    free(n->inputs);
    free(n->outputs);
    free(n);
    for (int i = idx; i < acount(g->nodes) - 1; i++)
        g->nodes[i] = g->nodes[i + 1];
    asetlen(g->nodes, acount(g->nodes) - 1);
    graph_changed(g);
    return 0;
}

int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    if (!graph_mutation_allowed(g) || !from || !to) return -1;
    uint64_t from_pin_id = from_pin >= 0 && from_pin < from->output_count
                             ? from->output_pins[from_pin].id
                             : 0;
    uint64_t to_pin_id = to_pin >= 0 && to_pin < to->input_count
                           ? to->input_pins[to_pin].id
                           : 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != from->id || e->to_id != to->id ||
            e->from_pin_id != from_pin_id || e->to_pin_id != to_pin_id)
            continue;
        adel(g->edges, i);
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
        if (pin) *pin = -1;
        return NULL;
    }
    return n;
}

static node *edge_to_node(const graph *g, const edge *e, int *pin)
{
    int is_out = 0;
    node *n = graph_node_by_id(g, e->to_id);
    if (!n || !graph_pin_by_id(n, e->to_pin_id, &is_out, pin) || is_out) {
        if (pin) *pin = -1;
        return NULL;
    }
    return n;
}

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin)
{
    if (!g || !n || !src || !src_pin || pin < 0 || pin >= n->input_count)
        return 0;
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

int graph_exec_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin, int *order)
{
    if (!g || !n || !src || !src_pin || !order || pin < 0 ||
        pin >= n->input_count || n->input_pins[pin].kind != PIN_EXEC)
        return 0;
    uint64_t id = n->input_pins[pin].id;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_EXEC && e->to_id == n->id && e->to_pin_id == id) {
            *src = edge_from_node(g, e, src_pin);
            if (!*src) return 0;
            *order = e->exec_order;
            return 1;
        }
    }
    return 0;
}

int graph_exec_input_count(const graph *g, const node *n, int pin)
{
    if (!g || !n || pin < 0 || pin >= n->input_count || n->input_pins[pin].kind != PIN_EXEC)
        return 0;
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++)
        if (g->edges[i].kind == GRAPH_EDGE_EXEC && g->edges[i].to_id == n->id &&
            g->edges[i].to_pin_id == n->input_pins[pin].id)
            count++;
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
        pin >= n->input_count || n->input_pins[pin].kind != PIN_EXEC)
        return 0;
    int at = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->to_id != n->id ||
            e->to_pin_id != n->input_pins[pin].id)
            continue;
        if (at++ != ordinal) continue;
        *src = edge_from_node(g, e, src_pin);
        if (!*src) return 0;
        *order = e->exec_order;
        return 1;
    }
    return 0;
}

int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{
    if (!g || !n || !dst || !dst_pin || pin < 0 || pin >= n->output_count ||
        n->output_pins[pin].kind != PIN_EXEC || order < 0)
        return 0;
    uint64_t id = n->output_pins[pin].id;
    for (int i = 0; i < acount(g->edges); i++)
        if (g->edges[i].kind == GRAPH_EDGE_EXEC && g->edges[i].from_id == n->id &&
            g->edges[i].from_pin_id == id && g->edges[i].exec_order == order) {
            *dst = edge_to_node(g, &g->edges[i], dst_pin);
            return *dst ? 1 : 0;
        }
    return 0;
}

int graph_exec_target_at(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin)
{ return graph_exec_output_target(g, n, pin, order, dst, dst_pin); }

node *graph_node_by_id(const graph *g, uint64_t id)
{
    if (!g || id == 0 || id >= (uint64_t)acount(g->id_table)) return NULL;
    return g->id_table[id];
}

const pin_spec *graph_pin_by_id(const node *n, uint64_t id, int *is_output, int *index)
{
    if (!n || id == 0) return NULL;
    for (int i = 0; i < n->input_count; i++)
        if (n->input_pins[i].id == id) {
            if (is_output) *is_output = 0;
            if (index) *index = i;
            return &n->input_pins[i];
        }
    for (int i = 0; i < n->output_count; i++)
        if (n->output_pins[i].id == id) {
            if (is_output) *is_output = 1;
            if (index) *index = i;
            return &n->output_pins[i];
        }
    return NULL;
}

int graph_set_pin_default(graph *g, node *n, int output, int index, const pin_value *value)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || !value) return -1;
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || p[index].kind != PIN_DATA ||
        value->type != p[index].type)
        return -1;
    p[index].default_value = *value;
    p[index].default_value.type_name = p[index].type_name;
    if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        char *s = graph_own_str(g, value->str, strlen(value->str));
        if (!s) return -1;
        p[index].default_value.str = s;
    }
    /* Keep the runtime slot coherent until the next evaluation. The editor
     * still writes authored data only through this API. */
    if (!output) n->inputs[index] = p[index].default_value;
    else n->outputs[index] = p[index].default_value;
    graph_changed(g);
    return 0;
}

int graph_set_pin_name(graph *g, node *n, int output, int index, const char *name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || !name) return -1;
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count) return -1;
    char *copy = graph_own_str(g, name, strlen(name));
    if (!copy) return -1;
    p[index].name = copy;
    graph_changed(g);
    return 0;
}

int graph_set_pin_type(graph *g, node *n, int output, int index, pin_type type, const char *type_name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count) return -1;
    char *owned_type_name = type_name
                              ? graph_own_str(g, type_name, strlen(type_name))
                              : NULL;
    if (type_name && !owned_type_name) return -1;
    p[index].type = type;
    p[index].type_name = owned_type_name;
    p[index].default_value = (pin_value){ .type = type, .type_name = owned_type_name };
    if (!output) n->inputs[index] = p[index].default_value;
    else n->outputs[index] = p[index].default_value;

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
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || container < PIN_CONTAINER_SINGLE ||
        container > PIN_CONTAINER_MAP)
        return -1;
    p[index].container = container;
    graph_changed(g);
    return 0;
}

int graph_move_pin(graph *g, node *n, int output, int index, int target_index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    pin_value *values = output ? n->outputs : n->inputs;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || target_index < 0 || target_index >= count)
        return -1;
    if (index == target_index) return index;
    pin_spec moved_pin = pins[index];
    pin_value moved_value = values[index];
    if (index < target_index) {
        memmove(pins + index, pins + index + 1, (size_t)(target_index - index) * sizeof *pins);
        memmove(values + index, values + index + 1, (size_t)(target_index - index) * sizeof *values);
    } else {
        memmove(pins + target_index + 1, pins + target_index, (size_t)(index - target_index) * sizeof *pins);
        memmove(
            values + target_index + 1,
            values + target_index,
            (size_t)(index - target_index) * sizeof *values
        );
    }
    pins[target_index] = moved_pin;
    values[target_index] = moved_value;

    graph_changed(g);
    return target_index;
}

int graph_set_node_disabled(graph *g, node *n, int disabled)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    n->disabled = disabled != 0;
    graph_changed(g);
    return 0;
}

static int set_node_text(graph *g, node *n, char **slot, const char *text)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    if (!text) {
        *slot = NULL;
        graph_changed(g);
        return 0;
    }
    char *copy = graph_own_str(g, text, strlen(text));
    if (!copy) return -1;
    *slot = copy;
    graph_changed(g);
    return 0;
}

int graph_set_node_title(graph *g, node *n, const char *title)
{ return set_node_text(g, n, &n->title_override, title); }

int graph_set_node_comment(graph *g, node *n, const char *comment)
{ return set_node_text(g, n, &n->comment, comment); }

int graph_set_node_reference(graph *g, node *n, uint64_t reference_id, const char *reference_name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    n->reference_id = reference_id;
    if (reference_name) {
        char *copy = graph_own_str(g, reference_name, strlen(reference_name));
        if (!copy) return -1;
        n->reference_name = copy;
    } else n->reference_name = NULL;
    graph_changed(g);
    return 0;
}

int graph_add_pin(graph *g, node *n, int output, const pin_spec *template_pin)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n) || !template_pin)
        return -1;
    int *count = output ? &n->output_count : &n->input_count;
    pin_spec **pins = output ? &n->output_pins : &n->input_pins;
    pin_value **values = output ? &n->outputs : &n->inputs;
    int next = *count + 1;
    pin_spec *grown_pins = (pin_spec *)xmalloc((size_t)next * sizeof *grown_pins);
    pin_value *grown_values = (pin_value *)xmalloc((size_t)next * sizeof *grown_values);
    memcpy(grown_pins, *pins, (size_t)*count * sizeof *grown_pins);
    memcpy(grown_values, *values, (size_t)*count * sizeof *grown_values);
    free(*pins);
    free(*values);
    *pins = grown_pins;
    *values = grown_values;
    init_pin(&(*pins)[*count], template_pin, g);
    (*values)[*count] = (*pins)[*count].default_value;
    (*count)++;
    graph_changed(g);
    return *count - 1;
}

int graph_remove_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, n)) return -1;
    int *count = output ? &n->output_count : &n->input_count;
    pin_spec **pins = output ? &n->output_pins : &n->input_pins;
    pin_value **values = output ? &n->outputs : &n->inputs;
    if (index < 0 || index >= *count) return -1;
    uint64_t pin_id = (*pins)[index].id;
    int w = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *e = &g->edges[i];
        if ((output && e->from_id == n->id && e->from_pin_id == pin_id) ||
            (!output && e->to_id == n->id && e->to_pin_id == pin_id))
            continue;
        g->edges[w++] = *e;
    }
    asetlen(g->edges, w);
    memmove(*pins + index, *pins + index + 1, (size_t)(*count - index - 1) * sizeof **pins);
    memmove(*values + index, *values + index + 1, (size_t)(*count - index - 1) * sizeof **values);
    (*count)--;
    if (*count == 0) {
        free(*pins);
        free(*values);
        *pins = NULL;
        *values = NULL;
    } else {
        *pins = (pin_spec *)xrealloc(*pins, (size_t)*count * sizeof **pins);
        *values = (pin_value *)xrealloc(*values, (size_t)*count * sizeof **values);
    }
    graph_changed(g);
    return 0;
}

void *graph_own_block(graph *g, size_t size)
{
    if (!g || size == 0) return NULL;
    void *block = xcalloc(1, size);
    apush(g->blocks, block);
    return block;
}

void graph_free(graph *g)
{
    if (!g) return;
    for (int i = 0; i < acount(g->nodes); i++) {
        free(g->nodes[i]->input_pins);
        free(g->nodes[i]->output_pins);
        free(g->nodes[i]->inputs);
        free(g->nodes[i]->outputs);
        free(g->nodes[i]);
    }
    afree(g->nodes);
    afree(g->id_table);
    afree(g->edges);
    for (int i = 0; i < acount(g->blocks); i++) free(g->blocks[i]);
    afree(g->blocks);
    for (int i = 0; i < acount(g->strings); i++) free(g->strings[i]);
    afree(g->strings);
    memset(g, 0, sizeof *g);
}

int graph_set_event_name(graph *g, node *event_node, const char *name)
{
    if (!graph_mutation_allowed(g) || !node_in_graph(g, event_node) ||
        !event_node->desc || event_node->desc->flow_kind != NODE_FLOW_EVENT ||
        !name || !name[0])
        return -1;
    char *copy = graph_own_str(g, name, strlen(name));
    if (!copy) return -1;
    event_node->reference_name = copy;
    graph_changed(g);
    return 0;
}

const char *graph_event_name(const node *event_node)
{ return event_node ? event_node->reference_name : NULL; }

int graph_set_metadata(graph *g, const char *name, const char *description)
{
    if (!graph_mutation_allowed(g) || !name || !description) return -1;
    char *n = graph_own_str(g, name, strlen(name));
    char *d = graph_own_str(g, description, strlen(description));
    if (!n || !d) return -1;
    g->name = n;
    g->description = d;
    graph_changed(g);
    return 0;
}

char *graph_own_str(graph *g, const char *s, size_t len)
{
    if (!g || !s) return NULL;
    char *copy = (char *)xmalloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    apush(g->strings, copy);
    return copy;
}
