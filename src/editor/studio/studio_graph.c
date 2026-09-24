/* The behavior graph canvas: nodes are RmlUi elements in a context of their own
 * whose dp ratio is the zoom, so text is rasterized at its displayed size and
 * clipping keeps working (a transformed element would not clip). The canvas is
 * a texture shown by a surface in the Behavior area; window input is replayed
 * into it in graph units. Interaction follows docs/editor-graph-references.md;
 * edits go through the graph editor's mutation helpers and history. */
#include "editor/graph/search.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "platform/cute.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PATH = 96,
    CURVE_POINTS = 256,
};

static const float ZOOM_MIN = 0.1f, ZOOM_MAX = 2.0f;
static const float GRID = 32;
static const float HEAD = 24, PAD = 4, ROW = 22; /* graph.rcss row pitch */
static const float THRESHOLD = 4;   /* window px before a press drags */
static const float WIRE_HIT = 6;    /* window px */
static const float FAR_ZOOM = 0.5f; /* below: titles only */
static const double FIT_SECONDS = 0.3;

/* ---------- Model access ---------- */

static editor *graph_editor(const studio *s)
{
    return s->scene->graph_editor;
}

bool studio_graph_active(const studio *s)
{
    const editor *e = graph_editor(s);
    return e && s->scene->graph_document_id && e->active_graph;
}

static graph *active_graph(const studio *s)
{
    return studio_graph_active(s) ? graph_editor(s)->active_graph : NULL;
}

static bool graph_readonly(const studio *s)
{
    const graph *g = active_graph(s);
    return !g || g->math_expression || !behavior_asset_mutable(graph_editor(s)->asset);
}

/* Streaming edits must reach the preview on the next tick. */
static void touched(editor *e)
{
    e->edit_pending = 1;
}

static bool node_visible(const graph *g, const node *n)
{
    return n && n->desc && !graph_node_is_inline(g, n);
}

/* Row of a pin among the visible pins of its side, as arrange counts rows. */
static int pin_row(const graph *g, const node *n, int kind, int pin)
{
    int row = 0;
    for (int p = 0; p < pin; p++) {
        row += ed_pin_visible(g, n, kind, p);
    }
    return row;
}

static void node_path(char *out, size_t size, uint64_t id)
{
    snprintf(out, size, "world/stage/n%llu", (unsigned long long)id);
}

static void socket_path(char *out, size_t size, uint64_t id, int kind, int pin)
{
    snprintf(
        out,
        size,
        kind == ED_PIN_INPUT ? "world/stage/n%llu/b/in/i%d/s"
                             : "world/stage/n%llu/b/out/o%d/s",
        (unsigned long long)id,
        pin
    );
}

static uint32_t pack(ImVec4 c, float alpha)
{
    return ((uint32_t)lroundf(c.x * 255) << 24) |
           ((uint32_t)lroundf(c.y * 255) << 16) |
           ((uint32_t)lroundf(c.z * 255) << 8) |
           (uint32_t)lroundf(fminf(c.w * alpha, 1) * 255);
}

static void css_color(char *out, size_t size, ImVec4 c)
{
    snprintf(
        out,
        size,
        "#%02x%02x%02x",
        (unsigned)lroundf(c.x * 255),
        (unsigned)lroundf(c.y * 255),
        (unsigned)lroundf(c.z * 255)
    );
}

static ImVec4 pin_color(const pin_spec *spec)
{
    return spec->kind == PIN_EXEC ? (ImVec4){ 0.91f, 0.91f, 0.92f, 1 }
                                  : ed_spec_color(spec);
}

static const char *node_kind(const node *n)
{
    if (ed_is_graph_call(n)) {
        return "call";
    }
    switch (n->desc->display_class) {
        case NODE_DISPLAY_EVENT: return "event";
        case NODE_DISPLAY_FUNCTION: return "function";
        case NODE_DISPLAY_FLOW: return "flow";
        case NODE_DISPLAY_VARIABLE: return "variable";
        case NODE_DISPLAY_LITERAL: return "literal";
        default: return "pure";
    }
}

static uint32_t node_icon(const node *n)
{
    if (ed_is_graph_call(n)) {
        return KIT_ICON_FUNCTION;
    }
    switch (n->desc->display_class) {
        case NODE_DISPLAY_EVENT: return KIT_ICON_EVENT;
        case NODE_DISPLAY_FLOW: return KIT_ICON_BRANCH;
        case NODE_DISPLAY_FUNCTION: return KIT_ICON_FUNCTION;
        case NODE_DISPLAY_VARIABLE: return KIT_ICON_FIELD;
        default: return 0;
    }
}

/* ---------- Node layout ----------
   Nodes are sized here rather than by RmlUi, from measured text and the
   graph.rcss metrics, so a node's box is known before it is laid out: arrange,
   framing and hit tests never wait a frame. */

typedef enum
{
    VALUE_NONE,
    VALUE_CHOICE,
    VALUE_BOOL,
    VALUE_VECTOR,
    VALUE_TEXT,
    VALUE_NUMBER,
    VALUE_CHIP,
} value_kind;

typedef struct
{
    const pin_spec *spec;
    bool connected;
    bool inline_source;
    bool show_label;
    char label[128];
    char chip[160];
    value_kind value;
} pin_parts;

static const float SOCKET_PAD = 10, PART_GAP = 6, COLUMN_GAP = 12;
static const float NODE_MIN = 120, NODE_MAX = 360;

static value_kind value_kind_of(const studio *s, node *n, int output, int p)
{
    const pin_spec *spec = output ? &n->output_pins[p] : &n->input_pins[p];
    int width = ed_vector_width(spec);
    const editor_choices *choices = ed_pin_choices(graph_editor(s), n, output, p);
    if (spec->container != PIN_CONTAINER_SINGLE) {
        return VALUE_NONE;
    }
    if (choices && choices->count > 0) {
        return VALUE_CHOICE;
    }
    if (spec->type == PIN_BOOL) {
        return VALUE_BOOL;
    }
    if (width > 0 && !(spec->flags & PIN_FLAG_COLOR)) {
        return VALUE_VECTOR;
    }
    if (spec->type == PIN_STR || spec->type == PIN_NAME) {
        return VALUE_TEXT;
    }
    return spec->type >= PIN_I8 && spec->type <= PIN_F64 ? VALUE_NUMBER : VALUE_NONE;
}

static bool exec_label(const pin_spec *spec, const char *label)
{
    return spec->kind == PIN_EXEC &&
           (!strcmp(label, "Exec") || !strcmp(label, "Then") ||
            !strcmp(label, "In") || !strcmp(label, "Out"));
}

static void pin_parts_of(const studio *s, node *n, int kind, int p, pin_parts *out)
{
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    bool input = kind == ED_PIN_INPUT;
    const pin_spec *spec = input ? &n->input_pins[p] : &n->output_pins[p];
    *out = (pin_parts){ .spec = spec };
    node *src = NULL;
    int src_pin = -1;
    if (input) {
        out->connected = graph_input_source(gr, n, p, &src, &src_pin) != 0;
        out->inline_source = out->connected && graph_node_is_inline(gr, src);
    } else {
        out->connected = graph_output_local(gr, n, p) != NULL;
        for (int i = 0; i < acount(gr->edges) && !out->connected; i++) {
            out->connected = gr->edges[i].from_id == n->id &&
                             gr->edges[i].from_pin_id == spec->id;
        }
    }
    ed_pin_label(n, spec, out->label, sizeof out->label);
    out->show_label = !exec_label(spec, out->label);
    if (out->inline_source) {
        ed_node_title(e, src, out->chip, sizeof out->chip);
        out->value = VALUE_CHIP;
    } else if (input && spec->binding) {
        char label[160];
        snprintf(out->chip, sizeof out->chip, "%s", ed_binding_label(e, spec->binding, label, sizeof label));
        out->value = VALUE_CHIP;
    } else if ((!input || !out->connected) && ed_pin_editable(gr, n, kind, p)) {
        out->value = value_kind_of(s, n, !input, p);
    }
}

static float text_width(const studio *s, const char *text, float size, int weight)
{
    return s->graph.kit ? kit_text_width(s->graph.kit, text, size, weight)
                        : strlen(text) * size * 0.55f;
}

static float pin_width(const studio *s, node *n, int kind, int p)
{
    pin_parts parts;
    pin_parts_of(s, n, kind, p, &parts);
    float w = 2 * SOCKET_PAD;
    if (parts.show_label) {
        w += text_width(s, parts.label, 12, 400);
    }
    float value = 0;
    switch (parts.value) {
        case VALUE_CHOICE: value = 96; break;
        case VALUE_BOOL: value = 16; break;
        case VALUE_VECTOR:
            value = 58.0f * (float)ed_vector_width(parts.spec);
            break;
        case VALUE_TEXT: value = 96; break;
        case VALUE_NUMBER: value = 64; break;
        case VALUE_CHIP:
            value = fminf(text_width(s, parts.chip, 11, 400) + 12, 150);
            break;
        default: break;
    }
    if (value > 0) {
        w += value + (parts.show_label ? PART_GAP : 0);
    }
    return w;
}

typedef struct
{
    float w, h;
    int rows;
} node_extent;

static node_extent measure_node(const studio *s, node *n);

