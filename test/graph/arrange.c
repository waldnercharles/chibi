#include "graph/authoring/arrange.h"

#include "graph/authoring/arrange_score.h"
#include "graph/authoring/layout.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "test/suite_internal.h"

#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

/* Mirrors graph_layout's input: its measurement, node-ID order, zeroed seeds
 * and skipped inline nodes, so the benchmark times the layout alone. */
typedef struct arrange_case
{
    arrange_box *boxes;
    arrange_link *links;
    arrange_point *seeds;
} arrange_case;

/* Pin rows on graph_layout's fixed measurement: a 28 unit title, 4 of
 * padding and 24 unit rows. */
#define PIN_Y(row) (32 + ((float)(row) + 0.5f) * 24)
#define LINK(a, b, from_row, to_row)                                           \
    { .from = a, .to = b, .from_y = PIN_Y(from_row), .to_y = PIN_Y(to_row) }

static int arrange_node_slot(const graph *g, uint64_t id)
{
    int slot = 0;
    bool found = false;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (graph_node_is_inline(g, g->nodes[i])) {
            continue;
        }
        slot += g->nodes[i]->id < id;
        found |= g->nodes[i]->id == id;
    }
    return found ? slot : -1;
}

static int arrange_compare_links(const void *a, const void *b)
{
    const arrange_link *x = a;
    const arrange_link *y = b;
    float keys[][2] = { { (float)x->from, (float)y->from },
                        { x->from_y, y->from_y },
                        { (float)x->to, (float)y->to },
                        { x->to_y, y->to_y } };
    for (int i = 0; i < 4; i++) {
        if (keys[i][0] != keys[i][1]) {
            return keys[i][0] < keys[i][1] ? -1 : 1;
        }
    }
    return 0;
}

static arrange_case arrange_case_of(const graph *g)
{
    arrange_case c = { 0 };
    int boxes = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        boxes += !graph_node_is_inline(g, g->nodes[i]);
    }
    asetlen(c.boxes, boxes);
    asetlen(c.seeds, boxes);
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        int slot = arrange_node_slot(g, n->id);
        if (slot < 0) {
            continue;
        }
        c.boxes[slot] = graph_layout_box(g, n);
        c.seeds[slot] = (arrange_point){ 0, 0 };
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        node *from = graph_node_by_id(g, e->from_id);
        node *to = graph_node_by_id(g, e->to_id);
        int from_pin, to_pin;
        if (!from || !to || !graph_pin_by_id(from, e->from_pin_id, NULL, &from_pin) ||
            !graph_pin_by_id(to, e->to_pin_id, NULL, &to_pin) ||
            arrange_node_slot(g, from->id) < 0 || arrange_node_slot(g, to->id) < 0) {
            continue;
        }
        apush(
            c.links,
            ((arrange_link){ .from = arrange_node_slot(g, from->id),
                             .to = arrange_node_slot(g, to->id),
                             .from_y = graph_layout_pin_y(g, from, true, from_pin),
                             .to_y = graph_layout_pin_y(g, to, false, to_pin),
                             .flow = from->output_pins[from_pin].kind == PIN_EXEC })
        );
    }
    if (acount(c.links)) {
        qsort(c.links, (size_t)acount(c.links), sizeof *c.links, arrange_compare_links);
    }
    return c;
}

static void arrange_case_free(arrange_case *c)
{
    afree(c->boxes);
    afree(c->links);
    afree(c->seeds);
}

static arrange_graph arrange_input(const arrange_case *c)
{
    return (arrange_graph){ c->boxes, acount(c->boxes), c->links, acount(c->links) };
}

static const arrange_box arrange_test_box = { 200, 84, 0 };

static const char *const arrange_style_names[ARRANGE_ROUTE_STYLE_COUNT] = {
    "curved",
    "orthogonal",
    "straight",
    "subway"
};

static arrange_score arrange_measure_links(
    const arrange_point *pos,
    int count,
    const arrange_link *links,
    int link_count,
    arrange_route_style style
)
{
    arrange_box boxes[8];
    for (int i = 0; i < count; i++) {
        boxes[i] = arrange_test_box;
    }
    return arrange_measure(&(arrange_graph){ boxes, count, links, link_count }, pos, style);
}

static bool arrange_near(arrange_point a, arrange_point b)
{
    return fabsf(a.x - b.x) < 1.0e-3f && fabsf(a.y - b.y) < 1.0e-3f;
}

