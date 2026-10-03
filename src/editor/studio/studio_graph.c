/* The behavior graph canvas: nodes are RmlUi elements in a context of their own
 * whose dp ratio includes graph zoom and standard-font scale, so text is
 * rasterized at its displayed size and clipping keeps working (a transformed
 * element would not clip). The canvas is a texture shown by a surface in the
 * Behavior area; window input is replayed into it in graph units. Interaction
 * follows docs/editor-graph-references.md; edits go through the graph editor's
 * mutation helpers and history. */
#include "diagnostics/tracy.h"
#include "editor/studio/studio_graph_internal.h"
#include "graph/authoring/expand.h"
#include "graph/authoring/pin_edit.h"
#include "graph/document/extract.h"
#include "graph/nodes/behavior_expression.h"
#include "platform/cute.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Invalidation follows authored model revisions, never the camera. Pending
 * drafts have not received an edit revision yet, so frame boundaries explicitly
 * invalidate them before geometry is consumed. */
void sg_calculations_invalidate(studio *s)
{
    studio_graph *g = &s->graph;
    asetlen(g->layouts, 0);
    if (g->calculations) {
        cf_map_clear(g->calculations->layout_index);
        g->calculations->routes_dirty = true;
        g->calculations->unused_dirty = true;
    }
}

void sg_calculations_sync(const studio *s)
{
    studio_graph *g = (studio_graph *)&s->graph;
    if (!g->calculations) {
        g->calculations = xcalloc(1, sizeof *g->calculations);
    }
    struct studio_graph_calculations *c = g->calculations;
    graph *gr = sg_active_graph(s);
    editor *e = sg_graph_editor(s);
    int forms = s->prefs.short_bindings | s->prefs.title_flow << 1 |
                s->prefs.compact_nodes << 2;
    unsigned edit = editor_edit_revision(e), assets = s->scene->asset_index_generation;
    unsigned fonts = rml_font_generation();
    float ratio = studio_pixel_ratio(s);
    if (c->lifetime != gr->lifetime_id || c->topology != graph_topology_revision(gr) ||
        c->edit != edit || c->asset != e->asset || c->assets != assets ||
        c->fonts != fonts || c->forms != forms ||
        c->choices != e->choices_resolver || c->choices_user != e->choices_user ||
        c->standard_family != s->prefs.standard_font_family ||
        c->code_family != s->prefs.code_font_family ||
        c->standard_size != s->prefs.standard_font_size ||
        c->code_size != s->prefs.code_font_size || c->pixel_ratio != ratio ||
        c->comfortable != s->prefs.comfortable) {
        sg_calculations_invalidate((studio *)s);
        c->lifetime = gr->lifetime_id;
        c->topology = graph_topology_revision(gr);
        c->edit = edit;
        c->assets = assets;
        c->fonts = fonts;
        c->asset = e->asset;
        c->choices = e->choices_resolver;
        c->choices_user = e->choices_user;
        c->forms = forms;
        c->standard_family = s->prefs.standard_font_family;
        c->code_family = s->prefs.code_font_family;
        c->standard_size = s->prefs.standard_font_size;
        c->code_size = s->prefs.code_font_size;
        c->pixel_ratio = ratio;
        c->comfortable = s->prefs.comfortable;
    }
}

/* A referenced local definition can change without changing the caller's
 * topology. Compare its version, once per frame, rather than its nodes/strings. */
static void calculations_definitions(studio *s)
{
    struct studio_graph_calculations *c = s->graph.calculations;
    const behavior_asset *asset = sg_graph_editor(s)->asset;
    bool changed = acount(c->definitions) != acount(asset->graphs) * 2;
    if (!changed) {
        for (int i = 0; i < acount(asset->graphs); i++) {
            const graph *gr = asset->graphs[i];
            changed |= c->definitions[i * 2] != gr->lifetime_id ||
                       c->definitions[i * 2 + 1] != graph_topology_revision(gr);
        }
    }
    if (!changed) {
        return;
    }
    sg_calculations_invalidate(s);
    asetlen(c->definitions, 0);
    for (int i = 0; i < acount(asset->graphs); i++) {
        apush(c->definitions, asset->graphs[i]->lifetime_id);
        apush(c->definitions, graph_topology_revision(asset->graphs[i]));
    }
}

