#include "editor/graph/eval_view.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/shader/shader_graph.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

static void ed_format_text_repair(editor *e, node *n);

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ed_select_appended_visible(editor *e, int first)
{
    for (int i = first; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (!graph_node_is_inline(e->active_graph, n)) {
            ed_add_selected(e, n);
        }
    }
}

char *ed_snapshot(const editor *e)
{
    return behavior_asset_save_json(e->asset);
}

void ed_commit_snapshot(editor *e)
{
    if (!e->pending_snapshot) {
        return;
    }
    e->edit_pending = 1;
    ed_push_string(&e->undo, e->pending_snapshot);
    e->pending_snapshot = NULL;
    ed_free_string_stack(&e->redo);
}

void ed_snapshot_before(editor *e)
{
    char *snapshot = ed_snapshot(e);
    if (!snapshot) {
        return;
    }
    e->edit_pending = 1;
    ed_push_string(&e->undo, snapshot);
    ed_free_string_stack(&e->redo);
}

void ed_cancel_snapshot(editor *e)
{
    free(e->pending_snapshot);
    e->pending_snapshot = NULL;
}

static void accept_image_drop(editor *e)
{
    if (e->asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
        e->asset->domain != BEHAVIOR_DOMAIN_SHAPE &&
        e->asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return;
    }
    const ImGuiPayload *payload = ImGui_AcceptDragDropPayload("CHIBI_IMAGE", 0);
    if (!payload || !payload->Delivery || payload->DataSize <= 1) {
        return;
    }
    const char *path = payload->Data;
    if (path[payload->DataSize - 1] != '\0') {
        return;
    }
    ImVec2 position = ed_screen_to_world(e, ImGui_GetMousePos());
    node *n = editor_spawn(
        e,
        registry_node_by_name("Shader Image Texture"),
        position.x,
        position.y
    );
    assert(n);
    pin_value value = { .type = PIN_STR, .str = path };
    int result = graph_set_pin_default(e->active_graph, n, 0, SHADER_IMAGE_INPUT_IMAGE, &value);
    assert(result == 0);
    (void)result;
    ed_select_node(e, n, false);
}

void ed_accept_graph_call_drop(editor *e)
{
    if (e->active_graph->math_expression) {
        return;
    }
    if (!ImGui_BeginDragDropTarget()) {
        return;
    }
    const ImGuiPayload *payload = ImGui_AcceptDragDropPayload("CB_GRAPH_CALL", 0);
    if (payload && payload->Delivery && payload->DataSize == sizeof(uint64_t)) {
        uint64_t graph_id;
        memcpy(&graph_id, payload->Data, sizeof graph_id);
        graph *target = behavior_asset_find_graph(e->asset, graph_id);
        if (target && (target->kind == GRAPH_FUNCTION || target->kind == GRAPH_MACRO)) {
            (void)ed_place_graph_call_at(e, target, ed_screen_to_world(e, ImGui_GetMousePos()));
        }
    }
    accept_image_drop(e);
    ImGui_EndDragDropTarget();
}

void ed_destroy_preview(editor *e)
{
    if (e->instance) {
        behavior_instance_destroy(e->instance);
        e->instance = NULL;
    }
    e->activation = NULL;
    behavior_asset_free(e->preview_behavior_asset);
    e->preview_behavior_asset = NULL;
    e->instance_destroyed = 1;
    eval_view_set_run(e->ev, NULL);
}