static bool arrange_is_line(const arrange_piece *p)
{
    return arrange_near(p->p0, p->p1) && arrange_near(p->p2, p->p3);
}

static void arrange_measure_checks()
{
    /* Skewed so the wires do not cross exactly on a shared sample point,
     * which the strict segment test only resolves under fused rounding. */
    const arrange_point grid[] = { { 0, 0 }, { 0, 300 }, { 600, 0 }, { 600, 240 } };
    const arrange_link swapped[] = { LINK(0, 3, 0, 0), LINK(1, 2, 0, 0) };
    const arrange_link parallel[] = { LINK(0, 2, 0, 0), LINK(1, 3, 0, 0) };
    const arrange_link fanned[] = { LINK(0, 2, 0, 0), LINK(0, 3, 0, 0) };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        bool counted = arrange_measure_links(grid, 4, swapped, 2, style).wire_crossings ==
                           1 &&
                       arrange_measure_links(grid, 4, parallel, 2, style).wire_crossings ==
                           0;
        test_check(counted, arrange_style_names[style]);
        arrange_score fan = arrange_measure_links(grid, 4, fanned, 2, style);
        test_check(fan.wire_crossings == 0 && fan.wire_overlaps == 0, "arrange ignores wires sharing a pin");
        test_check(fan.ink_length < fan.wire_length, "arrange counts a shared trunk once");
    }

    /* Authored handles on the pins draw a straight line, here through box 1. */
    const arrange_point row[] = { { 0, 0 }, { 400, -20 }, { 800, 0 } };
    const arrange_link through[] = {
        { .from = 0, .to = 2, .from_y = PIN_Y(0), .to_y = PIN_Y(0), .custom_handles = true }
    };
    arrange_score hit = arrange_measure_links(row, 3, through, 1, ARRANGE_ROUTE_CURVED);
    test_check(hit.wire_node_hits == 1 && hit.node_overlaps == 0, "arrange counts a wire through an unrelated node");

    /* A backward wire goes around a taller node between its ends. */
    arrange_box tall[3] = { arrange_test_box, arrange_test_box, arrange_test_box };
    tall[1].height = 300;
    const arrange_link back[] = { LINK(2, 0, 0, 0) };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_score around = arrange_measure(&(arrange_graph){ tall, 3, back, 1 }, row, style);
        test_check(around.wire_node_hits == 0, "arrange routes backward wires around the nodes between their ends");
    }

    const arrange_point stacked[] = { { 0, 0 }, { 100, 40 } };
    test_check(
        arrange_measure_links(stacked, 2, NULL, 0, ARRANGE_ROUTE_CURVED).node_overlaps == 1,
        "arrange counts overlapping nodes"
    );

    /* Authored handles on the pins draw straight lines, here along one run. */
    const arrange_point run[] = { { 0, 0 }, { 250, 0 }, { 600, 0 }, { 900, 0 } };
    const arrange_link on_top[] = {
        { .from = 0, .to = 2, .from_y = PIN_Y(0), .to_y = PIN_Y(0), .custom_handles = true },
        { .from = 1, .to = 3, .from_y = PIN_Y(0), .to_y = PIN_Y(0), .custom_handles = true }
    };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        test_check(
            arrange_measure_links(run, 4, on_top, 2, style).wire_overlaps == 1,
            "arrange counts wires on top of each other"
        );
    }
}

/* How many pieces of a path change height. */
static int arrange_climbs(const arrange_piece *pieces, int count, bool vertical_only)
{
    int climbs = 0;
    for (int i = 0; i < count; i++) {
        bool level = pieces[i].p0.y == pieces[i].p3.y;
        bool vertical = pieces[i].p0.x == pieces[i].p3.x;
        climbs += !level && (!vertical_only || vertical);
    }
    return climbs;
}

/* How many times a path changes height: runs of pieces that are not level. */
static int arrange_climb_runs(const arrange_piece *pieces, int count)
{
    int runs = 0;
    bool climbing = false;
    for (int i = 0; i < count; i++) {
        bool level = pieces[i].p0.y == pieces[i].p3.y &&
                     pieces[i].p1.y == pieces[i].p0.y &&
                     pieces[i].p2.y == pieces[i].p0.y;
        runs += !level && !climbing;
        climbing = !level;
    }
    return runs;
}

/* A drawn pin a few units off the height its route planned slides the run
 * beside it instead of jogging, and a subway drop barely longer than two
 * diagonals becomes one. */