/* Measured once per frame; mutations clear the cache. */
static node_extent node_size(const studio *s, node *n)
{
    studio_graph *g = (studio_graph *)&s->graph;
    for (int i = 0; i < acount(g->sizes); i++) {
        if (g->sizes[i].id == n->id) {
            return (node_extent){ g->sizes[i].w, g->sizes[i].h, 0 };
        }
    }
    node_extent extent = measure_node(s, n);
    apush(g->sizes, ((typeof(*g->sizes)){ n->id, extent.w, extent.h }));
    return extent;
}

static node_extent measure_node(const studio *s, node *n)
{
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    char title[256];
    ed_node_title(e, n, title, sizeof title);
    ed_node_presentation look = ed_node_appearance(n->desc);
    float head = text_width(s, title, 12, 600) + 16 +
                 (look.symbol || node_icon(n) ? 20 : 0);
    float in = 0, out = 0;
    int inputs = 0, outputs = 0;
    for (int p = 0; p < n->input_count; p++) {
        if (ed_pin_visible(gr, n, ED_PIN_INPUT, p)) {
            in = fmaxf(in, pin_width(s, n, ED_PIN_INPUT, p));
            inputs++;
        }
    }
    for (int p = 0; p < n->output_count; p++) {
        if (ed_pin_visible(gr, n, ED_PIN_OUTPUT, p)) {
            out = fmaxf(out, pin_width(s, n, ED_PIN_OUTPUT, p));
            outputs++;
        }
    }
    float body = in + out + (in > 0 && out > 0 ? COLUMN_GAP : 0);
    node_extent extent;
    extent.w = ceilf(fminf(fmaxf(fmaxf(head, body), NODE_MIN), NODE_MAX));
    extent.rows = inputs > outputs ? inputs : outputs;
    extent.h = HEAD + PAD + (float)extent.rows * ROW + 6 + 2;
    return extent;
}

/* A node's box in graph units. */
static bool node_box(const studio *s, node *n, float box[4])
{
    node_extent extent = node_size(s, n);
    box[0] = n->x;
    box[1] = n->y;
    box[2] = extent.w;
    box[3] = extent.h;
    return true;
}

typedef struct
{
    uint64_t node;
    int kind; /* -1 on the node body */
    int pin;
} graph_hit;

static graph_hit parse_hit(const char *path)
{
    graph_hit hit = { 0, -1, -1 };
    unsigned long long id;
    int used = 0;
    if (!path || sscanf(path, "world/stage/n%llu%n", &id, &used) != 1) {
        return hit;
    }
    hit.node = id;
    int pin, more = 0;
    if (sscanf(path + used, "/b/in/i%d%n", &pin, &more) == 1) {
        hit.kind = ED_PIN_INPUT;
        hit.pin = pin;
    } else if (sscanf(path + used, "/b/out/o%d%n", &pin, &more) == 1) {
        hit.kind = ED_PIN_OUTPUT;
        hit.pin = pin;
    }
    return hit;
}

/* ---------- Camera ---------- */

static float canvas_w(const studio_graph *g)
{
    return g->surface.rect.w / g->zoom;
}

static float canvas_h(const studio_graph *g)
{
    return g->surface.rect.h / g->zoom;
}

/* Window px to graph units. */
static void to_world(const studio_graph *g, float wx, float wy, float *x, float *y)
{
    *x = g->x + (wx - g->surface.rect.x) / g->zoom;
    *y = g->y + (wy - g->surface.rect.y) / g->zoom;
}

static void zoom_at(studio_graph *g, float zoom, float wx, float wy)
{
    zoom = fminf(fmaxf(zoom, ZOOM_MIN), ZOOM_MAX);
    float fx, fy;
    to_world(g, wx, wy, &fx, &fy);
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
static void camera_step(studio *s)
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
static void frame_nodes(studio *s, bool selection, bool animate)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    editor *e = graph_editor(s);
    if (!gr || g->surface.rect.w <= 0) {
        return;
    }
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!node_visible(gr, n) || (selection && !ed_node_selected(e, n)) ||
            !node_box(s, n, box)) {
            continue;
        }
        lo[0] = fminf(lo[0], box[0]);
        lo[1] = fminf(lo[1], box[1]);
        hi[0] = fmaxf(hi[0], box[0] + box[2]);
        hi[1] = fmaxf(hi[1], box[1] + box[3]);
    }
    if (lo[0] > hi[0]) {
        if (selection) {
            frame_nodes(s, false, animate);
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

/* Each graph keeps its own camera; a graph seen for the first time is framed
   once its nodes have been measured. */
/* Undo replaces the asset and its graphs, so views key on the behavior's name
   and the graph's saved ID rather than on memory. */
static uint64_t graph_key(const studio *s)
{
    graph *gr = active_graph(s);
    if (!gr) {
        return 0;
    }
    uint64_t h = 14695981039346656037ull;
    for (const char *c = s->scene->graph_document_id; *c; c++) {
        h = (h ^ (unsigned char)*c) * 1099511628211ull;
    }
    return (h ^ gr->graph_id) * 1099511628211ull | 1;
}

/* Returns true when a different graph came into view. */
static bool follow_graph(studio *s)
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
    bool measured;  /* every visible node has a size */
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
    graph *gr = active_graph(s);
    in->measured = true;
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!node_visible(gr, n) || !node_box(s, n, box)) {
            continue;
        }
        apush(in->nodes, n);
        apush(in->boxes, ((arrange_box){ box[2], box[3], HEAD, ROW, PAD }));
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
                             .from_pin = pin_row(gr, from, ED_PIN_OUTPUT, from_pin),
                             .to_pin = pin_row(gr, to, ED_PIN_INPUT, to_pin) })
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
static void update_routes(studio *s)
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

/* Arranges the graph as one undo step. With keep_placed, only automatic nodes
   move, as opening a graph does. Returns false until every node is measured. */
static bool arrange(studio *s, bool keep_placed)
{
    editor *e = graph_editor(s);
    arrange_input in = { 0 };
    arrange_input_build(s, &in);
    if (!in.measured || !acount(in.nodes)) {
        bool measured = in.measured;
        arrange_input_free(&in);
        return measured;
    }
    arrange_graph layout = arrange_of(&in);
    arrange_layout(&layout, in.pos);
    bool moved = false;
    for (int i = 0; i < acount(in.nodes) && !moved; i++) {
        moved = !(keep_placed && in.nodes[i]->placed) &&
                (in.nodes[i]->x != in.pos[i].x || in.nodes[i]->y != in.pos[i].y);
    }
    if (moved) {
        ed_snapshot_before(e);
        for (int i = 0; i < acount(in.nodes); i++) {
            if (!(keep_placed && in.nodes[i]->placed)) {
                in.nodes[i]->x = in.pos[i].x;
                in.nodes[i]->y = in.pos[i].y;
                in.nodes[i]->placed = false;
            }
        }
    }
    arrange_input_free(&in);
    return true;
}

/* ---------- Mutations ---------- */

static node *node_by_id(const studio *s, uint64_t id)
{
    graph *gr = active_graph(s);
    return gr && id ? graph_node_by_id(gr, id) : NULL;
}

static void show_error(studio *s, const char *message)
{
    kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                           .title = "Can't connect",
                           .description = message }
    );
    studio_record(s, KIT_TOAST_WARNING, message);
}

/* Connects two sockets through the editor's rules (conversions, inline
   sources, exec ordering). Reports the refusal the editor gives. */
static bool connect_pins(studio *s, node *a, int a_kind, int a_pin, node *b, int b_kind, int b_pin)
{
    editor *e = graph_editor(s);
    ed_pin_view from = { a, a_kind, a_pin, { 0, 0 }, 0 };
    ed_pin_view to = { b, b_kind, b_pin, { 0, 0 }, 0 };
    ed_clear_connect_error(e);
    e->pending_snapshot = ed_snapshot(e);
    int connected = ed_connect_with_inline(e, &from, &to, false);
    if (connected) {
        ed_commit_snapshot(e);
    } else {
        ed_cancel_snapshot(e);
        if (e->connect_error[0]) {
            show_error(s, e->connect_error);
        }
    }
    return connected;
}

static bool pins_compatible(const node *a, int a_kind, int a_pin, const node *b, int b_kind, int b_pin)
{
    if (a == b || a_kind == b_kind) {
        return false;
    }
    const pin_spec *out = a_kind == ED_PIN_OUTPUT ? &a->output_pins[a_pin]
                                                  : &b->output_pins[b_pin];
    const pin_spec *in = a_kind == ED_PIN_OUTPUT ? &b->input_pins[b_pin]
                                                 : &a->input_pins[a_pin];
    return out->kind == in->kind && ed_connection_specs_valid(out, in, out->kind) != 0;
}

/* Dropped on a node body: the first compatible socket, exact types first. */
static int body_target(const graph *gr, const node *from, int kind, int pin, const node *to)
{
    int want = kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT;
    int count = want == ED_PIN_INPUT ? to->input_count : to->output_count;
    int fallback = -1;
    for (int p = 0; p < count; p++) {
        if (!ed_pin_visible(gr, to, want, p) ||
            !pins_compatible(from, kind, pin, to, want, p)) {
            continue;
        }
        node *src = NULL;
        int src_pin = -1;
        bool taken = want == ED_PIN_INPUT &&
                     graph_input_source(gr, to, p, &src, &src_pin);
        if (!taken) {
            return p;
        }
        if (fallback < 0) {
            fallback = p;
        }
    }
    return fallback;
}

