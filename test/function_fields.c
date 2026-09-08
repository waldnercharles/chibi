#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "runtime/components.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    behavior_asset *asset;
    uint64_t function;
    uint64_t call;
    uint64_t entry_field;
    uint64_t call_field;
    bool executable;
    bool delayed;
} fixture;

static int field(node *n, int source, const char *name)
{
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].field_source == n->output_pins[source].id &&
            strcmp(n->output_pins[i].field_name, name) == 0) {
            return i;
        }
    }
    assert(false);
    return -1;
}

static void compile(behavior_asset *asset)
{
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; errors && i < acount(report.items); i++) {
        fprintf(stderr, "function fields: %s\n", report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
}

static fixture make_fixture(bool executable, bool delayed)
{
    fixture f = { .asset = behavior_asset_new("Function fields"),
                  .executable = executable,
                  .delayed = delayed };
    graph *g = behavior_asset_add_function(f.asset, "Coordinates", !executable);
    f.function = g->graph_id;
    node *entry = graph_node_by_id(g, g->entry_id);
    node *ret = graph_node_by_id(g, g->return_id);
    int base = executable ? 1 : 0;
    pin_spec vector = { "Position", PIN_STRUCT, "CF_V2", PIN_DATA };
    pin_spec extra = { "Height", PIN_F32, "float", PIN_DATA };
    assert(graph_add_pin(g, entry, 1, &vector) >= 0);
    assert(graph_expose_output_fields(g, entry, base) == 0);
    int x = field(entry, base, "x");
    f.entry_field = entry->output_pins[x].id;
    assert(graph_add_pin(g, entry, 1, &extra) >= 0);
    int height = entry->output_count - 1;
    assert(entry->output_pins[height].field_source == 0);
    assert(graph_add_pin(g, ret, 0, &vector) >= 0);
    vector.name = "Original";
    assert(graph_add_pin(g, ret, 0, &vector) >= 0);
    node *make = graph_add_node(g, registry_node_by_name("Make_CF_V2"));
    assert(make);
    assert(graph_connect_data(g, entry, x, make, 0) == 0);
    assert(graph_connect_data(g, entry, height, make, 1) == 0);
    assert(graph_connect_data(g, make, 0, ret, base) == 0);
    assert(graph_connect_data(g, entry, base, ret, base + 1) == 0);
    if (executable) {
        if (delayed) {
            node *delay = graph_add_node(g, &NODE_Delay);
            pin_value duration = { .type = PIN_F64, .type_name = "double", .d = 0.25 };
            assert(graph_set_pin_default(g, delay, 0, 1, &duration) == 0);
            graph_connect_exec(g, entry, 0, delay, 0, 0);
            graph_connect_exec(g, delay, 0, ret, 0, 0);
        } else {
            graph_connect_exec(g, entry, 0, ret, 0, 0);
        }
    }
    node *call = behavior_asset_add_function_call_node(f.asset, g);
    f.call = call->id;
    assert(call->input_count == base + 2 && call->output_count == base + 2);
    CF_V2 *position = graph_own_block(f.asset->graph, sizeof *position);
    *position = (CF_V2){ 3, 4 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = position };
    assert(graph_set_pin_default(f.asset->graph, call, 0, base, &value) == 0);
    value = (pin_value){ .type = PIN_F32, .type_name = "float", .f = 9 };
    assert(graph_set_pin_default(f.asset->graph, call, 0, base + 1, &value) == 0);
    assert(graph_expose_output_fields(f.asset->graph, call, base) == 0);
    f.call_field = call->output_pins[field(call, base, "x")].id;
    node *consumer = graph_add_node(f.asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(graph_connect_data(f.asset->graph, call, field(call, base, "x"), consumer, 0) == 0);
    if (executable) {
        node *event = graph_add_node(f.asset->graph, &NODE_Event);
        assert(graph_set_event_name(f.asset->graph, event, "Go") == 0);
        graph_connect_exec(f.asset->graph, event, 0, call, 0, 0);
    }
    compile(f.asset);
    return f;
}

static void verify(fixture f)
{
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, NULL, NULL);
    assert(instance);
    node *call = graph_node_by_id(f.asset->graph, f.call);
    int base = f.executable ? 1 : 0;
    arena memory;
    arena_init(&memory, 16);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    flow_run *run = NULL;
    if (f.executable) {
        flow_error error;
        run = behavior_instance_dispatch(instance, "Go", (flow_inputs){ 0 }, &error);
        assert(run);
        if (f.delayed) {
            assert(flow_run_status(run) == FLOW_SUSPENDED);
            assert(flow_run_tick_at(run, 0.25) == FLOW_OK);
        }
        assert(flow_run_status(run) == FLOW_COMPLETED);
    }
    int outputs[] = { field(call, base, "x"), base, base + 1 };
    for (int i = 0; i < 3; i++) {
        pin_value value;
        int result = run
                       ? flow_run_watch_output(run, call, outputs[i], &value)
                       : graph_eval_output(f.asset->graph, call, outputs[i], &memory, &value, &context);
        assert(result == 0);
        if (i == 0) {
            assert(value.type == PIN_F32 && value.f == 3);
            continue;
        }
        assert(value.type == PIN_STRUCT && value.ptr);
        const CF_V2 *vector = value.ptr;
        assert(vector->x == 3 && vector->y == (i == 1 ? 9 : 4));
    }
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
}

static void roundtrip(fixture f)
{
    char *json = behavior_asset_save_json(f.asset);
    behavior_asset *loaded = behavior_asset_load_json(json);
    free(json);
    assert(loaded);
    behavior_asset *original = f.asset;
    f.asset = loaded;
    graph *g = behavior_asset_find_graph(loaded, f.function);
    node *entry = graph_node_by_id(g, g->entry_id);
    node *call = graph_node_by_id(loaded->graph, f.call);
    int base = f.executable ? 1 : 0;
    assert(entry->output_pins[field(entry, base, "x")].id == f.entry_field);
    assert(call->output_pins[field(call, base, "x")].id == f.call_field);
    compile(loaded);
    verify(f);
    assert(graph_hide_output_fields(g, entry, base) == 0);
    assert(entry->output_pins[field(entry, base, "x")].id == f.entry_field);
    assert(graph_hide_output_fields(loaded->graph, call, base) == 0);
    assert(call->output_pins[field(call, base, "x")].id == f.call_field);
    compile(loaded);
    verify(f);
    behavior_asset_free(loaded);
    behavior_asset_free(original);
}

static void stale_signature()
{
    fixture f = make_fixture(false, false);
    graph *g = behavior_asset_find_graph(f.asset, f.function);
    node *entry = graph_node_by_id(g, g->entry_id);
    pin_spec added = { "Additional", PIN_F32, "float", PIN_DATA };
    assert(graph_add_pin(g, entry, 1, &added) >= 0);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(f.asset, &report) > 0);
    bool found = false;
    for (int i = 0; i < acount(report.items); i++) {
        found |= report.items[i].rule == CMPL_RULE_CALL_SIGNATURE &&
                 report.items[i].node_id == f.call;
    }
    assert(found);
    compile_report_free(&report);
    behavior_asset_free(f.asset);
}

void test_function_fields()
{
    for (int mode = 0; mode < 3; mode++) {
        fixture f = make_fixture(mode != 0, mode == 2);
        verify(f);
        roundtrip(f);
    }
    stale_signature();
    puts("function fields PASS: projected parameters and results, appended arguments, waits and roundtrip");
}