static void arrange_turn_checks()
{
    arrange_point track[] = { { 60, 6 }, { 60, 120 } };
    arrange_piece *pieces = NULL;
    arrange_path(
        ARRANGE_ROUTE_ORTHOGONAL,
        (arrange_point){ 0, 0 },
        (arrange_point){ 140, 120 },
        track,
        2,
        &pieces
    );
    bool slid = true;
    for (int i = 0; i < acount(pieces); i++) {
        slid &= !(pieces[i].p0.y == 6 && pieces[i].p3.y == 6);
    }
    test_check(slid && acount(pieces) == 5, "arrange slides a run to a drawn pin instead of jogging");
    aclear(pieces);
    arrange_point drop[] = { { 60, 0 }, { 60, 60 } };
    arrange_path(ARRANGE_ROUTE_SUBWAY, (arrange_point){ 0, 0 }, (arrange_point){ 140, 60 }, drop, 2, &pieces);
    test_check(!arrange_climbs(pieces, acount(pieces), true), "arrange draws a short subway drop as one diagonal");
    afree(pieces);
}

/* Every style draws a joined chain of pieces from pin to pin. */
static void arrange_path_checks()
{
    arrange_point from = { 0, 0 };
    arrange_point to = { 400, 150 };
    arrange_point lane[] = { { 120, 60 }, { 280, 60 } };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_piece *pieces = NULL;
        arrange_path(style, from, to, lane, 2, &pieces);
        int count = acount(pieces);
        bool joined = count > 0 && arrange_near(pieces[0].p0, from) &&
                      arrange_near(pieces[count - 1].p3, to);
        bool shaped = true;
        for (int i = 0; i < count; i++) {
            const arrange_piece *p = &pieces[i];
            joined &= i == 0 || arrange_near(pieces[i - 1].p3, p->p0);
            float dx = fabsf(p->p3.x - p->p0.x);
            float dy = fabsf(p->p3.y - p->p0.y);
            switch (style) {
                case ARRANGE_ROUTE_STRAIGHT:
                    shaped &= arrange_is_line(p);
                    break;
                case ARRANGE_ROUTE_ORTHOGONAL:
                    shaped &= arrange_is_line(p) ? dx < 1.0e-3f || dy < 1.0e-3f
                                                 : fabsf(dx - dy) < 1.0e-3f;
                    break;
                case ARRANGE_ROUTE_SUBWAY:
                    shaped &= arrange_is_line(p) && (dx < 1.0e-3f || dy < 1.0e-3f ||
                                                     fabsf(dx - dy) < 1.0e-3f);
                    break;
                default: break;
            }
        }
        test_check(joined, "arrange path pieces join from pin to pin");
        test_check(shaped, arrange_style_names[style]);
        test_check(pieces[count - 1].segment == 2, "arrange path pieces name their stretch");

        /* Without waypoints, a backward wire still leaves and enters level. */
        aclear(pieces);
        arrange_path(style, to, from, NULL, 0, &pieces);
        count = acount(pieces);
        bool level = pieces[0].p1.y == to.y && pieces[count - 1].p2.y == from.y &&
                     pieces[0].p1.x >= to.x && pieces[count - 1].p2.x <= from.x;
        test_check(style == ARRANGE_ROUTE_CURVED || level, "arrange backward wires leave and enter pins level");
        afree(pieces);
    }
}

