#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"

#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <string.h>

int ed_link_equal(const ed_link_ref *a, const ed_link_ref *b)
{
    return a->from_id == b->from_id && a->from_pin_id == b->from_pin_id &&
           a->to_id == b->to_id && a->to_pin_id == b->to_pin_id;
}

void ed_clear_selection(editor *e)
{
    aclear(e->selected);
    aclear(e->selected_links);
}

void ed_reset_node_interaction(editor *e)
{
    ed_clear_selection(e);
    aclear(e->drag_nodes);
    aclear(e->views);
    aclear(e->pins);
    e->context_node = NULL;
    e->reveal_target = NULL;
    e->front_node = NULL;
    e->hovered_node = NULL;
    e->drag_toggle_node = NULL;
    e->drag_toggle_selected = 0;
    e->edit_text_node = NULL;
    e->binding_node = NULL;
    e->binding_node_id = 0;
    e->binding_pin = -1;
    e->binding_axis = -1;
    e->binding_selected = 0;
    e->binding_focus_search = false;
    e->link_pin = (ed_pin_view){ 0 };
    e->spawn_link_pin = (ed_pin_view){ 0 };
    e->fields_node = 0;
    e->open_call_node = 0;
    e->math_node_id = 0;
    e->edit_history_open = 0;
    e->edit_changed = 0;
    e->has_context_link = 0;
    e->has_hovered_link = 0;
    e->spawn_link_pending = 0;
    e->dragging = 0;
    e->linking = 0;
    for (int i = acount(e->graph_callers) - 1; i >= 0; i--) {
        const ed_graph_caller *caller = &e->graph_callers[i];
        graph *g = behavior_asset_find_graph(e->asset, caller->view.graph_id);
        if (g && graph_node_by_id(g, caller->node_id)) {
            continue;
        }
        memmove(
            e->graph_callers + i,
            e->graph_callers + i + 1,
            (size_t)(acount(e->graph_callers) - i - 1) * sizeof *e->graph_callers
        );
        asetlen(e->graph_callers, acount(e->graph_callers) - 1);
    }
}

ed_graph_view *ed_graph_view_for(editor *e, uint64_t graph_id, int create)
{
    for (int i = 0; i < acount(e->graph_views); i++) {
        if (e->graph_views[i].graph_id == graph_id) {
            return &e->graph_views[i];
        }
    }
    if (!create) {
        return NULL;
    }
    apush(e->graph_views, (ed_graph_view){ .graph_id = graph_id, .zoom = 1.0f });
    return &alast(e->graph_views);
}

void ed_save_graph_view(editor *e)
{
    if (!e->active_graph) {
        return;
    }
    ed_graph_view *view = ed_graph_view_for(e, e->active_graph->graph_id, 1);
    view->fit_mode = e->fit_mode;
    view->pan = e->pan;
    view->zoom = e->zoom;
    view->view_initialized = e->view_initialized;
    view->entry_index = e->entry_index;
}

void ed_load_graph_view(editor *e)
{
    e->camera_moving = false;
    e->fit_mode = false;
    e->fit_resize_pending = false;
    e->fit_restore_pending = false;
    if (!e->active_graph) {
        e->pan = (ImVec2){ 0.0f, 0.0f };
        e->zoom = 1.0f;
        e->view_initialized = 0;
        e->entry_index = 0;
        return;
    }
    ed_graph_view *view = ed_graph_view_for(e, e->active_graph->graph_id, 0);
    if (!view) {
        e->pan = (ImVec2){ 0.0f, 0.0f };
        e->zoom = 1.0f;
        e->view_initialized = 0;
        e->entry_index = 0;
        return;
    }
    e->fit_mode = view->fit_mode;
    e->pan = view->pan;
    e->zoom = view->zoom;
    e->view_initialized = view->view_initialized;
    e->entry_index = view->entry_index;
}

int ed_switch_graph(editor *e, graph *graph)
{
    if (!e || !graph || graph->owner != e->asset) {
        return -1;
    }
    if (e->active_graph == graph) {
        return 0;
    }
    ed_save_graph_view(e);
    e->active_graph = graph;
    e->extract_requested = false;
    e->fields_node = 0;
    e->open_call_node = 0;
    ed_load_graph_view(e);
    e->arrange_pending = !e->view_initialized;
    ed_clear_selection(e);
    e->selected_variable = NULL;
    e->reveal_target = NULL;
    e->context_node = NULL;
    e->has_context_link = 0;
    e->front_node = NULL;
    e->hovered_node = NULL;
    e->has_hovered_link = 0;
    e->spawn_link_pending = 0;
    e->dragging = 0;
    e->linking = 0;
    aclear(e->drag_nodes);
    return 0;
}

int ed_link_selected(const editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < acount(e->selected_links); i++) {
        if (ed_link_equal(link, &e->selected_links[i])) {
            return 1;
        }
    }
    return 0;
}

void ed_add_selected_link(editor *e, const ed_link_ref *link)
{
    if (ed_link_selected(e, link)) {
        return;
    }
    apush(e->selected_links, *link);
}

void ed_remove_selected_link(editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < acount(e->selected_links); i++) {
        if (!ed_link_equal(link, &e->selected_links[i])) {
            continue;
        }
        adel(e->selected_links, i);
        return;
    }
}

int ed_node_selected(const editor *e, const node *n)
{
    for (int i = 0; i < acount(e->selected); i++) {
        if (e->selected[i] == n) {
            return 1;
        }
    }
    return 0;
}

void ed_add_selected(editor *e, node *n)
{
    if (ed_node_selected(e, n)) {
        return;
    }
    apush(e->selected, n);
}

void ed_remove_selected(editor *e, node *n)
{
    for (int i = 0; i < acount(e->selected); i++) {
        if (e->selected[i] != n) {
            continue;
        }
        adel(e->selected, i);
        return;
    }
}

void ed_select_node(editor *e, node *n, int append)
{
    e->selected_variable = NULL;
    if (!append) {
        aclear(e->selected);
        aclear(e->selected_links);
    }
    if (n) {
        ed_add_selected(e, n);
        e->front_node = n;
    }
}

void ed_select_variable(editor *e, behavior_variable *v)
{
    ed_clear_selection(e);
    e->selected_variable = v;
}
