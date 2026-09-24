#include "editor/graph/eval_view.h"
#include "editor/graph/preview_gate.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "render/scene_renderer.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <float.h>
#include <imgui/dcimgui_internal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
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

int ed_name_spawned_event(editor *e, node *n)
{
    if (!n->desc || n->desc->flow_kind != NODE_FLOW_EVENT || !e->event_catalog_count) {
        return 0;
    }
    for (int i = 0; i < e->event_catalog_count; i++) {
        const editor_event_signature *signature = &e->event_catalog[i];
        if (ed_event_name_used(e, signature->name)) {
            continue;
        }
        if (graph_set_node_reference(e->active_graph, n, 0, signature->name) != 0) {
            return -1;
        }
        return ed_event_add_declared_params(e, n, signature) == signature->param_count
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
    editor *e = (editor *)calloc(1, sizeof *e);
    if (!e) {
        return NULL;
    }
    e->asset = asset;
    e->document_key = xstrdup(asset->name);
    e->ui_context = EDITOR_UI_CONTEXT_BEHAVIOR;
    e->inspector_active = true;
    e->graph_visible = 1;
    e->active_graph = asset ? asset->graph : NULL;
    e->arrange_pending = true;
    e->edit_revision_asset = asset;
    e->saved_snapshot = asset ? behavior_asset_save_json(asset) : NULL;
    compile_report_init(&e->compile);
    e->ev = eval_view_new();
    eval_view_set_formatter(e->ev, e->value_formatter, e->value_formatter_user);
    e->zoom = 1.0f;
    return e;
}

void editor_set_value_formatter(editor *e, editor_value_formatter formatter, void *user)
{
    e->value_formatter = formatter;
    e->value_formatter_user = user;
    eval_view_set_formatter(e->ev, formatter, user);
}

void editor_set_flags_resolver(editor *e, editor_flags_resolver resolver, void *user)
{
    e->flags_resolver = resolver;
    e->flags_user = user;
}

void editor_set_color_resolver(editor *e, editor_color_resolver resolver, void *user)
{
    e->color_resolver = resolver;
    e->color_user = user;
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
    free(e->math_text);
    afree(e->asset_catalog);
    afree(e->prototype_catalog);
    app_scene_renderer_free(e->shader_preview);
    engine_viewport_free(e->shader_viewport);
    free(e->math_saved);
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
        afree(history->views);
        afree(history->links);
        free(history->location.nodes);
    }
    afree(e->documents);
    free(e->pending_snapshot);
    free(e->clipboard);
    free(e->log_text);
    free(e->compile_text);
    free(e->edit_revision_snapshot);
    free(e->saved_snapshot);
    ed_destroy_preview(e);
    compile_report_free(&e->compile);
    behavior_asset_free(e->asset);
    afree(e->graph_views);
    afree(e->graph_callers);
    afree(e->views);
    afree(e->pins);
    arrange_routes_free(&e->wire_routes);
    afree(e->wire_route);
    afree(e->selected);
    afree(e->selected_links);
    afree(e->drag_nodes);
    eval_view_free(e->ev);
    free(e);
}

void editor_prepare_docking(void)
{
    editor_ui_prepare();
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

void editor_set_graph_visible(editor *e, int visible)
{
    if (e) {
        e->graph_visible = visible != 0;
    }
}

bool editor_has_inspection_selection(const editor *e)
{
    return e->active_graph->math_expression || e->selected_variable ||
           acount(e->selected) || acount(e->selected_links);
}

void editor_set_inspector_active(editor *e, bool active)
{
    e->inspector_active = active;
}

void editor_set_save_handler(editor *e, editor_save_handler handler, void *user)
{
    if (!e) {
        return;
    }
    e->save_handler = handler;
    e->save_user = user;
}

int editor_host_captures_keyboard(void)
{
    const ImGuiIO *io = ImGui_GetIO();
    const ImGuiContext *context = ImGui_GetCurrentContext();
    bool active_control = context->ActiveId &&
                          (!context->ActiveIdWindow ||
                           context->ActiveId != context->ActiveIdWindow->MoveId);
    /* Navigation claims every key in a focused window containing controls.
     * Callers already require viewport focus; only active UI interaction should
     * prevent that viewport from receiving gameplay keys. Clicking the viewport
     * image holds its window move ID, which owns the pointer rather than keys. */
    return io->WantTextInput || active_control ||
           ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopup) ||
           (io->WantCaptureKeyboard && !io->NavActive);
}

void editor_set_event_catalog(editor *e, const editor_event_signature *events, int count)
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