static void arrange_route_checks()
{
    arrange_box boxes[3] = { arrange_test_box, arrange_test_box, arrange_test_box };
    const arrange_point row[] = { { 0, 0 }, { 400, -20 }, { 800, 0 } };
    const arrange_link links[] = { LINK(0, 2, 0, 0), LINK(0, 1, 1, 0) };
    arrange_graph g = { boxes, 3, links, 2 };
    arrange_routes routes = { 0 };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_route(&g, row, style, &routes);
        bool clear = routes.start[1] > routes.start[0];
        for (int i = routes.start[0]; i < routes.start[1]; i++) {
            float y = routes.points[i].y;
            clear &= routes.points[i].x < row[1].x ||
                     routes.points[i].x > row[1].x + 200 || y < row[1].y ||
                     y > row[1].y + arrange_test_box.height;
        }
        test_check(clear, "arrange routes a wire past the node in a column it skips");
        test_check(arrange_measure(&g, row, style).wire_node_hits == 0, "arrange measures routed wires as drawn");
    }
    arrange_route(&g, row, ARRANGE_ROUTE_CURVED, &routes);
    test_check(routes.start[2] == routes.start[1], "arrange leaves wires between neighbouring columns unrouted");

    arrange_point *first = NULL;
    for (int i = 0; i < acount(routes.points); i++) {
        apush(first, routes.points[i]);
    }
    arrange_route(&g, row, ARRANGE_ROUTE_CURVED, &routes);
    test_check(
        acount(first) == acount(routes.points) &&
            !memcmp(first, routes.points, sizeof *first * (size_t)acount(first)),
        "arrange routes are deterministic"
    );
    afree(first);

    /* Staggered wires between two columns turn on separate tracks, ordered so
     * they do not cross; a fan-out turns on one shared track. */
    arrange_box pair[4] = { arrange_test_box, arrange_test_box, arrange_test_box, arrange_test_box };
    const arrange_point gap[] = { { 0, 0 }, { 0, 100 }, { 300, 160 }, { 300, 260 } };
    const arrange_link staggered[] = { LINK(0, 2, 0, 0), LINK(1, 3, 0, 0) };
    arrange_graph tracks = { pair, 4, staggered, 2 };
    arrange_route(&tracks, gap, ARRANGE_ROUTE_ORTHOGONAL, &routes);
    test_check(
        acount(routes.points) == 4 && routes.points[0].x != routes.points[2].x,
        "arrange gives overlapping vertical runs their own tracks"
    );
    arrange_score score = arrange_measure(&tracks, gap, ARRANGE_ROUTE_ORTHOGONAL);
    test_check(score.wire_crossings == 0 && score.wire_overlaps == 0, "arrange orders tracks so staggered wires never cross");
    const arrange_link fan[] = { LINK(0, 2, 0, 0), LINK(0, 3, 0, 0) };
    arrange_graph fanned = { pair, 4, fan, 2 };
    arrange_route(&fanned, gap, ARRANGE_ROUTE_ORTHOGONAL, &routes);
    test_check(
        acount(routes.points) == 4 && routes.points[0].x == routes.points[2].x,
        "arrange runs a fan-out down one trunk"
    );

    arrange_route(&(arrange_graph){ 0 }, NULL, ARRANGE_ROUTE_CURVED, &routes);
    test_check(acount(routes.start) == 1 && !acount(routes.points), "arrange routes an empty graph");
    arrange_routes_free(&routes);
}

/* Wires from one output share lanes through the columns they both skip. */
static void arrange_bundle_checks()
{
    arrange_box boxes[4] = { arrange_test_box, arrange_test_box, arrange_test_box, arrange_test_box };
    const arrange_link links[] = {
        LINK(0, 1, 0, 0), LINK(1, 2, 0, 0), LINK(2, 3, 0, 0),
        LINK(0, 2, 1, 1), LINK(0, 3, 1, 1),
    };
    arrange_graph g = { boxes, 4, links, 5 };
    arrange_routes routes = { 0 };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_point pos[4] = { 0 };
        arrange_layout(&g, style, pos);
        arrange_route(&g, pos, style, &routes);
        /* Both long wires cross column 1 at the same height. */
        float middle = pos[1].x + arrange_test_box.width * 0.5f;
        float lane_y[2] = { 0 };
        for (int k = 0; k < 2; k++) {
            const arrange_link *l = &links[3 + k];
            int first = routes.start[3 + k];
            arrange_point from = { pos[l->from].x + arrange_test_box.width,
                                   pos[l->from].y + l->from_y };
            arrange_point to = { pos[l->to].x, pos[l->to].y + l->to_y };
            arrange_piece *pieces = NULL;
            arrange_path(style, from, to, routes.points + first, routes.start[4 + k] - first, &pieces);
            for (int i = 0; i < acount(pieces); i++) {
                if (pieces[i].p0.x <= middle && pieces[i].p3.x >= middle) {
                    lane_y[k] = pieces[i].p0.y;
                }
            }
            afree(pieces);
        }
        arrange_score score = arrange_measure(&g, pos, style);
        test_check(
            score.wire_node_hits == 0 && score.ink_length < score.wire_length,
            arrange_style_names[style]
        );
    }
    arrange_routes_free(&routes);
}

