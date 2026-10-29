#include "graph/model/local.h"

#include "graph/model/graph_internal.h"

#include <assert.h>
#include <string.h>

const graph_local *graph_local_by_id(const graph *g, uint64_t id)
{
    for (int i = 0; i < acount(g->locals); i++) {
        if (g->locals[i].id == id) {
            return &g->locals[i];
        }
    }
    return NULL;
}

const graph_local *graph_output_local(const graph *g, const node *n, int pin)
{
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        if (local->source_id == n->id &&
            local->source_pin_id == n->output_pins[pin].id) {
            return local;
        }
    }
    return NULL;
}

const graph_local *graph_input_local(const graph *g, const node *n, int pin)
{
    const pin_binding *binding = n->input_pins[pin].binding;
    return binding && binding->kind == PIN_BINDING_LOCAL
             ? graph_local_by_id(g, binding->local_id)
             : NULL;
}

bool graph_edge_is_local(const graph *g, const edge *e)
{
    node *target = graph_node_by_id(g, e->to_id);
    int pin = -1, output = 0;
    if (e->kind != GRAPH_EDGE_DATA ||
        !graph_pin_by_id(target, e->to_pin_id, &output, &pin) || output) {
        return false;
    }
    const graph_local *local = graph_input_local(g, target, pin);
    return local && local->source_id == e->from_id &&
           local->source_pin_id == e->from_pin_id;
}

int graph_rename_local(graph *g, uint64_t id, const char *name)
{
    graph_local *local = (graph_local *)graph_local_by_id(g, id);
    if (!local || !name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < acount(g->locals); i++) {
        if (g->locals[i].id != id && !strcmp(g->locals[i].name, name)) {
            return -1;
        }
    }
    local->name = graph_own_str(g, name, strlen(name));
    g->topology_revision++;
    return 0;
}

bool graph_local_declaration_valid(graph *g, node *source, int pin, const char *name)
{
    if (g->flow_pause_count || !source || graph_node_by_id(g, source->id) != source ||
        pin < 0 || pin >= source->output_count || !name || !name[0] ||
        source->output_pins[pin].kind != PIN_DATA ||
        graph_output_local(g, source, pin)) {
        return false;
    }
    for (int i = 0; i < acount(g->locals); i++) {
        if (!strcmp(g->locals[i].name, name)) {
            return false;
        }
    }
    return true;
}
uint64_t graph_declare_local(graph *g, node *source, int pin, const char *name)
{
    if (!graph_local_declaration_valid(g, source, pin, name)) {
        return 0;
    }
    graph_local local = { .id = graph_alloc_id(g),
                          .name = graph_own_str(g, name, strlen(name)),
                          .source_id = source->id,
                          .source_pin_id = source->output_pins[pin].id };
    apush(g->locals, local);
    g->topology_revision++;
    return local.id;
}

int graph_expand_local(graph *g, uint64_t id)
{
    if (g->flow_pause_count || !graph_local_by_id(g, id)) {
        return -1;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->input_count; p++) {
            const pin_binding *b = n->input_pins[p].binding;
            if (b && b->kind == PIN_BINDING_LOCAL && b->local_id == id) {
                int set_pin_binding_result = graph_set_pin_binding(g, n, 0, p, NULL);
                assert(set_pin_binding_result == 0);
                (void)set_pin_binding_result;
            }
        }
    }
    for (int i = 0; i < acount(g->locals); i++) {
        if (g->locals[i].id == id) {
            adel(g->locals, i);
            break;
        }
    }
    g->topology_revision++;
    return 0;
}

void graph_prune_locals(graph *g)
{
    for (int i = acount(g->locals) - 1; i >= 0; i--) {
        /* A local still loading has no source yet, and its uses follow. */
        bool used = !g->locals[i].source_id;
        for (int n = 0; n < acount(g->nodes) && !used; n++) {
            for (int p = 0; p < g->nodes[n]->input_count; p++) {
                const pin_binding *b = g->nodes[n]->input_pins[p].binding;
                used |= b && b->kind == PIN_BINDING_LOCAL &&
                        b->local_id == g->locals[i].id;
            }
        }
        if (!used) {
            graph_expand_local(g, g->locals[i].id);
        }
    }
}