void editor_draw(editor *e)
{
    if (e && !e->icon_font) {
        e->icon_font = ed_find_font("Chibi Body");
        e->math_font = ed_find_font("Chibi Math");
    }
    if (!e || !e->ev) {
        return;
    }
    if (e->settings_open && e->settings_frame != ImGui_GetFrameCount()) {
        ed_finish_settings(e);
    }
    uint64_t open_call = e->open_call_node;
    e->open_call_node = 0;
    if (open_call) {
        editor_open_graph_call(e, open_call);
    }
    ed_apply_field_request(e);
    if (e->inspector_active) {
        editor_ui_set_history_available(acount(e->undo) > 0, acount(e->redo) > 0);
    }
    if (e->ui_context == EDITOR_UI_CONTEXT_SCENE || !e->graph_visible) {
        ed_draw_compiler_results(e);
        ed_draw_output_log(e);
        return;
    }
    ImGuiIO *io = ImGui_GetIO();
    flow_state preview_state = e->activation ? flow_run_status(e->activation) : FLOW_IDLE;
    const behavior_instance *display_instance = ed_display_instance(e);
    const flow_run *display_run = ed_display_run(e);
    eval_view_set_instance(e->ev, display_instance);
    eval_view_set_catalog(e->ev, e->asset_catalog, acount(e->asset_catalog));
    if (ed_inspection_blocked(e) ||
        (!e->inspection_run && editor_preview_allowed(preview_state))) {
        eval_view_refresh(e->ev, e->active_graph);
    }
    eval_view_set_run(e->ev, display_run);
    if (editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS)) {
        ImGui_SetNextWindowSize((ImVec2){ 960.0f, 640.0f }, ImGuiCond_FirstUseEver);
        if (editor_ui_take_focus(EDITOR_UI_FOCUS_NODE_CANVAS)) {
            ImGui_SetNextWindowFocus();
        }
        bool root = e->active_graph == e->asset->graph;
        bool hide_tab = root && editor_ui_document_bar_visible();
        ImGuiWindowClass window_class = { .DockingAllowUnclassed = true,
                                          .DockNodeFlagsOverrideSet = hide_tab ? ImGuiDockNodeFlags_AutoHideTabBar
                                                                               : 0 };
        ImGuiWindow *window = ImGui_FindWindowByName("Node Canvas");
        if (window && window->DockNode &&
            window->WindowClass.DockNodeFlagsOverrideSet !=
                window_class.DockNodeFlagsOverrideSet) {
            if (hide_tab) {
                window->DockNode->WantHiddenTabBarUpdate = true;
            } else {
                ImGuiDockNode_SetLocalFlags(
                    window->DockNode,
                    window->DockNode->LocalFlags & ~ImGuiDockNodeFlags_HiddenTabBar
                );
            }
        }
        ImGui_SetNextWindowClass(&window_class);
        char title[512];
        if (root) {
            snprintf(title, sizeof title, "%s###Node Canvas", e->asset->name);
        } else {
            snprintf(
                title,
                sizeof title,
                "%s / %s###Node Canvas",
                e->asset->name,
                e->active_graph->name
            );
        }
        if (!editor_ui_begin_pane(
                EDITOR_UI_PANE_NODE_CANVAS,
                title,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            )) {
            ImGui_End();
        } else {
            ed_draw_extract_popup(e);
            /* Toolbar actions can replace the document or destroy its preview. */
            eval_view_set_instance(e->ev, ed_display_instance(e));
            eval_view_set_run(e->ev, ed_display_run(e));
            ImVec2 canvas_min = ImGui_GetCursorScreenPos();
            ImVec2 canvas_size = ImGui_GetContentRegionAvail();
            /* Resizing a neighboring pane must not move the node under the
             * pointer between clicks. Explicit Fit/Open still frames the graph. */
            e->canvas_min = canvas_min;
            ed_resize_fitted_view(e, canvas_size);
            e->controls_hovered = 0;
            ed_draw_toolbar(e);
            /* Floating toolbar actions can invalidate preview pointers. */
            eval_view_set_instance(e->ev, ed_display_instance(e));
            eval_view_set_run(e->ev, ed_display_run(e));
            /* Layout needs the active font and a measured canvas. Opening only
             * schedules this work; inspecting another pane never schedules it. */
            if (e->arrange_pending && canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
                e->arrange_pending = false;
                ed_arrange_automatic(e);
            }
            ed_center_initial_view(e);
            if ((io->MouseDown[0] || io->MouseDown[1] || io->MouseDown[2]) &&
                ed_point_in_rect(io->MousePos, canvas_min, ed_add(canvas_min, canvas_size))) {
                e->fit_resize_pending |= e->fit_mode && e->camera_moving;
                e->camera_moving = false;
            }
            ed_camera_advance(e, io->DeltaTime);
            ed_build_layout(e);
            ImVec2 hover_mouse = io->MousePos;
            e->hovered_node = NULL;
            e->has_hovered_link = 0;
            if (ed_point_in_rect(hover_mouse, e->canvas_min, ed_add(e->canvas_min, e->canvas_size))) {
                ed_update_hover(e, hover_mouse);
            }
            ed_draw_canvas(e);
            ed_draw_front_nodes(e);
            ed_draw_connect_error(e);
            const char *error = eval_view_error(e->ev);
            if (error[0]) {
                ImGui_SetCursorScreenPos(ed_add(e->canvas_min, (ImVec2){ 12.0f, 12.0f }));
                ImGui_TextColored((ImVec4){ 1.0f, 0.35f, 0.35f, 1.0f }, "Graph error: %s", error);
            }
            ed_handle_input(e);
            ed_draw_selection_overlay(e);
            ImGui_SetCursorScreenPos(e->canvas_min);
            ImGui_Dummy(e->canvas_size);
            ed_accept_graph_call_drop(e);
            ed_handle_shortcuts(e);
            ed_handle_popups(e);
            ed_draw_binding_popup(e);
            ImGui_End();
        }
    }
    if (e->ui_context == EDITOR_UI_CONTEXT_BEHAVIOR) {
        if (e->inspector_active) {
            ed_draw_details(e);
        }
    } else if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM) {
        /* Systems do not have the Behavior Asset tree, but their graph still needs
         * the same UE-style Details panel for node and wire inspection. */
        if (e->inspector_active) {
            ed_draw_details(e);
        }
    }
    if (e->ui_context != EDITOR_UI_CONTEXT_SYSTEM) {
        ed_draw_compiler_results(e);
        ed_draw_output_log(e);
    }
}
