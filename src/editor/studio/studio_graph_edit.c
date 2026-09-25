/* Graph canvas edits, refactoring, usage and the add-node palette. */
#include "editor/graph/search.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/ui/labels.h"
#include "graph/authoring/linked.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Mutations ---------- */

/* The nodes a frame carries: those wholly inside it when the drag begins. */
void sg_comment_contents(studio *s, uint64_t id, bool frame_only)
{
    studio_graph *g = &s->graph;
    graph *gr = sg_active_graph(s);
    graph_comment_box *box = sg_comment_by_id(s, id);
    if (!box || frame_only) {
        return;
    }
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float b[4];
        if (sg_node_visible(gr, n) && sg_node_box(s, n, b) && b[0] >= box->x &&
            b[1] >= box->y && b[0] + b[2] <= box->x + box->width &&
            b[1] + b[3] <= box->y + box->height) {
            apush(g->drag, ((typeof(*g->drag)){ n->id, n->x, n->y }));
        }
    }
}

/* C frames the selection with a comment (Unreal, Blender's Ctrl J). */
void sg_frame_selection(studio *s)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = 0; i < acount(e->selected); i++) {
        float b[4];
        if (sg_node_box(s, e->selected[i], b)) {
            lo[0] = fminf(lo[0], b[0]);
            lo[1] = fminf(lo[1], b[1]);
            hi[0] = fmaxf(hi[0], b[0] + b[2]);
            hi[1] = fmaxf(hi[1], b[1] + b[3]);
        }
    }
    if (lo[0] > hi[0]) {
        return;
    }
    const float pad = 24, title = 32;
    ed_snapshot_before(e);
    graph_add_comment_box(
        gr,
        "Comment",
        lo[0] - pad,
        lo[1] - pad - title,
        hi[0] - lo[0] + 2 * pad,
        hi[1] - lo[1] + 2 * pad + title
    );
}

void sg_show_error(studio *s, const char *message)
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
bool sg_connect_pins(studio *s, node *a, int a_kind, int a_pin, node *b, int b_kind, int b_pin)
{
    editor *e = sg_graph_editor(s);
    ed_pin_view from = { a, a_kind, a_pin, { 0, 0 }, 0 };
    ed_pin_view to = { b, b_kind, b_pin, { 0, 0 }, 0 };
    ed_clear_connect_error(e);
    e->pending_snapshot = ed_snapshot(e);
    int connected = ed_connect(e, &from, &to);
    if (connected) {
        ed_commit_snapshot(e);
    } else {
        ed_cancel_snapshot(e);
        if (e->connect_error[0]) {
            sg_show_error(s, e->connect_error);
        }
    }
    return connected;
}

