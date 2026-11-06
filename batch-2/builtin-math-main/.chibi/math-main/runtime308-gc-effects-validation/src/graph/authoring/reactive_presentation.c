#include "graph/model/reactive_presentation.h"

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "support/array.h"

#include <string.h>

static bool guard_contains(const graph *g, const node *guard, const node *n, int depth)
{
    if (!guard || depth > acount(g->nodes) ||
        (guard->desc != &NODE_Not_Equal && guard->desc != &NODE_Or)) {
        return false;
    }
    if (guard == n) {
        return true;
    }
    if (guard->desc != &NODE_Or) {
        return false;
    }
    for (int i = 0; i < guard->input_count; i++) {
        node *source = NULL;
        int pin;
        if (graph_input_source(g, guard, i, &source, &pin) &&
            guard_contains(g, source, n, depth + 1)) {
            return true;
        }
    }
    return false;
}

static bool only_guard_consumers(const graph *g, const node *read, const node *guard)
{
    bool consumed = false;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != read->id) {
            continue;
        }
        consumed = true;
        if (!guard_contains(g, guard, graph_node_by_id(g, e->to_id), 0)) {
            return false;
        }
    }
    return consumed;
}

const node *behavior_reactive_authored_node(const graph *g, const node *n)
{
    if (!n || !g->owner) {
        return n;
    }
    for (int i = 0; i < acount(g->owner->reactives); i++) {
        const behavior_reactive *r = &g->owner->reactives[i];
        if (r->kind != BEHAVIOR_REACTIVE_BIND || r->graph_id != g->graph_id) {
            continue;
        }
        node *event = graph_node_by_id(g, r->event_id);
        node *set = graph_node_by_id(g, r->statement_node_id);
        node *branch = NULL, *guard = NULL;
        int pin;
        if (!event || !set || !graph_exec_output_target(g, event, 0, 0, &branch, &pin) ||
            branch->desc != &NODE_Branch) {
            continue;
        }
        for (int k = 0; k < branch->input_count; k++) {
            if (!strcmp(branch->input_pins[k].name, "Condition")) {
                graph_input_source(g, branch, k, &guard, &pin);
                break;
            }
        }
        if (n == event || n == branch || guard_contains(g, guard, n, 0) ||
            (n->id == r->source_node_id && only_guard_consumers(g, n, guard))) {
            return set;
        }
    }
    return n;
}
