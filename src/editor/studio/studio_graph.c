/* The behavior graph canvas: nodes are RmlUi elements in a context of their own
 * whose dp ratio is the zoom, so text is rasterized at its displayed size and
 * clipping keeps working (a transformed element would not clip). The canvas is
 * a texture shown by a surface in the Behavior area; window input is replayed
 * into it in graph units. Interaction follows docs/editor-graph-references.md;
 * edits go through the graph editor's mutation helpers and history. */
#include "editor/studio/studio_graph_internal.h"
#include "graph/authoring/expand.h"
#include "graph/authoring/pin_edit.h"
#include "graph/document/extract.h"
#include "graph/nodes/behavior_expression.h"
#include "platform/cute.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>

/* ---------- Model access ---------- */

editor *sg_graph_editor(const studio *s)
{
    return s->scene->graph_editor;
}

bool studio_graph_active(const studio *s)
{
    const editor *e = sg_graph_editor(s);
    return e && s->scene->graph_document_id && e->active_graph;
}

graph *sg_active_graph(const studio *s)
{
    return studio_graph_active(s) ? sg_graph_editor(s)->active_graph : NULL;
}

bool sg_graph_readonly(const studio *s)
{
    const graph *g = sg_active_graph(s);
    return !g || g->math_expression ||
           !behavior_asset_mutable(sg_graph_editor(s)->asset);
}

bool sg_node_visible(const graph *g, const node *n)
{
    return n && n->desc && !graph_node_is_inline(g, n);
}

node *sg_node_by_id(const studio *s, uint64_t id)
{
    graph *gr = sg_active_graph(s);
    return gr && id ? graph_node_by_id(gr, id) : NULL;
}

graph_comment_box *sg_comment_by_id(const studio *s, uint64_t id)
{
    graph *gr = sg_active_graph(s);
    for (int i = 0; gr && id && i < acount(gr->comment_boxes); i++) {
        if (gr->comment_boxes[i].id == id) {
            return &gr->comment_boxes[i];
        }
    }
    return NULL;
}

void sg_link_ref_of(const edge *ed, ed_link_ref *out)
{
    *out = (ed_link_ref){ ed->from_id, ed->from_pin_id, ed->to_id, ed->to_pin_id };
}