bool sg_pins_compatible(const node *a, int a_kind, int a_pin, const node *b, int b_kind, int b_pin)
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
int sg_body_target(const graph *gr, const node *from, int kind, int pin, const node *to)
{
    int want = kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT;
    int count = want == ED_PIN_INPUT ? to->input_count : to->output_count;
    int fallback = -1;
    for (int p = 0; p < count; p++) {
        if (!ed_pin_visible(gr, to, want, p) ||
            !sg_pins_compatible(from, kind, pin, to, want, p)) {
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

void sg_select_only(editor *e, node *n)
{
    ed_clear_selection(e);
    if (n) {
        ed_add_selected(e, n);
    }
}

void sg_delete_selection(studio *s)
{
    editor *e = sg_graph_editor(s);
    if (sg_graph_readonly(s) || (!acount(e->selected) && !acount(e->selected_links))) {
        return;
    }
    ed_delete_selected(e);
}

void sg_copy_selection(studio *s)
{
    editor *e = sg_graph_editor(s);
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

/* Where a paste lands: the pointer over the canvas, else the view's center. */
static void paste_point(const studio *s, float *x, float *y)
{
    const studio_graph *g = &s->graph;
    if (g->in.mouse_x > -1e8f) {
        *x = g->world_x;
        *y = g->world_y;
        return;
    }
    *x = g->x + g->surface.rect.w / 2 / g->zoom;
    *y = g->y + g->surface.rect.h / 2 / g->zoom;
}

/* Pasted nodes center on the pointer (Unreal) and are selected. */
static void place_pasted(studio *s, int first)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = first; i < acount(gr->nodes); i++) {
        float b[4];
        if (sg_node_visible(gr, gr->nodes[i]) && sg_node_box(s, gr->nodes[i], b)) {
            lo[0] = fminf(lo[0], b[0]);
            lo[1] = fminf(lo[1], b[1]);
            hi[0] = fmaxf(hi[0], b[0] + b[2]);
            hi[1] = fmaxf(hi[1], b[1] + b[3]);
        }
    }
    float x, y;
    paste_point(s, &x, &y);
    float dx = lo[0] <= hi[0] ? roundf(x - (lo[0] + hi[0]) / 2) : 0;
    float dy = lo[1] <= hi[1] ? roundf(y - (lo[1] + hi[1]) / 2) : 0;
    ed_clear_selection(e);
    for (int i = first; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        n->x += dx;
        n->y += dy;
        if (sg_node_visible(gr, n)) {
            n->placed = true;
            ed_add_selected(e, n);
        }
    }
}

void sg_paste(studio *s)
{
    editor *e = sg_graph_editor(s);
    if (sg_graph_readonly(s)) {
        return;
    }
    char *clipboard = cf_clipboard_get();
    const char *text = clipboard && clipboard[0] ? clipboard : e->clipboard;
    if (text) {
        ed_snapshot_before(e);
        int first = acount(e->active_graph->nodes);
        if (graph_append_json(e->active_graph, text, 0, 0) == 0) {
            place_pasted(s, first);
        }
    }
    if (clipboard) {
        cf_free(clipboard);
    }
}

/* ---------- Selection layout ---------- */

/* Applies moved positions as one undo step; `before` is the asset as it was. */
static void commit_moves(editor *e, char *before, bool moved)
{
    if (!moved) {
        free(before);
        return;
    }
    e->pending_snapshot = before;
    ed_commit_snapshot(e);
}

static void move_node(node *n, float x, float y, bool *moved)
{
    *moved |= n->x != x || n->y != y;
    n->x = x;
    n->y = y;
    n->placed = true;
}

/* The selected nodes that draw a box, with their boxes. */
static align_box *selected_boxes(studio *s, node ***nodes)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    align_box *boxes = NULL;
    for (int i = 0; i < acount(e->selected); i++) {
        float b[4];
        if (sg_node_visible(gr, e->selected[i]) && sg_node_box(s, e->selected[i], b)) {
            apush(*nodes, e->selected[i]);
            apush(boxes, ((align_box){ b[0], b[1], b[2], b[3] }));
        }
    }
    return boxes;
}

/* Moves the nodes to their boxes as one undo step, and frees both. */
static void apply_boxes(studio *s, node **nodes, align_box *boxes)
{
    editor *e = sg_graph_editor(s);
    char *before = sg_graph_readonly(s) ? NULL : ed_snapshot(e);
    bool moved = false;
    for (int i = 0; before && i < acount(nodes); i++) {
        move_node(nodes[i], roundf(boxes[i].x), roundf(boxes[i].y), &moved);
    }
    commit_moves(e, before, moved);
    afree(nodes);
    afree(boxes);
}

void sg_align(studio *s, align_edge edge)
{
    node **nodes = NULL;
    align_box *boxes = selected_boxes(s, &nodes);
    align_boxes(boxes, acount(boxes), edge);
    apply_boxes(s, nodes, boxes);
}

void sg_distribute(studio *s, bool vertical)
{
    node **nodes = NULL;
    align_box *boxes = selected_boxes(s, &nodes);
    distribute_boxes(boxes, acount(boxes), vertical);
    apply_boxes(s, nodes, boxes);
}

/* Moves one end of a drawn wire so the wire runs level. */
static void level_wire(studio *s, const edge *ed, bool move_source, bool *moved)
{
    graph *gr = sg_active_graph(s);
    node *from, *to;
    int from_pin, to_pin;
    if (!sg_edge_ends(gr, ed, &from, &from_pin, &to, &to_pin) ||
        !sg_node_visible(gr, from) || !sg_node_visible(gr, to) ||
        graph_edge_is_local(gr, ed)) {
        return;
    }
    float ax, ay, bx, by;
    sg_socket_at(s, from, ED_PIN_OUTPUT, from_pin, &ax, &ay);
    sg_socket_at(s, to, ED_PIN_INPUT, to_pin, &bx, &by);
    node *n = move_source ? from : to;
    move_node(n, n->x, n->y + (move_source ? by - ay : ay - by), moved);
}

/* A node's leading wire: its first wired execution input, else its first
   wired data input; a node without inputs leads with its first output. */
static const edge *leading_wire(const graph *gr, const node *n, bool *source)
{
    const edge *data = NULL, *out = NULL;
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        if (ed->to_id == n->id && ed->kind == GRAPH_EDGE_EXEC) {
            *source = false;
            return ed;
        }
        if (ed->to_id == n->id && !data && !graph_edge_is_local(gr, ed) &&
            !graph_node_is_inline(gr, graph_node_by_id(gr, ed->from_id))) {
            data = ed;
        }
        out = !out && ed->from_id == n->id ? ed : out;
    }
    *source = !data;
    return data ? data : out;
}