void sg_calculations_free(studio *s)
{
    struct studio_graph_calculations *c = s->graph.calculations;
    if (!c) {
        return;
    }
    cf_map_free(c->layout_index);
    afree(c->definitions);
    afree(c->routing.nodes);
    afree(c->routing.boxes);
    afree(c->routing.pos);
    afree(c->routing.links);
    afree(c->routing.edge_link);
    free(c);
    s->graph.calculations = NULL;
}

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
    if (studio_event_renameable(n) &&
        menu_item(k, "rename-event", "Rename Event", KIT_ICON_EDIT, "F2", readonly)) {
        studio_refactor_rename_event(s, studio_event_renameable(n));
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

static float canvas_w(const studio *s)
{
    return s->graph.surface.rect.w / sg_zoom(s);
}

static float canvas_h(const studio *s)
{
    return s->graph.surface.rect.h / sg_zoom(s);
}

static bool ensure_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    if (g->kit) {
        return true;
    }
    float scale = studio_pixel_ratio(s);
    g->backend = rml_backend_create(
        (int)fmaxf(g->surface.rect.w, 1),
        (int)fmaxf(g->surface.rect.h, 1),
        scale
    );
    if (!g->backend) {
        return false;
    }
    g->kit = kit_create(rml_backend_context(g->backend), "graph.rml");
    kit_theme(g->kit, s->page.document.rcss ? s->page.document.rcss : "");
    studio_apply_graph_font_settings(g->kit, &s->prefs);
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
    bool shown = g->area >= 0 && r.w >= 1 && r.h >= 1 && studio_graph_active(s);
    /* Nothing traces the debug target while no graph shows it. */
    app_scene_authoring_set_debug_hidden(s->scene, !shown);
    if (!shown || !ensure_canvas(s)) {
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
    float scale = studio_pixel_ratio(s);
    float graph_ratio = studio_graph_pixel_ratio(s);
    rml_backend_resize_pixels(g->backend, (int)lroundf(r.w * scale), (int)lroundf(r.h * scale), graph_ratio);
    sg_calculations_sync(s);
    calculations_definitions(s);
    if (editor_has_pending_edits(e)) {
        sg_calculations_invalidate(s);
    }
    sg_replay(s);
    sg_pointer(s);
    sg_shortcuts(s);
    sg_nav_keys(s);
    sg_calculations_sync(s);
    calculations_definitions(s);
    if (editor_has_pending_edits(e)) {
        sg_calculations_invalidate(s);
    } /* gestures and field drafts may have edited the graph */
    sg_camera_step(s);
    if (e->arrange_pending) {
        sg_arrange(s, true);
        e->arrange_pending = false;
    }
    if (!g->framed) {
        sg_frame_open(s);
        g->framed = true;
    }
    node *reveal = sg_node_by_id(s, g->reveal);
    if (reveal) {
        sg_show_node(s, reveal, g->reveal_frame);
    }
    g->reveal = 0;
    graph_ratio = studio_graph_pixel_ratio(s);
    rml_backend_resize_pixels(g->backend, (int)lroundf(r.w * scale), (int)lroundf(r.h * scale), graph_ratio);
    g->in.time = s->time;
    g->in.dp_ratio = graph_ratio;
    g->in.zoom = sg_zoom(s);
    g->in.width = canvas_w(s);
    g->in.height = canvas_h(s);
    kit_set_body_class(g->kit, "far", sg_far(s));
    kit_set_body_class(g->kit, "comfortable", s->prefs.comfortable);
    bool light = studio_theme_is_light(s->prefs.theme, s->system_light);
    bool theme_changed = g->theme_initialized &&
                         (g->theme != s->prefs.theme || g->theme_light != light);
    kit_set_body_class(g->kit, "theme-switching", theme_changed);
    if (!g->theme_initialized || theme_changed) {
        studio_theme_apply(g->kit, s->prefs.theme, s->system_light);
        g->theme = s->prefs.theme;
        g->theme_light = light;
        g->theme_initialized = true;
    }
    kit_set_theme(g->kit, "reduced-motion", s->prefs.reduced_motion);
    sg_find_unused(s);
    CHIBI_TRACE_ZONE_BEGIN(values_zone, "Studio graph values");
    ed_refresh_values(e);
    CHIBI_TRACE_ZONE_END(values_zone);
    CHIBI_TRACE_ZONE_BEGIN(emit_zone, "Studio graph emit");
    kit_begin(g->kit, &g->in);
    sg_emit_canvas(s);
    kit_end(g->kit);
    CHIBI_TRACE_ZONE_END(emit_zone);
    CHIBI_TRACE_ZONE_BEGIN(update_zone, "Studio graph update");
    rml_backend_update(g->backend, delta_seconds);
    CHIBI_TRACE_ZONE_END(update_zone);
    CHIBI_TRACE_ZONE_BEGIN(render_zone, "Studio graph render");
    rml_backend_render(g->backend);
    CHIBI_TRACE_ZONE_END(render_zone);
    rml_backend_set_texture_revision(
        s->backend,
        "graph",
        rml_backend_texture(g->backend),
        rml_backend_texture_revision(g->backend)
    );
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
    sg_calculations_free(s);
    afree(g->layouts);
    afree(g->unused);
    afree(g->problems);
    afree(g->find.hits);
    afree(g->visits);
    afree(g->shown);
    afree(g->live.values);
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
    if (committed && g->rename[0] && (!gr->name || strcmp(gr->name, g->rename)) &&
        !studio_members_rename_across(s, STUDIO_MEMBER_GRAPH, gr->graph_id, gr->name, g->rename)) {
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

static const char *actor_name(const app_scene_authoring *scene, ecs_entity entity)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
            return scene->actors[i].name;
        }
    }
    return NULL;
}