bool sg_edge_ends(const graph *gr, const edge *ed, node **from, int *from_pin, node **to, int *to_pin)
{
    int from_out = 0, to_out = 0;
    *from = graph_node_by_id(gr, ed->from_id);
    *to = graph_node_by_id(gr, ed->to_id);
    return *from && *to &&
           graph_pin_by_id(*from, ed->from_pin_id, &from_out, from_pin) && from_out &&
           graph_pin_by_id(*to, ed->to_pin_id, &to_out, to_pin) && !to_out;
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

/* 1 when any data pin is watched, 0 when none is, -1 without data pins. */
static int node_watched(const node *n)
{
    int watched = -1;
    for (int p = 0; p < n->input_count + n->output_count; p++) {
        const pin_spec *spec = p < n->input_count
                                 ? &n->input_pins[p]
                                 : &n->output_pins[p - n->input_count];
        if (spec->kind == PIN_DATA) {
            watched = watched > 0 || (spec->flags & PIN_FLAG_WATCH) ? 1 : 0;
        }
    }
    return watched;
}

static bool folded_inputs(const graph *gr, node *n)
{
    node *source = NULL;
    int pin = -1;
    for (int p = 0; p < n->input_count; p++) {
        if (graph_inline_source(gr, n, p, &source, &pin)) {
            return true;
        }
    }
    return false;
}

/* Draws the sources folded into n's inputs as their own nodes; automatic
   arrangement places them clear of their neighbours. */
static void expand_inputs(editor *e, node *n)
{
    graph *gr = e->active_graph;
    node **sources = NULL;
    for (int p = 0; p < n->input_count; p++) {
        node *source = NULL;
        int pin = -1;
        if (graph_inline_source(gr, n, p, &source, &pin)) {
            apush(sources, source);
        }
    }
    for (int i = 0; i < acount(sources); i++) {
        (void)graph_set_node_presentation(gr, sources[i], NODE_PRESENTATION_EXPANDED);
        sources[i]->placed = false;
    }
    afree(sources);
    e->arrange_pending = true;
}

/* Alignment for two or more selected nodes, and straightening (Unreal). */
static bool selection_items(studio *s)
{
    static const struct
    {
        const char *key, *label;
        uint32_t icon;
        align_edge edge;
    } ALIGN[] = {
        { "top", "Align Top", KIT_ICON_ALIGN_TOP, ALIGN_TOP },
        { "middle", "Align Middle", KIT_ICON_ALIGN_MIDDLE, ALIGN_MIDDLE },
        { "bottom", "Align Bottom", KIT_ICON_ALIGN_BOTTOM, ALIGN_BOTTOM },
        { "left", "Align Left", KIT_ICON_ALIGN_LEFT, ALIGN_LEFT },
        { "center", "Align Center", KIT_ICON_ALIGN_CENTER, ALIGN_CENTER },
        { "right", "Align Right", KIT_ICON_ALIGN_RIGHT, ALIGN_RIGHT },
    };
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    bool readonly = sg_graph_readonly(s);
    int count = acount(e->selected);
    bool ran = false;
    if (count > 1 && kit_submenu_begin(k, "align", "Alignment", KIT_ICON_LAYOUT)) {
        for (int i = 0; i < (int)(sizeof ALIGN / sizeof *ALIGN) && !ran; i++) {
            if (menu_item(k, ALIGN[i].key, ALIGN[i].label, ALIGN[i].icon, NULL, readonly)) {
                sg_align(s, ALIGN[i].edge);
                ran = true;
            }
        }
        kit_menu_separator(k, "s0");
        if (!ran && menu_item(
                        k,
                        "horizontal",
                        "Distribute Horizontally",
                        KIT_ICON_DISTRIBUTE_X,
                        NULL,
                        readonly || count < 3
                    )) {
            sg_distribute(s, false);
            ran = true;
        }
        if (!ran &&
            menu_item(k, "vertical", "Distribute Vertically", KIT_ICON_DISTRIBUTE_Y, NULL, readonly || count < 3)) {
            sg_distribute(s, true);
            ran = true;
        }
        kit_submenu_end(k);
    }
    if (!ran &&
        menu_item(k, "straighten", "Straighten Connections", KIT_ICON_MOVE, "Q", readonly || !count)) {
        sg_straighten(s);
        ran = true;
    }
    return ran;
}

/* Grows the selection along its wires: one step (Blender's Select Linked From
   and To) or to the end (Unreal's Select All Input and Output Nodes). */
static bool select_items(studio *s)
{
    static const struct
    {
        const char *key, *label, *chord;
        bool upstream, all;
    } LINKED[] = {
        { "inputs", "Input Nodes", "L", true, false },
        { "outputs", "Output Nodes", "Alt+L", false, false },
        { "all-inputs", "All Input Nodes", NULL, true, true },
        { "all-outputs", "All Output Nodes", NULL, false, true },
    };
    kit *k = s->kit;
    if (!kit_submenu_begin(k, "select", "Select", KIT_ICON_SELECT)) {
        return false;
    }
    bool ran = false;
    for (int i = 0; i < (int)(sizeof LINKED / sizeof *LINKED) && !ran; i++) {
        if (i == 2) {
            kit_menu_separator(k, "s0");
        }
        const char *chord = LINKED[i].chord ? studio_chord(s, LINKED[i].chord) : NULL;
        if (menu_item(k, LINKED[i].key, LINKED[i].label, 0, chord, false)) {
            sg_select_linked(s, LINKED[i].upstream, LINKED[i].all);
            ran = true;
        }
    }
    kit_submenu_end(k);
    return ran;
}

/* An action may free the node, its graph or the whole asset, so the menu stops
   after one runs; the click closes it. */
static void node_menu(studio *s, node *n)
{
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    bool readonly = sg_graph_readonly(s);
    char title[256];
    kit_menu_group(k, "title", ed_node_title(e, n, title, sizeof title));
    const char *definition = studio_chord(s, "Alt+G");
    if (ed_is_graph_call(n) &&
        menu_item(k, "open", "Open Graph", KIT_ICON_EXTERNAL, definition, false)) {
        editor_open_graph_call(e, n->id);
        return;
    }
    if (!ed_is_graph_call(n) && studio_node_definable(n) &&
        menu_item(k, "definition", "Go to Definition", KIT_ICON_EXTERNAL, definition, false)) {
        studio_go_to_definition(s, n);
        return;
    }
    if (studio_node_referable(n) &&
        menu_item(k, "refs", "Find References", KIT_ICON_REFERENCES, studio_chord(s, "Shift+Alt+F"), false)) {
        studio_find_node_references(s, n);
        return;
    }
    if (studio_node_traceable(n) &&
        menu_item(k, "calls", "Call Hierarchy", KIT_ICON_CALLERS, studio_chord(s, "Shift+Alt+H"), false)) {
        studio_calls_node(s, n);
        return;
    }
    graph_pin_run run;
    if (graph_pin_run_of(n, &run) &&
        menu_item(k, "addpin", "Add Pin", KIT_ICON_ADD, NULL, readonly)) {
        sg_add_run_pin(s, n);
        return;
    }
    if (menu_item(k, "dup", "Duplicate", KIT_ICON_COPY, studio_chord(s, "Shift+D"), readonly)) {
        ed_duplicate_selected(e);
        return;
    }
    if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X", readonly)) {
        sg_delete_selection(s);
        return;
    }
    kit_menu_separator(k, "s1");
    int count = acount(e->selected);
    bool locked = readonly || e->ui_context == EDITOR_UI_CONTEXT_SYSTEM;
    if (menu_item(
            k,
            "collapse",
            "Collapse to Function",
            KIT_ICON_FUNCTION,
            studio_chord(s, "Mod+G"),
            locked || behavior_asset_can_extract_function(e->asset, gr, e->selected, count)
        )) {
        sg_collapse(s, GRAPH_FUNCTION);
        return;
    }
    if (menu_item(
            k,
            "macro",
            "Collapse to Macro",
            KIT_ICON_EXPRESSION,
            NULL,
            locked || behavior_asset_can_extract_macro(e->asset, gr, e->selected, count)
        )) {
        sg_collapse(s, GRAPH_MACRO);
        return;
    }
    if (ed_is_graph_call(n) && n->desc != &NODE_BehaviorExpression &&
        menu_item(
            k,
            "expand-node",
            "Expand Node",
            KIT_ICON_UNFOLD,
            NULL,
            readonly || behavior_asset_can_expand_call(e->asset, gr, n)
        )) {
        sg_expand_node(s, n);
        return;
    }
    if (sg_refreshable(s) &&
        menu_item(k, "refresh", "Refresh Nodes", KIT_ICON_REFRESH, NULL, readonly)) {
        sg_refresh_nodes(s);
        return;
    }
    const char *expression = behavior_expression_can_convert_selection(
        e->asset,
        gr,
        e->selected,
        acount(e->selected)
    );
    if (menu_item(
            k,
            "expression",
            "Convert to Expression",
            KIT_ICON_EXPRESSION,
            NULL,
            readonly || expression || e->ui_context == EDITOR_UI_CONTEXT_SYSTEM
        )) {
        sg_convert_to_expression(s);
        return;
    }
    kit_menu_separator(k, "s2");
    if (select_items(s) || selection_items(s)) {
        return;
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
        return;
    }
    if (node_behavior_for(n) != NODE_BEHAVIOR_PURE &&
        menu_item(k, "break", n->breakpoint ? "Clear Breakpoint" : "Set Breakpoint", KIT_ICON_CIRCLE, "F9", readonly)) {
        ed_snapshot_before(e);
        graph_set_breakpoint(gr, n, !n->breakpoint);
        return;
    }
    int watch = node_watched(n);
    if (watch >= 0 &&
        menu_item(k, "watch", watch ? "Stop Watching" : "Watch Values", KIT_ICON_TARGET, NULL, readonly)) {
        ed_snapshot_before(e);
        for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
            int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
            for (int p = 0; p < count; p++) {
                const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[p]
                                                            : &n->output_pins[p];
                if (spec->kind == PIN_DATA) {
                    graph_set_pin_watch(gr, n, kind == ED_PIN_OUTPUT, p, !watch);
                }
            }
        }
        return;
    }
    if (sg_node_comment_items(s, n)) {
        return;
    }
    if (ed_source_scope_available(e) && graph_node_can_inline(gr, n) &&
        menu_item(k, "inline", "Inline", KIT_ICON_EXPRESSION, NULL, readonly)) {
        ed_snapshot_before(e);
        (void)graph_inline_node(gr, n);
        ed_select_node(e, graph_inline_owner(gr, n), false);
        return;
    }
    if (folded_inputs(gr, n) &&
        menu_item(k, "expand", "Expand Inputs", KIT_ICON_MAXIMIZE, NULL, readonly)) {
        ed_snapshot_before(e);
        expand_inputs(e, n);
    }
}