static void select_only(editor *e, node *n)
{
    ed_clear_selection(e);
    if (n) {
        ed_add_selected(e, n);
    }
}

static void delete_selection(studio *s)
{
    editor *e = graph_editor(s);
    if (graph_readonly(s) || (!acount(e->selected) && !acount(e->selected_links))) {
        return;
    }
    ed_delete_selected(e);
}

static void copy_selection(studio *s)
{
    editor *e = graph_editor(s);
    if (!acount(e->selected)) {
        return;
    }
    char *json = graph_save_nodes_json(e->active_graph, e->selected, acount(e->selected));
    if (json) {
        cf_clipboard_set(json);
        free(e->clipboard);
        e->clipboard = json;
    }
}

static void paste(studio *s)
{
    editor *e = graph_editor(s);
    if (graph_readonly(s)) {
        return;
    }
    char *clipboard = cf_clipboard_get();
    const char *text = clipboard && clipboard[0] ? clipboard : e->clipboard;
    if (text) {
        ed_snapshot_before(e);
        int first = acount(e->active_graph->nodes);
        if (graph_append_json(e->active_graph, text, 24, 24) == 0) {
            ed_clear_selection(e);
            for (int i = first; i < acount(e->active_graph->nodes); i++) {
                if (node_visible(e->active_graph, e->active_graph->nodes[i])) {
                    ed_add_selected(e, e->active_graph->nodes[i]);
                }
            }
        }
    }
    if (clipboard) {
        cf_free(clipboard);
    }
}

/* ---------- Input ---------- */

static float drag_distance(const studio_graph *g)
{
    return hypotf(
        g->in.mouse_x * g->zoom + g->surface.rect.x - g->press_x,
        g->in.mouse_y * g->zoom + g->surface.rect.y - g->press_y
    );
}

/* The wire under the pointer, from the curves drawn last frame. */
static int hit_wire(const studio *s)
{
    const studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    float best = WIRE_HIT / g->zoom;
    int found = -1;
    float points[CURVE_POINTS * 2];
    for (int w = 0; w < acount(g->wire_edges); w++) {
        int count = kit_wire_points(g->kit, "back/wires", w, points, CURVE_POINTS);
        count = count < CURVE_POINTS ? count : CURVE_POINTS;
        for (int i = 1; i < count; i++) {
            float ax = points[i * 2 - 2], ay = points[i * 2 - 1];
            float bx = points[i * 2], by = points[i * 2 + 1];
            float dx = bx - ax, dy = by - ay, px = g->in.mouse_x - ax,
                  py = g->in.mouse_y - ay;
            float t = fmaxf(0, fminf(1, (px * dx + py * dy) / fmaxf(dx * dx + dy * dy, 1e-6f)));
            float d = hypotf(px - dx * t, py - dy * t);
            if (d < best) {
                best = d;
                found = g->wire_edges[w];
            }
        }
    }
    return found < acount(gr->edges) ? found : -1;
}

static void link_ref_of(const edge *ed, ed_link_ref *out)
{
    *out = (ed_link_ref){ ed->from_id, ed->from_pin_id, ed->to_id, ed->to_pin_id };
}

static void begin_link(studio *s, node *n, int kind, int pin)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    node *src = NULL;
    int src_pin = -1;
    /* Dragging a connected data input picks its wire up (Blender, Rete). */
    if (kind == ED_PIN_INPUT && n->input_pins[pin].kind == PIN_DATA &&
        graph_input_source(gr, n, pin, &src, &src_pin) &&
        !graph_node_is_inline(gr, src)) {
        ed_snapshot_before(e);
        graph_remove_edge(gr, src, src_pin, n, pin);
        n = src;
        kind = ED_PIN_OUTPUT;
        pin = src_pin;
    }
    g->gesture = STUDIO_GRAPH_LINK;
    g->link_node = n->id;
    g->link_kind = kind;
    g->link_pin = pin;
}

static void end_link(studio *s, graph_hit hit)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    node *from = node_by_id(s, g->link_node);
    node *to = node_by_id(s, hit.node);
    g->gesture = STUDIO_GRAPH_IDLE;
    if (!from) {
        return;
    }
    if (to && hit.kind >= 0 && hit.pin >= 0) {
        if (pins_compatible(from, g->link_kind, g->link_pin, to, hit.kind, hit.pin) ||
            hit.kind != g->link_kind) {
            connect_pins(s, from, g->link_kind, g->link_pin, to, hit.kind, hit.pin);
        }
        return;
    }
    if (to && to != from) {
        int target = body_target(gr, from, g->link_kind, g->link_pin, to);
        if (target >= 0) {
            connect_pins(
                s,
                from,
                g->link_kind,
                g->link_pin,
                to,
                g->link_kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT,
                target
            );
        }
        return;
    }
    if (!to) {
        /* Dropped on empty canvas: search for a node that fits, spawned here
           and wired (Blender, Unreal). */
        e->spawn_link_pending = 1;
        e->spawn_link_pin = (ed_pin_view){ from, g->link_kind, g->link_pin, { 0, 0 }, 0 };
        e->pending_snapshot = ed_snapshot(e);
        g->spawn_pending = true;
        g->spawn_x = g->world_x;
        g->spawn_y = g->world_y;
        kit_command_open(s->kit, "Add a node for this wire");
    }
}

static void finish_box(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    float x0 = fminf(g->box[0], g->box[2]), x1 = fmaxf(g->box[0], g->box[2]);
    float y0 = fminf(g->box[1], g->box[3]), y1 = fmaxf(g->box[1], g->box[3]);
    ed_clear_selection(e);
    for (int i = 0; i < acount(g->box_base); i++) {
        node *n = graph_node_by_id(gr, g->box_base[i]);
        if (n) {
            ed_add_selected(e, n);
        }
    }
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float b[4];
        if (!node_visible(gr, n) || !node_box(s, n, b)) {
            continue;
        }
        bool inside = b[0] < x1 && b[0] + b[2] > x0 && b[1] < y1 && b[1] + b[3] > y0;
        if (inside && !ed_node_selected(e, n)) {
            ed_add_selected(e, n);
        }
    }
}

/* Mouse wheels zoom; trackpads pan with two fingers and zoom with a modifier.
   A device reads as a trackpad from horizontal or fractional deltas, and the
   reading holds until input pauses for half a second (ComfyUI). */
static void wheel(studio *s, const editor_rml_input *ev)
{
    studio_graph *g = &s->graph;
    float dx = ev->wheel_x, dy = ev->wheel_y;
    bool fractional = fabsf(dy - roundf(dy)) > 0.01f || fabsf(dx - roundf(dx)) > 0.01f;
    if (s->time - g->wheel_time > 0.5) {
        g->trackpad = dx != 0 || fractional;
    } else if (dx != 0 || fractional) {
        g->trackpad = true;
    }
    g->wheel_time = s->time;
    bool zoom = !g->trackpad ||
                (ev->modifiers & (EDITOR_RML_MOD_CONTROL | EDITOR_RML_MOD_META));
    if (zoom) {
        zoom_at(g, g->zoom * powf(2, -dy * 0.2f), ev->x, ev->y);
    } else {
        g->x += dx * 40 / g->zoom;
        g->y += dy * 40 / g->zoom;
        g->camera.active = false;
    }
}

static void press(studio *s, int button, graph_hit hit, bool on_control)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    unsigned mods = g->in.modifiers;
    bool additive = mods & (KIT_MOD_SHIFT | KIT_MOD_CTRL | KIT_MOD_META);
    if (button == 2 || (button == 0 && g->space)) {
        g->gesture = STUDIO_GRAPH_PAN;
        return;
    }
    if (button == 1) {
        g->gesture = STUDIO_GRAPH_RIGHT;
        return;
    }
    if (on_control) {
        return;
    }
    node *n = node_by_id(s, hit.node);
    if (n && g->double_click && ed_is_graph_call(n)) {
        /* Double-click enters a call (Blender's Tab); Back returns. */
        editor_open_graph_call(e, n->id);
        return;
    }
    if (n && hit.kind >= 0 && hit.pin >= 0 && !graph_readonly(s)) {
        begin_link(s, n, hit.kind, hit.pin);
        return;
    }
    if (n) {
        bool selected = ed_node_selected(e, n);
        g->press_node = n->id;
        g->press_toggle = additive && selected;
        if (!selected) {
            if (!additive) {
                ed_clear_selection(e);
            }
            ed_add_selected(e, n);
        }
        e->front_node = n;
        g->gesture = STUDIO_GRAPH_PRESS;
        return;
    }
    if (g->hover_edge >= 0 && g->hover_edge < acount(gr->edges)) {
        ed_link_ref link;
        link_ref_of(&gr->edges[g->hover_edge], &link);
        if ((mods & KIT_MOD_ALT) && !graph_readonly(s)) {
            ed_clear_selection(e);
            ed_add_selected_link(e, &link);
            ed_delete_selected(e);
            return;
        }
        if (!additive) {
            ed_clear_selection(e);
        }
        ed_add_selected_link(e, &link);
        return;
    }
    g->gesture = STUDIO_GRAPH_BOX;
    g->box[0] = g->press_wx;
    g->box[1] = g->press_wy;
    g->box[2] = g->world_x;
    g->box[3] = g->world_y;
    asetlen(g->box_base, 0);
    if (additive) {
        for (int i = 0; i < acount(e->selected); i++) {
            apush(g->box_base, e->selected[i]->id);
        }
    }
    ed_clear_selection(e);
}