static void arrange_long_link_checks()
{
    arrange_box boxes[5] = { arrange_test_box,
                             arrange_test_box,
                             arrange_test_box,
                             arrange_test_box,
                             arrange_test_box };
    const arrange_link links[] = {
        LINK(0, 1, 0, 0), LINK(1, 2, 0, 0), LINK(2, 3, 0, 0),
        LINK(3, 4, 0, 0), LINK(0, 3, 1, 1), LINK(1, 3, 1, 2),
    };
    arrange_graph g = { boxes, 5, links, 6 };
    arrange_point pos[5] = { 0 };
    arrange_layout(&g, ARRANGE_ROUTE_CURVED, pos);
    int *long_links = NULL;
    arrange_long_links(&g, pos, ARRANGE_ROUTE_CURVED, 1000, &long_links);
    test_check(acount(long_links) == 1 && long_links[0] == 4, "arrange reports links that span three columns");
    afree(long_links);

    const arrange_point grid[] = { { 0, 0 }, { 0, 300 }, { 600, 0 }, { 600, 240 } };
    const arrange_link swapped[] = { LINK(0, 3, 0, 0), LINK(1, 2, 0, 0) };
    arrange_graph crossing = { boxes, 4, swapped, 2 };
    arrange_long_links(&crossing, grid, ARRANGE_ROUTE_CURVED, 1, &long_links);
    test_check(acount(long_links) == 2, "arrange reports links that cross enough wires");
    afree(long_links);
}

/* An execution chain stays level though its first node's data outputs pull
 * it toward the nodes they feed. */
static void arrange_flow_checks()
{
    arrange_box boxes[6];
    for (int i = 0; i < 6; i++) {
        boxes[i] = arrange_test_box;
    }
    boxes[0].height = 132;
    const arrange_link links[] = {
        { .from = 0, .to = 1, .from_y = PIN_Y(0), .to_y = PIN_Y(0), .flow = true },
        { .from = 1, .to = 2, .from_y = PIN_Y(0), .to_y = PIN_Y(0), .flow = true },
        LINK(0, 3, 1, 0),
        LINK(0, 4, 2, 0),
        LINK(0, 5, 3, 0),
    };
    arrange_graph g = { boxes, 6, links, 5 };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_point pos[6] = { { 0, 0 },   { 0, 0 },   { 0, 0 },
                                 { 0, 100 }, { 0, 200 }, { 0, 300 } };
        arrange_layout(&g, style, pos);
        test_check(arrange_measure(&g, pos, style).flow_bends == 0, "arrange keeps execution chains level");
    }
}

/* Unconnected components stack top to bottom by band, then by first box. */
static void arrange_band_checks()
{
    arrange_box boxes[6];
    for (int i = 0; i < 6; i++) {
        boxes[i] = arrange_test_box;
        boxes[i].band = (int[]){ 2, 2, 0, 0, 1, 5 }[i];
    }
    const arrange_link links[] = { LINK(0, 1, 0, 0), LINK(2, 3, 0, 0), LINK(4, 5, 0, 0) };
    arrange_graph g = { boxes, 6, links, 3 };
    arrange_point pos[6] = { 0 };
    arrange_layout(&g, ARRANGE_ROUTE_CURVED, pos);
    test_check(pos[2].y < pos[4].y && pos[4].y < pos[0].y, "arrange stacks event chains by band");
    test_check(pos[0].x == pos[2].x && pos[2].x == pos[4].x, "arrange starts every band at the left edge");
}

/* Bands arrange apart, so where their widths differ one band's neighbouring
 * ranks can share a column spanned by another's wider box. */
static void arrange_band_width_checks()
{
    arrange_box boxes[8];
    arrange_link links[6];
    for (int i = 0; i < 8; i++) {
        boxes[i] = (arrange_box){ i < 4 ? 180 : 260, 84, i < 4 ? 0 : 1 };
    }
    for (int i = 0; i < 6; i++) {
        int from = i < 3 ? i : i + 1;
        links[i] = (arrange_link){ .from = from,
                                   .to = from + 1,
                                   .from_y = PIN_Y(0),
                                   .to_y = PIN_Y(0),
                                   .flow = true };
    }
    arrange_graph g = { boxes, 8, links, 6 };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_point pos[8] = { 0 };
        arrange_layout(&g, style, pos);
        test_check(arrange_measure(&g, pos, style).flow_bends == 0, "arrange keeps bands of different widths level");
    }
}

