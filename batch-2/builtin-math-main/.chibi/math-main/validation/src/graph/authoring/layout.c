#include "graph/authoring/layout.h"

#include "graph/authoring/arrange.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/events.h"
#include "support/array.h"
#include "support/math.h"
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
    LAYOUT_BAND_OTHER,
};

int graph_layout_band(const node *n)
{
    if (n->desc->flow_kind != NODE_FLOW_EVENT) {
        return LAYOUT_BAND_OTHER;
    }
    const char *name = graph_event_name(n);
    if (host_event_is(name, HOST_EVENT_BEGIN)) {
        return LAYOUT_BAND_BEGIN;
    }
    if (host_event_is(name, HOST_EVENT_TICK)) {
        return LAYOUT_BAND_TICK;
    }
    return host_event_reserved(name) ? LAYOUT_BAND_HOST : LAYOUT_BAND_CUSTOM;
}

bool graph_pin_visible(const graph *g, const node *n, bool output, int pin)
{
    const pin_spec *spec = output ? &n->output_pins[pin] : &n->input_pins[pin];
    node *src = NULL;
    int src_pin = 0;
    return output || n->show_advanced || !(spec->flags & PIN_FLAG_ADVANCED) ||
           spec->binding || graph_input_source(g, n, pin, &src, &src_pin);
}

static bool pin_exec(const node *n, bool output, int pin)
{
    return (output ? n->output_pins : n->input_pins)[pin].kind == PIN_EXEC;
}

/* Drawn pins on one side of a node, execution or data. */
static int pin_rows(const graph *g, const node *n, bool output, bool exec, int before)
{
    int rows = 0;
    for (int p = 0; p < before; p++) {
        rows += graph_pin_visible(g, n, output, p) && pin_exec(n, output, p) == exec;
    }
    return rows;
}

static int exec_rows(const graph *g, const node *n)
{
    return math_max(
        pin_rows(g, n, false, true, n->input_count),
        pin_rows(g, n, true, true, n->output_count)
    );
}

float graph_layout_pin_y(const graph *g, const node *n, bool output, int pin)
{
    bool exec = pin_exec(n, output, pin);
    int row = pin_rows(g, n, output, exec, pin);
    if (!exec) {
        row += exec_rows(g, n) +
               (output ? 0 : pin_rows(g, n, true, false, n->output_count));
    }
    return pin_y(row);
}

arrange_box graph_layout_box(const graph *g, const node *n)
{
    int rows = exec_rows(g, n) + pin_rows(g, n, true, false, n->output_count) +
               pin_rows(g, n, false, false, n->input_count);
    return (arrange_box){ .width = 220,
                          .height = 36 + 24 * (float)math_max(rows, 1),
                          .band = graph_layout_band(n) };
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
        boxes[i] = graph_layout_box(graph, graph->nodes[order[i].index]);
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
        apush(
            links,
            ((arrange_link){
                .from = slot[from],
                .to = slot[to],
                .from_y = graph_layout_pin_y(graph, graph->nodes[from], true, from_pin),
                .to_y = graph_layout_pin_y(graph, graph->nodes[to], false, to_pin),
                .flow = graph->nodes[from]->output_pins[from_pin].kind == PIN_EXEC })
        );
    }
    if (acount(links)) {
        asort(links, compare_links);
    }
    arrange_graph input = { .boxes = boxes,
                            .box_count = boxes_count,
                            .links = links,
                            .link_count = acount(links) };
    arrange_layout(&input, ARRANGE_ROUTE_CURVED, arranged);
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