static void drag(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    switch (g->gesture) {
        case STUDIO_GRAPH_PRESS:
            if (drag_distance(g) < THRESHOLD || graph_readonly(s)) {
                break;
            }
            g->gesture = STUDIO_GRAPH_MOVE;
            asetlen(g->drag, 0);
            for (int i = 0; i < acount(e->selected); i++) {
                node *n = e->selected[i];
                apush(g->drag, ((typeof(*g->drag)){ n->id, n->x, n->y }));
            }
            e->pending_snapshot = ed_snapshot(e);
            /* fallthrough */
        case STUDIO_GRAPH_MOVE: {
            float dx = g->world_x - g->press_wx, dy = g->world_y - g->press_wy;
            for (int i = 0; i < acount(g->drag); i++) {
                node *n = node_by_id(s, g->drag[i].id);
                if (n) {
                    n->x = g->drag[i].x + dx;
                    n->y = g->drag[i].y + dy;
                    n->placed = true;
                }
            }
            break;
        }
        case STUDIO_GRAPH_BOX:
            g->box[2] = g->world_x;
            g->box[3] = g->world_y;
            finish_box(s);
            break;
        case STUDIO_GRAPH_RIGHT:
            if (drag_distance(g) >= THRESHOLD) {
                g->gesture = STUDIO_GRAPH_PAN;
            }
            break;
        default: break;
    }
}

static void release(studio *s, graph_hit hit)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    switch (g->gesture) {
        case STUDIO_GRAPH_PRESS: {
            /* A click on a selected node without a drag narrows the selection
               to it; with a modifier it toggles. */
            node *n = node_by_id(s, g->press_node);
            if (n && g->press_toggle) {
                ed_remove_selected(e, n);
            } else if (n && !(g->in.modifiers & (KIT_MOD_SHIFT | KIT_MOD_CTRL | KIT_MOD_META))) {
                select_only(e, n);
            }
            break;
        }
        case STUDIO_GRAPH_MOVE: {
            bool moved = false;
            for (int i = 0; i < acount(g->drag); i++) {
                node *n = node_by_id(s, g->drag[i].id);
                moved |= n && (n->x != g->drag[i].x || n->y != g->drag[i].y);
            }
            if (moved) {
                ed_commit_snapshot(e);
            } else {
                ed_cancel_snapshot(e);
            }
            break;
        }
        case STUDIO_GRAPH_LINK: end_link(s, hit); return;
        case STUDIO_GRAPH_RIGHT: {
            /* A right click that didn't move opens the node or wire menu, or on
               empty canvas the add-node search (Unreal's action menu). */
            node *n = node_by_id(
                s,
                parse_hit(g->press_path[0] ? g->press_path : NULL).node
            );
            int wire = n ? -1 : hit_wire(s);
            if (n || wire >= 0) {
                if (n && !ed_node_selected(e, n)) {
                    select_only(e, n);
                }
                g->menu_node = n ? n->id : 0;
                g->menu_edge = wire;
                g->menu_request = true;
                g->menu_x = g->press_x;
                g->menu_y = g->press_y;
            } else if (!graph_readonly(s)) {
                g->spawn_pending = true;
                g->spawn_x = g->world_x;
                g->spawn_y = g->world_y;
                kit_command_open(s->kit, "Add a node");
            }
            break;
        }
        default: break;
    }
    g->gesture = STUDIO_GRAPH_IDLE;
}

static void cancel_gesture(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    if (g->gesture == STUDIO_GRAPH_MOVE) {
        for (int i = 0; i < acount(g->drag); i++) {
            node *n = node_by_id(s, g->drag[i].id);
            if (n) {
                n->x = g->drag[i].x;
                n->y = g->drag[i].y;
            }
        }
        ed_cancel_snapshot(e);
    }
    g->gesture = STUDIO_GRAPH_IDLE;
}

/* Replays this frame's window input into the canvas context: pointer input
   while the canvas is hovered or holds a press, keys while one of its fields
   has focus. The canvas's own gestures read the replayed state. */
static void replay(studio *s)
{
    studio_graph *g = &s->graph;
    kit_frame_input *in = &g->in;
    for (int b = 0; b < 3; b++) {
        in->pressed[b] = in->released[b] = false;
    }
    in->wheel = 0;
    g->press_button = -1;
    bool blocked = kit_command_is_open(s->kit) ||
                   (kit_wants_pointer(s->kit) && !g->surface.hovered);
    for (int i = 0; i < acount(g->events); i++) {
        editor_rml_input ev = g->events[i];
        bool pointer = ev.type <= EDITOR_RML_POINTER_WHEEL;
        if (pointer) {
            float cx = (ev.x - g->surface.rect.x) / g->zoom;
            float cy = (ev.y - g->surface.rect.y) / g->zoom;
            bool inside = ev.x >= g->surface.rect.x && ev.y >= g->surface.rect.y &&
                          ev.x < g->surface.rect.x + g->surface.rect.w &&
                          ev.y < g->surface.rect.y + g->surface.rect.h;
            bool take = g->captured || (inside && !blocked && g->surface.hovered);
            if (ev.type == EDITOR_RML_POINTER_DOWN && !take) {
                kit_blur(g->kit);
                continue;
            }
            if (!take) {
                if (ev.type == EDITOR_RML_POINTER_MOVE && in->mouse_x > -1e8f) {
                    editor_rml_input leave = { .type = EDITOR_RML_POINTER_LEAVE };
                    editor_rml_backend_process_input(g->backend, &leave);
                    in->mouse_x = in->mouse_y = -1e9f;
                }
                continue;
            }
            in->mouse_x = cx;
            in->mouse_y = cy;
            ev.x = cx;
            ev.y = cy;
            in->modifiers = ((ev.modifiers & EDITOR_RML_MOD_CONTROL) ? KIT_MOD_CTRL
                                                                     : 0u) |
                            ((ev.modifiers & EDITOR_RML_MOD_SHIFT) ? KIT_MOD_SHIFT : 0u) |
                            ((ev.modifiers & EDITOR_RML_MOD_ALT) ? KIT_MOD_ALT : 0u) |
                            ((ev.modifiers & EDITOR_RML_MOD_META) ? KIT_MOD_META : 0u);
            if (ev.type == EDITOR_RML_POINTER_WHEEL) {
                wheel(s, &g->events[i]);
                continue;
            }
            if (ev.type == EDITOR_RML_POINTER_DOWN && ev.button >= 0 && ev.button < 3) {
                in->down[ev.button] = in->pressed[ev.button] = true;
                g->captured = true;
                s->focus_area = g->area;
                /* Hit-test where the press happened: a quick click or drag can
                   arrive within one frame. */
                const char *path = kit_hovered_path(g->kit);
                snprintf(g->press_path, sizeof g->press_path, "%s", path ? path : "");
                float wx = g->events[i].x, wy = g->events[i].y;
                g->double_click = ev.button == 0 &&
                                  s->time - g->last_press_time < 0.35 &&
                                  hypotf(wx - g->last_press_x, wy - g->last_press_y) <
                                      THRESHOLD;
                g->last_press_time = g->double_click ? 0 : s->time;
                g->last_press_x = wx;
                g->last_press_y = wy;
                g->press_control = kit_pointer_on_control(g->kit);
                g->press_button = ev.button;
                g->press_x = g->events[i].x;
                g->press_y = g->events[i].y;
                to_world(g, g->press_x, g->press_y, &g->press_wx, &g->press_wy);
            }
            if (ev.type == EDITOR_RML_POINTER_UP && ev.button >= 0 && ev.button < 3) {
                in->down[ev.button] = false;
                in->released[ev.button] = true;
                g->captured = in->down[0] || in->down[1] || in->down[2];
            }
        } else if (!kit_wants_keyboard(g->kit)) {
            continue;
        }
        editor_rml_backend_process_input(g->backend, &ev);
    }
    asetlen(g->events, 0);
}

