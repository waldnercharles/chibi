#include "editor/graph/eval_view.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/xalloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int ed_event_name_used(const editor *e, const char *name)
{
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        const node *n = e->active_graph->nodes[i];
        if (n->desc->flow_kind == NODE_FLOW_EVENT && n->reference_name &&
            strcmp(n->reference_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_event_param_count(const node *n)
{
    int count = 0;
    for (int i = 0; i < n->output_count; i++) {
        count += n->output_pins[i].kind == PIN_DATA;
    }
    return count;
}

/* Appends only the declared parameters this event is still missing, so no
 * existing pin is retyped or removed and no edge is invalidated under EVT-5. */
int ed_event_add_declared_params(editor *e, node *n, const graph_host_event_desc *signature)
{
    int added = 0;
    for (int i = ed_event_param_count(n); i < signature->payload_count; i++) {
        added += graph_add_pin(e->active_graph, n, 1, &signature->payload[i]) >= 0;
    }
    return added;
}

int ed_name_spawned_event(editor *e, node *n)
{
    if (!n->desc || n->desc->flow_kind != NODE_FLOW_EVENT || !e->event_catalog_count) {
        return 0;
    }
    for (int i = 0; i < e->event_catalog_count; i++) {
        const graph_host_event_desc *signature = &e->event_catalog[i];
        if (ed_event_name_used(e, signature->name)) {
            continue;
        }
        if (graph_set_node_reference(e->active_graph, n, 0, signature->name) != 0) {
            return -1;
        }
        return ed_event_add_declared_params(e, n, signature) == signature->payload_count
                 ? 0
                 : -1;
    }
    return -1;
}

void ed_free_string_stack(char ***stack)
{
    for (int i = 0; i < acount(*stack); i++) {
        free((*stack)[i]);
    }
    afree(*stack);
}

void ed_push_string(char ***stack, char *value)
{
    if (!value) {
        return;
    }
    if (acount(*stack) == ED_HIST_MAX) {
        free((*stack)[0]);
        memmove(*stack, *stack + 1, (size_t)(ED_HIST_MAX - 1) * sizeof **stack);
        (*stack)[ED_HIST_MAX - 1] = value;
        return;
    }
    apush(*stack, value);
}

editor *editor_new(behavior_asset *asset)
{
    registry_validate();
    editor *e = xcalloc(1, sizeof *e);
    e->asset = asset;
    e->document_key = xstrdup(asset->name);
    e->ui_context = EDITOR_UI_CONTEXT_BEHAVIOR;
    e->active_graph = asset->graph;
    e->arrange_pending = true;
    e->edit_revision_asset = asset;
    e->saved_snapshot = behavior_asset_save_json(asset);
    compile_report_init(&e->compile);
    e->ev = eval_view_new();
    return e;
}

void editor_set_value_formatter(editor *e, editor_value_formatter formatter, void *user)
{
    e->value_formatter = formatter;
    e->value_formatter_user = user;
    eval_view_set_formatter(e->ev, formatter, user);
}

void editor_set_choices_resolver(editor *e, editor_choices_resolver resolver, void *user)
{
    e->choices_resolver = resolver;
    e->choices_user = user;
}

const graph *editor_active_graph(const editor *e)
{
    return e ? e->active_graph : NULL;
}

void editor_free(editor *e)
{
    if (!e) {
        return;
    }
    afree(e->asset_catalog);
    afree(e->prototype_catalog);
    ed_free_string_stack(&e->undo);
    ed_free_string_stack(&e->redo);
    free(e->document_key);
    for (int i = 0; i < acount(e->documents); i++) {
        ed_document_history *history = &e->documents[i];
        free(history->key);
        free(history->json);
        free(history->saved);
        ed_free_string_stack(&history->undo);
        ed_free_string_stack(&history->redo);
        afree(history->links);
        free(history->location.nodes);
    }
    afree(e->documents);
    free(e->pending_snapshot);
    free(e->clipboard);
    free(e->edit_revision_snapshot);
    free(e->saved_snapshot);
    ed_destroy_preview(e);
    compile_report_free(&e->compile);
    behavior_asset_free(e->asset);
    afree(e->graph_callers);
    afree(e->selected);
    afree(e->selected_links);
    eval_view_free(e->ev);
    free(e);
}

void editor_set_ui_context(editor *e, editor_ui_context context)
{
    if (!e || context < EDITOR_UI_CONTEXT_SCENE || context > EDITOR_UI_CONTEXT_SYSTEM) {
        return;
    }
    e->ui_context = context;
    if (context == EDITOR_UI_CONTEXT_SYSTEM && e->active_graph) {
        e->active_graph->dataflow = 1;
    }
}

static bool retained_keyboard;

void editor_set_retained_keyboard(bool captured)
{
    retained_keyboard = captured;
}

int editor_host_captures_keyboard(void)
{
    return retained_keyboard;
}

void editor_set_event_catalog(editor *e, const graph_host_event_desc *events, int count)
{
    if (!e) {
        return;
    }
    e->event_catalog = count > 0 ? events : NULL;
    e->event_catalog_count = count > 0 ? count : 0;
}

void editor_set_asset_opener(editor *e, int (*open)(void *, const char *, uint64_t), void *user)
{
    e->open_asset = open;
    e->open_asset_user = user;
}

void editor_set_prototype_catalog(editor *e, const char *const *names, int count)
{
    aclear(e->prototype_catalog);
    for (int i = 0; i < count; i++) {
        apush(e->prototype_catalog, names[i]);
    }
}

void editor_set_asset_catalog(editor *e, const behavior_asset *const *assets, int count)
{
    aclear(e->asset_catalog);
    for (int i = 0; i < count; i++) {
        apush(e->asset_catalog, assets[i]);
    }
}

/* The values pins show: the inspected run's watches, or a preview
   evaluation while no run owns pin storage. */
void ed_refresh_values(editor *e)
{
    eval_view_set_instance(e->ev, ed_display_instance(e));
    eval_view_set_catalog(e->ev, e->asset_catalog, acount(e->asset_catalog));
    if (ed_inspection_blocked(e) || !e->inspection_run) {
        eval_view_refresh(e->ev, e->active_graph);
    }
    eval_view_set_run(e->ev, ed_display_run(e));
}
