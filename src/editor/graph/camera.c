#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/authoring/arrange.h"
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
static int ed_node_index(node *const *nodes, const node *n)
{
    for (int i = 0; i < acount(nodes); i++) {
        if (nodes[i] == n) {
            return i;
        }
    }
    return -1;
}

static void ed_arrange(editor *e, bool history)
{
    graph *g = e->active_graph;
    node **nodes = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (!graph_node_is_inline(g, g->nodes[i])) {
            apush(nodes, g->nodes[i]);
        }
    }
    int count = acount(nodes);
    if (!count) {
        afree(nodes);
        return;
    }
    float line = ImGui_GetTextLineHeight();
    arrange_box *boxes = NULL;
    arrange_point *pos = NULL;
    arrange_link *links = NULL;
    asetlen(boxes, count);
    asetlen(pos, count);
    for (int i = 0; i < count; i++) {
        node *n = nodes[i];
        ImVec2 size = ed_node_dimensions(e, g, n);
        boxes[i] = (arrange_box){ size.x,
                                  size.y,
                                  line + 2.0f * ed_node_title_padding(n),
                                  ed_row_height(n),
                                  ed_node_body_padding(n) };
        pos[i] = (arrange_point){ n->x, n->y };
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *ed = &g->edges[i];
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
        int from_at = ed_node_index(nodes, from);
        int to_at = ed_node_index(nodes, to);
        if (from_at < 0 || to_at < 0) {
            continue;
        }
        apush(links, (arrange_link){ from_at, to_at, from_pin, to_pin });
    }
    arrange_graph layout = { boxes, count, links, acount(links) };
    arrange_layout(&layout, pos);

    int moved = 0;
    for (int i = 0; i < count && !moved; i++) {
        moved = nodes[i]->x != pos[i].x || nodes[i]->y != pos[i].y;
    }
    /* An already arranged graph must not push a do-nothing undo entry, and
     * must not destroy the preview instance at the edit choke point. */
    if (moved) {
        if (history) {
            ed_snapshot_before(e);
        }
        for (int i = 0; i < count; i++) {
            nodes[i]->x = pos[i].x;
            nodes[i]->y = pos[i].y;
            nodes[i]->placed = false;
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
    afree(links);
    afree(pos);
    afree(boxes);
    afree(nodes);
}

void ed_auto_arrange(editor *e)
{
    ed_arrange(e, true);
}
void ed_arrange_in_transaction(editor *e)
{
    ed_arrange(e, false);
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
    ImVec2 p0, p1, p2, p3;
    if (!ed_link_curve(e, wire, &p0, &p1, &p2, &p3)) {
        return false;
    }
    *minimum = p0;
    *maximum = p0;
    for (int segment = 0; segment <= wire->reroute_count; segment++) {
        ImVec2 a = p0, b = p1, c = p2, d = p3;
        if (wire->reroute_count) {
            ed_route_segment(e, wire, segment, p0, p3, &a, &b, &c, &d);
        }
        ed_curve_axis_bounds(a.x, b.x, c.x, d.x, &minimum->x, &maximum->x);
        ed_curve_axis_bounds(a.y, b.y, c.y, d.y, &minimum->y, &maximum->y);
    }
    return true;
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
