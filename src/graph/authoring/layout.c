#include "graph/authoring/layout.h"

#include "graph/authoring/arrange.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>

typedef struct node_order
{
    uint64_t id;
    int index;
} node_order;

static int compare_node_ids(const void *a, const void *b)
{
    const node_order *left = a, *right = b;
    return left->id < right->id ? -1 : left->id > right->id;
}

static int compare_links(const void *a, const void *b)
{
    const arrange_link *left = a, *right = b;
    if (left->from != right->from) {
        return left->from < right->from ? -1 : 1;
    }
    if (left->from_pin != right->from_pin) {
        return left->from_pin < right->from_pin ? -1 : 1;
    }
    if (left->to != right->to) {
        return left->to < right->to ? -1 : 1;
    }
    return left->to_pin < right->to_pin ? -1 : left->to_pin > right->to_pin;
}

static int node_index(const graph *graph, uint64_t id)
{
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (graph->nodes[i]->id == id) {
            return i;
        }
    }
    return -1;
}

/* Arranges the nodes that draw a box. Inline nodes are drawn inside their
 * consumer, so they have no box, no wires and keep their position. */
static void arrange_boxes(graph *graph, bool keep_placed)
{
    int count = acount(graph->nodes);
    /* Arrange in ID order from zeroed seeds so in-memory node order and
     * current positions cannot change the result. */
    node_order *order = NULL;
    int *slot = xmalloc((size_t)(count ? count : 1) * sizeof *slot);
    for (int i = 0; i < count; i++) {
        slot[i] = -1;
        if (!graph_node_is_inline(graph, graph->nodes[i])) {
            apush(order, ((node_order){ graph->nodes[i]->id, i }));
        }
    }
    int boxes_count = acount(order);
    if (!boxes_count) {
        free(slot);
        return;
    }
    qsort(order, (size_t)boxes_count, sizeof *order, compare_node_ids);
    arrange_box *boxes = xmalloc((size_t)boxes_count * sizeof *boxes);
    arrange_point *arranged = xcalloc((size_t)boxes_count, sizeof *arranged);
    for (int i = 0; i < boxes_count; i++) {
        const node *node = graph->nodes[order[i].index];
        int rows = node->input_count > node->output_count ? node->input_count
                                                          : node->output_count;
        boxes[i] = (arrange_box){ .width = 220,
                                  .height = 36 + 24 * rows,
                                  .title_height = 28,
                                  .row_height = 24,
                                  .pad = 4 };
        slot[order[i].index] = i;
    }
    arrange_link *links = NULL;
    for (int i = 0; i < acount(graph->edges); i++) {
        const edge *edge = &graph->edges[i];
        int from = node_index(graph, edge->from_id),
            to = node_index(graph, edge->to_id);
        int from_pin, to_pin;
        if (from < 0 || to < 0 || slot[from] < 0 || slot[to] < 0 ||
            !graph_pin_by_id(graph->nodes[from], edge->from_pin_id, NULL, &from_pin) ||
            !graph_pin_by_id(graph->nodes[to], edge->to_pin_id, NULL, &to_pin)) {
            continue;
        }
        apush(links, ((arrange_link){ .from = slot[from], .to = slot[to], .from_pin = from_pin, .to_pin = to_pin }));
    }
    if (acount(links)) {
        qsort(links, (size_t)acount(links), sizeof *links, compare_links);
    }
    arrange_graph input = { .boxes = boxes,
                            .box_count = boxes_count,
                            .links = links,
                            .link_count = acount(links) };
    arrange_layout(&input, arranged);
    for (int i = 0; i < boxes_count; i++) {
        node *node = graph->nodes[order[i].index];
        if (keep_placed && node->placed) {
            continue;
        }
        node->x = arranged[i].x;
        node->y = arranged[i].y;
        node->placed = false;
    }
    afree(links);
    free(arranged);
    free(boxes);
    free(slot);
    afree(order);
}

void graph_layout(graph *graph)
{
    arrange_boxes(graph, false);
    for (int i = 0; i < acount(graph->edges); i++) {
        graph->edges[i].reroute_count = 0;
        graph->edges[i].custom_handles = false;
    }
}

void graph_place_unplaced(graph *graph)
{
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (!graph->nodes[i]->placed && !graph_node_is_inline(graph, graph->nodes[i])) {
            arrange_boxes(graph, true);
            return;
        }
    }
}