/* Every behavior_asset swap must release the preview copy and its activations. */
int ed_adopt_behavior_asset_with_identity(editor *e, const char *json, uint64_t owner_identity)
{
    ed_destroy_preview(e);
    uint64_t active_graph_id = e->active_graph ? e->active_graph->graph_id : 0;
    ed_save_graph_view(e);
    behavior_asset *fresh = behavior_asset_load_json(json);
    if (!fresh) {
        return -1;
    }
    if (owner_identity) {
        fresh->identity = owner_identity;
    }
    /* The value preview borrows nodes from the old asset. */
    eval_view_free(e->ev);
    e->ev = eval_view_new();
    eval_view_set_formatter(e->ev, e->value_formatter, e->value_formatter_user);
    asetlen(e->views, 0);
    asetlen(e->pins, 0);
    behavior_asset_free(e->asset);
    e->asset = fresh;
    e->fields_node = 0;
    e->open_call_node = 0;
    e->active_graph = behavior_asset_find_graph(e->asset, active_graph_id);
    if (!e->active_graph) {
        e->active_graph = e->asset->graph;
    }
    ed_load_graph_view(e);
    e->arrange_pending = false;
    compile_report_free(&e->compile);
    compile_report_init(&e->compile);
    ed_rebuild_compile_text(e);
    ed_clear_selection(e);
    e->selected_variable = NULL;
    e->behavior_asset_search[0] = '\0';
    e->dragging = 0;
    e->linking = 0;
    e->reveal_target = NULL;
    e->context_node = NULL;
    e->context_graph = NULL;
    e->graph_rename[0] = '\0';
    e->has_context_link = 0;
    e->front_node = NULL;
    e->spawn_link_pending = 0;
    e->edit_text_node = NULL;
    aclear(e->drag_nodes);
    e->log_count = 0;
    ed_rebuild_log_text(e);
    e->edit_pending = 1;
    return 0;
}

int ed_adopt_behavior_asset(editor *e, const char *json)
{
    return ed_adopt_behavior_asset_with_identity(
        e,
        json,
        e && e->asset ? e->asset->identity : 0
    );
}

void ed_undo(editor *e)
{
    if (acount(e->undo) == 0) {
        return;
    }
    char *target = apop(e->undo);
    char *current = behavior_asset_save_json(e->asset);
    if (current) {
        ed_push_string(&e->redo, current);
    }
    ed_adopt_behavior_asset(e, target);
    free(target);
}

void ed_redo(editor *e)
{
    if (acount(e->redo) == 0) {
        return;
    }
    char *target = apop(e->redo);
    char *current = behavior_asset_save_json(e->asset);
    if (current) {
        ed_push_string(&e->undo, current);
    }
    ed_adopt_behavior_asset(e, target);
    free(target);
}

bool editor_can_undo(const editor *e)
{
    return e && acount(e->undo) > 0;
}

bool editor_can_redo(const editor *e)
{
    return e && acount(e->redo) > 0;
}

void ed_delete_selected(editor *e)
{
    if (acount(e->selected) == 0 && acount(e->selected_links) == 0) {
        return;
    }
    uint64_t *selected_ids = NULL;
    for (int i = 0; i < acount(e->selected); i++) {
        node *n = e->selected[i];
        apush(selected_ids, n->id);
        for (int pin = 0; pin < n->input_count; pin++) {
            node *source = NULL;
            int source_pin = -1;
            if (graph_inline_source(e->active_graph, n, pin, &source, &source_pin)) {
                apush(selected_ids, source->id);
            }
        }
    }
    ed_snapshot_before(e);
    for (int i = acount(e->selected_links) - 1; i >= 0; i--) {
        int index = ed_edge_index(e, &e->selected_links[i]);
        if (index >= 0) {
            edge ed = e->active_graph->edges[index];
            node *from = graph_node_by_id(e->active_graph, ed.from_id);
            node *to = graph_node_by_id(e->active_graph, ed.to_id);
            int from_pin = -1, to_pin = -1, fo = 0, to_out = 0;
            if (from && to &&
                graph_pin_by_id(from, ed.from_pin_id, &fo, &from_pin) && fo &&
                graph_pin_by_id(to, ed.to_pin_id, &to_out, &to_pin) && !to_out) {
                if (graph_remove_edge(e->active_graph, from, from_pin, to, to_pin) == 0) {
                    ed_format_text_repair(e, to);
                }
            }
        }
    }

    for (int i = acount(selected_ids) - 1; i >= 0; i--) {
        node *target = graph_node_by_id(e->active_graph, selected_ids[i]);
        if (target) {
            int removed = graph_remove_node(e->active_graph, target);
            assert(removed == 0);
            (void)removed;
        }
    }
    ed_reset_node_interaction(e);
    afree(selected_ids);
}

void ed_copy_selected(editor *e)
{
    if (acount(e->selected) == 0) {
        return;
    }
    char *json = graph_save_nodes_json(e->active_graph, e->selected, acount(e->selected));
    if (!json) {
        return;
    }
    ImGui_SetClipboardText(json);
    free(e->clipboard);
    e->clipboard = json;
}

