#include "editor/graph/eval_view.h"
#include "editor/ui/labels.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"

#include <assert.h>

static void ed_log_sink(const node *origin, const char *message, void *user);

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const flow_run *ed_display_run(const editor *e)
{
    return ed_inspection_blocked(e) || !e->inspection_instance ? NULL : e->inspection_run;
}

/* Authoring edits mutate the working Behavior Asset, not the preview snapshot. */

void ed_compile(editor *e)
{
    ed_refresh_stale_behavior_expressions(e);
    compile_report_free(&e->compile);
    compile_report_init(&e->compile);
    behavior_asset_free(e->compiled);
    e->compiled = behavior_asset_validate(e->asset, e->asset_catalog, acount(e->asset_catalog), &e->compile);
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
    e->preview_behavior_asset = behavior_asset_clone(e->asset);
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
}

void editor_preview_reset(editor *e)
{
    if (e) {
        ed_destroy_preview(e);
    }
}

static void ed_refresh_edit_revision(editor *e)
{
    if (e->edit_revision_initialized && !e->edit_pending) {
        return;
    }
    av_value *snapshot = ed_value_snapshot(e->asset);
    if (!snapshot) {
        return;
    }
    int changed = e->edit_revision_initialized
                    ? e->edit_revision_asset != e->asset ||
                          !e->edit_revision_snapshot ||
                          !av_equals_ordered(e->edit_revision_snapshot, snapshot)
                    : e->edit_pending;
    av_value_free(e->edit_revision_snapshot);
    e->edit_revision_snapshot = snapshot;
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
    if (e->instance) {
        (void)behavior_instance_tick(e->instance, monotonic_time);
    }
}

void editor_set_inspection_instance(editor *e, const behavior_instance *instance)
{
    if (!e) {
        return;
    }
    if (instance != e->inspection_instance) {
        e->flow_activity_epoch++;
    }
    if (!instance && e->inspection_instance) {
        e->inspection_trace = NULL;
        e->inspection_trace_count = 0;
    }
    e->inspection_instance = instance;
}

void editor_set_inspection_blocked(editor *e, bool blocked)
{
    if (e->inspection_blocked != blocked) {
        e->flow_activity_epoch++;
    }
    e->inspection_blocked = blocked;
    if (!blocked) {
        return;
    }
    e->inspection_instance = NULL;
    e->inspection_run = NULL;
    e->inspection_trace = NULL;
    e->inspection_trace_count = 0;
    eval_view_set_instance(e->ev, NULL);
    eval_view_set_run(e->ev, NULL);
}

void editor_set_inspection_run(editor *e, const flow_run *run)
{
    if (!e) {
        return;
    }
    if (e->inspection_run != run) {
        e->flow_activity_epoch++;
    }
    e->inspection_run = run;
}

void editor_set_inspection_trace_revision(
    editor *e,
    const behavior_instance_trace_entry *trace,
    int count,
    uint64_t revision
)
{
    if (!e) {
        return;
    }
    e->inspection_trace = trace;
    e->inspection_trace_count = count > 0 ? count : 0;
    e->inspection_trace_revision = revision;
}

void editor_set_inspection_trace(editor *e, const behavior_instance_trace_entry *trace, int count)
{
    if (!e) {
        return;
    }
    e->flow_activity_epoch++;
    editor_set_inspection_trace_revision(e, trace, count, 0);
}

int editor_log_count(const editor *e)
{
    return e->log_count;
}

editor_log_line editor_log_at(const editor *e, int index, char *origin, size_t origin_size)
{
    assert(index >= 0 && index < e->log_count);
    const ed_log_entry *entry = &e->log[index];
    snprintf(origin, origin_size, "%s", "Unknown");
    if (entry->origin && entry->origin->desc) {
        editor_node_label(entry->origin->desc, origin, origin_size);
    }
    return (editor_log_line){ entry->timestamp, entry->step, entry->message };
}

void editor_clear_log(editor *e)
{
    e->log_count = 0;
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
    const flow_run *run = ed_display_run(e);
    entry->timestamp = e->monotonic_time;
    entry->step = run ? flow_run_steps(run) : 0;
    snprintf(entry->message, sizeof entry->message, "%s", message ? message : "");
}
