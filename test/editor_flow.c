#include "editor/workspace/editor.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "test/support/report.h"

#include <assert.h>
#include <stdio.h>

static int finish(node *, struct flow_context *)
{
    return FLOW_OK;
}

static pin_spec exec[] = { { "Exec", PIN_FLOW, NULL, PIN_EXEC } };
static node_desc sink = {
    .name = "Activity sink",
    .inputs = exec,
    .input_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = finish,
};

static editor_edge_activity activity(editor *editor, const edge *edge)
{
    editor_edge_activity result;
    editor_query_edge_activity(editor, edge, &result);
    return result;
}

void test_editor_flow()
{
    behavior_asset *asset = behavior_asset_new("Flow activity");
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    node *branch = graph_add_node(g, &NODE_Branch);
    node *condition = graph_add_node(g, registry_node_by_name("Bool"));
    assert(graph_set_pin_watch(g, condition, 1, 0, true) == 0);
    node *yes = graph_add_node(g, &sink);
    node *no = graph_add_node(g, &sink);
    assert(graph_set_event_name(g, event, "Input") == 0);
    assert(
        graph_set_pin_default(g, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = true }) == 0
    );
    assert(graph_connect_exec(g, event, 0, branch, 0, 0) == 0);
    assert(graph_connect_data(g, condition, 0, branch, 1) == 0);
    assert(graph_connect_exec(g, branch, 0, yes, 0, 0) == 0);
    assert(graph_connect_exec(g, branch, 1, no, 0, 0) == 0);
    const edge *data = &g->edges[1];
    const edge *taken = &g->edges[2];
    const edge *untaken = &g->edges[3];
    editor *editor = editor_new(asset);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(behavior_instance_tick(instance, 10) >= 0);
    flow_error error;
    flow_run *run = behavior_instance_dispatch(instance, "Input", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    editor_set_inspection_instance(editor, instance);
    editor_set_inspection_run(editor, run);
    editor_tick(editor, 10);

    editor_edge_activity result = activity(editor, taken);
    assert(result.traversed && result.timestamp == 10 && result.opacity > 0);
    result = activity(editor, untaken);
    assert(!result.traversed && result.opacity == 0);
    result = activity(editor, data);
    assert(result.traversed && result.timestamp == 10);
    assert(result.opacity == 0);
    pin_value value;
    assert(flow_run_watch_output(run, condition, 0, &value) == 0);
    assert(value.type == PIN_BOOL && value.b);

    int count = 0;
    const flow_traversal *records = flow_run_traversals(run, &count);
    flow_traversal record = { 0 };
    for (int i = 0; i < count; i++) {
        if (records[i].from_id == taken->from_id &&
            records[i].from_pin_id == taken->from_pin_id) {
            record = records[i];
        }
    }
    assert(record.from_id == taken->from_id);
    behavior_instance_trace_entry retained[] = { { .traversal = record },
                                                 { .traversal = record } };
    retained[0].traversal.timestamp = 12;
    retained[1].traversal.timestamp = 8;
    editor_set_inspection_trace(editor, retained, 2);
    editor_tick(editor, 12);
    result = activity(editor, taken);
    assert(result.timestamp == 12 && result.opacity > 0);
    retained[0].traversal.timestamp = 9;
    assert(activity(editor, taken).timestamp == 10);

    editor_set_inspection_run(editor, NULL);
    assert(activity(editor, taken).timestamp == 9);
    assert(activity(editor, untaken).opacity == 0);
    editor_set_inspection_instance(editor, NULL);
    assert(activity(editor, data).opacity == 0);

    editor_set_inspection_blocked(editor, true);
    editor_query query;
    editor_query_state(editor, &query);
    assert(query.inspection_blocked);
    assert(activity(editor, taken).opacity == 0);
    assert(activity(editor, untaken).opacity == 0);
    assert(activity(editor, data).opacity == 0);
    assert(flow_run_watch_output(run, condition, 0, &value) == 0 && value.b);
    editor_set_inspection_blocked(editor, false);

    behavior_instance_destroy(instance);
    editor_free(editor);
    test_note("editor flow PASS: exec-only activity, untaken branches, data values and newest trace\n");
}