static void arrange_layout_checks()
{
    /* Fan-out, a long edge, a cycle, a self-loop, and a second component. */
    const arrange_link links[] = {
        LINK(0, 1, 0, 0), LINK(0, 2, 1, 0), LINK(0, 4, 2, 0),
        LINK(1, 3, 0, 0), LINK(2, 3, 0, 1), LINK(3, 4, 0, 1),
        LINK(4, 1, 1, 1), LINK(2, 2, 1, 2), LINK(5, 6, 0, 0),
    };
    /* A chain with wires fanning out past several of its columns. */
    const arrange_link fans[] = {
        LINK(0, 1, 0, 0), LINK(1, 2, 0, 0), LINK(2, 3, 0, 0), LINK(3, 4, 0, 0),
        LINK(0, 2, 1, 1), LINK(0, 3, 2, 1), LINK(0, 4, 1, 1), LINK(1, 4, 1, 2),
    };
    arrange_box boxes[7];
    for (int i = 0; i < 7; i++) {
        boxes[i] = arrange_test_box;
    }
    arrange_graph g = { boxes, 7, links, (int)(sizeof links / sizeof *links) };
    arrange_graph fanned = { boxes, 5, fans, (int)(sizeof fans / sizeof *fans) };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        arrange_point first[7] = { 0 };
        arrange_point second[7] = { 0 };
        arrange_layout(&g, style, first);
        arrange_layout(&g, style, second);
        test_check(!memcmp(first, second, sizeof first), "arrange is deterministic");
        arrange_score cycle = arrange_measure(&g, first, style);
        test_check(cycle.node_overlaps == 0, "arrange leaves no overlapping nodes");
        test_check(cycle.wire_node_hits == 0, "arrange keeps cycles and self-loops off nodes");
        test_check(first[0].x < first[1].x && first[1].x < first[3].x, "arrange ranks sources before their targets");

        arrange_layout(&fanned, style, first);
        arrange_score score = arrange_measure(&fanned, first, style);
        test_check(score.wire_node_hits == 0, "arrange leaves lanes for wires that skip a column");
        test_check(score.wire_overlaps == 0, arrange_style_names[style]);
        arrange_routes routes = { 0 };
        arrange_route(&fanned, first, style, &routes);
        bool once = true;
        for (int i = 0; i < fanned.link_count; i++) {
            const arrange_link *l = &fans[i];
            arrange_point from = { first[l->from].x + arrange_test_box.width,
                                   first[l->from].y + l->from_y };
            arrange_point to = { first[l->to].x, first[l->to].y + l->to_y };
            arrange_piece *pieces = NULL;
            int at = routes.start[i];
            arrange_path(style, from, to, routes.points + at, routes.start[i + 1] - at, &pieces);
            once &= arrange_climb_runs(pieces, acount(pieces)) <= 1;
            afree(pieces);
        }
        test_check(once, "arrange keeps lanes level so every wire changes height once");
        arrange_routes_free(&routes);
    }
}

static int arrange_compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void arrange_collect(const char *dir, char ***paths)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        struct stat info;
        if (stat(path, &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            arrange_collect(path, paths);
            continue;
        }
        size_t len = strlen(path);
        size_t ext = strlen(ASSET_EXTENSION);
        if (len > ext && !strcmp(path + len - ext, ASSET_EXTENSION)) {
            apush(*paths, strdup(path));
        }
    }
    closedir(d);
}

static void arrange_sort_paths(char **paths)
{
    if (acount(paths)) {
        qsort(paths, (size_t)acount(paths), sizeof *paths, arrange_compare_paths);
    }
}

static behavior_asset *arrange_load(const char *path)
{
    char *json = text_file_read(path);
    if (!json || !strstr(json, "\ngraphs:")) {
        free(json);
        return NULL;
    }
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    return asset;
}

static void arrange_score_add(arrange_score *total, arrange_score s)
{
    total->wire_crossings += s.wire_crossings;
    total->wire_node_hits += s.wire_node_hits;
    total->node_overlaps += s.node_overlaps;
    total->wire_overlaps += s.wire_overlaps;
    total->wire_bends += s.wire_bends;
    total->flow_bends += s.flow_bends;
    total->wire_length += s.wire_length;
    total->ink_length += s.ink_length;
}

