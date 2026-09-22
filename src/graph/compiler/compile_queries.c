#include "graph/compiler/behavior_function.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <string.h>

static void append(compile_report *out, compile_diagnostic d)
{
    for (int i = 0; i < acount(out->items); i++) {
        const compile_diagnostic *old = &out->items[i];
        if (old->graph_id == d.graph_id && old->node_id == d.node_id &&
            old->pin_id == d.pin_id && old->rule == d.rule &&
            !strcmp(old->message, d.message)) {
            return;
        }
    }
    apush(out->items, d);
    out->warning_count += d.severity == CMPL_WARNING;
    out->note_count += d.severity == CMPL_NOTE;
}

typedef struct
{
    graph *graph;
    node *node;
} location;

/* Follow actual data dependencies and execution edges, including linked calls.
 * A visited source is reported once even when several Tick entries reach it. */
static void tick_queries(const behavior_asset *asset, compile_report *out)
{
    location *pending = NULL, *visited = NULL;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc->flow_kind == NODE_FLOW_EVENT && n->reference_name &&
            !strcmp(n->reference_name, "Tick")) {
            apush(pending, ((location){ asset->graph, n }));
        }
    }
    while (acount(pending)) {
        location at = apop(pending);
        node *n = at.node;
        bool seen = n->disabled;
        for (int i = 0; !seen && i < acount(visited); i++) {
            seen = visited[i].graph == at.graph && visited[i].node == n;
        }
        if (seen) {
            continue;
        }
        apush(visited, at);
        if (n->desc->work == NODE_WORK_WORLD_QUERY ||
            n->desc->work == NODE_WORK_LINEAR_SEARCH) {
            compile_diagnostic d = { .severity = CMPL_WARNING,
                                     .rule = CMPL_RULE_QUERY_SCAN,
                                     .graph_id = at.graph->graph_id,
                                     .node_id = n->id,
                                     .pin_id = n->output_count
                                                 ? n->output_pins[0].id
                                                 : 0 };
            snprintf(d.message, sizeof d.message, "Query reachable from Tick: candidates per call multiply by active instances. Use selective filters and profile at the target population. Cached membership still requires searching candidates.");
            if (n->desc->work == NODE_WORK_LINEAR_SEARCH) {
                d.rule = CMPL_RULE_CONTAINER;
                d.pin_id = n->input_pins[0].id;
                snprintf(d.message, sizeof d.message, "Array search reachable from Tick scans candidates on each evaluation. When you only need unique membership, use a Set so large owned scalar collections can use an index.");
            }
            append(out, d);
        }
        if (n->desc == &NODE_CallFunction ||
            n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression ||
            n->desc == &NODE_CallMacro) {
            graph *callee = behavior_asset_find_graph(asset, n->reference_id);
            if (callee) {
                node *entry = graph_node_by_id(callee, callee->entry_id);
                node *ret = graph_node_by_id(callee, callee->return_id);
                if (entry) {
                    apush(pending, ((location){ callee, entry }));
                }
                if (ret) {
                    apush(pending, ((location){ callee, ret }));
                }
            }
        }
        for (int p = 0; p < n->input_count; p++) {
            node *source;
            int pin;
            if (n->input_pins[p].kind == PIN_DATA &&
                graph_input_source(at.graph, n, p, &source, &pin)) {
                apush(pending, ((location){ at.graph, source }));
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].kind != PIN_EXEC) {
                continue;
            }
            node *target;
            int pin;
            for (int order = 0;
                 graph_exec_target_at(at.graph, n, p, order, &target, &pin);
                 order++) {
                apush(pending, ((location){ at.graph, target }));
            }
        }
    }
    afree(pending);
    afree(visited);
}

void behavior_asset_query_diagnostics(const behavior_asset *asset, compile_report *out)
{
    tick_queries(asset, out);
    for (int j = 0; j < acount(asset->graphs); j++) {
        graph *g = asset->graphs[j];
        bool reductions = false;
        for (int i = 0; i < acount(g->nodes); i++) {
            const node *n = g->nodes[i];
            if (n->disabled) {
                continue;
            }
            reductions |= n->desc->work == NODE_WORK_ENTITY_REDUCTION;
            if (n->desc->work == NODE_WORK_SET || n->desc->work == NODE_WORK_SET_COPY) {
                compile_diagnostic d = { .severity = n->desc->work == NODE_WORK_SET_COPY
                                                       ? CMPL_WARNING
                                                       : CMPL_NOTE,
                                         .rule = CMPL_RULE_CONTAINER,
                                         .graph_id = g->graph_id,
                                         .node_id = n->id,
                                         .pin_id = n->input_pins[0].id };
                snprintf(
                    d.message,
                    sizeof d.message,
                    "%s",
                    n->desc->work == NODE_WORK_SET_COPY
                        ? "Set updates copy their ordered result. Repeated Add "
                          "in a loop can still be quadratic. Large owned "
                          "scalar Sets index membership automatically."
                        : "Owned scalar Sets with at least 64 members use an "
                          "index while retaining iteration order. Small, "
                          "borrowed and aggregate Sets use flat storage."
                );
                append(out, d);
            }
        }
        if (!reductions) {
            continue;
        }
        for (int i = 0; i < acount(g->nodes); i++) {
            node *entry = g->nodes[i];
            if (entry->desc->flow_kind != NODE_FLOW_EVENT && entry->id != g->entry_id) {
                continue;
            }
            behavior_function_diagnostic exclusion;
            behavior_function *f = behavior_function_lower(asset, g, entry, &exclusion);
            int count;
            const compile_diagnostic *items = behavior_function_query_diagnostics(f, &count);
            for (int k = 0; k < count; k++) {
                if (items[k].graph_id == g->graph_id) {
                    append(out, items[k]);
                }
            }
            behavior_function_free(f);
        }
    }
}
