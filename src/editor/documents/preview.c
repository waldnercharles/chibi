#include "editor/graph/eval_view.h"
#include "editor/graph/preview_gate.h"
#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "support/xalloc.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

static void ed_log_sink(const node *origin, const char *message, void *user);

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ed_activation_live(const editor *e)
{
    return e && e->activation &&
           !editor_preview_allowed(flow_run_status(e->activation));
}

const flow_run *ed_display_run(const editor *e)
{
    if (ed_inspection_blocked(e)) {
        return NULL;
    }
    return e->inspection_instance ? e->inspection_run
                                  : (ed_activation_live(e) ? e->activation : NULL);
}

/* Authoring edits mutate the working Behavior Asset, not the preview snapshot. */

static const char *ed_flow_state_name(flow_state state)
{
    switch (state) {
        case FLOW_RUNNING: return "RUNNING";
        case FLOW_SUSPENDED: return "SUSPENDED";
        case FLOW_PAUSED: return "PAUSED";
        case FLOW_COMPLETED: return "COMPLETED";
        case FLOW_FAILED: return "FAILED";
        case FLOW_IDLE:
        default: return "IDLE";
    }
}

static node *ed_entry_at(const editor *e, int index)
{
    int at = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (n->desc->flow_kind != NODE_FLOW_EVENT) {
            continue;
        }
        if (at++ == index) {
            return n;
        }
    }
    return NULL;
}

static int ed_entry_count(const editor *e)
{
    int count = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        if (e->active_graph->nodes[i]->desc->flow_kind == NODE_FLOW_EVENT) {
            count++;
        }
    }
    return count;
}

static void ed_dispatch_selected(editor *e)
{
    if (e->active_graph != e->asset->graph) {
        return;
    }
    node *entry = ed_entry_at(e, e->entry_index);
    if (!entry || !entry->reference_name) {
        return;
    }
    flow_error error = { 0 };
    flow_run *act = editor_dispatch(e, entry->reference_name, (flow_inputs){ 0 }, &error);
    if (!act) {
        char message[ED_LOG_MESSAGE];
        snprintf(
            message,
            sizeof message,
            "Dispatch %s failed: %s",
            entry->reference_name,
            error.message ? error.message : "unknown error"
        );
        ed_show_error(e, message);
    }
}

static void ed_run_pause(editor *e)
{
    if (e->activation) {
        (void)flow_run_pause(e->activation);
    }
}
void ed_compile(editor *e)
{
    ed_refresh_stale_behavior_expressions(e);
    compile_report_free(&e->compile);
    compile_report_init(&e->compile);
    char message[256];
    behavior_asset *linked = behavior_asset_link_libraries(
        e->asset,
        e->asset_catalog,
        acount(e->asset_catalog),
        message,
        sizeof message
    );
    if (linked) {
        behavior_asset_compile(linked, &e->compile);
        behavior_asset_free(linked);
    } else {
        compile_diagnostic diagnostic = { .severity = CMPL_ERROR,
                                          .rule = CMPL_RULE_MISSING_REFERENCE };
        snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message[0] ? message : "Could not link project definitions.");
        apush(e->compile.items, diagnostic);
        e->compile.error_count++;
    }
    ed_rebuild_compile_text(e);
    if (acount(e->compile.items)) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_COMPILER_RESULTS, true);
    }
}

static void ed_run_resume(editor *e)
{
    if (e->activation) {
        (void)flow_run_resume(e->activation);
    }
}
static void ed_run_step(editor *e)
{
    if (e->activation) {
        (void)flow_run_step(e->activation);
    }
}
static void ed_run_step_over(editor *e)
{
    if (e->activation) {
        (void)flow_run_step_over(e->activation);
    }
}
static void ed_run_reveal(editor *e)
{
    const node *current = flow_run_current_node(e->activation);
    if (!current) {
        return;
    }
    int count = 0;
    const flow_call_frame *stack = flow_run_call_stack(e->activation, &count);
    for (int i = count - 1; i >= 0; i--) {
        if (graph_node_by_id(stack[i].graph, current->id) == current) {
            const graph *runtime = stack[i].graph;
            uint64_t graph_id = runtime->graph_id;
            uint64_t node_id = current->id;
            if (runtime->source_asset) {
                node_id -= runtime->graph_id - runtime->source_graph_id;
                graph_id = runtime->source_graph_id;
                if (strcmp(runtime->source_asset, e->asset->name) != 0) {
                    if (!e->open_asset) {
                        return;
                    }
                    char *name = strdup(runtime->source_asset);
                    int result = e->open_asset(e->open_asset_user, name, graph_id);
                    free(name);
                    if (result != 0) {
                        return;
                    }
                }
            }
            editor_reveal_node_by_id(e, graph_id, node_id);
            return;
        }
    }
}