/* Scores every arena2d behavior graph arranged and drawn in `style`. */
static arrange_score arrange_arena_score(arrange_route_style style, int *graphs)
{
    char **paths = NULL;
    arrange_collect("projects/arena2d/content/behaviors", &paths);
    arrange_sort_paths(paths);
    arrange_score total = { 0 };
    *graphs = 0;
    for (int p = 0; p < acount(paths); p++) {
        behavior_asset *asset = arrange_load(paths[p]);
        for (int gi = 0; asset && gi < acount(asset->graphs); gi++) {
            arrange_case c = arrange_case_of(asset->graphs[gi]);
            arrange_graph g = arrange_input(&c);
            arrange_layout(&g, style, c.seeds);
            arrange_score_add(&total, arrange_measure(&g, c.seeds, style));
            arrange_case_free(&c);
            (*graphs)++;
        }
        if (asset) {
            behavior_asset_free(asset);
        }
        free(paths[p]);
    }
    afree(paths);
    return total;
}

/* Score ceilings for the arena2d behaviors per style, a little above what
 * each style scores today so rounding differences between platforms pass. */
static void arrange_content_checks()
{
    static const struct
    {
        int crossings;
        int bends;
        int flow_bends;
        float ink;
    } ceiling[ARRANGE_ROUTE_STYLE_COUNT] = {
        [ARRANGE_ROUTE_CURVED] = { 31, 394, 74, 83800.0f },
        [ARRANGE_ROUTE_ORTHOGONAL] = { 32, 394, 74, 89000.0f },
        [ARRANGE_ROUTE_STRAIGHT] = { 34, 394, 74, 84500.0f },
        [ARRANGE_ROUTE_SUBWAY] = { 26, 394, 74, 86000.0f },
    };
    for (int style = 0; style < ARRANGE_ROUTE_STYLE_COUNT; style++) {
        int graphs = 0;
        arrange_score s = arrange_arena_score(style, &graphs);
        printf(
            "arrange arena2d %-10s graphs=%d crossings=%d hits=%d overlaps=%d "
            "wire_overlaps=%d bends=%d flow_bends=%d length=%.0f ink=%.0f\n",
            arrange_style_names[style],
            graphs,
            s.wire_crossings,
            s.wire_node_hits,
            s.node_overlaps,
            s.wire_overlaps,
            s.wire_bends,
            s.flow_bends,
            s.wire_length,
            s.ink_length
        );
        test_check(graphs > 0, "arrange scores the arena2d behaviors");
        test_check(s.node_overlaps == 0 && s.wire_node_hits == 0, arrange_style_names[style]);
        test_check(s.wire_overlaps == 0, "arrange keeps arena2d wires off each other");
        test_check(s.wire_crossings <= ceiling[style].crossings, "arrange keeps arena2d crossings in budget");
        test_check(s.wire_bends <= ceiling[style].bends, "arrange keeps arena2d turns in budget");
        test_check(s.flow_bends <= ceiling[style].flow_bends, "arrange keeps arena2d execution chains straight");
        test_check(s.ink_length <= ceiling[style].ink, "arrange keeps arena2d ink in budget");
    }
}

void run_graph_arrange_checks()
{
    arrange_measure_checks();
    arrange_path_checks();
    arrange_turn_checks();
    arrange_route_checks();
    arrange_bundle_checks();
    arrange_long_link_checks();
    arrange_band_checks();
    arrange_flow_checks();
    arrange_band_width_checks();
    arrange_layout_checks();
    arrange_content_checks();
}

static double arrange_now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1.0e6;
}