/* An instance as the debug target menu names it: its object's name, or its
   prototype's and its entity's slot when the game spawned it. A destroyed
   object's prototype is the one the target kept. */
static void instance_label(
    app_scene_authoring *scene,
    ecs_entity entity,
    const char *prototype,
    char *buf,
    size_t size
)
{
    const char *name = actor_name(scene, entity);
    if (name) {
        snprintf(buf, size, "%s", name);
        return;
    }
    snprintf(
        buf,
        size,
        "%s %u",
        prototype ? prototype : runtime_entity_label(scene, entity),
        (unsigned)entity.slot
    );
}

typedef enum
{
    CHOICE_NONE,
    CHOICE_INSTANCE,
    CHOICE_NEWEST,      /* Follow newest */
    CHOICE_SPAWN_BREAK, /* Break when spawned */
} choice_kind;

typedef struct
{
    choice_kind kind;
    ecs_entity entity;
    const char *prototype;
    char label[96];
} debug_choice;

static bool prototype_attaches(const game_prototype *prototype, const char *behavior)
{
    for (int i = 0; i < acount(prototype->graphs); i++) {
        if (!strcmp(prototype->graphs[i], behavior)) {
            return true;
        }
    }
    return false;
}

/* Per prototype that attaches the open behavior: Follow newest, which
   follows each one the game spawns (Unreal's debug filter by class), and
   Break when spawned. The scene's game only; a preview runs one object. */
static void prototype_choices(studio *s, debug_choice **choices)
{
    app_scene_authoring *scene = s->scene;
    if (scene->active_runtime_target != APP_RUNTIME_SCENE) {
        return;
    }
    for (choice_kind kind = CHOICE_NEWEST; kind <= CHOICE_SPAWN_BREAK; kind++) {
        for (int i = 0; i < acount(scene->prototypes); i++) {
            const game_prototype *prototype = scene->prototypes[i].prototype;
            if (!prototype_attaches(prototype, scene->graph_document_id)) {
                continue;
            }
            debug_choice choice = { .kind = kind, .prototype = prototype->name };
            snprintf(choice.label, sizeof choice.label, "%s", prototype->name);
            apush(*choices, choice);
        }
    }
}

static kit_command_item choice_item(studio *s, const debug_choice *choice, bool targeted)
{
    app_scene_authoring *scene = s->scene;
    const char *newest = scene->debug_newest;
    switch (choice->kind) {
        case CHOICE_NONE:
            return (kit_command_item){ .label = choice->label,
                                       .icon = KIT_ICON_CLOSE,
                                       .current = !targeted };
        case CHOICE_INSTANCE:
            return (kit_command_item){
                .label = choice->label,
                .group = actor_name(scene, choice->entity) ? "Scene" : "Spawned",
                .icon = KIT_ICON_TARGET,
                .current = targeted && !newest &&
                           ecs_entity_to_u64(choice->entity) ==
                               ecs_entity_to_u64(scene->debug.entity),
            };
        case CHOICE_NEWEST:
            return (kit_command_item){
                .label = choice->label,
                .group = "Follow newest",
                .keywords = "newest latest last spawned follow",
                .icon = studio_prototype_icon(s, choice->prototype),
                .current = newest && !strcmp(newest, choice->prototype),
            };
        case CHOICE_SPAWN_BREAK: {
            bool on = app_scene_authoring_spawn_break(scene, choice->prototype);
            return (kit_command_item){
                .label = choice->label,
                .group = "Break when spawned",
                .keywords = "break pause spawn breakpoint",
                .icon = on ? KIT_ICON_DOT : KIT_ICON_CIRCLE,
                .current = on,
            };
        }
    }
    return (kit_command_item){ 0 };
}