/* Canvas menus live in the Studio document; emitted with the Behavior area. */
static void studio_graph_menus(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    if (g->menu_request) {
        kit_popup_open_at(k, "graph-menu", g->menu_x, g->menu_y);
        g->menu_request = false;
    }
    if (!kit_popup_begin(k, "graph-menu", "menu")) {
        return;
    }
    graph *gr = sg_active_graph(s);
    node *n = sg_node_by_id(s, g->menu_node);
    bool readonly = sg_graph_readonly(s);
    int output = 0, pin = -1;
    if (n && graph_pin_by_id(n, g->menu_pin, &output, &pin)) {
        sg_pin_menu(s, n, output ? ED_PIN_OUTPUT : ED_PIN_INPUT, pin);
    } else if (n) {
        node_menu(s, n);
    } else if (sg_comment_by_id(s, g->menu_comment)) {
        sg_comment_menu(s, sg_comment_by_id(s, g->menu_comment));
    } else if (g->menu_edge >= 0 && g->menu_edge < acount(gr->edges)) {
        edge *ed = &gr->edges[g->menu_edge];
        if (ed->reroute_count &&
            menu_item(k, "reroutes", "Clear Reroutes", KIT_ICON_RESET, NULL, readonly)) {
            ed_snapshot_before(e);
            ed->reroute_count = 0;
        } else if (menu_item(k, "straighten", "Straighten Connection", KIT_ICON_MOVE, "Q", readonly)) {
            sg_straighten_wire(s, g->menu_edge);
        }
        if (menu_item(k, "delete", "Delete Wire", KIT_ICON_TRASH, studio_chord(s, "Alt+Click"), readonly)) {
            ed_link_ref link;
            sg_link_ref_of(&gr->edges[g->menu_edge], &link);
            ed_clear_selection(e);
            ed_add_selected_link(e, &link);
            ed_delete_selected(e);
        }
    }
    kit_popup_end(k);
}