void ed_cut_selected(editor *e)
{
    ed_copy_selected(e);
    if (acount(e->selected) == 0) {
        return;
    }
    ed_delete_selected(e);
}

void ed_paste(editor *e)
{
    const char *clipboard = ImGui_GetClipboardText();
    if (!clipboard || !clipboard[0]) {
        clipboard = e->clipboard;
    }
    if (!clipboard) {
        return;
    }
    ed_snapshot_before(e);
    int first = acount(e->active_graph->nodes);
    if (graph_append_json(e->active_graph, clipboard, 24.0f, 24.0f) == 0) {
        ed_clear_selection(e);
        ed_select_appended_visible(e, first);
    }
}

void ed_duplicate_selected(editor *e)
{
    if (acount(e->selected) == 0) {
        return;
    }
    char *json = graph_save_nodes_json(e->active_graph, e->selected, acount(e->selected));
    if (!json) {
        return;
    }
    ed_snapshot_before(e);
    int first = acount(e->active_graph->nodes);
    if (graph_append_json(e->active_graph, json, 24.0f, 24.0f) == 0) {
        ed_clear_selection(e);
        ed_select_appended_visible(e, first);
    }
    free(json);
}

void ed_begin_drag(editor *e, ImVec2 mouse)
{
    asetlen(e->drag_nodes, acount(e->selected));
    for (int i = 0; i < acount(e->drag_nodes); i++) {
        e->drag_nodes[i] = (ed_drag_node){ e->selected[i],
                                           e->selected[i]->x,
                                           e->selected[i]->y };
    }
    e->drag_start_mouse = mouse;
    e->dragging = 1;
    e->pending_snapshot = ed_snapshot(e);
}

void ed_update_drag(editor *e, ImVec2 mouse)
{
    ImVec2 delta = ed_scale(ed_sub(mouse, e->drag_start_mouse), 1.0f / e->zoom);
    for (int i = 0; i < acount(e->drag_nodes); i++) {
        e->drag_nodes[i].n->x = e->drag_nodes[i].x + delta.x;
        e->drag_nodes[i].n->y = e->drag_nodes[i].y + delta.y;
        e->drag_nodes[i].n->placed = true;
    }
}

void ed_update_handle_drag(editor *e, ImVec2 mouse)
{
    edge *ed = &e->active_graph->edges[e->handle_edge];
    assert(e->handle_side >= 0 && e->handle_side < ed->reroute_count);
    ImVec2 point = ed_screen_to_world(e, mouse);
    ed->reroutes[e->handle_side].x = point.x;
    ed->reroutes[e->handle_side].y = point.y;
}

/* CAT-38: a Format Text node always carries one trailing Generic argument
 * pin; connecting the last pin adopts its type and reveals the next spare.
 * Capped so the group cannot grow without bound. */
#define ED_FMT_TEXT_MAX_ARGS 12

static int ed_format_text_generic(const node *n, int index)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0 || index < 1 ||
        index >= n->input_count) {
        return 0;
    }
    const pin_spec *pin = &n->input_pins[index];
    return pin->kind == PIN_DATA && pin->type == PIN_FLOW &&
           (pin->flags & PIN_FLAG_WILDCARD);
}

static void ed_append_generic_arg(editor *e, node *n)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0) {
        return;
    }
    if (n->input_count - 1 >= ED_FMT_TEXT_MAX_ARGS) {
        return;
    }
    char name[32];
    snprintf(name, sizeof name, "Arg%d", n->input_count - 1);
    pin_spec pin = { .name = name, .type = PIN_FLOW, .kind = PIN_DATA, .flags = PIN_FLAG_WILDCARD };
    graph_add_pin(e->active_graph, n, 0, &pin);
}

static void ed_format_text_repair(editor *e, node *n)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0) {
        return;
    }
    int generic_count = 0;
    for (int i = 1; i < n->input_count; i++) {
        generic_count += ed_format_text_generic(n, i);
    }
    for (int i = n->input_count - 1; i > 0 && generic_count > 1; i--) {
        if (!ed_format_text_generic(n, i)) {
            continue;
        }
        if (graph_remove_pin(e->active_graph, n, 0, i) == 0) {
            generic_count--;
        }
    }
}

static void ed_format_text_ensure_spare(editor *e, node *n)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0) {
        return;
    }
    for (int i = 1; i < n->input_count; i++) {
        if (ed_format_text_generic(n, i)) {
            return;
        }
    }
    ed_append_generic_arg(e, n);
}