static void pointer(studio *s)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    to_world(
        g,
        g->in.mouse_x * g->zoom + g->surface.rect.x,
        g->in.mouse_y * g->zoom + g->surface.rect.y,
        &g->world_x,
        &g->world_y
    );
    bool inside = g->in.mouse_x > -1e8f;
    graph_hit hit = inside ? parse_hit(kit_hovered_path(g->kit))
                           : (graph_hit){ 0, -1, -1 };
    bool on_control = inside && kit_pointer_on_control(g->kit);
    node *hovered = node_by_id(s, hit.node);
    g->hover_node = hovered ? hovered->id : 0;
    g->hover_kind = hovered && !on_control ? hit.kind : -1;
    g->hover_pin = hovered && !on_control ? hit.pin : -1;
    g->hover_edge = inside && !hovered && g->gesture == STUDIO_GRAPH_IDLE
                      ? hit_wire(s)
                      : -1;
    if (g->press_button >= 0 && g->gesture == STUDIO_GRAPH_IDLE) {
        press(s, g->press_button, parse_hit(g->press_path[0] ? g->press_path : NULL), g->press_control);
    }
    if (g->gesture == STUDIO_GRAPH_PAN) {
        /* The graph point grabbed at the press stays under the pointer. */
        g->x = g->press_wx - g->in.mouse_x;
        g->y = g->press_wy - g->in.mouse_y;
        g->world_x = g->press_wx;
        g->world_y = g->press_wy;
        g->camera.active = false;
    } else {
        drag(s);
    }
    bool released = g->in.released[0] || g->in.released[1] || g->in.released[2];
    if (released && !(g->in.down[0] || g->in.down[1] || g->in.down[2])) {
        if (g->gesture == STUDIO_GRAPH_PAN) {
            g->gesture = STUDIO_GRAPH_IDLE;
        } else {
            release(s, hit);
        }
    }
    (void)gr;
}

/* Canvas shortcuts act on the area under the pointer or last clicked (Blender). */
static void shortcuts(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    bool over = s->hover_area == g->area ||
                (s->hover_area < 0 && s->focus_area == g->area);
    g->space = over && SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SPACE];
    if (!over || kit_wants_keyboard(s->kit) || kit_wants_keyboard(g->kit) ||
        kit_command_is_open(s->kit)) {
        return;
    }
    for (int i = 0; i < acount(s->keys); i++) {
        SDL_Keycode key = s->keys[i].key;
        bool mod = s->keys[i].mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI);
        bool shift = s->keys[i].mod & SDL_KMOD_SHIFT;
        if (key == SDLK_ESCAPE && g->gesture != STUDIO_GRAPH_IDLE) {
            cancel_gesture(s);
        } else if (!mod && (key == SDLK_DELETE || key == SDLK_BACKSPACE || key == SDLK_X)) {
            delete_selection(s);
        } else if (!mod && !shift && key == SDLK_F) {
            frame_nodes(s, acount(e->selected) > 0, true);
        } else if (!mod && key == SDLK_HOME) {
            frame_nodes(s, false, true);
        } else if (mod && key == SDLK_A) {
            graph *gr = active_graph(s);
            ed_clear_selection(e);
            for (int n = 0; n < acount(gr->nodes); n++) {
                if (node_visible(gr, gr->nodes[n])) {
                    ed_add_selected(e, gr->nodes[n]);
                }
            }
        } else if ((mod && key == SDLK_D) || (shift && key == SDLK_D)) {
            if (!graph_readonly(s)) {
                ed_duplicate_selected(e);
            }
        } else if (mod && key == SDLK_C) {
            copy_selection(s);
        } else if (mod && key == SDLK_X) {
            copy_selection(s);
            delete_selection(s);
        } else if (mod && key == SDLK_V) {
            paste(s);
        } else if ((shift && key == SDLK_A) || key == SDLK_TAB) {
            if (!graph_readonly(s)) {
                g->spawn_pending = true;
                g->spawn_x = g->world_x;
                g->spawn_y = g->world_y;
                kit_command_open(s->kit, "Add a node");
            }
        } else if (!mod && shift && key == SDLK_L) {
            arrange(s, false);
        } else if (key == SDLK_F9 && !graph_readonly(s)) {
            /* Breakpoints on the selected nodes that run (Unreal). */
            bool snapshot = false;
            for (int n = 0; n < acount(e->selected); n++) {
                node *target = e->selected[n];
                if (node_behavior_for(target) == NODE_BEHAVIOR_PURE) {
                    continue;
                }
                if (!snapshot) {
                    ed_snapshot_before(e);
                    snapshot = true;
                }
                graph_set_breakpoint(e->active_graph, target, !target->breakpoint);
            }
        }
    }
}

/* ---------- Usage ----------
   Nodes no event reaches, and pure nodes whose values nothing used reads, draw
   dimmed: the author sees dead code before the compiler prunes it. */

static bool is_unused(const studio_graph *g, uint64_t id)
{
    for (int i = 0; i < acount(g->unused); i++) {
        if (g->unused[i] == id) {
            return true;
        }
    }
    return false;
}

static void find_unused(studio *s)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    asetlen(g->unused, 0);
    int count = acount(gr->nodes);
    bool *used = calloc((size_t)(count ? count : 1), sizeof *used);
    bool roots = false;
    for (int i = 0; i < count; i++) {
        const node *n = gr->nodes[i];
        bool root = n->desc->flow_kind == NODE_FLOW_EVENT ||
                    n->id == gr->entry_id || n->id == gr->return_id || gr->dataflow;
        used[i] = root;
        roots |= root;
    }
    if (!roots) {
        free(used);
        return;
    }
    /* Exec reach forward, then data demand backward, to a fixed point. */
    for (bool changed = true; changed;) {
        changed = false;
        for (int e = 0; e < acount(gr->edges); e++) {
            const edge *ed = &gr->edges[e];
            int from = -1, to = -1;
            for (int i = 0; i < count && (from < 0 || to < 0); i++) {
                from = gr->nodes[i]->id == ed->from_id ? i : from;
                to = gr->nodes[i]->id == ed->to_id ? i : to;
            }
            if (from < 0 || to < 0) {
                continue;
            }
            if (ed->kind == GRAPH_EDGE_EXEC && used[from] && !used[to]) {
                used[to] = changed = true;
            }
            if (ed->kind == GRAPH_EDGE_DATA && used[to] && !used[from] &&
                node_behavior_for(gr->nodes[from]) == NODE_BEHAVIOR_PURE) {
                used[from] = changed = true;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        if (!used[i] && node_visible(gr, gr->nodes[i])) {
            apush(g->unused, gr->nodes[i]->id);
        }
    }
    free(used);
}

/* ---------- Menus ---------- */

static bool menu_item(kit *k, const char *key, const char *label, uint32_t icon, const char *shortcut, bool disabled)
{
    kit_menu_item_desc d = { .label = label,
                             .icon = icon,
                             .shortcut = shortcut,
                             .checked = -1,
                             .disabled = disabled };
    return kit_menu_item(k, key, &d);
}

void studio_graph_menus(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = s->kit;
    editor *e = graph_editor(s);
    if (g->menu_request) {
        kit_popup_open_at(k, "graph-menu", g->menu_x, g->menu_y);
        g->menu_request = false;
    }
    if (!kit_popup_begin(k, "graph-menu", "menu")) {
        return;
    }
    graph *gr = active_graph(s);
    node *n = node_by_id(s, g->menu_node);
    bool readonly = graph_readonly(s);
    if (n) {
        char title[256];
        kit_menu_group(k, "title", ed_node_title(e, n, title, sizeof title));
        if (ed_is_graph_call(n) &&
            menu_item(k, "open", "Open Graph", KIT_ICON_EXTERNAL, "Double-click", false)) {
            editor_open_graph_call(e, n->id);
        }
        if (menu_item(k, "dup", "Duplicate", KIT_ICON_COPY, "Shift D", readonly)) {
            ed_duplicate_selected(e);
        }
        if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X", readonly)) {
            delete_selection(s);
        }
        kit_menu_separator(k, "s0");
        if (menu_item(
                k,
                "mute",
                n->disabled ? "Enable" : "Disable",
                n->disabled ? KIT_ICON_EYE : KIT_ICON_EYE_OFF,
                NULL,
                readonly
            )) {
            ed_snapshot_before(e);
            graph_set_node_disabled(gr, n, !n->disabled);
        }
        if (node_behavior_for(n) != NODE_BEHAVIOR_PURE &&
            menu_item(k, "break", n->breakpoint ? "Clear Breakpoint" : "Set Breakpoint", KIT_ICON_CIRCLE, "F9", readonly)) {
            ed_snapshot_before(e);
            graph_set_breakpoint(gr, n, !n->breakpoint);
        }
    } else if (g->menu_edge >= 0 && g->menu_edge < acount(gr->edges)) {
        if (menu_item(k, "delete", "Delete Wire", KIT_ICON_TRASH, "Alt click", readonly)) {
            ed_link_ref link;
            link_ref_of(&gr->edges[g->menu_edge], &link);
            ed_clear_selection(e);
            ed_add_selected_link(e, &link);
            ed_delete_selected(e);
        }
    }
    kit_popup_end(k);
}

/* ---------- Emission ---------- */