/* ---------- Host ---------- */

static float canvas_w(const studio_graph *g)
{
    return g->surface.rect.w / g->zoom;
}

static float canvas_h(const studio_graph *g)
{
    return g->surface.rect.h / g->zoom;
}

static bool ensure_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    if (g->kit) {
        return true;
    }
    float scale = cf_app_get_pixel_scale();
    g->backend = rml_backend_create(
        (int)fmaxf(g->surface.rect.w, 1),
        (int)fmaxf(g->surface.rect.h, 1),
        scale
    );
    if (!g->backend) {
        return false;
    }
    g->kit = kit_create(rml_backend_context(g->backend), "graph.rml");
    kit_theme(g->kit, s->theme.rcss);
    g->in.mouse_x = g->in.mouse_y = -1e9f;
    if (g->zoom <= 0) {
        g->zoom = 1;
    }
    return g->kit != NULL;
}

void studio_graph_frame(studio *s, double delta_seconds)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    kit_rect r = g->surface.rect;
    if (g->area < 0 || r.w < 1 || r.h < 1 || !studio_graph_active(s) ||
        !ensure_canvas(s)) {
        asetlen(g->events, 0);
        g->captured = false;
        g->area = -1;
        return;
    }
    sg_follow_graph(s);
    /* Node settings change node sizes and the wire style changes column gaps,
       so automatic nodes arrange again. */
    int forms = s->prefs.short_bindings | s->prefs.title_flow << 1 |
                s->prefs.compact_nodes << 2 | (int)s->prefs.wires << 3;
    e->arrange_pending |= forms != g->forms;
    g->forms = forms;
    float scale = cf_app_get_pixel_scale();
    rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    asetlen(g->layouts, 0);
    sg_replay(s);
    sg_pointer(s);
    sg_shortcuts(s);
    sg_nav_keys(s);
    asetlen(g->layouts, 0); /* gestures may have edited the graph */
    sg_camera_step(s);
    if (e->arrange_pending) {
        sg_arrange(s, true);
        e->arrange_pending = false;
    }
    if (!g->framed) {
        sg_frame_nodes(s, false, false);
        g->framed = true;
    }
    node *reveal = sg_node_by_id(s, g->reveal);
    if (reveal) {
        sg_show_node(s, reveal, g->reveal_frame);
    }
    g->reveal = 0;
    rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    g->in.time = s->time;
    g->in.dp_ratio = g->zoom * scale;
    g->in.zoom = g->zoom;
    g->in.width = canvas_w(g);
    g->in.height = canvas_h(g);
    kit_set_body_class(g->kit, "far", sg_far(s));
    kit_set_body_class(g->kit, "comfortable", s->prefs.comfortable);
    kit_set_body_class(g->kit, "dark", !s->light);
    kit_set_theme(g->kit, "reduced-motion", s->prefs.reduced_motion);
    sg_find_unused(s);
    ed_refresh_values(e);
    kit_begin(g->kit, &g->in);
    sg_emit_canvas(s);
    kit_end(g->kit);
    rml_backend_update(g->backend, delta_seconds);
    rml_backend_render(g->backend);
    rml_backend_set_texture(s->backend, "graph", rml_backend_texture(g->backend));
}