int ed_connection_specs_valid(const pin_spec *output, const pin_spec *input, pin_kind expected_kind)
{
    if ((output->flags | input->flags) & PIN_FLAG_SETTING) {
        return 0;
    }
    if (expected_kind == PIN_DATA && (input->flags & PIN_FLAG_REFERENCE)) {
        if (!(output->flags & PIN_FLAG_REFERENCE) ||
            (!(input->flags & PIN_FLAG_WILDCARD) &&
             (output->type != input->type || output->container != input->container))) {
            return 0;
        }
    }
    if (output->kind != expected_kind || input->kind != expected_kind) {
        return 0;
    }
    if (expected_kind != PIN_DATA || (output->flags & PIN_FLAG_WILDCARD) ||
        (input->flags & PIN_FLAG_WILDCARD) || pin_types_compatible(output, input)) {
        return 1;
    }
    return conversion_for_pins(output, input) != NULL;
}

int ed_connect(editor *e, const ed_pin_view *a, const ed_pin_view *b)
{
    return ed_connect_with_inline(e, a, b, true);
}

int ed_connect_with_inline(editor *e, const ed_pin_view *a, const ed_pin_view *b, bool inline_source)
{
    ed_clear_connect_error(e);
    const ed_pin_view *output = a->kind == ED_PIN_OUTPUT ? a : b;
    const ed_pin_view *input = a->kind == ED_PIN_INPUT ? a : b;
    if (output->kind != ED_PIN_OUTPUT || input->kind != ED_PIN_INPUT) {
        return 0;
    }
    const pin_spec *out_spec = &output->n->output_pins[output->index];
    const pin_spec *in_spec = &input->n->input_pins[input->index];
    const char *out_name = ed_pin_type_name(out_spec);
    const char *in_name = ed_pin_type_name(in_spec);
    if (!ed_connection_specs_valid(out_spec, in_spec, out_spec->kind)) {
        snprintf(e->connect_error, sizeof e->connect_error, "Cannot connect %s output to %s input", out_name, in_name);
        e->connect_error_until = e->monotonic_time + ED_TOAST_DURATION;
        return 0;
    }
    /* PIN-11: a Generic data input accepts any data peer and adopts its
     * type in core; it is not a type mismatch and never toasts. */
    int generic_target = out_spec->kind == PIN_DATA &&
                         (in_spec->flags & PIN_FLAG_WILDCARD);
    if (out_spec->kind == PIN_DATA && !generic_target &&
        (!(out_spec->flags & PIN_FLAG_WILDCARD) ||
         (in_spec->type == PIN_STR && conversion_for_pins(out_spec, in_spec))) &&
        !pin_types_compatible(out_spec, in_spec)) {
        const node_desc *conv = conversion_for_pins(out_spec, in_spec);
        assert(conv);
        int connected = graph_connect_data_conversion(
                            e->active_graph,
                            output->n,
                            output->index,
                            input->n,
                            input->index,
                            conv
                        ) == 0;
        if (connected && inline_source) {
            (void)ed_simplify_component_getter(
                e,
                output->n,
                output->index,
                input->n,
                input->index,
                conv
            );
        }
        return connected;
    }
    if (out_spec->kind == PIN_EXEC) {
        int order = output->n->desc->flow_kind == NODE_FLOW_SEQUENCE ? output->index
                                                                     : 0;
        return graph_connect_exec(
                   e->active_graph,
                   output->n,
                   output->index,
                   input->n,
                   input->index,
                   order
               ) == 0;
    }
    node *old_source = NULL;
    int old_source_pin = -1;
    const pin_binding *old_binding = NULL;
    pin_binding old_binding_copy = { 0 };
    const node_desc *old_conversion = NULL;
    bool reference_input = in_spec->flags & PIN_FLAG_REFERENCE;
    char *reference_before = reference_input ? ed_snapshot(e) : NULL;
    if (reference_input) {
        old_binding = graph_pin_binding(e->active_graph, input->n, 0, input->index);
        if (old_binding) {
            old_binding_copy = *old_binding;
        }
        if (graph_input_source(e->active_graph, input->n, input->index, &old_source, &old_source_pin)) {
            old_conversion = graph_input_conversion(
                e->active_graph,
                input->n,
                input->index
            );
        }
    }
    int ok = graph_connect_data(
                 e->active_graph,
                 output->n,
                 output->index,
                 input->n,
                 input->index
             ) == 0;
    if (!ok) {
        if (reference_before) {
            (void)ed_adopt_behavior_asset(e, reference_before);
            free(reference_before);
        }
        return 0;
    }
    if (ok && generic_target) {
        ed_format_text_ensure_spare(e, input->n);
    }
    if (ok) {
        node *expression = (output->n->desc == &NODE_CallPureFunction ||
                            output->n->desc == &NODE_BehaviorExpression)
                             ? output->n
                             : NULL;
        if (!expression && (input->n->desc == &NODE_CallPureFunction ||
                            input->n->desc == &NODE_BehaviorExpression)) {
            expression = input->n;
        }
        if (expression &&
            behavior_asset_find_graph(e->asset, expression->reference_id) &&
            behavior_asset_find_graph(e->asset, expression->reference_id)->math_expression) {
            behavior_expression_error error;
            if (behavior_expression_refresh(e->asset, expression, &error) != 0) {
                if (reference_before) {
                    (void)ed_adopt_behavior_asset(e, reference_before);
                } else if (reference_input) {
                    (void)graph_remove_edge(
                        e->active_graph,
                        output->n,
                        output->index,
                        input->n,
                        input->index
                    );
                    if (old_source) {
                        (void)graph_connect_data_conversion(
                            e->active_graph,
                            old_source,
                            old_source_pin,
                            input->n,
                            input->index,
                            old_conversion
                        );
                    }
                    if (old_binding) {
                        (void)graph_set_pin_binding(
                            e->active_graph,
                            input->n,
                            0,
                            input->index,
                            &old_binding_copy
                        );
                    }
                }
                e->expression_error = error;
                free(reference_before);
                return 0;
            }
        }
        if (inline_source && !reference_input) {
            (void)ed_simplify_component_getter(
                e,
                output->n,
                output->index,
                input->n,
                input->index,
                NULL
            );
        }
    }
    free(reference_before);
    return ok;
}

