#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"

#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const behavior_asset *editor_behavior_asset(const editor *e)
{
    return e ? e->asset : NULL;
}

int editor_open_behavior_asset(editor *e, const behavior_asset *source, uint64_t graph_id)
{
    if (!e || !source || !behavior_asset_find_graph(source, graph_id)) {
        return -1;
    }
    free(e->document_key);
    e->document_key = NULL;
    char *json = behavior_asset_save_json(source);
    if (!json) {
        return -1;
    }
    int result = ed_adopt_behavior_asset_with_identity(e, json, source->identity);
    free(json);
    if (result != 0) {
        return -1;
    }
    aclear(e->graph_callers);
    aclear(e->graph_views);
    ed_load_graph_view(e);
    free(e->saved_snapshot);
    e->saved_snapshot = behavior_asset_save_json(e->asset);
    ed_free_string_stack(&e->undo);
    ed_free_string_stack(&e->redo);
    e->inspection_instance = NULL;
    e->inspection_run = NULL;
    e->arrange_pending = true;
    return editor_open_graph(e, graph_id);
}

bool ed_can_go_back(const editor *e)
{
    for (int i = acount(e->graph_callers) - 1; i >= 0; i--) {
        if (behavior_asset_find_graph(e->asset, e->graph_callers[i].view.graph_id)) {
            return true;
        }
    }
    return false;
}

int editor_open_graph(editor *e, uint64_t graph_id)
{
    if (!e || !e->asset) {
        return -1;
    }
    graph *target = behavior_asset_find_graph(e->asset, graph_id);
    if (!target) {
        return -1;
    }
    if (target != e->active_graph) {
        aclear(e->graph_callers);
    }
    return ed_switch_graph(e, target);
}

int editor_open_graph_call(editor *e, uint64_t node_id)
{
    if (!e || !e->active_graph) {
        return -1;
    }
    node *call = graph_node_by_id(e->active_graph, node_id);
    if (!ed_is_graph_call(call)) {
        return -1;
    }
    if (call->reference_asset) {
        if (!e->open_asset) {
            return -1;
        }
        char *name = strdup(call->reference_asset);
        uint64_t target_id = call->reference_id;
        int result = e->open_asset(e->open_asset_user, name, target_id);
        free(name);
        return result;
    }
    graph *target = behavior_asset_find_graph(e->asset, call->reference_id);
    if (!target || target == e->active_graph ||
        (call->desc == &NODE_CallMacro ? target->kind != GRAPH_MACRO
                                       : target->kind != GRAPH_FUNCTION)) {
        return -1;
    }
    ed_save_graph_view(e);
    ed_graph_caller caller = { *ed_graph_view_for(e, e->active_graph->graph_id, 0),
                               node_id };
    if (acount(e->graph_callers) == ED_HIST_MAX) {
        memmove(
            e->graph_callers,
            e->graph_callers + 1,
            (ED_HIST_MAX - 1) * sizeof *e->graph_callers
        );
        asetlen(e->graph_callers, ED_HIST_MAX - 1);
    }
    apush(e->graph_callers, caller);
    ed_cancel_snapshot(e);
    return ed_switch_graph(e, target);
}

int editor_graph_back(editor *e)
{
    if (!e || !e->asset) {
        return -1;
    }
    while (acount(e->graph_callers)) {
        ed_graph_caller caller = apop(e->graph_callers);
        graph *target = behavior_asset_find_graph(e->asset, caller.view.graph_id);
        if (!target) {
            continue;
        }
        ed_cancel_snapshot(e);
        ed_switch_graph(e, target);
        e->arrange_pending = false;
        e->camera_moving = false;
        e->fit_mode = caller.view.fit_mode;
        e->fit_resize_pending = false;
        e->fit_restore_pending = true;
        e->camera_activity_time = ImGui_GetTime();
        e->pan = caller.view.pan;
        e->zoom = caller.view.zoom;
        e->view_initialized = caller.view.view_initialized;
        e->entry_index = caller.view.entry_index;
        node *call = graph_node_by_id(target, caller.node_id);
        if (call) {
            ed_select_node(e, call, 0);
        }
        return 0;
    }
    return -1;
}

int editor_graph_history_count(const editor *e)
{
    return e ? acount(e->graph_callers) : 0;
}

uint64_t editor_graph_history_id(const editor *e, int index)
{
    return e && index >= 0 && index < acount(e->graph_callers)
             ? e->graph_callers[index].view.graph_id
             : 0;
}

int editor_graph_ancestor(editor *e, int index)
{
    if (!e || index < 0 || index >= acount(e->graph_callers)) {
        return -1;
    }
    asetlen(e->graph_callers, index + 1);
    return editor_graph_back(e);
}