static int by_x(const void *a, const void *b)
{
    const node *left = *(node *const *)a, *right = *(node *const *)b;
    return (left->x > right->x) - (left->x < right->x);
}

void sg_straighten(studio *s)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    if (sg_graph_readonly(s)) {
        return;
    }
    char *before = ed_snapshot(e);
    bool moved = false;
    for (int i = 0; i < acount(e->selected_links); i++) {
        for (int w = 0; w < acount(gr->edges); w++) {
            ed_link_ref ref;
            sg_link_ref_of(&gr->edges[w], &ref);
            if (ed_link_equal(&ref, &e->selected_links[i])) {
                level_wire(s, &gr->edges[w], false, &moved);
            }
        }
    }
    node **order = NULL;
    for (int i = 0; i < acount(e->selected); i++) {
        apush(order, e->selected[i]);
    }
    if (acount(order) > 1) {
        qsort(order, (size_t)acount(order), sizeof *order, by_x);
    }
    for (int i = 0; i < acount(order); i++) {
        bool source = false;
        const edge *ed = leading_wire(gr, order[i], &source);
        if (ed) {
            level_wire(s, ed, source, &moved);
        }
    }
    afree(order);
    commit_moves(e, before, moved);
}

void sg_straighten_wire(studio *s, int index)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    if (sg_graph_readonly(s) || index < 0 || index >= acount(gr->edges)) {
        return;
    }
    char *before = ed_snapshot(e);
    bool moved = false;
    level_wire(s, &gr->edges[index], false, &moved);
    commit_moves(e, before, moved);
}

bool sg_spawn_node(studio *s, const char *name, float x, float y)
{
    editor *e = sg_graph_editor(s);
    const node_desc *desc = registry_node_by_name(name);
    if (!desc || sg_graph_readonly(s)) {
        return false;
    }
    char label[128];
    editor_node_label(desc, label, sizeof label);
    ed_search_match *found = ed_search_matches(e, label);
    node *n = NULL;
    for (int i = 0; i < acount(found) && !n; i++) {
        if (found[i].desc == desc && !found[i].function && !found[i].variable) {
            n = ed_spawn_match_at(e, &found[i], x, y);
        }
    }
    afree(found);
    if (n) {
        n->placed = true;
        sg_select_only(e, n);
    }
    return n != NULL;
}

/* ---------- Usage ----------
   Nodes no event reaches, and pure nodes whose values nothing used reads, draw
   dimmed: the author sees dead code before the compiler prunes it. */