static bool ed_expression_call_stale(const graph *caller, const node *call)
{
    for (int i = 0; i < call->input_count; i++) {
        if (call->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        node *source;
        int source_pin;
        if (!graph_input_source(caller, call, i, &source, &source_pin)) {
            continue;
        }
        const pin_spec *output = &source->output_pins[source_pin];
        const pin_spec *input = &call->input_pins[i];
        if (input->flags & PIN_FLAG_REFERENCE) {
            if (!(output->flags & PIN_FLAG_REFERENCE) ||
                (!(input->flags & PIN_FLAG_WILDCARD) &&
                 !pin_types_compatible(output, input))) {
                return true;
            }
            pin_binding binding = { 0 };
            if (graph_resolve_reference((graph *)caller, source, source_pin, &binding) !=
                0) {
                return true;
            }
            continue;
        }
        const node_desc *conversion = graph_input_conversion(caller, call, i);
        if (conversion ? conversion_matches_pins(output, input, conversion)
                       : pin_types_compatible(output, input)) {
            continue;
        }
        return true;
    }
    for (int i = 0; i < call->input_count; i++) {
        if ((call->input_pins[i].flags & PIN_FLAG_REFERENCE) &&
            call->input_pins[i].binding) {
            return true;
        }
    }
    return false;
}

void ed_refresh_stale_behavior_expressions(editor *e)
{
    /* Loading or an older edit can leave a direct edge on a generic expression
     * input after its public pin has fallen back to double. Repair that
     * boundary before the read-only compiler pass reports it. */
    for (int i = 0; i < acount(e->asset->graphs); i++) {
        graph *caller = e->asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *call = caller->nodes[j];
            if (call->desc != &NODE_CallPureFunction &&
                call->desc != &NODE_BehaviorExpression) {
                continue;
            }
            graph *generated = behavior_asset_find_graph(e->asset, call->reference_id);
            if (!generated || !generated->math_expression ||
                !ed_expression_call_stale(caller, call)) {
                continue;
            }
            behavior_expression_error error = { 0 };
            if (behavior_expression_refresh(e->asset, call, &error) != 0 &&
                error.message[0]) {
                e->expression_error = error;
            }
        }
    }
}
