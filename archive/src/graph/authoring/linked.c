#include "graph/authoring/linked.h"

#include "graph/authoring/graph_edit.h"
#include "support/array.h"

#include <assert.h>

static bool listed(node *const *nodes, int first, int count, const node *n)
{
    for (int i = first; i < count; i++) {
        if (nodes[i] == n) {
            return true;
        }
    }
    return false;
}

/* The drawn nodes one wire away from n: an inline source is drawn inside n,
   so its own sources count as n's, and an inline consumer is its owner. */
static void neighbours(const graph *g, const node *n, bool upstream, node ***out)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if ((upstream ? e->to_id : e->from_id) != n->id) {
            continue;
        }
        node *next = graph_node_by_id(g, upstream ? e->from_id : e->to_id);
        assert(next);
        if (!upstream) {
            apush(*out, graph_inline_owner(g, next));
        } else if (graph_node_is_inline(g, next)) {
            neighbours(g, next, true, out);
        } else {
            apush(*out, next);
        }
    }
}

void graph_linked_nodes(const graph *g, node *const *from, int count, bool upstream, bool all, node ***out)
{
    int start = acount(*out);
    node **frontier = NULL;
    for (int i = 0; i < count; i++) {
        apush(frontier, from[i]);
    }
    while (acount(frontier)) {
        node **reached = NULL;
        for (int i = 0; i < acount(frontier); i++) {
            neighbours(g, frontier[i], upstream, &reached);
        }
        asetlen(frontier, 0);
        for (int i = 0; i < acount(reached); i++) {
            node *n = reached[i];
            if (listed(from, 0, count, n) || listed(*out, start, acount(*out), n)) {
                continue;
            }
            apush(*out, n);
            if (all) {
                apush(frontier, n);
            }
        }
        afree(reached);
    }
    afree(frontier);
}