bool sg_is_unused(const studio_graph *g, uint64_t id)
{
    for (int i = 0; i < acount(g->unused); i++) {
        if (g->unused[i] == id) {
            return true;
        }
    }
    return false;
}

void sg_find_unused(studio *s)
{
    studio_graph *g = &s->graph;
    graph *gr = sg_active_graph(s);
    asetlen(g->unused, 0);
    int count = acount(gr->nodes);
    bool *used = xcalloc((size_t)(count ? count : 1), sizeof *used);
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
        if (!used[i] && sg_node_visible(gr, gr->nodes[i])) {
            apush(g->unused, gr->nodes[i]->id);
        }
    }
    free(used);
}

/* ---------- Selection ---------- */

/* Grows the selection along its wires (Blender's L and Alt+L, Unreal's Select
   All Input and Output Nodes); selection never enters history. */
void sg_select_linked(studio *s, bool upstream, bool all)
{
    editor *e = sg_graph_editor(s);
    node **linked = NULL;
    graph_linked_nodes(sg_active_graph(s), e->selected, acount(e->selected), upstream, all, &linked);
    for (int i = 0; i < acount(linked); i++) {
        ed_add_selected(e, linked[i]);
    }
    afree(linked);
}

/* ---------- Refactoring ---------- */

/* Ctrl+G: the selection becomes a call to a new function whose interface is
   the wires crossing its edge (Blender, Unreal). */
void sg_collapse_to_function(studio *s)
{
    editor *e = sg_graph_editor(s);
    for (int i = 1;; i++) {
        snprintf(e->extract_name, sizeof e->extract_name, i == 1 ? "New Function" : "New Function %d", i);
        if (!behavior_asset_find_graph_by_name(e->asset, e->extract_name)) {
            break;
        }
    }
    const char *error = ed_extract_selection(e);
    if (error) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = "Can't collapse",
                               .description = error }
        );
    }
}

void sg_convert_to_expression(studio *s)
{
    editor *e = sg_graph_editor(s);
    behavior_expression_error error = { 0 };
    node **selection = NULL;
    for (int i = 0; i < acount(e->selected); i++) {
        apush(selection, e->selected[i]);
    }
    if (!ed_convert_behavior_expression_selection(e, selection, acount(selection), &error)) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = "Can't convert",
                               .description = error.message[0] ? error.message : NULL }
        );
    }
    afree(selection);
}

/* Folds reads into bindings and names values used three or more times. */
void sg_optimize(studio *s)
{
    editor *e = sg_graph_editor(s);
    if (ed_optimize_graph(e) < 0) {
        return;
    }
    studio_report(s, e->optimize_summary);
}

/* Each tree of three or more math nodes becomes one expression. */
void sg_collapse_math(studio *s)
{
    editor *e = sg_graph_editor(s);
    ed_snapshot_before(e);
    behavior_expression_error error;
    int created = behavior_expression_collapse_math(e->asset, e->active_graph, 3, &error);
    if (created < 0) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = "Can't convert",
                               .description = error.message }
        );
        return;
    }
    char message[64];
    snprintf(message, sizeof message, "%d expressions", created);
    studio_report(s, message);
    e->arrange_pending = created > 0;
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
    graph *gr = sg_active_graph(s);
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
        asetlen(s->graph.layouts, 0);
        n->y -= sg_pin_y(s, n, input ? ED_PIN_INPUT : ED_PIN_OUTPUT, p);
        if (!input) {
            n->x -= sg_layout_of(s, n)->w;
        }
        return;
    }
}

bool studio_graph_palette(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
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
            sg_select_only(e, n);
            n->placed = true;
            if (linked) {
                place_on_wire(s, n, &from);
            }
        }
        if (e->spawn_link_pending) {
            /* Nothing took the wire: a comment, or a spawn the editor refused. */
            ed_cancel_snapshot(e);
            e->spawn_link_pending = 0;
        }
        g->spawn_pending = false;
    }
    afree(items);
    afree(matches);
    return true;
}