static void ed_run_stop(editor *e)
{
    behavior_instance *target = e->inspection_instance
                                  ? (behavior_instance *)e->inspection_instance
                                  : e->instance;
    if (!target || !e->activation) {
        return;
    }
    (void)behavior_instance_stop_activation(target, e->activation);
    eval_view_set_run(e->ev, NULL);
}

static void ed_debug_action(editor *e, const char *label, int enabled, void (*action)(editor *))
{
    bool clicked = ImGui_MenuItemEx(label, NULL, false, enabled);
    if (clicked) {
        action(e);
    }
}

void ed_draw_debug_menu(editor *e)
{
    int entries = ed_entry_count(e);
    if (e->entry_index >= entries) {
        e->entry_index = entries ? entries - 1 : 0;
    }
    node *entry = ed_entry_at(e, e->entry_index);
    const char *entry_name = entry && entry->reference_name ? entry->reference_name
                                                            : "(unnamed event)";
    ImGui_SetNextItemWidth(200.0f);
    if (ImGui_BeginCombo("Event", entry_name, 0)) {
        int at = 0;
        for (int i = 0; i < acount(e->active_graph->nodes); i++) {
            node *n = e->active_graph->nodes[i];
            if (n->desc->flow_kind != NODE_FLOW_EVENT) {
                continue;
            }
            char label[160];
            snprintf(
                label,
                sizeof label,
                "%s##entry_%d",
                n->reference_name ? n->reference_name : "(unnamed event)",
                at
            );
            if (ImGui_SelectableEx(label, at == e->entry_index, 0, (ImVec2){ 0.0f, 0.0f })) {
                e->entry_index = at;
            }
            at++;
        }
        ImGui_EndCombo();
    }
    const behavior_instance *target_instance = ed_display_instance(e);
    int attached = target_instance != NULL;
    if (target_instance) {
        ImGui_TextUnformatted(
            behavior_backend_name(behavior_instance_backend(target_instance))
        );
        const behavior_instance_failure *failure = behavior_instance_last_failure(target_instance);
        if (failure) {
            ImGui_TextWrapped("Last failure: %s", failure->message);
            ImGui_TextDisabled(
                "%s / %s / node %llu / pin %d",
                failure->event_name,
                failure->graph_name,
                (unsigned long long)failure->node_id,
                failure->pin
            );
        }
    }
    int can_dispatch = !ed_inspection_blocked(e) &&
                       e->active_graph == e->asset->graph && entry &&
                       entry->reference_name;
    ed_debug_action(e, "Dispatch", can_dispatch, ed_dispatch_selected);
    flow_state state = !ed_inspection_blocked(e) && e->activation
                         ? flow_run_status(e->activation)
                         : FLOW_IDLE;
    int can_pause = state == FLOW_RUNNING || state == FLOW_SUSPENDED;
    int can_resume = state == FLOW_PAUSED || state == FLOW_SUSPENDED;
    int can_step = can_resume;
    int can_stop = attached && state != FLOW_IDLE;
    ed_debug_action(e, "Pause", can_pause, ed_run_pause);
    ed_debug_action(e, "Resume", can_resume, ed_run_resume);
    ed_debug_action(e, "Step", can_step, ed_run_step);
    ed_debug_action(e, "Step Over", can_step, ed_run_step_over);
    ed_debug_action(e, "Stop", can_stop, ed_run_stop);
    ed_debug_action(e, "Show Current Node", flow_run_current_node(e->activation) != NULL, ed_run_reveal);
    ImGui_Separator();
    if (attached) {
        ImGui_Text(
            "Runtime: attached  Activation: %s  Steps: %lu",
            ed_flow_state_name(state),
            e->activation ? flow_run_steps(e->activation) : 0
        );
    } else if (e->instance_destroyed) {
        ImGui_TextColored((ImVec4){ 1.0f, 0.6f, 0.3f, 1.0f }, "Runtime snapshot reset");
    } else {
        ImGui_Text("Runtime: none  Activation: IDLE  Steps: 0");
    }
}

behavior_instance *editor_preview_instance(const editor *e)
{
    return e ? e->instance : NULL;
}

unsigned editor_preview_generation(const editor *e)
{
    return e ? e->instance_generation : 0;
}

unsigned editor_edit_revision(const editor *e)
{
    return e ? e->edit_revision : 0;
}

bool editor_has_pending_edits(const editor *e)
{
    return e && e->edit_pending;
}