/* FNV-1a over the placed coordinates, to spot any change in the output. */
static unsigned arrange_hash(const arrange_point *pos, int count)
{
    unsigned hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)pos;
    for (size_t i = 0; i < (size_t)count * sizeof *pos; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static char **arrange_content_paths()
{
    char **paths = NULL;
    arrange_collect("content", &paths);
    DIR *projects = opendir("projects");
    struct dirent *entry;
    while (projects && (entry = readdir(projects))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char dir[512];
        snprintf(dir, sizeof dir, "projects/%s/content", entry->d_name);
        arrange_collect(dir, &paths);
    }
    if (projects) {
        closedir(projects);
    }
    arrange_sort_paths(paths);
    return paths;
}

typedef struct arrange_bench_total
{
    arrange_score score;
    double ms;
    double worst_ms;
    int graphs;
    int points;
} arrange_bench_total;

/* Arranges, routes and scores one graph in one style, printing its row. */
static void arrange_bench_graph(
    const char *name,
    const arrange_case *c,
    arrange_route_style style,
    int repeats,
    arrange_bench_total *total
)
{
    arrange_graph g = arrange_input(c);
    arrange_point *pos = NULL;
    arrange_routes routes = { 0 };
    asetlen(pos, g.box_count);
    double layout_ms = 1.0e30;
    double route_ms = 1.0e30;
    for (int r = 0; r < repeats; r++) {
        memcpy(pos, c->seeds, (size_t)g.box_count * sizeof *pos);
        double start = arrange_now_ms();
        arrange_layout(&g, style, pos);
        double placed = arrange_now_ms();
        arrange_route(&g, pos, style, &routes);
        double routed = arrange_now_ms();
        layout_ms = math_min(layout_ms, placed - start);
        route_ms = math_min(route_ms, routed - placed);
    }
    arrange_score s = arrange_measure(&g, pos, style);
    printf(
        "%-64s %-10s %5d %5d %8.3f %8.3f %5d %5d %5d %5d %5d %6d %9.0f %9.0f "
        "%8.2f %08x\n",
        name,
        arrange_style_names[style],
        g.box_count,
        g.link_count,
        layout_ms,
        route_ms,
        s.wire_crossings,
        s.wire_node_hits,
        s.node_overlaps,
        s.wire_overlaps,
        s.wire_bends,
        acount(routes.points),
        s.wire_length,
        s.ink_length,
        arrange_cost(s),
        arrange_hash(pos, g.box_count)
    );
    arrange_score_add(&total->score, s);
    total->ms += layout_ms + route_ms;
    total->worst_ms = math_max(total->worst_ms, layout_ms + route_ms);
    total->points += acount(routes.points);
    total->graphs++;
    arrange_routes_free(&routes);
    afree(pos);
}

/* Times arrange_layout and arrange_route on every graph asset under content/
 * and projects/<name>/content/, run from the repository root, in every route
 * style. Prints one row per graph and style plus totals per style so layout
 * speed and quality can be compared across changes. `only` limits the run to
 * graph names containing it; `style` to one style by name. */
int bench_graph_arrange(int repeats, const char *only, const char *style)
{
    registry_validate();
    char **paths = arrange_content_paths();
    arrange_bench_total totals[ARRANGE_ROUTE_STYLE_COUNT] = { 0 };
    printf(
        "%-64s %-10s %5s %5s %8s %8s %5s %5s %5s %5s %5s %6s %9s %9s %8s %8s\n",
        "graph",
        "style",
        "nodes",
        "links",
        "ms",
        "route",
        "cross",
        "hits",
        "over",
        "wover",
        "bends",
        "points",
        "length",
        "ink",
        "cost",
        "hash"
    );
    for (int p = 0; p < acount(paths); p++) {
        behavior_asset *asset = arrange_load(paths[p]);
        for (int gi = 0; asset && gi < acount(asset->graphs); gi++) {
            char name[512];
            snprintf(name, sizeof name, "%s#%d", paths[p], gi);
            if (only && !strstr(name, only)) {
                continue;
            }
            arrange_case c = arrange_case_of(asset->graphs[gi]);
            for (int s = 0; acount(c.boxes) >= 2 && s < ARRANGE_ROUTE_STYLE_COUNT; s++) {
                if (!style || !strcmp(style, arrange_style_names[s])) {
                    arrange_bench_graph(name, &c, s, repeats, &totals[s]);
                }
            }
            arrange_case_free(&c);
        }
        if (asset) {
            behavior_asset_free(asset);
        }
        free(paths[p]);
    }
    afree(paths);
    for (int s = 0; s < ARRANGE_ROUTE_STYLE_COUNT; s++) {
        const arrange_bench_total *t = &totals[s];
        if (!t->graphs) {
            continue;
        }
        printf(
            "total %-10s graphs=%d ms=%.3f worst_ms=%.3f crossings=%d hits=%d "
            "overlaps=%d wire_overlaps=%d bends=%d flow_bends=%d points=%d "
            "length=%.0f ink=%.0f cost=%.2f\n",
            arrange_style_names[s],
            t->graphs,
            t->ms,
            t->worst_ms,
            t->score.wire_crossings,
            t->score.wire_node_hits,
            t->score.node_overlaps,
            t->score.wire_overlaps,
            t->score.wire_bends,
            t->score.flow_bends,
            t->points,
            t->score.wire_length,
            t->score.ink_length,
            arrange_cost(t->score)
        );
    }
    return 0;
}
