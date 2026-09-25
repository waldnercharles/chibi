/* The Node tab in Properties: the selected graph node's pins and settings. */
#include "editor/studio/studio_graph_internal.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"

#include <stdio.h>

uint64_t studio_graph_node(const studio *s)
{
    const editor *e = sg_graph_editor(s);
    return studio_graph_active(s) && acount(e->selected) > 0 ? e->selected[0]->id : 0;
}

/* The Node tab: every pin, advanced ones included, with what feeds it; the
   canvas shows the common case, this shows everything (Unreal's Details). */
static bool used(const graph *gr, const node *n, int p)
{
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *e = &gr->edges[i];
        if (e->from_id == n->id && e->from_pin_id == n->output_pins[p].id) {
            return true;
        }
    }
    return false;
}

static void sync_value_names(studio_graph *g, const graph *gr, const node *n)
{
    g->names_node = n->id;
    g->names_revision = graph_topology_revision(gr);
    for (int p = 0; p < STUDIO_VALUE_NAMES; p++) {
        const graph_local *local = p < n->output_count
                                     ? graph_output_local(gr, n, p)
                                     : NULL;
        snprintf(g->names[p], sizeof g->names[p], "%s", local ? local->name : "");
    }
}

/* Names, renames or, with an empty name, unnames a value. */
static void name_value(studio *s, node *n, int p, const char *name)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    const graph_local *local = graph_output_local(gr, n, p);
    if (!local && !name[0]) {
        return;
    }
    if (local && !strcmp(local->name, name)) {
        return;
    }
    ed_snapshot_before(e);
    bool ok = !name[0] ? graph_expand_local(gr, local->id) == 0
            : local    ? graph_rename_local(gr, local->id, name) == 0
                       : graph_create_local(gr, n, p, name) != 0;
    if (!ok) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = local ? "A value has that name" : "Can't name this value" }
        );
    }
    sync_value_names(&s->graph, gr, n);
}