static void choose(studio *s, const debug_choice *choice)
{
    switch (choice->kind) {
        case CHOICE_NONE:
        case CHOICE_INSTANCE:
            app_scene_authoring_choose_debug_target(s->scene, choice->entity);
            break;
        case CHOICE_NEWEST:
            app_scene_authoring_follow_newest(s->scene, choice->prototype);
            break;
        case CHOICE_SPAWN_BREAK:
            studio_toggle_spawn_break(s, choice->prototype);
            break;
    }
}

/* The running instances of the open behavior to debug, searchable, after
   No target (Unreal's debug object menu), then Follow and Break when
   spawned per prototype. */
static void debug_target_menu(studio *s, const char *key)
{
    app_scene_authoring *scene = s->scene;
    game_behavior_world *behaviors = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                                       ? game_prototype_preview_behaviors(scene->prototype_preview)
                                       : scene->live->behaviors;
    bool targeted = scene->debug_status == APP_DEBUG_AVAILABLE ||
                    scene->debug_status == APP_DEBUG_STALE;
    debug_choice *choices = NULL;
    apush(choices, ((debug_choice){ .kind = CHOICE_NONE, .label = "No target" }));
    prototype_choices(s, &choices);
    for (int i = 0; i < game_behavior_attachment_count(behaviors); i++) {
        game_behavior_attachment_info info = game_behavior_attachment_info_index(behaviors, i);
        if (!strcmp(info.asset_name, scene->graph_document_id)) {
            debug_choice choice = { .kind = CHOICE_INSTANCE, .entity = info.entity };
            instance_label(scene, info.entity, NULL, choice.label, sizeof choice.label);
            apush(choices, choice);
        }
    }
    kit_command_item *items = NULL;
    for (int i = 0; i < acount(choices); i++) {
        apush(items, choice_item(s, &choices[i], targeted));
    }
    int chosen = kit_dropdown(s->kit, key, items, acount(items));
    if (chosen >= 0) {
        choose(s, &choices[chosen]);
    }
    afree(items);
    afree(choices);
}

/* What the target button adds to the instance's name: a destroyed object's
   frozen data, a stale definition, or Follow newest. */
static const char *target_state(const app_scene_authoring *scene)
{
    if (scene->debug_destroyed) {
        return " \xc2\xb7 Destroyed";
    }
    if (scene->debug_status == APP_DEBUG_STALE) {
        return " \xc2\xb7 Edited";
    }
    return scene->debug_newest ? " \xc2\xb7 Newest" : "";
}

static const char *target_description(const app_scene_authoring *scene)
{
    if (scene->debug_destroyed) {
        return "Its object was destroyed. The graph shows what it last did "
               "until "
               "another target is chosen.";
    }
    if (scene->debug_status == APP_DEBUG_STALE) {
        return "It runs the saved behavior. Save to run this graph.";
    }
    return scene->debug_newest
             ? "Follows the newest one the game spawned; choose an instance to "
               "stay on one."
             : "The running instance whose wires, nodes and values this graph "
               "shows. Selecting an object shows its instance.";
}

/* While the game runs, which instance the graph shows (Unreal's debug
   object): the selected object's, chosen here, the newest of a prototype,
   or none. */