void studio_graph_free(studio *s)
{
    studio_graph *g = &s->graph;
    kit_destroy(g->kit);
    rml_backend_destroy(g->backend);
    arrange_routes_free(&g->routes);
    afree(g->edge_route);
    afree(g->wire_edges);
    afree(g->events);
    afree(g->views);
    afree(g->drag);
    afree(g->box_base);
    afree(g->layouts);
    afree(g->unused);
    afree(g->problems);
    afree(g->find.hits);
    afree(g->visits);
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
    if (g->gesture == STUDIO_GRAPH_RESIZE) {
        return sg_resize_cursor(g->resize_edges);
    }
    if (g->gesture == STUDIO_GRAPH_LINK || g->hover_pin >= 0) {
        return "crosshair";
    }
    return rml_backend_cursor(g->backend);
}

bool studio_graph_can_replay(studio *s, bool redo)
{
    const editor *e = sg_graph_editor(s);
    return studio_graph_active(s) &&
           (redo ? editor_can_redo(e) : editor_can_undo(e));
}

void studio_graph_history(studio *s, bool redo)
{
    editor *e = sg_graph_editor(s);
    sg_cancel_gesture(s);
    if (redo) {
        editor_redo(e);
    } else {
        editor_undo(e);
    }
}

/* ---------- Area ---------- */

/* Adds a graph under the first free name and opens it, as one undo step. */
static void create_graph(editor *e, graph_kind kind, const char *base)
{
    char name[128];
    snprintf(name, sizeof name, "%s", base);
    for (int i = 2; behavior_asset_find_graph_by_name(e->asset, name); i++) {
        snprintf(name, sizeof name, "%s %d", base, i);
    }
    editor_command create = { .kind = EDITOR_COMMAND_CREATE_GRAPH,
                              .data.create_graph = { kind, name } };
    if (editor_command_apply(e, create) == 0) {
        graph *made = behavior_asset_find_graph_by_name(e->asset, name);
        if (made) {
            editor_open_graph(e, made->graph_id);
        }
    }
}

/* The graph's name as a field, in the picker's place (Blender's F2): Enter
   or leaving the field renames, Escape keeps the name. */
static void rename_field(studio *s, graph *gr)
{
    studio_graph *g = &s->graph;
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    bool committed = kit_text_field(k, "rename", g->rename, sizeof g->rename, NULL, 0);
    if (committed && g->rename[0] && (!gr->name || strcmp(gr->name, g->rename))) {
        editor_command rename = { .kind = EDITOR_COMMAND_RENAME_GRAPH,
                                  .data.rename_graph = { gr->graph_id, g->rename } };
        if (editor_command_apply(e, rename) != 0) {
            kit_toast(k, 0, &(kit_toast_desc){ .type = KIT_TOAST_ERROR, .title = "A graph has that name" });
        }
    }
    if (g->rename_focus) {
        kit_focus(k);
        g->rename_focus = false;
    } else if (!kit_item_active(k)) {
        g->renaming = false;
    }
}

