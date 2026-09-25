#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/authoring/arrange.h"
#include "graph/authoring/layout.h"
#include "graph/model/registry.h"
#include "support/math.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Layered auto-arrange. The measurement and the wire topology come from the
 * editor; the layout itself lives in arrange.c, which knows no ImGui. */
typedef struct ed_arrange_input
{
    node **nodes;
    arrange_box *boxes;
    arrange_point *pos;
    arrange_link *links;
    int *edge_link; /* per edge: its link, or -1 */
} ed_arrange_input;

static int ed_node_index(node *const *nodes, const node *n)
{
    for (int i = 0; i < acount(nodes); i++) {
        if (nodes[i] == n) {
            return i;
        }
    }
    return -1;
}

/* Where pin row `row` sits below the node's top. */
static float pin_y(const node *n, int row)
{
    return ImGui_GetTextLineHeight() + 2.0f * ed_node_title_padding(n) +
           ed_node_body_padding(n) + ((float)row + 0.5f) * ed_row_height(n);
}

/* Reads the measured layout, so ed_build_layout must have run for the graph. */
static void ed_arrange_input_build(const editor *e, ed_arrange_input *in)
{
    graph *g = e->active_graph;
    for (int i = 0; i < acount(e->views); i++) {
        node *n = e->views[i].n;
        apush(in->nodes, n);
        apush(in->boxes, ((arrange_box){ e->views[i].size.x, e->views[i].size.y, graph_layout_band(n) }));
        apush(in->pos, ((arrange_point){ n->x, n->y }));
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *ed = &g->edges[i];
        apush(in->edge_link, -1);
        node *from = graph_node_by_id(g, ed->from_id);
        node *to = graph_node_by_id(g, ed->to_id);
        if (!from || !to) {
            continue;
        }
        int from_out = 0, to_out = 0, from_pin = -1, to_pin = -1;
        if (!graph_pin_by_id(from, ed->from_pin_id, &from_out, &from_pin) || !from_out) {
            continue;
        }
        if (!graph_pin_by_id(to, ed->to_pin_id, &to_out, &to_pin) || to_out) {
            continue;
        }
        int from_at = ed_node_index(in->nodes, from);
        int to_at = ed_node_index(in->nodes, to);
        if (from_at < 0 || to_at < 0) {
            continue;
        }
        in->edge_link[i] = acount(in->links);
        apush(
            in->links,
            ((arrange_link){
                .from = from_at,
                .to = to_at,
                .from_y = pin_y(from, ed_pin_row(e, from, ED_PIN_OUTPUT, from_pin)),
                .to_y = pin_y(to, ed_pin_row(e, to, ED_PIN_INPUT, to_pin)),
                .flow = from->output_pins[from_pin].kind == PIN_EXEC })
        );
    }
}

static void ed_arrange_input_free(ed_arrange_input *in)
{
    afree(in->nodes);
    afree(in->boxes);
    afree(in->pos);
    afree(in->links);
    afree(in->edge_link);
}

static arrange_graph ed_arrange_graph(const ed_arrange_input *in)
{
    return (arrange_graph){ in->boxes, acount(in->boxes), in->links, acount(in->links) };
}