void studio_node_properties(studio *s)
{
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    node *n = sg_node_by_id(s, studio_graph_node(s));
    if (!n) {
        kit_label(k, "empty", "prop-empty", "No node selected");
        return;
    }
    char title[256], label[160], text[256];
    bool readonly = sg_graph_readonly(s);
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, sg_node_icon(s, n) ? sg_node_icon(s, n) : KIT_ICON_HEXAGON);
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    kit_label(k, "name", "title", ed_node_title(e, n, title, sizeof title));
    kit_push(k, "lineage", NULL, "lineage");
    kit_label(k, "of", NULL, n->desc->category ? n->desc->category : n->desc->name);
    kit_pop(k);
    kit_pop(k);
    kit_pop(k);
    const char *source = n->desc == &NODE_BehaviorExpression
                           ? behavior_expression_source(e->asset, n)
                           : NULL;
    if (source) {
        /* The expression is the node: edit it here, see errors in place. */
        if (kit_panel_begin(k, "expression", "Expression", NULL, true)) {
            studio_graph *g = &s->graph;
            if (g->expression_node != n->id) {
                snprintf(g->expression, sizeof g->expression, "%s", source);
                g->expression_node = n->id;
                g->expression_error[0] = '\0';
            }
            if (kit_text_field(k, "source", g->expression, sizeof g->expression, "a + b", readonly ? KIT_READONLY : 0)) {
                behavior_expression_error error = { 0 };
                if (editor_set_behavior_expression(e, n, g->expression, &error) != 0) {
                    snprintf(
                        g->expression_error,
                        sizeof g->expression_error,
                        "%s",
                        error.message[0] ? error.message : "Invalid expression"
                    );
                } else {
                    g->expression_error[0] = '\0';
                }
            }
            if (g->expression_error[0]) {
                kit_label(k, "error", "notice", g->expression_error);
            }
        }
        kit_panel_end(k);
    }
    if (n->input_count && kit_panel_begin(k, "inputs", "Inputs", NULL, true)) {
        for (int p = 0; p < n->input_count; p++) {
            const pin_spec *spec = &n->input_pins[p];
            if (spec->flags & PIN_FLAG_SETTING && spec->kind != PIN_DATA) {
                continue;
            }
            char key[16];
            snprintf(key, sizeof key, "i%d", p);
            ed_pin_label(n, spec, label, sizeof label);
            kit_prop_begin(k, key, label);
            node *src = NULL;
            int src_pin = -1;
            if (spec->kind == PIN_EXEC) {
                kit_label(k, "v", "dim", "Execution");
            } else if (graph_input_source(gr, n, p, &src, &src_pin)) {
                char source[160];
                ed_pin_label(src, &src->output_pins[src_pin], source, sizeof source);
                snprintf(
                    text,
                    sizeof text,
                    "%s \xe2\x80\xba %s",
                    ed_node_title(e, src, title, sizeof title),
                    source
                );
                kit_label(k, "v", "dim", text);
            } else if (spec->binding) {
                kit_label(k, "v", "chip", ed_binding_label(e, spec->binding, text, sizeof text));
            } else if (!readonly && sg_value_kind_of(s, n, 0, p) != VALUE_NONE) {
                sg_emit_value_in(s, k, n, 0, p);
            } else {
                kit_label(k, "v", "dim", ed_pin_type_name(spec));
            }
            kit_prop_end(k);
        }
    }
    if (n->input_count) {
        kit_panel_end(k);
    }
    if (n->output_count && kit_panel_begin(k, "outputs", "Outputs", NULL, true)) {
        studio_graph *g = &s->graph;
        if (g->names_node != n->id ||
            g->names_revision != graph_topology_revision(gr)) {
            sync_value_names(g, gr, n);
        }
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *spec = &n->output_pins[p];
            char key[16];
            snprintf(key, sizeof key, "o%d", p);
            ed_pin_label(n, spec, label, sizeof label);
            kit_prop_begin(k, key, label);
            if (ed_pin_editable(gr, n, ED_PIN_OUTPUT, p) && !readonly &&
                sg_value_kind_of(s, n, 1, p) != VALUE_NONE) {
                sg_emit_value_in(s, k, n, 1, p);
            } else if (p < STUDIO_VALUE_NAMES && spec->kind == PIN_DATA && used(gr, n, p)) {
                /* A named value (Unreal's named reroute): uses read the name
                   instead of drawing a wire; clearing it brings wires back. */
                if (kit_text_field(
                        k,
                        "name",
                        g->names[p],
                        sizeof g->names[p],
                        ed_pin_type_name(spec),
                        readonly ? KIT_READONLY : 0
                    )) {
                    name_value(s, n, p, g->names[p]);
                }
            } else {
                kit_label(k, "v", "dim", spec->kind == PIN_EXEC ? "Execution" : ed_pin_type_name(spec));
            }
            kit_prop_end(k);
        }
    }
    if (n->output_count) {
        kit_panel_end(k);
    }
    if (kit_panel_begin(k, "node", "Node", NULL, true)) {
        unsigned flags = readonly ? KIT_DISABLED : 0;
        bool disabled = n->disabled != 0;
        kit_prop_begin(k, "disabled", "Disabled");
        if (kit_checkbox(k, "v", &disabled, NULL, flags)) {
            ed_snapshot_before(e);
            graph_set_node_disabled(gr, n, disabled);
        }
        kit_prop_end(k);
        if (node_behavior_for(n) != NODE_BEHAVIOR_PURE) {
            bool breakpoint = n->breakpoint != 0;
            kit_prop_begin(k, "break", "Breakpoint");
            if (kit_checkbox(k, "v", &breakpoint, NULL, flags)) {
                ed_snapshot_before(e);
                graph_set_breakpoint(gr, n, breakpoint);
            }
            kit_prop_end(k);
        }
        if (ed_node_has_advanced_inputs(n)) {
            bool advanced = n->show_advanced;
            kit_prop_begin(k, "advanced", "Advanced Inputs");
            if (kit_checkbox(k, "v", &advanced, "Show on node", flags)) {
                ed_snapshot_before(e);
                n->show_advanced = advanced;
            }
            kit_prop_end(k);
        }
    }
    kit_panel_end(k);
}