static void emit_value(studio *s, node *n, int output, int p)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    const pin_spec *spec = output ? &n->output_pins[p] : &n->input_pins[p];
    if (spec->container != PIN_CONTAINER_SINGLE) {
        return;
    }
    pin_value value = spec->default_value;
    bool changed = false, streaming = false;
    unsigned flags = graph_readonly(s) ? KIT_READONLY : 0;
    const editor_choices *choices = ed_pin_choices(e, n, output, p);
    int width = ed_vector_width(spec);
    double number = 0;
    float vector[4] = { 0 };
    char text[256];
    if (choices && choices->count > 0) {
        const char *labels[64];
        int count = choices->count < 64 ? choices->count : 64, index = 0;
        for (int i = 0; i < count; i++) {
            labels[i] = choices->items[i].label;
            if (choices->items[i].value ==
                (spec->type == PIN_ENUM ? value.e : value.i)) {
                index = i;
            }
        }
        if (kit_choice(k, "v", &index, labels, count, flags)) {
            if (spec->type == PIN_ENUM) {
                value.e = (int32_t)choices->items[index].value;
            } else {
                value.i = (int32_t)choices->items[index].value;
            }
            changed = true;
        }
    } else if (spec->type == PIN_BOOL) {
        bool b = value.b;
        if (kit_checkbox(k, "v", &b, NULL, flags)) {
            value.b = b;
            changed = true;
        }
    } else if (width > 0 && !(spec->flags & PIN_FLAG_COLOR)) {
        if (spec->default_value.ptr) {
            memcpy(vector, spec->default_value.ptr, (size_t)width * sizeof(float));
        }
        static const char *const axes[] = { "X", "Y", "Z", "W" };
        for (int a = 0; a < width; a++) {
            char key[8];
            snprintf(key, sizeof key, "v%d", a);
            double axis = vector[a];
            kit_number_desc desc = { .precision = 2,
                                     .step = 0.1,
                                     .addon = axes[a],
                                     .addon_cls = a == 0 ? "x"
                                                : a == 1 ? "y"
                                                         : NULL,
                                     .flags = flags };
            if (kit_number(k, key, &axis, &desc)) {
                vector[a] = (float)axis;
                changed = streaming = true;
            }
            if (kit_item_committed(k)) {
                g->value_editing = false;
            }
        }
        if (changed) {
            value.ptr = vector;
        }
    } else if (spec->type == PIN_STR || spec->type == PIN_NAME) {
        snprintf(text, sizeof text, "%s", value.str ? value.str : "");
        if (kit_text_field(k, "v", text, sizeof text, NULL, flags)) {
            value.str = text;
            changed = true;
        }
    } else if (spec->type >= PIN_I8 && spec->type <= PIN_F64) {
        bool integer = spec->type < PIN_F32;
        switch (spec->type) {
            case PIN_I8: number = value.i8; break;
            case PIN_U8: number = value.u8; break;
            case PIN_I16: number = value.i16; break;
            case PIN_U16: number = value.u16; break;
            case PIN_I32: number = value.i; break;
            case PIN_U32: number = value.u; break;
            case PIN_I64: number = (double)value.i64; break;
            case PIN_U64: number = (double)value.u64; break;
            case PIN_F32: number = value.f; break;
            default: number = value.d; break;
        }
        kit_number_desc desc = { .integer = integer,
                                 .precision = integer ? 0 : 2,
                                 .step = integer ? 1 : 0.1,
                                 .flags = flags };
        if (kit_number(k, "v", &number, &desc)) {
            switch (spec->type) {
                case PIN_I8: value.i8 = (int8_t)number; break;
                case PIN_U8: value.u8 = (uint8_t)number; break;
                case PIN_I16: value.i16 = (int16_t)number; break;
                case PIN_U16: value.u16 = (uint16_t)number; break;
                case PIN_I32: value.i = (int32_t)number; break;
                case PIN_U32: value.u = (uint32_t)number; break;
                case PIN_I64: value.i64 = (int64_t)number; break;
                case PIN_U64: value.u64 = (uint64_t)number; break;
                case PIN_F32: value.f = (float)number; break;
                default: value.d = number; break;
            }
            changed = streaming = true;
        }
        if (kit_item_committed(k)) {
            g->value_editing = false;
        }
    }
    if (!changed || graph_readonly(s)) {
        return;
    }
    /* A scrub is one undo step; discrete edits are one each. */
    if (!streaming || !g->value_editing) {
        ed_snapshot_before(e);
        g->value_editing = streaming;
    }
    graph_set_pin_default(gr, n, output, p, &value);
    touched(e);
}

static void emit_pin(studio *s, node *n, int kind, int p)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    bool input = kind == ED_PIN_INPUT;
    pin_parts parts;
    pin_parts_of(s, n, kind, p, &parts);
    const pin_spec *spec = parts.spec;
    char key[16], color[16];
    snprintf(key, sizeof key, input ? "i%d" : "o%d", p);
    bool hovered = g->hover_node == n->id && g->hover_kind == kind &&
                   g->hover_pin == p;
    node *link_from = g->gesture == STUDIO_GRAPH_LINK ? node_by_id(s, g->link_node)
                                                      : NULL;
    bool source = link_from == n && g->link_kind == kind && g->link_pin == p;
    bool fits = link_from &&
                pins_compatible(link_from, g->link_kind, g->link_pin, n, kind, p);
    kit_push(k, key, NULL, KIT_CX(k, "gpin", { "exec", spec->kind == PIN_EXEC }));
    kit_flag(k, "data-hover", hovered);
    kit_push(k, "s", "span", "socket");
    kit_flag(k, "data-hollow", !parts.connected && !parts.inline_source);
    kit_flag(k, "data-hover", hovered);
    kit_flag(k, "data-target", hovered && fits);
    /* While a wire is dragged, sockets it can't reach recede (imgui-node-editor). */
    kit_flag(k, "data-dim", link_from && !source && !fits);
    css_color(color, sizeof color, pin_color(spec));
    if (spec->kind == PIN_EXEC) {
        kit_icon(k, "a", "arrow", KIT_ICON_PLAY);
        kit_style(k, "color", parts.connected ? color : "#8a8a90");
    } else {
        kit_leaf(k, "d", "span", "dot");
        kit_style(k, parts.connected ? "background-color" : "border-color", color);
    }
    kit_pop(k);
    if (!input && parts.value != VALUE_NONE) {
        emit_value(s, n, 1, p);
    }
    if (parts.show_label) {
        kit_label(k, "l", "gpin-label", parts.label);
    }
    if (input && parts.value == VALUE_CHIP) {
        kit_label(k, "c", parts.inline_source ? "chip source" : "chip", parts.chip);
    } else if (input && parts.value != VALUE_NONE) {
        emit_value(s, n, 0, p);
    }
    kit_pop(k);
}

static void emit_node(studio *s, node *n)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    char key[32], position[32], title[256];
    snprintf(key, sizeof key, "n%llu", (unsigned long long)n->id);
    kit_push(k, key, NULL, "gnode");
    snprintf(position, sizeof position, "%.1fdp", n->x);
    kit_style(k, "left", position);
    snprintf(position, sizeof position, "%.1fdp", n->y);
    kit_style(k, "top", position);
    snprintf(position, sizeof position, "%.0fdp", node_size(s, n).w);
    kit_style(k, "width", position);
    kit_attr(k, "data-kind", node_kind(n));
    kit_flag(k, "data-selected", ed_node_selected(e, n));
    kit_flag(k, "data-active", e->front_node == n && ed_node_selected(e, n) && acount(e->selected) > 1);
    kit_flag(k, "data-disabled", n->disabled != 0);
    kit_flag(k, "data-dim", is_unused(g, n->id));
    kit_flag(k, "data-breakpoint", n->breakpoint != 0);
    kit_push(k, "h", NULL, "gnode-head");
    ed_node_presentation look = ed_node_appearance(n->desc);
    uint32_t icon = node_icon(n);
    if (look.symbol) {
        kit_label(k, "sym", "symbol", look.symbol);
    } else if (icon) {
        kit_icon(k, "i", NULL, icon);
    }
    kit_label(k, "t", "title", ed_node_title(e, n, title, sizeof title));
    kit_pop(k);
    kit_push(k, "b", NULL, "gnode-body");
    kit_push(k, "in", NULL, "gpins in");
    for (int p = 0; p < n->input_count; p++) {
        if (ed_pin_visible(gr, n, ED_PIN_INPUT, p)) {
            emit_pin(s, n, ED_PIN_INPUT, p);
        }
    }
    kit_pop(k);
    kit_push(k, "out", NULL, "gpins out");
    for (int p = 0; p < n->output_count; p++) {
        if (ed_pin_visible(gr, n, ED_PIN_OUTPUT, p)) {
            emit_pin(s, n, ED_PIN_OUTPUT, p);
        }
    }
    kit_pop(k);
    kit_pop(k);
    kit_pop(k);
}