static void debug_target(studio *s, int area)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    if (!studio_session(s) || scene->graph_document_kind != APP_EDITOR_DOCUMENT_BEHAVIOR) {
        return;
    }
    char name[96], label[128] = "No target", key[32];
    if (scene->debug_status == APP_DEBUG_AVAILABLE ||
        scene->debug_status == APP_DEBUG_STALE) {
        instance_label(scene, scene->debug.entity, scene->debug.prototype, name, sizeof name);
        snprintf(label, sizeof label, "%s%s", name, target_state(scene));
    } else if (scene->debug_newest) {
        /* Waiting for the first to spawn. */
        snprintf(label, sizeof label, "Newest %s", scene->debug_newest);
    }
    kit_button_desc d = {
        .label = label,
        .icon = KIT_ICON_TARGET,
        .variant = KIT_SUBTLE,
        .size = KIT_SM,
        .cls = "debug-target",
        .morph = true,
        .tip = "Debug target",
        .description = target_description(scene),
    };
    snprintf(key, sizeof key, "debug-target:%d", area);
    if (kit_button(k, "debug", &d)) {
        kit_dropdown_open(k, key, "Search instances");
    }
    if (kit_dropdown_is_open(k, key)) {
        debug_target_menu(s, key);
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
    debug_target(s, area);
    if (s->graph.kit && s->graph.surface.rect.w > 0) {
        kit_number_flow(
            k,
            "zoom",
            "readout",
            roundf(sg_zoom(s) * 100),
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
    kit_button_desc find = {
        .icon = KIT_ICON_SEARCH,
        .tip = "Find",
        .shortcut = studio_command_describe(s, STUDIO_COMMAND_FIND).shortcut,
        .flags = g->find.open ? KIT_ON : 0
    };
    if (kit_button(k, "find", &find)) {
        studio_graph_find(s);
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

static int visible_nodes(graph *gr)
{
    int count = 0;
    for (int i = 0; i < acount(gr->nodes); i++) {
        count += sg_node_visible(gr, gr->nodes[i]);
    }
    return count;
}

/* With no behavior open, the area offers the selected object's behaviors,
   then a new one or search: an empty editor says how to fill it. */
static void behavior_empty(studio *s)
{
    kit *k = s->kit;
    kit_push(k, "empty", NULL, "pending-editor behavior-empty");
    kit_icon(k, "icon", "glyph", KIT_ICON_GRAPH);
    kit_label(k, "title", "title", "No behavior open");
    scene_actor *actor = selected_actor(s->scene);
    char **names = actor ? instance_behavior_names(s->scene, actor->entity) : NULL;
    char heading[160] = "Select an object to see its behaviors";
    if (actor) {
        snprintf(
            heading,
            sizeof heading,
            acount(names) ? "Behaviors on %s" : "%s has no behaviors",
            actor->name
        );
    }
    kit_label(k, "on", "dim", heading);
    kit_push(k, "choices", NULL, "behavior-choices");
    for (int i = 0; i < acount(names); i++) {
        kit_button_desc open = { .label = names[i],
                                 .icon = KIT_ICON_GRAPH,
                                 .variant = KIT_SUBTLE };
        if (kit_button(k, names[i], &open)) {
            app_scene_authoring_open_behavior(s->scene, names[i]);
        }
    }
    kit_pop(k);
    free_behavior_names(names);
    kit_push(k, "actions", NULL, "behavior-choices");
    kit_button_desc create = { .label = "New Behavior", .icon = KIT_ICON_ADD };
    if (kit_button(k, "new", &create)) {
        studio_command_run(s, STUDIO_COMMAND_NEW_BEHAVIOR);
    }
    kit_button_desc search = { .label = "Search",
                               .icon = KIT_ICON_SEARCH,
                               .shortcut = studio_chord(s, "Mod+K") };
    if (kit_button(k, "search", &search)) {
        studio_command_run(s, STUDIO_COMMAND_SEARCH);
    }
    kit_pop(k);
    kit_pop(k);
}

void studio_behavior_body(studio *s, int area)
{
    kit *k = s->kit;
    studio_graph *g = &s->graph;
    if (!studio_graph_active(s)) {
        behavior_empty(s);
        g->area = -1;
        return;
    }
    g->area = area;
    kit_surface(k, "graph", "graph-surface", "graph", &g->surface);
    /* An empty graph says how to begin, where a node will go. */
    if (!visible_nodes(sg_active_graph(s))) {
        char hint[96];
        snprintf(hint, sizeof hint, "Add a node with %s or a right click", studio_chord(s, "Shift+A"));
        kit_label(k, "empty", "graph-empty", hint);
    }
    studio_members_drop(s);
    studio_debug_call_stack(s);
    sg_find_panel(s);
    studio_graph_menus(s);
    studio_definition_peek(s);
}
