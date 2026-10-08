#include "graph/authoring/optimize_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    const graph *graph;
    const node *source;
    unsigned char *execution;
    unsigned char *data;
} set_proof;

enum
{
    VISITING = 1,
    PROVEN,
    REJECTED
};

static int node_index(const graph *g, const node *n)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            return i;
        }
    }
    return -1;
}

static bool stopped(const node *n)
{
    return n->disabled || n->breakpoint || n->breakpoint_enabled;
}

/* These nodes evaluate their data inputs before changing state or suspending.
 * Unknown callbacks may request an input after changing the variable. */
static bool reads_before_effects(const node *n)
{
    const node_desc *d = n->desc;
    return d == &NODE_SetVariable || d == &NODE_Branch || d == &NODE_ForLoop ||
           d == &NODE_ForEach || d == &NODE_Delay || d == &NODE_Return ||
           d == &NODE_CallFunction || d == &NODE_BehaviorExpression ||
           d == &NODE_CallMacro;
}

static bool reached_after_set(set_proof *proof, const node *n)
{
    const graph *g = proof->graph;
    int index = node_index(g, n);
    if (index < 0 || n == proof->source || stopped(n)) {
        return false;
    }
    if (proof->execution[index]) {
        return proof->execution[index] == PROVEN;
    }
    proof->execution[index] = VISITING;
    bool reached = false, valid = true;
    for (int i = 0; valid && i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->to_id != n->id) {
            continue;
        }
        reached = true;
        const node *previous = graph_node_by_id(g, e->from_id);
        if (!previous || stopped(previous)) {
            valid = false;
            break;
        }
        if (previous == proof->source) {
            continue;
        }
        // Only synchronous built-ins with known variable effects can be crossed.
        bool transparent = previous->desc == &NODE_Branch ||
                           (previous->desc == &NODE_SetVariable &&
                            previous->reference_id != proof->source->reference_id);
        valid = transparent && reached_after_set(proof, previous);
    }
    proof->execution[index] = reached && valid ? PROVEN : REJECTED;
    return proof->execution[index] == PROVEN;
}

static bool uses_current_value(set_proof *proof, const node *n)
{
    const graph *g = proof->graph;
    int index = node_index(g, n);
    if (index < 0 || n == proof->source || stopped(n)) {
        return false;
    }
    if (proof->data[index]) {
        return proof->data[index] == PROVEN;
    }
    proof->data[index] = VISITING;
    if (node_behavior_for(n) != NODE_BEHAVIOR_PURE) {
        bool valid = reads_before_effects(n) && reached_after_set(proof, n);
        proof->data[index] = valid ? PROVEN : REJECTED;
        return valid;
    }
    bool used = false, valid = true;
    for (int i = 0; valid && i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != n->id) {
            continue;
        }
        used = true;
        valid = e->kind == GRAPH_EDGE_DATA &&
                uses_current_value(proof, graph_node_by_id(g, e->to_id));
    }
    proof->data[index] = used && valid ? PROVEN : REJECTED;
    return proof->data[index] == PROVEN;
}

bool graph_set_output_is_current(const graph *g, const node *source, const node *consumer)
{
    size_t count = (size_t)acount(g->nodes);
    unsigned char *states = xmalloc(count * 2);
    memset(states, 0, count * 2);
    set_proof proof = { g, source, states, states + count };
    bool valid = uses_current_value(&proof, consumer);
    free(states);
    return valid;
}
