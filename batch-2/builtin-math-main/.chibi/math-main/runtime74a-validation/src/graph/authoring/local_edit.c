#include "graph/authoring/local_edit.h"

#include "graph/authoring/graph_edit.h"
#include "graph/model/graph_internal.h"
#include "graph/model/local.h"
#include "graph/nodes/conv_nodes.h"

#include <assert.h>
#include <string.h>

uint64_t graph_create_local(graph *g, node *source, int pin, const char *name)
{
    if (!graph_local_declaration_valid(g, source, pin, name)) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != source->id ||
            e->from_pin_id != source->output_pins[pin].id) {
            continue;
        }
        node *target = graph_node_by_id(g, e->to_id);
        int p = -1, output = 0;
        const pin_spec *input = graph_pin_by_id(target, e->to_pin_id, &output, &p);
        if (e->kind != GRAPH_EDGE_DATA || !input || output || input->binding ||
            input->kind != PIN_DATA || input->type == PIN_FLOW ||
            (input->flags & PIN_FLAG_SETTING) ||
            (e->conversion
                 ? !conversion_matches_pins(&source->output_pins[pin], input, e->conversion)
                 : !pin_types_compatible(&source->output_pins[pin], input))) {
            return 0;
        }
        count++;
    }
    if (!count) {
        return 0;
    }
    uint64_t id = graph_declare_local(g, source, pin, name);
    assert(id);
    const graph_local local = *graph_local_by_id(g, id);
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != local.source_id || e->from_pin_id != local.source_pin_id) {
            continue;
        }
        node *target = graph_node_by_id(g, e->to_id);
        int p = -1;
        graph_pin_by_id(target, e->to_pin_id, NULL, &p);
        pin_binding binding = { .kind = PIN_BINDING_LOCAL, .local_id = local.id };
        int set_pin_binding_result = graph_set_pin_binding(g, target, 0, p, &binding);
        assert(set_pin_binding_result == 0);
        (void)set_pin_binding_result;
    }
    return local.id;
}
int graph_use_local(graph *g, uint64_t id, node *n, int pin)
{
    const graph_local *local = graph_local_by_id(g, id);
    node *source = local ? graph_node_by_id(g, local->source_id) : NULL;
    int output = 0, index = -1;
    if (!source || source == n ||
        !graph_pin_by_id(source, local->source_pin_id, &output, &index) || !output) {
        return -1;
    }
    pin_binding binding = { .kind = PIN_BINDING_LOCAL, .local_id = id };
    return graph_connect_data(g, source, index, n, pin) ||
                   graph_set_pin_binding(g, n, 0, pin, &binding)
             ? -1
             : 0;
}
const char *graph_validate_locals(const graph *g)
{
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        node *source = graph_node_by_id(g, local->source_id);
        int output = 0;
        const pin_spec *pin = graph_pin_by_id(source, local->source_pin_id, &output, NULL);
        if (!local->id || !local->name || !local->name[0] || !pin || !output ||
            pin->kind != PIN_DATA || pin->type == PIN_FLOW) {
            return "local references a missing or invalid source output";
        }
        for (int j = 0; j < i; j++) {
            if (g->locals[j].id == local->id ||
                !strcmp(g->locals[j].name, local->name) ||
                (g->locals[j].source_id == local->source_id &&
                 g->locals[j].source_pin_id == local->source_pin_id)) {
                return "duplicate local identity, name, or source";
            }
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        for (int p = 0; p < n->input_count; p++) {
            const pin_binding *b = n->input_pins[p].binding;
            if (!b || b->kind != PIN_BINDING_LOCAL) {
                continue;
            }
            const graph_local *local = graph_local_by_id(g, b->local_id);
            node *source = NULL;
            int pin = -1;
            if (!local || !graph_input_source(g, n, p, &source, &pin) ||
                local->source_id != source->id ||
                local->source_pin_id != source->output_pins[pin].id) {
                return "local use has a missing or mismatched dependency edge";
            }
            const node_desc *conversion = graph_input_conversion(g, n, p);
            if (conversion
                    ? !conversion_matches_pins(&source->output_pins[pin], &n->input_pins[p], conversion)
                    : !pin_types_compatible(&source->output_pins[pin], &n->input_pins[p])) {
                return "local use has an incompatible conversion";
            }
        }
    }
    return NULL;
}