static uint64_t ed_hash(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

/* Routes depend only on the arrange input, so they are rebuilt when it
 * changes rather than every frame. */
void ed_route_wires(editor *e)
{
    ed_arrange_input in = { 0 };
    ed_arrange_input_build(e, &in);
    uint64_t key = 14695981039346656037ull;
    for (int i = 0; i < acount(in.boxes); i++) {
        key = ed_hash(key, &in.boxes[i], sizeof in.boxes[i]);
        key = ed_hash(key, &in.pos[i], sizeof in.pos[i]);
    }
    for (int i = 0; i < acount(in.links); i++) {
        const arrange_link *l = &in.links[i];
        key = ed_hash(key, &l->from, sizeof l->from);
        key = ed_hash(key, &l->to, sizeof l->to);
        key = ed_hash(key, &l->from_y, sizeof l->from_y);
        key = ed_hash(key, &l->to_y, sizeof l->to_y);
    }
    if (key != e->wire_route_key || !e->wire_routes.start) {
        arrange_graph layout = ed_arrange_graph(&in);
        arrange_route(&layout, in.pos, ARRANGE_ROUTE_CURVED, &e->wire_routes);
        e->wire_route_key = key;
    }
    afree(e->wire_route);
    e->wire_route = in.edge_link;
    in.edge_link = NULL;
    ed_arrange_input_free(&in);
}

/* Placed nodes' positions override the arrangement when `keep_placed` is set;
 * only automatic nodes move. */
static void ed_arrange(editor *e, bool history, bool keep_placed)
{
    ed_build_layout(e);
    ed_arrange_input in = { 0 };
    ed_arrange_input_build(e, &in);
    int count = acount(in.nodes);
    if (!count) {
        ed_arrange_input_free(&in);
        return;
    }
    arrange_graph layout = ed_arrange_graph(&in);
    arrange_layout(&layout, ARRANGE_ROUTE_CURVED, in.pos);

    int moved = 0;
    for (int i = 0; i < count && !moved; i++) {
        moved = !(keep_placed && in.nodes[i]->placed) &&
                (in.nodes[i]->x != in.pos[i].x || in.nodes[i]->y != in.pos[i].y);
    }
    /* An already arranged graph must not push a do-nothing undo entry, and
     * must not destroy the preview instance at the edit choke point. */
    if (moved) {
        if (history) {
            ed_snapshot_before(e);
        }
        for (int i = 0; i < count; i++) {
            if (keep_placed && in.nodes[i]->placed) {
                continue;
            }
            in.nodes[i]->x = in.pos[i].x;
            in.nodes[i]->y = in.pos[i].y;
            in.nodes[i]->placed = false;
        }
    }
    /* The arranged graph rarely still sits where the view was looking, so
     * frame it. Fitting moves zoom and pan, so the screen-space layout is
     * rebuilt after it rather than before. */
    if (e->view_initialized) {
        ed_zoom_fit(e, false);
    } else {
        ed_frame_initial_view(e);
    }
    ed_build_layout(e);
    ed_arrange_input_free(&in);
}

void ed_auto_arrange(editor *e)
{
    ed_arrange(e, true, false);
}
void ed_arrange_automatic(editor *e)
{
    ed_arrange(e, true, true);
}
void ed_arrange_in_transaction(editor *e)
{
    ed_arrange(e, false, false);
}

static void ed_curve_axis_bounds(float p0, float p1, float p2, float p3, float *minimum, float *maximum)
{
    *minimum = ed_min(*minimum, ed_min(p0, p3));
    *maximum = ed_max(*maximum, ed_max(p0, p3));
    double a = -(double)p0 + 3.0 * p1 - 3.0 * p2 + p3;
    double b = 2.0 * ((double)p0 - 2.0 * p1 + p2);
    double c = (double)p1 - p0;
    double roots[2];
    int count = 0;
    if (fabs(a) < 1e-12) {
        if (fabs(b) >= 1e-12) {
            roots[count++] = -c / b;
        }
    } else {
        double discriminant = b * b - 4.0 * a * c;
        if (discriminant >= 0.0) {
            double root = sqrt(discriminant);
            roots[count++] = (-b + root) / (2.0 * a);
            roots[count++] = (-b - root) / (2.0 * a);
        }
    }
    for (int i = 0; i < count; i++) {
        double t = roots[i];
        if (t <= 0.0 || t >= 1.0) {
            continue;
        }
        double u = 1.0 - t;
        float value = (float)(u * u * u * p0 + 3.0 * u * u * t * p1 +
                              3.0 * u * t * t * p2 + t * t * t * p3);
        *minimum = ed_min(*minimum, value);
        *maximum = ed_max(*maximum, value);
    }
}

static bool ed_wire_bounds(const editor *e, const edge *wire, ImVec2 *minimum, ImVec2 *maximum)
{
    arrange_piece *pieces = NULL;
    int count = ed_link_pieces(e, wire, &pieces);
    if (count) {
        *minimum = ed_im(pieces[0].p0);
        *maximum = *minimum;
    }
    for (int k = 0; k < count; k++) {
        const arrange_piece *p = &pieces[k];
        ed_curve_axis_bounds(
            p->p0.x,
            p->p1.x,
            p->p2.x,
            p->p3.x,
            &minimum->x,
            &maximum->x
        );
        ed_curve_axis_bounds(
            p->p0.y,
            p->p1.y,
            p->p2.y,
            p->p3.y,
            &minimum->y,
            &maximum->y
        );
    }
    afree(pieces);
    return count > 0;
}

bool editor_graph_wires_visible(const editor *e)
{
    if (!e || e->canvas_size.x <= 0.0f || e->canvas_size.y <= 0.0f) {
        return false;
    }
    ImVec2 limit = ed_add(e->canvas_min, e->canvas_size);
    for (int i = 0; i < acount(e->active_graph->edges); i++) {
        ImVec2 minimum, maximum;
        if (!ed_wire_bounds(e, &e->active_graph->edges[i], &minimum, &maximum)) {
            continue;
        }
        if (minimum.x < e->canvas_min.x || minimum.y < e->canvas_min.y ||
            maximum.x > limit.x || maximum.y > limit.y) {
            return false;
        }
    }
    return true;
}

void ed_zoom_fit(editor *e, bool animate)
{
    e->fit_mode = true;
    e->fit_resize_pending = false;
    e->fit_restore_pending = false;
    int count = acount(e->active_graph->nodes);
    if (!count || e->canvas_size.x <= 0.0f || e->canvas_size.y <= 0.0f) {
        return;
    }
    int first_index = 0;
    while (first_index < count &&
           graph_node_is_inline(e->active_graph, e->active_graph->nodes[first_index])) {
        first_index++;
    }
    if (first_index == count) {
        return;
    }
    node *first = e->active_graph->nodes[first_index];
    ImVec2 first_size = ed_node_dimensions(e, e->active_graph, first);
    float min_x = first->x;
    float max_x = first->x + first_size.x;
    float min_y = first->y;
    float max_y = first->y + first_size.y;
    for (int i = first_index + 1; i < count; i++) {
        node *n = e->active_graph->nodes[i];
        if (graph_node_is_inline(e->active_graph, n)) {
            continue;
        }
        ImVec2 size = ed_node_dimensions(e, e->active_graph, n);
        if (n->x < min_x) {
            min_x = n->x;
        }
        if (n->x + size.x > max_x) {
            max_x = n->x + size.x;
        }
        if (n->y < min_y) {
            min_y = n->y;
        }
        if (n->y + size.y > max_y) {
            max_y = n->y + size.y;
        }
    }
    /* Rebuild at the current camera so bounds reflect newly arranged nodes. */
    ed_build_layout(e);
    for (int i = 0; i < acount(e->active_graph->edges); i++) {
        ImVec2 minimum, maximum;
        if (!ed_wire_bounds(e, &e->active_graph->edges[i], &minimum, &maximum)) {
            continue;
        }
        minimum = ed_screen_to_world(e, minimum);
        maximum = ed_screen_to_world(e, maximum);
        min_x = ed_min(min_x, minimum.x);
        max_x = ed_max(max_x, maximum.x);
        min_y = ed_min(min_y, minimum.y);
        max_y = ed_max(max_y, maximum.y);
    }
    float width = ed_max(max_x - min_x, 1.0f);
    float height = ed_max(max_y - min_y, 1.0f);
    float available_x = ed_max(e->canvas_size.x - 2.0f * ED_FIT_MARGIN, 1.0f);
    float available_y = ed_max(e->canvas_size.y - 2.0f * ED_FIT_MARGIN, 1.0f);
    float zoom = ed_min(available_x / width, available_y / height);
    zoom = ed_max(ED_MIN_ZOOM, ed_min(ED_MAX_ZOOM, zoom));
    ImVec2 center = { (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f };
    ImVec2 pan = ed_sub(ed_scale(e->canvas_size, 0.5f), ed_scale(center, zoom));
    ed_camera_target(e, pan, zoom, animate);
    e->view_initialized = 1;
}

static node *ed_opening_anchor(const editor *e)
{
    node *first = NULL;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (graph_node_is_inline(e->active_graph, n)) {
            continue;
        }
        if (n->desc->flow_kind == NODE_FLOW_EVENT || n->desc == &NODE_FunctionEntry ||
            !strcmp(n->desc->name, "Shader Surface Output") ||
            !strcmp(n->desc->name, "Shader Shape Output")) {
            return n;
        }
        if (!first || n->x < first->x || (n->x == first->x && n->y < first->y)) {
            first = n;
        }
    }
    return first;
}

void ed_frame_initial_view(editor *e)
{
    ed_zoom_fit(e, false);
    ImVec2 middle = ed_scale(e->canvas_size, 0.5f);
    ImVec2 center = ed_scale(ed_sub(middle, e->pan), 1.0f / e->zoom);
    float zoom = ed_max(ED_READABLE_ZOOM, ed_min(1.0f, e->zoom));
    ImVec2 pan = ed_sub(middle, ed_scale(center, zoom));
    node *anchor = ed_opening_anchor(e);
    if (e->zoom < ED_READABLE_ZOOM && anchor) {
        ImVec2 size = ed_node_dimensions(e, e->active_graph, anchor);
        bool output = !anchor->output_count && anchor->input_count;
        float x = output
                    ? ed_max(ED_FIT_MARGIN, e->canvas_size.x - size.x * zoom - ED_FIT_MARGIN)
                    : ED_FIT_MARGIN;
        /* Leave the first node row below the floating graph controls. */
        pan = (ImVec2){ x - anchor->x * zoom, ED_OPEN_TOP_MARGIN - anchor->y * zoom };
    }
    ed_camera_target(e, pan, zoom, false);
    e->view_initialized = true;
    e->fit_mode = false;
}

typedef struct ed_field_spacing
{
    node *n;
    ImVec2 size;
    bool moved;
} ed_field_spacing;

static int ed_field_spacing_order(const void *left, const void *right)
{
    const node *a = ((const ed_field_spacing *)left)->n;
    const node *b = ((const ed_field_spacing *)right)->n;
    if (a->y != b->y) {
        return a->y < b->y ? -1 : 1;
    }
    return a->id < b->id ? -1 : a->id > b->id;
}

static void ed_make_field_space(editor *e, node *expanded)
{
    ed_field_spacing *rows = NULL;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (graph_node_is_inline(e->active_graph, n)) {
            continue;
        }
        apush(rows, ((ed_field_spacing){ n, ed_node_dimensions(e, e->active_graph, n), n == expanded }));
    }
    qsort(rows, (size_t)acount(rows), sizeof *rows, ed_field_spacing_order);
    for (int i = 0; i < acount(rows); i++) {
        if (!rows[i].moved) {
            continue;
        }
        const node *above = rows[i].n;
        float bottom = above->y + rows[i].size.y + 12.0f;
        for (int j = i + 1; j < acount(rows); j++) {
            node *below = rows[j].n;
            bool same_column = above->x < below->x + rows[j].size.x &&
                               below->x < above->x + rows[i].size.x;
            if (same_column && below->y < bottom) {
                below->y = bottom;
                rows[j].moved = true;
            }
        }
    }
    afree(rows);
}

