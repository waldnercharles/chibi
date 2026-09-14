#include "graph/authoring/layout.h"

#include "graph/authoring/arrange.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>

static int node_index(const graph *graph, uint64_t id)
{
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (graph->nodes[i]->id == id) {
            return i;
        }
    }
    return -1;
}

void graph_layout(graph *graph)
{
    int count = acount(graph->nodes);
    if (!count) {
        return;
    }
    arrange_box *boxes = xmalloc((size_t)count * sizeof *boxes);
    arrange_point *positions = xmalloc((size_t)count * sizeof *positions);
    arrange_link *links = NULL;
    for (int i = 0; i < count; i++) {
        node *node = graph->nodes[i];
        int rows = node->input_count > node->output_count ? node->input_count
                                                          : node->output_count;
        boxes[i] = (arrange_box){ .width = 220,
                                  .height = 36 + 24 * rows,
                                  .title_height = 28,
                                  .row_height = 24,
                                  .pad = 4 };
        positions[i] = (arrange_point){ node->x, node->y };
    }
    for (int i = 0; i < acount(graph->edges); i++) {
        edge *edge = &graph->edges[i];
        int from = node_index(graph, edge->from_id),
            to = node_index(graph, edge->to_id);
        int from_pin, to_pin;
        if (from < 0 || to < 0 ||
            !graph_pin_by_id(graph->nodes[from], edge->from_pin_id, NULL, &from_pin) ||
            !graph_pin_by_id(graph->nodes[to], edge->to_pin_id, NULL, &to_pin)) {
            continue;
        }
        apush(links, ((arrange_link){ .from = from, .to = to, .from_pin = from_pin, .to_pin = to_pin }));
        edge->reroute_count = 0;
        edge->custom_handles = false;
    }
    arrange_graph input = { .boxes = boxes,
                            .box_count = count,
                            .links = links,
                            .link_count = acount(links) };
    arrange_layout(&input, positions);
    for (int i = 0; i < count; i++) {
        graph->nodes[i]->x = positions[i].x;
        graph->nodes[i]->y = positions[i].y;
    }
    afree(links);
    free(positions);
    free(boxes);
}
