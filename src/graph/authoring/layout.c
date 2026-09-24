#include "graph/authoring/layout.h"

#include "graph/authoring/arrange.h"
#include "graph/document/events.h"
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

/* A fixed measurement: a 28 unit title, 4 of padding and 24 unit pin rows. */
static float pin_y(int row)
{
    return 32 + ((float)row + 0.5f) * 24;
}

static int compare_links(const void *a, const void *b)
{
    const arrange_link *left = a, *right = b;
    if (left->from != right->from) {
        return left->from < right->from ? -1 : 1;
    }
    if (left->from_y != right->from_y) {
        return left->from_y < right->from_y ? -1 : 1;
    }
    if (left->to != right->to) {
        return left->to < right->to ? -1 : 1;
    }
    return left->to_y < right->to_y ? -1 : left->to_y > right->to_y;
}

enum
{
    LAYOUT_BAND_BEGIN,
    LAYOUT_BAND_TICK,
    LAYOUT_BAND_HOST,
    LAYOUT_BAND_CUSTOM,
    LAYOUT_BAND_END,
    LAYOUT_BAND_OTHER,
};

int graph_layout_band(const node *n)
{
    if (n->desc->flow_kind != NODE_FLOW_EVENT) {
        return LAYOUT_BAND_OTHER;
    }
    const char *name = graph_event_name(n);
    if (graph_host_event_is(name, GRAPH_HOST_EVENT_BEGIN)) {
        return LAYOUT_BAND_BEGIN;
    }
    if (graph_host_event_is(name, GRAPH_HOST_EVENT_TICK)) {
        return LAYOUT_BAND_TICK;
    }
    if (graph_host_event_is(name, GRAPH_HOST_EVENT_END)) {
        return LAYOUT_BAND_END;
    }
    return graph_event_host_name(name) ? LAYOUT_BAND_HOST : LAYOUT_BAND_CUSTOM;
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

static void default_layout(const graph *graph, vec2 *positions)
{
    int count = acount(graph->nodes);
    /* Arrange in ID order from zeroed seeds so in-memory node order and
     * current positions cannot change the result. */
    node_order *order = xmalloc((size_t)count * sizeof *order);
    int *slot = xmalloc((size_t)count * sizeof *slot);
    for (int i = 0; i < count; i++) {
        order[i] = (node_order){ graph->nodes[i]->id, i };
    }
    qsort(order, (size_t)count, sizeof *order, compare_node_ids);
    arrange_box *boxes = xmalloc((size_t)count * sizeof *boxes);
    arrange_point *arranged = xcalloc((size_t)count, sizeof *arranged);
    for (int i = 0; i < count; i++) {
        const node *node = graph->nodes[order[i].index];
        int rows = node->input_count > node->output_count ? node->input_count
                                                          : node->output_count;
        boxes[i] = (arrange_box){ .width = 220,
                                  .height = 36 + 24 * rows,
                                  .band = graph_layout_band(node) };
        slot[order[i].index] = i;
    }
    arrange_link *links = NULL;
    for (int i = 0; i < acount(graph->edges); i++) {
        const edge *edge = &graph->edges[i];
        int from = node_index(graph, edge->from_id),
            to = node_index(graph, edge->to_id);
        int from_pin, to_pin;
        if (from < 0 || to < 0 ||
            !graph_pin_by_id(graph->nodes[from], edge->from_pin_id, NULL, &from_pin) ||
            !graph_pin_by_id(graph->nodes[to], edge->to_pin_id, NULL, &to_pin)) {
            continue;
        }
        apush(
            links,
            ((arrange_link){ .from = slot[from],
                             .to = slot[to],
                             .from_y = pin_y(from_pin),
                             .to_y = pin_y(to_pin) })
        );
    }
    if (acount(links)) {
        qsort(links, (size_t)acount(links), sizeof *links, compare_links);
    }
    arrange_graph input = { .boxes = boxes,
                            .box_count = count,
                            .links = links,
                            .link_count = acount(links) };
    arrange_layout(&input, ARRANGE_ROUTE_CURVED, arranged);
    for (int i = 0; i < count; i++) {
        positions[order[i].index] = arranged[i];
    }
    afree(links);
    free(arranged);
    free(boxes);
    free(slot);
    free(order);
}

void graph_layout(graph *graph)
{
    int count = acount(graph->nodes);
    if (!count) {
        return;
    }
    vec2 *positions = xmalloc((size_t)count * sizeof *positions);
    default_layout(graph, positions);
    for (int i = 0; i < count; i++) {
        graph->nodes[i]->x = positions[i].x;
        graph->nodes[i]->y = positions[i].y;
        graph->nodes[i]->placed = false;
    }
    for (int i = 0; i < acount(graph->edges); i++) {
        graph->edges[i].reroute_count = 0;
        graph->edges[i].custom_handles = false;
    }
    free(positions);
}

void graph_place_unplaced(graph *graph)
{
    int count = acount(graph->nodes);
    bool unplaced = false;
    for (int i = 0; i < count && !unplaced; i++) {
        unplaced = !graph->nodes[i]->placed;
    }
    if (!unplaced) {
        return;
    }
    vec2 *positions = xmalloc((size_t)count * sizeof *positions);
    default_layout(graph, positions);
    for (int i = 0; i < count; i++) {
        if (!graph->nodes[i]->placed) {
            graph->nodes[i]->x = positions[i].x;
            graph->nodes[i]->y = positions[i].y;
        }
    }
    free(positions);
}