void editor_undo(editor *e)
{
    if (e) {
        ed_undo(e);
    }
}

void editor_redo(editor *e)
{
    if (e) {
        ed_redo(e);
    }
}

void editor_preview_create(editor *e)
{
    if (!e || e->instance || e->inspection_instance) {
        return;
    }
    char *json = behavior_asset_save_json(e->asset);
    e->preview_behavior_asset = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (e->preview_behavior_asset &&
        behavior_asset_lower_component_nodes(e->preview_behavior_asset, NULL, 0)) {
        behavior_asset_free(e->preview_behavior_asset);
        e->preview_behavior_asset = NULL;
    }
    if (!e->preview_behavior_asset) {
        return;
    }
    e->instance = behavior_instance_create(e->preview_behavior_asset, NULL, NULL, NULL);
    if (!e->instance) {
        behavior_asset_free(e->preview_behavior_asset);
        e->preview_behavior_asset = NULL;
        return;
    }
    e->instance->sink = ed_log_sink;
    e->instance->sink_user = e;
    e->instance_generation++;
    e->instance_destroyed = 0;
    e->activation = NULL;
}

void editor_preview_reset(editor *e)
{
    if (e) {
        ed_destroy_preview(e);
    }
}

static flow_run *editor_dispatch_instance(
    editor *e,
    behavior_instance *target,
    const char *name,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!target) {
        return NULL;
    }
    flow_run *act = behavior_instance_dispatch(target, name, inputs, error);
    if (!act) {
        return NULL;
    }
    e->activation = act;
    e->run_started_at = e->monotonic_time;
    return act;
}

flow_run *editor_dispatch_preview(editor *e, const char *name, flow_inputs inputs, flow_error *error)
{
    if (!e) {
        return NULL;
    }
    if (!e->instance) {
        editor_preview_create(e);
    }
    return editor_dispatch_instance(e, e->instance, name, inputs, error);
}

flow_run *editor_dispatch(editor *e, const char *name, flow_inputs inputs, flow_error *error)
{
    if (!e) {
        return NULL;
    }
    if (ed_inspection_blocked(e)) {
        if (error) {
            *error = (flow_error){
                .code = FLOW_ERR_STATE,
                .message = "Debug target is unavailable or has unapplied edits"
            };
        }
        return NULL;
    }
    if (e->inspection_instance) {
        return editor_dispatch_instance(e, (behavior_instance *)e->inspection_instance, name, inputs, error);
    }
    return editor_dispatch_preview(e, name, inputs, error);
}

static void ed_refresh_edit_revision(editor *e)
{
    if (e->edit_revision_initialized && !e->edit_pending) {
        return;
    }
    char *json = behavior_asset_save_json(e->asset);
    if (!json) {
        return;
    }
    int changed = e->edit_revision_initialized
                    ? e->edit_revision_asset != e->asset ||
                          !e->edit_revision_snapshot ||
                          strcmp(e->edit_revision_snapshot, json) != 0
                    : e->edit_pending;
    free(e->edit_revision_snapshot);
    e->edit_revision_snapshot = json;
    e->edit_revision_asset = e->asset;
    e->edit_revision_initialized = 1;
    e->edit_pending = 0;
    if (changed) {
        e->edit_revision++;
    }
}

void editor_tick(editor *e, double monotonic_time)
{
    if (!e) {
        return;
    }
    ed_refresh_edit_revision(e);
    e->monotonic_time = monotonic_time;
    if (e->connect_error[0] && monotonic_time >= e->connect_error_until + ED_TOAST_FADE) {
        ed_clear_connect_error(e);
    }
    if (e->instance) {
        (void)behavior_instance_tick(e->instance, monotonic_time);
    }
}

void editor_set_inspection_instance(editor *e, const behavior_instance *instance)
{
    if (!e) {
        return;
    }
    if (!instance && e->inspection_instance) {
        e->activation = NULL;
        e->inspection_trace = NULL;
        e->inspection_trace_count = 0;
    }
    e->inspection_instance = instance;
    if (instance) {
        e->activation = (flow_run *)e->inspection_run;
    }
}

void editor_set_inspection_blocked(editor *e, bool blocked)
{
    e->inspection_blocked = blocked;
    if (!blocked) {
        return;
    }
    e->inspection_instance = NULL;
    e->inspection_run = NULL;
    e->inspection_trace = NULL;
    e->inspection_trace_count = 0;
    e->activation = NULL;
    eval_view_set_instance(e->ev, NULL);
    eval_view_set_run(e->ev, NULL);
}