/* Wires follow the selection: attached wires brighten, others recede. */
static void emit_wires(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    kit_wire *wires = NULL;
    struct
    {
        char from[PATH], to[PATH];
    } *paths = NULL;
    float *via = NULL;
    int *via_first = NULL;
    asetlen(g->wire_edges, 0);
    update_routes(s);
    bool focus = acount(e->selected) > 0 || g->hover_node;
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int fo = 0, to_out = 0, fp = -1, tp = -1;
        if (!node_visible(gr, from) || !node_visible(gr, to) ||
            !graph_pin_by_id(from, ed->from_pin_id, &fo, &fp) || !fo ||
            !graph_pin_by_id(to, ed->to_pin_id, &to_out, &tp) || to_out) {
            continue;
        }
        apush(g->wire_edges, i);
        apush(paths, ((typeof(*paths)){ { 0 } }));
        socket_path(alast(paths).from, PATH, from->id, ED_PIN_OUTPUT, fp);
        socket_path(alast(paths).to, PATH, to->id, ED_PIN_INPUT, tp);
        const pin_spec *spec = &from->output_pins[fp];
        bool exec = spec->kind == PIN_EXEC;
        ed_link_ref ref;
        link_ref_of(ed, &ref);
        bool attached = ed_node_selected(e, from) || ed_node_selected(e, to) ||
                        g->hover_node == from->id || g->hover_node == to->id ||
                        g->hover_edge == i || ed_link_selected(e, &ref);
        float alpha = !focus || attached ? 1 : 0.35f;
        if (is_unused(g, from->id) || is_unused(g, to->id)) {
            alpha *= 0.5f;
        }
        kit_wire wire = { .color = pack(pin_color(spec), alpha),
                          .width = (exec ? 2.5f : 1.5f) + (attached && focus ? 0.5f : 0),
                          .flags = ed->conversion ? KIT_WIRE_DASHED : 0 };
        /* Executed wires glow and carry markers while their heat lasts. */
        editor_edge_activity activity;
        editor_query_edge_activity(e, ed, &activity);
        if (activity.opacity > 0) {
            wire.color = pack(pin_color(spec), 1);
            wire.width += 1.5f * activity.opacity;
            wire.flags |= KIT_WIRE_FLOW;
            wire.flow = (float)s->time;
        }
        if (ed_link_selected(e, &ref)) {
            wire.color = 0xffa53aff;
        }
        /* Authored reroutes win; otherwise the lanes arrange routes through. */
        int first = acount(via) / 2, count = 0;
        if (ed->reroute_count) {
            for (int r = 0; r < ed->reroute_count; r++) {
                apush(via, ed->reroutes[r].x - g->x);
                apush(via, ed->reroutes[r].y - g->y);
            }
            count = ed->reroute_count;
        } else if (g->edge_route && i < acount(g->edge_route) && g->edge_route[i] >= 0) {
            int link = g->edge_route[i];
            for (int p = g->routes.start[link]; p < g->routes.start[link + 1]; p++) {
                apush(via, g->routes.points[p].x - g->x);
                apush(via, g->routes.points[p].y - g->y);
                count++;
            }
        }
        wire.via_count = count;
        apush(via_first, first);
        apush(wires, wire);
    }
    /* Pointers into the grown arrays are taken once they stop growing. */
    for (int w = 0; w < acount(wires); w++) {
        wires[w].from = paths[w].from;
        wires[w].to = paths[w].to;
        wires[w].via = wires[w].via_count ? via + via_first[w] * 2 : NULL;
    }
    kit_wires_desc desc = { .wires = wires,
                            .count = acount(wires),
                            .style = s->prefs.wires,
                            .grid = GRID,
                            .grid_x = g->x,
                            .grid_y = g->y,
                            .grid_color = 0xffffff0a,
                            .grid_major_color = 0xffffff14 };
    kit_wires(k, "wires", NULL, &desc);
    afree(wires);
    afree(paths);
    afree(via);
    afree(via_first);
}

static void emit_front(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    char value[32];
    if (g->gesture == STUDIO_GRAPH_BOX) {
        float x0 = fminf(g->box[0], g->box[2]) - g->x,
              y0 = fminf(g->box[1], g->box[3]) - g->y;
        kit_leaf(k, "marquee", NULL, "marquee");
        snprintf(value, sizeof value, "%.1fdp", x0);
        kit_style(k, "left", value);
        snprintf(value, sizeof value, "%.1fdp", y0);
        kit_style(k, "top", value);
        snprintf(value, sizeof value, "%.1fdp", fabsf(g->box[2] - g->box[0]));
        kit_style(k, "width", value);
        snprintf(value, sizeof value, "%.1fdp", fabsf(g->box[3] - g->box[1]));
        kit_style(k, "height", value);
    }
    node *from = g->gesture == STUDIO_GRAPH_LINK ? node_by_id(s, g->link_node) : NULL;
    if (!from) {
        return;
    }
    char anchor[PATH], target[PATH];
    socket_path(anchor, sizeof anchor, from->id, g->link_kind, g->link_pin);
    const pin_spec *spec = g->link_kind == ED_PIN_OUTPUT
                             ? &from->output_pins[g->link_pin]
                             : &from->input_pins[g->link_pin];
    node *over = node_by_id(s, g->hover_node);
    bool snap = over && g->hover_kind >= 0 &&
                pins_compatible(from, g->link_kind, g->link_pin, over, g->hover_kind, g->hover_pin);
    if (snap) {
        socket_path(target, sizeof target, over->id, g->hover_kind, g->hover_pin);
    }
    bool invalid = over && g->hover_kind >= 0 && !snap;
    kit_wire wire = { .color = invalid ? 0xe5534bff : pack(pin_color(spec), 1),
                      .width = spec->kind == PIN_EXEC ? 2.5f : 2,
                      .to_x = g->in.mouse_x,
                      .to_y = g->in.mouse_y,
                      .from_x = g->in.mouse_x,
                      .from_y = g->in.mouse_y };
    /* Wires always run output to input, whichever end is being dragged. */
    if (g->link_kind == ED_PIN_OUTPUT) {
        wire.from = anchor;
        wire.to = snap ? target : NULL;
    } else {
        wire.to = anchor;
        wire.from = snap ? target : NULL;
    }
    kit_wires(
        k,
        "link",
        NULL,
        &(kit_wires_desc){ .wires = &wire, .count = 1, .style = s->prefs.wires }
    );
}

static void emit_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    graph *gr = active_graph(s);
    kit_root(k, "back");
    emit_wires(s);
    kit_root(k, "world");
    kit_push(k, "stage", NULL, "stage");
    char value[32];
    snprintf(value, sizeof value, "%.2fdp", -g->x);
    kit_style(k, "left", value);
    snprintf(value, sizeof value, "%.2fdp", -g->y);
    kit_style(k, "top", value);
    for (int i = 0; i < acount(gr->nodes); i++) {
        if (node_visible(gr, gr->nodes[i])) {
            emit_node(s, gr->nodes[i]);
        }
    }
    kit_pop(k);
    kit_root(k, "front");
    emit_front(s);
}

/* ---------- Host ---------- */

static bool ensure_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    if (g->kit) {
        return true;
    }
    float scale = cf_app_get_pixel_scale();
    g->backend = editor_rml_backend_create(
        (int)fmaxf(g->surface.rect.w, 1),
        (int)fmaxf(g->surface.rect.h, 1),
        scale
    );
    if (!g->backend) {
        return false;
    }
    g->kit = kit_create(editor_rml_backend_context(g->backend), "graph.rml");
    g->in.mouse_x = g->in.mouse_y = -1e9f;
    if (g->zoom <= 0) {
        g->zoom = 1;
    }
    return g->kit != NULL;
}

void studio_graph_frame(studio *s, double delta_seconds)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    kit_rect r = g->surface.rect;
    if (g->area < 0 || r.w < 1 || r.h < 1 || !studio_graph_active(s) ||
        !ensure_canvas(s)) {
        asetlen(g->events, 0);
        g->captured = false;
        g->area = -1;
        return;
    }
    /* Opening arranges automatic nodes; an undo that swaps the asset in place
       must not, or it would add history of its own. */
    if (!follow_graph(s)) {
        e->arrange_pending = false;
    }
    float scale = cf_app_get_pixel_scale();
    editor_rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    asetlen(g->sizes, 0);
    replay(s);
    pointer(s);
    shortcuts(s);
    asetlen(g->sizes, 0); /* gestures may have edited the graph */
    camera_step(s);
    if (e->arrange_pending) {
        arrange(s, true);
        e->arrange_pending = false;
    }
    if (!g->framed) {
        frame_nodes(s, false, false);
        g->framed = true;
    }
    editor_rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    g->in.time = s->time;
    g->in.dp_ratio = g->zoom * scale;
    g->in.width = canvas_w(g);
    g->in.height = canvas_h(g);
    kit_set_body_class(g->kit, "far", g->zoom < FAR_ZOOM);
    kit_set_body_class(g->kit, "comfortable", s->prefs.comfortable);
    kit_set_theme(g->kit, "reduced-motion", s->prefs.reduced_motion);
    find_unused(s);
    kit_begin(g->kit, &g->in);
    emit_canvas(s);
    kit_end(g->kit);
    editor_rml_backend_update(g->backend, delta_seconds);
    editor_rml_backend_render(g->backend);
    editor_rml_backend_set_texture(s->backend, "graph", editor_rml_backend_texture(g->backend));
}

void studio_graph_free(studio *s)
{
    studio_graph *g = &s->graph;
    kit_destroy(g->kit);
    editor_rml_backend_destroy(g->backend);
    arrange_routes_free(&g->routes);
    afree(g->edge_route);
    afree(g->wire_edges);
    afree(g->events);
    afree(g->views);
    afree(g->drag);
    afree(g->box_base);
    afree(g->sizes);
    afree(g->unused);
    *g = (studio_graph){ 0 };
}

bool studio_graph_wants_keyboard(const studio *s)
{
    return s->graph.kit && kit_wants_keyboard(s->graph.kit);
}

bool studio_graph_accepts_text(const studio *s)
{
    return s->graph.kit && kit_accepts_text(s->graph.kit);
}

