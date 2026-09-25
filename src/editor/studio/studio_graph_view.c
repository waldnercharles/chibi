/* The graph canvas camera, per-graph views and arrangement. */
#include "editor/studio/studio_graph_internal.h"
#include "support/array.h"

#include <math.h>

static const float ZOOM_MIN = 0.1f, ZOOM_MAX = 2.0f;
static const double FIT_SECONDS = 0.3;

/* ---------- Camera ---------- */

/* Window px to graph units. */
void sg_to_world(const studio_graph *g, float wx, float wy, float *x, float *y)
{
    *x = g->x + (wx - g->surface.rect.x) / g->zoom;
    *y = g->y + (wy - g->surface.rect.y) / g->zoom;
}

void sg_zoom_at(studio_graph *g, float zoom, float wx, float wy)
{
    zoom = fminf(fmaxf(zoom, ZOOM_MIN), ZOOM_MAX);
    float fx, fy;
    sg_to_world(g, wx, wy, &fx, &fy);
    g->zoom = zoom;
    g->x = fx - (wx - g->surface.rect.x) / zoom;
    g->y = fy - (wy - g->surface.rect.y) / zoom;
    g->camera.active = false;
}

static void camera_to(studio *s, float zoom, float x, float y, bool animate)
{
    studio_graph *g = &s->graph;
    if (!animate || s->prefs.reduced_motion) {
        g->zoom = zoom;
        g->x = x;
        g->y = y;
        g->camera.active = false;
        return;
    }
    g->camera.from_zoom = g->zoom;
    g->camera.from_x = g->x;
    g->camera.from_y = g->y;
    g->camera.zoom = zoom;
    g->camera.x = x;
    g->camera.y = y;
    g->camera.start = s->time;
    g->camera.active = true;
}

/* Interpolates in screen space so the path neither swoops nor drifts: the
   visible center moves linearly while zoom eases (easeInOutCubic). */
void sg_camera_step(studio *s)
{
    studio_graph *g = &s->graph;
    if (!g->camera.active) {
        return;
    }
    double t = fmin((s->time - g->camera.start) / FIT_SECONDS, 1);
    float u = (float)(t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2);
    float w = g->surface.rect.w, h = g->surface.rect.h;
    float from_cx = g->camera.from_x + w / 2 / g->camera.from_zoom;
    float from_cy = g->camera.from_y + h / 2 / g->camera.from_zoom;
    float to_cx = g->camera.x + w / 2 / g->camera.zoom;
    float to_cy = g->camera.y + h / 2 / g->camera.zoom;
    g->zoom = g->camera.from_zoom + (g->camera.zoom - g->camera.from_zoom) * u;
    g->x = from_cx + (to_cx - from_cx) * u - w / 2 / g->zoom;
    g->y = from_cy + (to_cy - from_cy) * u - h / 2 / g->zoom;
    g->camera.active = t < 1;
}

/* Fits the selection, or everything, with 10% padding and never past 1:1. */
void sg_frame_nodes(studio *s, bool selection, bool animate)
{
    studio_graph *g = &s->graph;
    graph *gr = sg_active_graph(s);
    editor *e = sg_graph_editor(s);
    if (!gr || g->surface.rect.w <= 0) {
        return;
    }
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!sg_node_visible(gr, n) || (selection && !ed_node_selected(e, n)) ||
            !sg_node_box(s, n, box)) {
            continue;
        }
        lo[0] = fminf(lo[0], box[0]);
        lo[1] = fminf(lo[1], box[1]);
        hi[0] = fmaxf(hi[0], box[0] + box[2]);
        hi[1] = fmaxf(hi[1], box[1] + box[3]);
    }
    if (lo[0] > hi[0]) {
        if (selection) {
            sg_frame_nodes(s, false, animate);
        }
        return;
    }
    float w = g->surface.rect.w, h = g->surface.rect.h;
    float bw = hi[0] - lo[0], bh = hi[1] - lo[1];
    float zoom = fminf(fminf(w / (bw * 1.2f), h / (bh * 1.2f)), 1);
    zoom = fminf(fmaxf(zoom, ZOOM_MIN), ZOOM_MAX);
    camera_to(s, zoom, lo[0] + bw / 2 - w / 2 / zoom, lo[1] + bh / 2 - h / 2 / zoom, animate);
}

static studio_graph_view *view_for(studio_graph *g, uint64_t graph_id)
{
    for (int i = 0; i < acount(g->views); i++) {
        if (g->views[i].graph_id == graph_id) {
            return &g->views[i];
        }
    }
    return NULL;
}

/* Undo replaces the asset and its graphs, so views key on the behavior's name
   and the graph's saved ID rather than on memory. */
static uint64_t graph_key(const studio *s)
{
    graph *gr = sg_active_graph(s);
    if (!gr) {
        return 0;
    }
    uint64_t h = 14695981039346656037ull;
    for (const char *c = s->scene->graph_document_id; *c; c++) {
        h = (h ^ (unsigned char)*c) * 1099511628211ull;
    }
    return (h ^ gr->graph_id) * 1099511628211ull | 1;
}

/* Each graph keeps its own camera; one seen for the first time is framed.
   Returns true when a different graph came into view. */