void editor_set_inspection_run(editor *e, const flow_run *run)
{
    if (!e) {
        return;
    }
    e->inspection_run = run;
    if (e->inspection_instance) {
        e->activation = (flow_run *)run;
    }
}

void editor_set_inspection_trace(editor *e, const behavior_instance_trace_entry *trace, int count)
{
    if (!e) {
        return;
    }
    e->inspection_trace = trace;
    e->inspection_trace_count = count > 0 ? count : 0;
}

void editor_set_execution_pulse(editor *e, double monotonic_time)
{
    if (!e || !e->active_graph) {
        return;
    }
    e->execution_pulse_time = monotonic_time;
    e->execution_pulse_graph_id = e->active_graph->graph_id;
    e->execution_pulse_active = 1;
}

void ed_rebuild_log_text(editor *e)
{
    if (!e->log_count) {
        if (e->log_text) {
            e->log_text[0] = '\0';
        }
        return;
    }
    size_t capacity = (size_t)ED_LOG_MAX * ED_LOG_LINE + 1;
    if (e->log_text_capacity < capacity) {
        e->log_text = xrealloc(e->log_text, capacity);
        e->log_text_capacity = capacity;
    }
    size_t used = 0;
    for (int i = 0; i < e->log_count; i++) {
        const ed_log_entry *entry = &e->log[i];
        char origin_label[128] = "Unknown";
        if (entry->origin && entry->origin->desc) {
            editor_node_label(entry->origin->desc, origin_label, sizeof origin_label);
        }
        char line[ED_LOG_LINE];
        int written = snprintf(
            line,
            sizeof line,
            "[%8.3f | step %lu] %s: %s",
            entry->timestamp,
            entry->step,
            origin_label,
            entry->message
        );
        if (written < 0) {
            written = 0;
        }
        size_t length = (size_t)written;
        if (length >= sizeof line) {
            length = sizeof line - 1;
        }
        if (i) {
            e->log_text[used++] = '\n';
        }
        memcpy(e->log_text + used, line, length);
        used += length;
    }
    e->log_text[used] = '\0';
}

void ed_rebuild_compile_text(editor *e)
{
    int count = acount(e->compile.items);
    if (!count) {
        if (e->compile_text) {
            e->compile_text[0] = '\0';
        }
        return;
    }
    size_t needed = 1;
    for (int i = 0; i < count; i++) {
        const compile_diagnostic *item = &e->compile.items[i];
        const char *severity = cmpl_severity_name(item->severity);
        needed += strlen(severity) + 2 + strlen(item->message) + (i != 0);
    }
    if (e->compile_text_capacity < needed) {
        e->compile_text = xrealloc(e->compile_text, needed);
        e->compile_text_capacity = needed;
    }
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        const compile_diagnostic *item = &e->compile.items[i];
        const char *severity = cmpl_severity_name(item->severity);
        if (i) {
            e->compile_text[used++] = '\n';
        }
        int written = snprintf(
            e->compile_text + used,
            e->compile_text_capacity - used,
            "%s  %s",
            severity,
            item->message
        );
        assert(written >= 0);
        used += (size_t)written;
    }
    e->compile_text[used] = '\0';
}

static void ed_log_sink(const node *origin, const char *message, void *user)
{
    editor *e = (editor *)user;
    if (!e) {
        return;
    }
    if (e->log_count == ED_LOG_MAX) {
        memmove(e->log, e->log + 1, (size_t)(ED_LOG_MAX - 1) * sizeof *e->log);
    }
    ed_log_entry *entry = &e->log[e->log_count < ED_LOG_MAX ? e->log_count++ : ED_LOG_MAX - 1];
    entry->origin = origin;
    entry->timestamp = e->monotonic_time - e->run_started_at;
    entry->step = e->activation ? flow_run_steps(e->activation) : 0;
    snprintf(entry->message, sizeof entry->message, "%s", message ? message : "");
    ed_rebuild_log_text(e);
}

void editor_select_node(editor *e, node *n)
{
    if (e) {
        ed_select_node(e, n, 0);
    }
}

void editor_reveal_node(editor *e, node *n)
{
    if (!e || !n) {
        return;
    }
    n = graph_inline_owner(e->active_graph, n);
    ed_select_node(e, n, 0);
    e->reveal_target = n;
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
}

int editor_reveal_node_by_id(editor *e, uint64_t graph_id, uint64_t node_id)
{
    if (!e || editor_open_graph(e, graph_id) != 0) {
        return -1;
    }
    node *n = graph_node_by_id(e->active_graph, node_id);
    if (!n) {
        return -1;
    }
    editor_reveal_node(e, n);
    return 0;
}
