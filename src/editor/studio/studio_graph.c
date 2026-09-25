/* The behavior graph canvas: nodes are RmlUi elements in a context of their own
 * whose dp ratio is the zoom, so text is rasterized at its displayed size and
 * clipping keeps working (a transformed element would not clip). The canvas is
 * a texture shown by a surface in the Behavior area; window input is replayed
 * into it in graph units. Interaction follows docs/editor-graph-references.md;
 * edits go through the graph editor's mutation helpers and history. */
#include "editor/studio/studio_graph_internal.h"
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

void studio_graph_menus(studio *s)
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
    if (n) {
        char title[256];
        kit_menu_group(k, "title", ed_node_title(e, n, title, sizeof title));
        if (ed_is_graph_call(n) &&
            menu_item(k, "open", "Open Graph", KIT_ICON_EXTERNAL, NULL, false)) {
            editor_open_graph_call(e, n->id);
        }
        if (menu_item(k, "dup", "Duplicate", KIT_ICON_COPY, studio_chord(s, "Shift+D"), readonly)) {
            ed_duplicate_selected(e);
        }
        if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X", readonly)) {
            sg_delete_selection(s);
        }
        kit_menu_separator(k, "s1");
        const char *extract = behavior_asset_can_extract_function(
            e->asset,
            gr,
            e->selected,
            acount(e->selected)
        );
        if (menu_item(
                k,
                "collapse",
                "Collapse to Function",
                KIT_ICON_FUNCTION,
                studio_chord(s, "Mod+G"),
                readonly || extract || e->ui_context == EDITOR_UI_CONTEXT_SYSTEM
            )) {
            sg_collapse_to_function(s);
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
        int watch = node_watched(n);
        if (watch >= 0 &&
            menu_item(k, "watch", watch ? "Stop Watching" : "Watch Values", KIT_ICON_TARGET, NULL, readonly)) {
            ed_snapshot_before(e);
            for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
                int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
                for (int p = 0; p < count; p++) {
                    const pin_spec *spec = kind == ED_PIN_INPUT
                                             ? &n->input_pins[p]
                                             : &n->output_pins[p];
                    if (spec->kind == PIN_DATA) {
                        graph_set_pin_watch(gr, n, kind == ED_PIN_OUTPUT, p, !watch);
                    }
                }
            }
        }
        if (ed_source_scope_available(e) && graph_node_can_inline(gr, n) &&
            menu_item(k, "inline", "Inline", KIT_ICON_EXPRESSION, NULL, readonly)) {
            ed_snapshot_before(e);
            (void)graph_inline_node(gr, n);
            ed_select_node(e, graph_inline_owner(gr, n), false);
        }
        if (folded_inputs(gr, n) &&
            menu_item(k, "expand", "Expand Inputs", KIT_ICON_MAXIMIZE, NULL, readonly)) {
            ed_snapshot_before(e);
            expand_inputs(e, n);
        }
    } else if (sg_comment_by_id(s, g->menu_comment)) {
        if (menu_item(k, "delete", "Delete Comment", KIT_ICON_TRASH, NULL, readonly)) {
            ed_snapshot_before(e);
            graph_remove_comment_box(gr, g->menu_comment);
        }
    } else if (g->menu_edge >= 0 && g->menu_edge < acount(gr->edges)) {
        edge *ed = &gr->edges[g->menu_edge];
        if (ed->reroute_count &&
            menu_item(k, "reroutes", "Clear Reroutes", KIT_ICON_RESET, NULL, readonly)) {
            ed_snapshot_before(e);
            ed->reroute_count = 0;
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
    editor *e = sg_graph_editor(s);
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
    if (!sg_follow_graph(s)) {
        e->arrange_pending = false;
    }
    /* Node settings change node sizes and the wire style changes column gaps,
       so automatic nodes arrange again. */
    int forms = s->prefs.short_bindings | s->prefs.title_flow << 1 |
                s->prefs.compact_nodes << 2 | (int)s->prefs.wires << 3;
    e->arrange_pending |= forms != g->forms;
    g->forms = forms;
    float scale = cf_app_get_pixel_scale();
    editor_rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    asetlen(g->layouts, 0);
    sg_replay(s);
    sg_pointer(s);
    sg_shortcuts(s);
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
    kit_set_body_class(g->kit, "far", sg_far(s));
    kit_set_body_class(g->kit, "comfortable", s->prefs.comfortable);
    kit_set_body_class(g->kit, "light", s->light);
    kit_set_theme(g->kit, "reduced-motion", s->prefs.reduced_motion);
    sg_find_unused(s);
    ed_refresh_values(e);
    kit_begin(g->kit, &g->in);
    sg_emit_canvas(s);
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
    afree(g->layouts);
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
    const editor *e = sg_graph_editor(s);
    return studio_graph_active(s) &&
           (redo ? editor_can_redo(e) : editor_can_undo(e));
}

void studio_graph_history(studio *s, bool redo)
{
    editor *e = sg_graph_editor(s);
    s->graph.gesture = STUDIO_GRAPH_IDLE;
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
    studio_graph *g = &s->graph;
    if (g->renaming && g->rename_graph == gr->graph_id) {
        rename_field(s, gr);
    } else {
        kit_button_desc pick = { .label = label,
                                 .icon = KIT_ICON_GRAPH,
                                 .cls = "graph-pick",
                                 .tip = "Graphs in this behavior" };
        if (kit_button(k, "pick", &pick)) {
            kit_popup_open(k, key);
        }
    }
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
                g->rename_focus = true;
                snprintf(g->rename, sizeof g->rename, "%s", gr->name ? gr->name : "");
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
                             .shortcut = studio_chord(s, "Shift+L"),
                             .flags = sg_graph_readonly(s) ? KIT_DISABLED : 0 };
    if (kit_button(k, "arrange", &tidy)) {
        sg_arrange(s, false);
        sg_frame_nodes(s, false, true);
    }
    kit_button_desc fit = { .icon = KIT_ICON_FRAME, .tip = "Frame all", .shortcut = "Home" };
    if (kit_button(k, "frame", &fit)) {
        sg_frame_nodes(s, false, true);
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
