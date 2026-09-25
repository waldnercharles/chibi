#include "editor/graph/flow_visual.h"
#include "editor/workspace/editor_internal.h"

#include <assert.h>
#include <math.h>

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

static int ed_flow_edge_matches(const flow_traversal *record, const edge *ed)
{
    return record->kind == ed->kind && record->from_id == ed->from_id &&
           record->from_pin_id == ed->from_pin_id &&
           record->to_id == ed->to_id && record->to_pin_id == ed->to_pin_id;
}

static const flow_traversal *latest_flow_record(const editor *e, const edge *ed)
{
    if (ed_inspection_blocked(e)) {
        return NULL;
    }
    const flow_traversal *latest = NULL;
    const flow_run *run = ed_display_run(e);
    if (run) {
        int count = 0;
        const flow_traversal *records = flow_run_traversals(run, &count);
        for (int i = count - 1; i >= 0; i--) {
            if (ed_flow_edge_matches(&records[i], ed) &&
                (!latest || records[i].timestamp > latest->timestamp)) {
                latest = &records[i];
            }
        }
    }
    if (!e->inspection_instance) {
        return latest;
    }
    for (int i = e->inspection_trace_count - 1; i >= 0; i--) {
        const flow_traversal *record = &e->inspection_trace[i].traversal;
        if (ed_flow_edge_matches(record, ed) &&
            (!latest || record->timestamp > latest->timestamp)) {
            latest = record;
        }
    }
    return latest;
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