void studio_behavior_header(studio *s, int area)
{
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    if (!gr) {
        kit_label(k, "title", "dim", "No behavior open");
        return;
    }
    char key[24];
    snprintf(key, sizeof key, "graphs:%d", area);
    studio_graph *g = &s->graph;
    sg_nav_header(s);
    if (g->renaming && g->rename_graph == gr->graph_id && g->rename_area == area) {
        rename_field(s, gr);
    } else {
        kit_button_desc pick = { .label = gr->name && gr->name[0] ? gr->name : "Event Graph",
                                 .icon = gr->kind == GRAPH_FUNCTION ? KIT_ICON_FUNCTION
                                       : gr->kind == GRAPH_MACRO ? KIT_ICON_EXPRESSION
                                                                 : KIT_ICON_EVENT,
                                 .cls = "crumb current",
                                 .tip = "Graphs" };
        if (kit_button(k, "pick", &pick)) {
            kit_popup_open(k, key);
        }
    }
    kit_pop(k);
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_group(k, "group", "Graphs");
        for (int i = 0; i < acount(e->asset->graphs); i++) {
            graph *item = e->asset->graphs[i];
            /* An expression's generated function belongs to its node, which
               opens it; listing them would repeat every expression here. */
            if (item->math_expression && item != gr) {
                continue;
            }
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
        kit_menu_separator(k, "s0");
        bool locked = !behavior_asset_mutable(e->asset);
        if (menu_item(k, "function", "New Function", KIT_ICON_ADD, NULL, locked)) {
            create_graph(e, GRAPH_FUNCTION, "New Function");
        }
        if (menu_item(k, "macro", "New Macro", KIT_ICON_ADD, NULL, locked)) {
            create_graph(e, GRAPH_MACRO, "New Macro");
        }
        /* Functions and macros are named; the event graph is the behavior. */
        if (gr->kind == GRAPH_FUNCTION || gr->kind == GRAPH_MACRO) {
            kit_menu_separator(k, "s1");
            if (menu_item(k, "rename", "Rename", KIT_ICON_EDIT, NULL, locked)) {
                g->renaming = true;
                g->rename_graph = gr->graph_id;
                g->rename_area = area;
                g->rename_focus = true;
                snprintf(g->rename, sizeof g->rename, "%s", gr->name ? gr->name : "");
            }
            if (menu_item(k, "refs", "Find References", KIT_ICON_REFERENCES, NULL, false)) {
                studio_find_graph_references(s, gr);
            }
            if (menu_item(k, "calls", "Call Hierarchy", KIT_ICON_CALLERS, NULL, false)) {
                studio_calls_graph(s, gr);
            }
            const char *remove = gr->kind == GRAPH_MACRO ? "Delete Macro"
                                                         : "Delete Function";
            if (menu_item(k, "delete", remove, KIT_ICON_TRASH, NULL, locked)) {
                editor_delete_graph(e, gr->graph_id);
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
        kit_tip(k, "Wires show this instance running", NULL, NULL);
    } else if (scene->debug_status == APP_DEBUG_STALE) {
        kit_label(k, "debug", "badge debug-target", "Edited");
        kit_tip(k, "The running behavior differs from this graph; save to update it", NULL, NULL);
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
    bool readonly = sg_graph_readonly(s) || gr->math_expression;
    kit_button_desc more = { .icon = KIT_ICON_MORE, .tip = "Graph" };
    if (kit_button(k, "more", &more)) {
        kit_popup_open(k, "graph-actions");
    }
    if (kit_popup_begin(k, "graph-actions", "menu")) {
        if (menu_item(k, "optimize", "Optimize", KIT_ICON_EXPRESSION, NULL, readonly)) {
            sg_optimize(s);
        }
        if (menu_item(k, "collapse", "Math to Expressions", KIT_ICON_EXPRESSION, NULL, readonly)) {
            sg_collapse_math(s);
        }
        kit_menu_separator(k, "s0");
        selection_items(s);
        kit_popup_end(k);
    }
    kit_button_desc tidy = { .icon = KIT_ICON_LAYOUT,
                             .tip = "Arrange",
                             .shortcut = studio_chord(s, "Shift+L"),
                             .flags = sg_graph_readonly(s) ? KIT_DISABLED : 0 };
    if (kit_button(k, "arrange", &tidy)) {
        sg_arrange(s, false);
        sg_frame_nodes(s, false, true);
    }
    kit_button_desc find = { .icon = KIT_ICON_SEARCH,
                             .tip = "Find",
                             .shortcut = studio_chord(s, "Mod+F"),
                             .flags = g->find.open ? KIT_ON : 0 };
    if (kit_button(k, "find", &find)) {
        sg_find_open(s);
    }
    kit_button_desc fit = { .icon = KIT_ICON_FRAME, .tip = "Frame all", .shortcut = "Home" };
    if (kit_button(k, "frame", &fit)) {
        sg_frame_nodes(s, false, true);
    }
    char view[24];
    snprintf(view, sizeof view, "graph-view:%d", area);
    kit_button_desc show = { .icon = KIT_ICON_OVERLAY, .tip = "View" };
    if (kit_button(k, "view", &show)) {
        kit_popup_open(k, view);
    }
    if (kit_popup_begin(k, view, "menu")) {
        studio_ui_panel(s, "panel", "GraphView");
        kit_popup_end(k);
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
    studio_members_drop(s);
    studio_debug_call_stack(s);
    sg_find_panel(s);
    studio_graph_menus(s);
    studio_definition_peek(s);
}
