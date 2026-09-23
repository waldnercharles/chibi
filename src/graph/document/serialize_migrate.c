#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"

/* Generated readers formerly had one Exec input and one Then continuation.
 * Keep data pin IDs and bypass the old execution step when opening an asset. */
int serialize_migrate_component_reads(graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->desc->pure_kind != NODE_PURE_READ ||
            node_behavior_for(n) == NODE_BEHAVIOR_PURE) {
            continue;
        }
        if (n->input_count != 1 || n->input_pins[0].kind != PIN_EXEC ||
            n->output_count != 2 || n->output_pins[0].kind != PIN_EXEC ||
            n->output_pins[1].kind != PIN_DATA) {
            serialize_error_set("component reader has an unsupported legacy pin layout");
            return -1;
        }
        edge *next = NULL;
        for (int j = 0; j < acount(g->edges); j++) {
            edge *e = &g->edges[j];
            if (e->kind != GRAPH_EDGE_EXEC || e->from_id != n->id) {
                continue;
            }
            if (next || e->to_id == n->id) {
                serialize_error_set("legacy component reader has ambiguous or cyclic execution wiring");
                return -1;
            }
            next = e;
        }
        if (next) {
            for (int j = 0; j < acount(g->edges); j++) {
                edge *e = &g->edges[j];
                if (e->kind == GRAPH_EDGE_EXEC && e->to_id == n->id) {
                    e->to_id = next->to_id;
                    e->to_pin_id = next->to_pin_id;
                    e->reroute_count = 0;
                    e->custom_handles = false;
                }
            }
        }
        graph_remove_pin(g, n, 0, 0);
        graph_remove_pin(g, n, 1, 0);
    }
    return 0;
}

int serialize_repair_math_calls(behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *call = caller->nodes[j];
            if ((call->desc != &NODE_CallPureFunction &&
                 call->desc != &NODE_BehaviorExpression &&
                 call->desc != &NODE_CallFunction) ||
                call->reference_asset) {
                continue;
            }
            graph *generated = behavior_asset_find_graph(asset, call->reference_id);
            if (!generated || !generated->math_expression || generated->source_asset) {
                continue;
            }
            if (behavior_expression_repair(asset, call) != 0) {
                serialize_error_set("could not repair Behavior Expression call");
                return -1;
            }
        }
    }
    return 0;
}

/* FMT-1/FMT-8/FMT-17: load a version-3 Behavior Asset document. The unavailable
 * owner handle type name is preserved as authored, never substituted. */