bool sg_follow_graph(studio *s)
{
    studio_graph *g = &s->graph;
    uint64_t id = graph_key(s);
    if (id == g->graph_id) {
        return false;
    }
    studio_graph_view *old = view_for(g, g->graph_id);
    if (g->graph_id && !old) {
        apush(g->views, ((studio_graph_view){ g->graph_id }));
        old = &alast(g->views);
    }
    if (old) {
        *old = (studio_graph_view){ g->graph_id, g->zoom, g->x, g->y };
    }
    g->graph_id = id;
    g->gesture = STUDIO_GRAPH_IDLE;
    g->camera.active = false;
    studio_graph_view *view = view_for(g, id);
    g->framed = view != NULL;
    if (view) {
        g->zoom = view->zoom;
        g->x = view->x;
        g->y = view->y;
    } else {
        g->zoom = 1;
        g->x = -40;
        g->y = -40;
    }
    return true;
}

/* ---------- Arrange ---------- */

typedef struct
{
    node **nodes;
    arrange_box *boxes;
    arrange_point *pos;
    arrange_link *links;
    int *edge_link; /* per edge: its link, or -1 */
} arrange_input;

static int input_index(node *const *nodes, const node *n)
{
    for (int i = 0; i < acount(nodes); i++) {
        if (nodes[i] == n) {
            return i;
        }
    }
    return -1;
}

static void arrange_input_build(const studio *s, arrange_input *in)
{
    graph *gr = sg_active_graph(s);
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!sg_node_visible(gr, n) || !sg_node_box(s, n, box)) {
            continue;
        }
        apush(in->nodes, n);
        apush(in->boxes, ((arrange_box){ box[2], box[3] }));
        apush(in->pos, ((arrange_point){ n->x, n->y }));
    }
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        apush(in->edge_link, -1);
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int from_out = 0, to_out = 0, from_pin = -1, to_pin = -1;
        if (!from || !to ||
            !graph_pin_by_id(from, ed->from_pin_id, &from_out, &from_pin) || !from_out ||
            !graph_pin_by_id(to, ed->to_pin_id, &to_out, &to_pin) || to_out) {
            continue;
        }
        int a = input_index(in->nodes, from), b = input_index(in->nodes, to);
        if (a < 0 || b < 0) {
            continue;
        }
        in->edge_link[i] = acount(in->links);
        apush(
            in->links,
            ((arrange_link){ .from = a,
                             .to = b,
                             .from_y = sg_pin_y(s, from, ED_PIN_OUTPUT, from_pin),
                             .to_y = sg_pin_y(s, to, ED_PIN_INPUT, to_pin) })
        );
    }
}

static void arrange_input_free(arrange_input *in)
{
    afree(in->nodes);
    afree(in->boxes);
    afree(in->pos);
    afree(in->links);
    afree(in->edge_link);
}

static arrange_graph arrange_of(const arrange_input *in)
{
    return (arrange_graph){ in->boxes, acount(in->boxes), in->links, acount(in->links) };
}

static uint64_t hash(uint64_t h, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ bytes[i]) * 1099511628211ull;
    }
    return h;
}

/* Lanes for wires that skip a column, rebuilt only when the layout changes. */
void sg_update_routes(studio *s)
{
    studio_graph *g = &s->graph;
    arrange_input in = { 0 };
    arrange_input_build(s, &in);
    uint64_t key = 14695981039346656037ull;
    key = hash(key, in.boxes, sizeof *in.boxes * (size_t)acount(in.boxes));
    key = hash(key, in.pos, sizeof *in.pos * (size_t)acount(in.pos));
    key = hash(key, in.links, sizeof *in.links * (size_t)acount(in.links));
    if (key != g->measured_key || !g->routes.start) {
        arrange_graph layout = arrange_of(&in);
        arrange_route(&layout, in.pos, &g->routes);
        g->measured_key = key;
    }
    afree(g->edge_route);
    g->edge_route = in.edge_link;
    in.edge_link = NULL;
    arrange_input_free(&in);
}

/* A user's Arrange moves every node as one undo step. Opening a graph lays
   out only its automatic nodes, whose positions documents don't keep, so that
   is neither an edit nor history. */
void sg_arrange(studio *s, bool automatic_only)
{
    editor *e = sg_graph_editor(s);
    arrange_input in = { 0 };
    arrange_input_build(s, &in);
    arrange_graph layout = arrange_of(&in);
    arrange_layout(&layout, in.pos);
    bool moved = false;
    for (int i = 0; i < acount(in.nodes) && !moved; i++) {
        moved = !(automatic_only && in.nodes[i]->placed) &&
                (in.nodes[i]->x != in.pos[i].x || in.nodes[i]->y != in.pos[i].y);
    }
    if (moved && !automatic_only) {
        ed_snapshot_before(e);
    }
    for (int i = 0; moved && i < acount(in.nodes); i++) {
        if (!(automatic_only && in.nodes[i]->placed)) {
            in.nodes[i]->x = in.pos[i].x;
            in.nodes[i]->y = in.pos[i].y;
            in.nodes[i]->placed = false;
        }
    }
    arrange_input_free(&in);
}
