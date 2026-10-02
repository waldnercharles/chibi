#include "editor/graph/flow_visual.h"
#include "editor/workspace/editor_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool ed_inspection_blocked(const editor *e)
{
    return e->inspection_blocked ||
           (e->inspection_instance && (e->edit_pending || e->pending_snapshot));
}

const behavior_instance *ed_display_instance(const editor *e)
{
    return ed_inspection_blocked(e)
             ? NULL
             : (e->inspection_instance ? e->inspection_instance : e->instance);
}

typedef struct
{
    bool used;
    flow_traversal record;
} activity_slot;

typedef struct ed_flow_activity_index
{
    activity_slot *slots;
    int capacity;
    bool valid;
    const flow_run *run;
    const behavior_instance *instance;
    const behavior_instance_trace_entry *trace;
    const graph *graph;
    int trace_count;
    uint64_t run_revision, trace_revision, epoch;
    uint64_t records_indexed;
} ed_flow_activity_index;

static uint64_t activity_hash(const flow_traversal *r)
{
    uint64_t key[] = { r->from_id, r->from_pin_id, r->to_id, r->to_pin_id, r->kind };
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 5; i++) {
        hash = (hash ^ key[i]) * 1099511628211ull;
    }
    return hash;
}

static bool activity_matches(const flow_traversal *a, const flow_traversal *b)
{
    return a->kind == b->kind && a->from_id == b->from_id &&
           a->from_pin_id == b->from_pin_id && a->to_id == b->to_id &&
           a->to_pin_id == b->to_pin_id;
}

static activity_slot *activity_find(ed_flow_activity_index *index, const flow_traversal *record)
{
    int mask = index->capacity - 1;
    int slot = (int)(activity_hash(record) & (uint64_t)mask);
    while (index->slots[slot].used &&
           !activity_matches(&index->slots[slot].record, record)) {
        slot = (slot + 1) & mask;
    }
    return &index->slots[slot];
}

static void activity_add(ed_flow_activity_index *index, const flow_traversal *record)
{
    activity_slot *slot = activity_find(index, record);
    if (!slot->used || record->timestamp > slot->record.timestamp) {
        slot->record = *record;
        slot->used = true;
    }
    index->records_indexed++;
}

void ed_flow_activity_free(editor *e)
{
    if (e->flow_activity) {
        free(e->flow_activity->slots);
        free(e->flow_activity);
        e->flow_activity = NULL;
    }
}

uint64_t ed_flow_activity_records_indexed(const editor *e)
{
    return e->flow_activity ? e->flow_activity->records_indexed : 0;
}

static ed_flow_activity_index *activity_index(const editor *e)
{
    editor *owner = (editor *)e; /* Queries borrow a lazily derived view. */
    if (!owner->flow_activity) {
        owner->flow_activity = xcalloc(1, sizeof *owner->flow_activity);
    }
    ed_flow_activity_index *index = owner->flow_activity;
    const flow_run *run = ed_display_run(e);
    uint64_t run_revision = flow_run_traversal_revision(run);
    const behavior_instance_trace_entry *trace = e->inspection_instance
                                                   ? e->inspection_trace
                                                   : NULL;
    int trace_count = trace ? e->inspection_trace_count : 0;
    if (index->valid && index->run == run && index->run_revision == run_revision &&
        index->instance == e->inspection_instance && index->trace == trace &&
        index->trace_count == trace_count &&
        index->trace_revision == e->inspection_trace_revision &&
        index->graph == e->active_graph && index->epoch == e->flow_activity_epoch) {
        return index;
    }
    int count = 0;
    const flow_traversal *records = flow_run_traversals(run, &count);
    int capacity = 16;
    while (capacity < 2 * (count + trace_count)) {
        capacity *= 2;
    }
    if (capacity > index->capacity) {
        index->slots = xrealloc(index->slots, (size_t)capacity * sizeof *index->slots);
        index->capacity = capacity;
    }
    memset(index->slots, 0, (size_t)index->capacity * sizeof *index->slots);
    /* Reverse order preserves the existing tie preference: current run first,
     * then retained history; only a strictly newer timestamp replaces it. */
    for (int i = count - 1; i >= 0; i--) {
        activity_add(index, &records[i]);
    }
    for (int i = trace_count - 1; i >= 0; i--) {
        activity_add(index, &trace[i].traversal);
    }
    index->run = run;
    index->run_revision = run_revision;
    index->instance = e->inspection_instance;
    index->trace = trace;
    index->trace_count = trace_count;
    index->trace_revision = e->inspection_trace_revision;
    index->graph = e->active_graph;
    index->epoch = e->flow_activity_epoch;
    index->valid = true;
    return index;
}

static const flow_traversal *latest_flow_record(const editor *e, const edge *ed)
{
    if (ed_inspection_blocked(e)) {
        return NULL;
    }
    ed_flow_activity_index *index = activity_index(e);
    flow_traversal key = {
        .from_id = ed->from_id,
        .from_pin_id = ed->from_pin_id,
        .to_id = ed->to_id,
        .to_pin_id = ed->to_pin_id,
        .kind = ed->kind,
    };
    activity_slot *slot = activity_find(index, &key);
    return slot->used ? &slot->record : NULL;
}

void editor_query_edge_activity(const editor *e, const edge *edge, editor_edge_activity *out)
{
    assert(e && edge && out);
    const flow_traversal *record = latest_flow_record(e, edge);
    *out = (editor_edge_activity){
        .traversed = record != NULL,
        .timestamp = record ? record->timestamp : 0,
    };
    if (edge->kind != GRAPH_EDGE_EXEC || !record) {
        return;
    }
    out->opacity = (float)flow_visual_heat(fmax(0.0, e->monotonic_time - record->timestamp));
}
