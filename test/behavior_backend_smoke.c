#include "graph/compiler/graph_jit_sljit.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "support/arena.h"
#include "whole_function_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

const registry_pack *const REGISTRY_PACKS[] = { &CORE_EXTRA_PACK, &CORE_FLOW_PACK };
const int REGISTRY_PACK_COUNT = 2;

int main()
{
    behavior_asset *asset = behavior_asset_new("Backend smoke");
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Value",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    graph *g = asset->graph;
    node *entry = graph_add_node(g, &NODE_Event);
    node *delay = graph_add_node(g, &NODE_Delay);
    node *write = behavior_asset_add_set_node(asset, variable->id);
    assert(graph_set_event_name(g, entry, "Run") == 0);
    assert(graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .5 }) == 0);
    assert(graph_set_pin_default(g, write, 0, 1, &(pin_value){ .type = PIN_F64, .d = 42 }) == 0);
    assert(graph_connect_exec(g, entry, 0, delay, 0, 0) == 0);
    assert(graph_connect_exec(g, delay, 0, write, 0, 0) == 0);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    flow_error error = { 0 };
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
#ifdef __EMSCRIPTEN__
    assert(!graph_sljit_available());
    assert(behavior_instance_backend(instance) == BEHAVIOR_BYTECODE);
#else
    if (!graph_sljit_available()) {
        assert(!run && error.code == FLOW_ERR_COMPILE);
        assert(strstr(error.message, "requires SLJIT"));
        assert(behavior_instance_backend(instance) == BEHAVIOR_UNAVAILABLE);
        assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
        puts("UNAVAILABLE: native execution rejected without interpreter fallback");
        return 0;
    }
    assert(behavior_instance_backend(instance) == BEHAVIOR_SLJIT);
#endif
    if (!run) {
        fprintf(stderr, "%s\n", error.message);
    }
    assert(run && flow_run_status(run) == FLOW_SUSPENDED);
    assert(behavior_instance_tick(instance, .25) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_SUSPENDED);
    assert(behavior_instance_tick(instance, .5) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(behavior_instance_get_variable(instance, variable->id, &value) == 0 && value.d == 42);
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: event, yield, resume and variable write; zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_function_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED && flow_run_steps(run) == 14);
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.f == 4
    );
    assert(
        behavior_instance_get_variable(instance, asset->variables[1].id, &value) == 0 &&
        value.f == 8
    );
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: whole-function loop, sequence and branch; zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_arguments_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    graph *function = asset->graphs[1];
    assert(behavior_program_function(
        behavior_program_graph(asset->program, function),
        graph_node_by_id(function, function->entry_id)
    ));
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(
        instance->variables[0].d == 3.25 && instance->variables[1].f == -2.5f &&
        instance->variables[2].b
    );
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: nested whole-function arguments, conversions, min/max and Return; "
        "zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_pure_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    instance->variables[0].d = 4;
    arena storage = { 0 };
    eval_context ctx = { .instance = instance,
                         .runtime = runtime_store_create(asset->graph) };
    uint64_t allocations = runtime_store_creations();
    assert(
        behavior_program_output(
            behavior_program_graph(asset->program, asset->graph),
            asset->graph->nodes[0],
            0,
            &storage,
            &value,
            &ctx,
            behavior_instance_backend(instance)
        ) == 0
    );
    assert(value.d == 9.25 && runtime_store_creations() == allocations);
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: nested pure bodies; no callee allocation or oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    return 0;
}