void ed_apply_field_request(editor *e)
{
    if (e->active_graph->math_expression) {
        return;
    }
    uint64_t node_id = e->fields_node;
    e->fields_node = 0;
    if (!node_id) {
        return;
    }
    node *n = graph_node_by_id(e->active_graph, node_id);
    int output = -1;
    int is_output = 0;
    if (!n || !graph_pin_by_id(n, e->fields_pin, &is_output, &output) || !is_output) {
        return;
    }
    ed_snapshot_before(e);
    int result = e->fields_expand
                   ? graph_expose_output_fields(e->active_graph, n, output)
                   : graph_hide_output_fields(e->active_graph, n, output);
    assert(result == 0);
    if (e->fields_expand) {
        ed_make_field_space(e, n);
    }
}

void ed_resize_fitted_view(editor *e, ImVec2 size)
{
    ImGuiIO *io = ImGui_GetIO();
    double now = ImGui_GetTime();
    bool held = false;
    for (int i = 0; i < 5; i++) {
        held |= io->MouseDown[i];
        if (io->MouseDown[i] || io->MouseClicked[i] || io->MouseReleased[i]) {
            e->camera_activity_time = now;
        }
    }
    if (io->MouseWheel != 0.0f || io->MouseWheelH != 0.0f) {
        e->camera_activity_time = now;
    }
    bool resized = ed_abs(size.x - e->canvas_size.x) > 0.5f ||
                   ed_abs(size.y - e->canvas_size.y) > 0.5f;
    if (resized) {
        e->canvas_resize_time = now;
        e->fit_resize_pending |= e->fit_mode && e->canvas_size.x > 0 &&
                                 e->canvas_size.y > 0;
    }
    e->canvas_size = size;
    if (!e->fit_mode || size.x <= 0 || size.y <= 0 || held || e->dragging ||
        e->panning || e->linking || e->selecting || ImGui_IsAnyItemActive() ||
        ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopup)) {
        return;
    }
    double settle = io->MouseDoubleClickTime;
    if (now - e->camera_activity_time < settle || now - e->canvas_resize_time < settle) {
        return;
    }
    /* Returning to a caller restores an exact camera, including the pane-size
     * changes caused by restoring its selection. Only later resizes refit it. */
    if (e->fit_restore_pending) {
        e->fit_restore_pending = false;
        e->fit_resize_pending = false;
        return;
    }
    if (e->fit_resize_pending) {
        ed_zoom_fit(e, true);
    }
}