const char *studio_graph_cursor(const studio *s)
{
    const studio_graph *g = &s->graph;
    if (!g->kit || !(g->surface.hovered || g->captured)) {
        return NULL;
    }
    if (g->gesture == STUDIO_GRAPH_PAN || g->gesture == STUDIO_GRAPH_MOVE) {
        return "move";
    }
    if (g->gesture == STUDIO_GRAPH_LINK || g->hover_pin >= 0) {
        return "crosshair";
    }
    return editor_rml_backend_cursor(g->backend);
}

bool studio_graph_can_replay(studio *s, bool redo)
{
    const editor *e = graph_editor(s);
    return studio_graph_active(s) &&
           (redo ? editor_can_redo(e) : editor_can_undo(e));
}

void studio_graph_history(studio *s, bool redo)
{
    editor *e = graph_editor(s);
    s->graph.gesture = STUDIO_GRAPH_IDLE;
    if (redo) {
        editor_redo(e);
    } else {
        editor_undo(e);
    }
}

/* ---------- Area ---------- */

void studio_behavior_header(studio *s, int area)
{
    kit *k = s->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    char label[256];
    if (!gr) {
        kit_label(k, "title", "dim", "No behavior open");
        return;
    }
    if (editor_graph_history_count(e) > 0) {
        kit_button_desc back = { .icon = KIT_ICON_LEFT, .tip = "Back to the caller" };
        if (kit_button(k, "back", &back)) {
            editor_graph_back(e);
        }
    }
    char key[24];
    snprintf(key, sizeof key, "graphs:%d", area);
    snprintf(
        label,
        sizeof label,
        "%s \xe2\x80\xba %s",
        s->scene->graph_document_id,
        gr->name && gr->name[0] ? gr->name : "Event Graph"
    );
    kit_button_desc pick = { .label = label,
                             .icon = KIT_ICON_GRAPH,
                             .cls = "graph-pick",
                             .tip = "Graphs in this behavior" };
    if (kit_button(k, "pick", &pick)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_group(k, "group", "Graphs");
        for (int i = 0; i < acount(e->asset->graphs); i++) {
            graph *item = e->asset->graphs[i];
            char item_key[24];
            snprintf(item_key, sizeof item_key, "g%d", i);
            kit_menu_item_desc d = {
                .label = item->name && item->name[0] ? item->name : "Event Graph",
                .icon = item->kind == GRAPH_FUNCTION ? KIT_ICON_FUNCTION
                      : item->kind == GRAPH_MACRO    ? KIT_ICON_EXPRESSION
                                                     : KIT_ICON_EVENT,
                .checked = -1,
                .current = item == gr
            };
            if (kit_menu_item(k, item_key, &d)) {
                editor_open_graph(e, item->graph_id);
            }
        }
        kit_popup_end(k);
    }
    kit_leaf(k, "gap", NULL, "spacer");
    /* Which runtime instance the wires show (Unreal's debug object). */
    app_scene_authoring *scene = s->scene;
    if (scene->debug_status == APP_DEBUG_AVAILABLE) {
        char debug[160];
        snprintf(
            debug,
            sizeof debug,
            "%s",
            runtime_entity_label(scene, scene->debug.entity)
        );
        kit_label(k, "debug", "badge accent debug-target", debug);
        kit_tip(k, "Wires show this instance running", NULL);
    } else if (scene->debug_status == APP_DEBUG_STALE) {
        kit_label(k, "debug", "badge debug-target", "Edited");
        kit_tip(k, "The running behavior differs from this graph; save to update it", NULL);
    }
    if (s->graph.kit && s->graph.surface.rect.w > 0) {
        kit_number_flow(
            k,
            "zoom",
            "readout",
            roundf(s->graph.zoom * 100),
            &(kit_number_flow_desc){ .suffix = "%" }
        );
    }
    kit_button_desc tidy = { .icon = KIT_ICON_LAYOUT,
                             .tip = "Arrange",
                             .shortcut = "Shift L",
                             .flags = graph_readonly(s) ? KIT_DISABLED : 0 };
    if (kit_button(k, "arrange", &tidy)) {
        arrange(s, false);
        frame_nodes(s, false, true);
    }
    kit_button_desc fit = { .icon = KIT_ICON_FRAME, .tip = "Frame all", .shortcut = "Home" };
    if (kit_button(k, "frame", &fit)) {
        frame_nodes(s, false, true);
    }
}

void studio_behavior_body(studio *s, int area)
{
    kit *k = s->kit;
    studio_graph *g = &s->graph;
    if (!studio_graph_active(s)) {
        kit_push(k, "empty", NULL, "pending-editor");
        kit_icon(k, "icon", NULL, KIT_ICON_GRAPH);
        kit_label(k, "name", NULL, "Open a behavior from Assets");
        kit_pop(k);
        g->area = -1;
        return;
    }
    g->area = area;
    kit_surface(k, "graph", "graph-surface", "graph", &g->surface);
    studio_graph_menus(s);
}

/* ---------- Add node ---------- */

/* How well a search result can take the dragged wire: 3 an exact socket type,
   2 a socket reached by conversion, 1 only a wildcard, 0 nothing typed. */
static int link_fit(const editor *e, const ed_search_match *match)
{
    const node_desc *desc = match->desc;
    const ed_pin_view *link = &e->spawn_link_pin;
    if (!desc || match->function || match->variable || !link->n) {
        return 1;
    }
    bool from_output = link->kind == ED_PIN_OUTPUT;
    const pin_spec *source = from_output ? &link->n->output_pins[link->index]
                                         : &link->n->input_pins[link->index];
    const pin_spec *pins = from_output ? desc->inputs : desc->outputs;
    int count = from_output ? desc->input_count : desc->output_count, best = 0;
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = &pins[i];
        if (pin->kind != source->kind || (pin->flags & PIN_FLAG_SETTING)) {
            continue;
        }
        bool named = pin->type == PIN_STRUCT || pin->type == PIN_ENUM;
        bool exact = pin->type == source->type &&
                     (!named || (pin->type_name && source->type_name &&
                                 !strcmp(pin->type_name, source->type_name)));
        int fit = pin->flags & PIN_FLAG_WILDCARD ? 1 : exact ? 3 : 2;
        best = fit > best ? fit : best;
    }
    return best;
}

/* A node spawned from a wire lands with its wired socket at the drop point
   (Blender, Unreal). */
static void place_on_wire(studio *s, node *n, const ed_pin_view *from)
{
    graph *gr = active_graph(s);
    bool input = from->kind == ED_PIN_OUTPUT;
    int count = input ? n->input_count : n->output_count;
    for (int p = 0; p < count; p++) {
        bool wired = false;
        if (input) {
            node *src = NULL;
            int src_pin = -1;
            wired = graph_input_source(gr, n, p, &src, &src_pin) && src == from->n;
        } else {
            for (int i = 0; i < acount(gr->edges) && !wired; i++) {
                wired = gr->edges[i].from_id == n->id &&
                        gr->edges[i].from_pin_id == n->output_pins[p].id &&
                        gr->edges[i].to_id == from->n->id;
            }
        }
        if (!wired) {
            continue;
        }
        int row = pin_row(gr, n, input ? ED_PIN_INPUT : ED_PIN_OUTPUT, p);
        n->y -= HEAD + PAD + ((float)row + 0.5f) * ROW;
        if (!input) {
            asetlen(s->graph.sizes, 0);
            n->x -= node_size(s, n).w;
        }
        return;
    }
}

bool studio_graph_palette(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    if (!g->spawn_pending) {
        return false;
    }
    if (!kit_command_is_open(s->kit) || !studio_graph_active(s)) {
        g->spawn_pending = false;
        if (e && e->spawn_link_pending) {
            ed_cancel_snapshot(e);
            e->spawn_link_pending = 0;
        }
        return false;
    }
    ed_search_match *found = ed_search_matches(e, kit_command_query(s->kit));
    /* For a dragged wire, nodes whose socket matches its type exactly come
       first, then conversions, then wildcards; each band keeps search order. */
    ed_search_match *matches = NULL;
    for (int band = 3; band >= 0; band--) {
        for (int i = 0; i < acount(found); i++) {
            if ((e->spawn_link_pending ? link_fit(e, &found[i]) : 3) == band) {
                apush(matches, found[i]);
            }
        }
    }
    afree(found);
    int count = acount(matches) < 200 ? acount(matches) : 200;
    kit_command_item *items = NULL;
    for (int i = 0; i < count; i++) {
        apush(
            items,
            ((kit_command_item){ matches[i].name,
                                 matches[i].category,
                                 NULL,
                                 NULL,
                                 NULL,
                                 matches[i].function ? KIT_ICON_FUNCTION
                                 : matches[i].host_event || matches[i].event ? KIT_ICON_EVENT
                                 : matches[i].variable ? KIT_ICON_FIELD
                                                       : 0 })
        );
    }
    int chosen = kit_command_ranked(s->kit, items, acount(items));
    if (chosen >= 0 && chosen < count) {
        bool linked = e->spawn_link_pending;
        ed_pin_view from = e->spawn_link_pin;
        node *n = ed_spawn_match_at(e, &matches[chosen], g->spawn_x, g->spawn_y);
        if (n) {
            select_only(e, n);
            n->placed = true;
            if (linked) {
                place_on_wire(s, n, &from);
            }
        }
        g->spawn_pending = false;
    }
    afree(items);
    afree(matches);
    return true;
}