/* Both coordinates use the same blend so wheel zoom holds its world point
 * beneath the pointer throughout the motion, not only at the destination. */
void ed_camera_target(editor *e, ImVec2 pan, float zoom, bool animate)
{
    e->camera_pan = pan;
    e->camera_zoom = zoom;
    e->camera_moving = animate && !editor_ui_reduced_motion();
    if (!e->camera_moving) {
        e->pan = pan;
        e->zoom = zoom;
    }
}

void ed_camera_advance(editor *e, float dt)
{
    if (!e->camera_moving) {
        return;
    }
    float blend = editor_ui_reduced_motion() ? 1.0f
                                             : 1.0f - math_pow(0.001f, dt / 0.18f);
    e->pan = ed_add(e->pan, ed_scale(ed_sub(e->camera_pan, e->pan), blend));
    e->zoom += (e->camera_zoom - e->zoom) * blend;
    if (ed_abs(e->camera_pan.x - e->pan.x) < 0.1f &&
        ed_abs(e->camera_pan.y - e->pan.y) < 0.1f &&
        ed_abs(e->camera_zoom - e->zoom) < 0.0001f) {
        e->pan = e->camera_pan;
        e->zoom = e->camera_zoom;
        e->camera_moving = false;
    }
}

/* Every pin type needs an opaque color: omitted designated entries hide both
 * its port and every connection sourced from it. */
