#include "graph/compiler/graph_jit_sljit.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <math.h>

static behavior_asset *counter_asset()
{
    behavior_asset *asset = behavior_asset_new("CompiledCounter");
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Count",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Count") == 0);
    node *get = behavior_asset_add_get_node(asset, variable->id);
    node *add = graph_add_node(g, &NODE_Add);
    assert(graph_set_pin_type(g, add, 0, 0, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(g, add, 0, 1, PIN_F64, "double") == 0);
    assert(graph_set_pin_type(g, add, 1, 0, PIN_F64, "double") == 0);
    node *first = behavior_asset_add_set_node(asset, variable->id);
    node *delay = graph_add_node(g, &NODE_Delay);
    node *last = behavior_asset_add_set_node(asset, variable->id);
    assert(graph_set_pin_default(g, add, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 }) == 0);
    assert(graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 }) == 0);
    assert(graph_connect_data(g, get, 0, add, 0) == 0);
    assert(graph_connect_data(g, add, 0, first, 1) == 0);
    assert(graph_connect_data(g, add, 0, last, 1) == 0);
    assert(graph_connect_exec(g, event, 0, first, 0, 0) == 0);
    assert(graph_connect_exec(g, first, 0, delay, 0, 0) == 0);
    assert(graph_connect_exec(g, delay, 0, last, 0, 0) == 0);
    return asset;
}

static void compiled_counter(behavior_backend backend)
{
    behavior_asset *asset = counter_asset();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(instance, backend) == 0);
    uint64_t eval_before = eval_oracle_calls();
    uint64_t flow_before = flow_oracle_calls();
    flow_error error = { 0 };
    flow_run *first = behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error);
    if (!first) {
        fprintf(stderr, "compiled counter: %s\n", error.message);
    }
    assert(first && flow_run_status(first) == FLOW_SUSPENDED);
    assert(flow_run_backend(first) == backend);
    flow_run *second = behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error);
    assert(second && flow_run_status(second) == FLOW_SUSPENDED);
    pin_value value;
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.d == 2
    );
    assert(behavior_instance_tick(instance, .5) == FLOW_OK);
    assert(behavior_instance_tick(instance, 1) == FLOW_OK);
    assert(flow_run_status(first) == FLOW_COMPLETED && flow_run_status(second) == FLOW_COMPLETED);
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.d == 4
    );
    assert(behavior_instance_release_activation(instance, first) == 0);
    assert(behavior_instance_release_activation(instance, second) == 0);
    behavior_instance_set_trace_enabled(instance, 0);
    first = behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error);
    assert(first && flow_run_status(first) == FLOW_SUSPENDED);
    int traversals = 0;
    flow_run_traversals(first, &traversals);
    assert(traversals == 0);
    assert(behavior_instance_stop_activation(instance, first) == FLOW_OK);
    assert(behavior_instance_tick(instance, 2) == FLOW_OK);
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.d == 5
    );
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);
        const graph_ir *ir = behavior_graph_ir(
            behavior_program_graph(asset->program, asset->graph)
        );
        assert(acount(ir->blocks) && acount(ir->values) && acount(ir->control));
        for (int i = 0; i < acount(ir->blocks); i++) {
            assert(ir->blocks[i].graph_id == asset->graph->graph_id);
            assert(ir->blocks[i].node_id && ir->blocks[i].pin_id);
        }
    } else {
        assert(eval_oracle_calls() > eval_before && flow_oracle_calls() > flow_before);
    }
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

void test_behavior_unavailable()
{
    assert(!graph_sljit_available());
    behavior_asset *asset = counter_asset();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_error error;
    assert(!behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error));
    assert(error.code == FLOW_ERR_COMPILE && strstr(error.message, "requires SLJIT"));
    assert(behavior_instance_backend(instance) == BEHAVIOR_UNAVAILABLE);
    assert(eval_oracle_calls() == eval_before && flow_oracle_calls() == flow_before);
    assert(behavior_instance_set_backend(instance, BEHAVIOR_ORACLE) == 0);
    assert(behavior_instance_dispatch(instance, "Count", (flow_inputs){ 0 }, &error));
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    puts("PASS: native JIT unavailable rejects dispatch without oracle fallback");
}

static void generation_guards()
{
    behavior_asset *asset = counter_asset();
    graph *function = behavior_asset_add_function(asset, "Value", 1);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec value = { .name = "Value", .kind = PIN_DATA, .type = PIN_F64, .type_name = "double" };
    assert(graph_add_pin(function, ret, 0, &value) == 0);
    behavior_program_publish(asset);
    uint64_t before = behavior_asset_revision(asset);
    assert(
        graph_set_pin_default(function, ret, 0, 0, &(pin_value){ .type = PIN_F64, .d = 7 }) == 0
    );
    assert(behavior_asset_revision(asset) != before);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(behavior_instance_backend(instance) != BEHAVIOR_UNAVAILABLE);
    assert(
        graph_set_pin_default(function, ret, 0, 0, &(pin_value){ .type = PIN_F64, .d = 9 }) != 0
    );
    behavior_instance_destroy(instance);
    assert(
        graph_set_pin_default(function, ret, 0, 0, &(pin_value){ .type = PIN_F64, .d = 9 }) == 0
    );
    behavior_asset_free(asset);
}

static void numeric_regions(pin_type type)
{
    const char *name = type == PIN_F32 ? "float" : "double";
    behavior_asset *a = behavior_asset_new("NumericRegions");
    behavior_variable *v = behavior_asset_add_variable(a, "X", type, name, PIN_CONTAINER_SINGLE, NULL, "");
    uint64_t id = v->id;
    node *get = behavior_asset_add_get_node(a, id);
    const node_desc *ops[] = { &NODE_Add, &NODE_Subtract, &NODE_Multiply, &NODE_Divide };
    node *nodes[4];
    for (int i = 0; i < 4; i++) {
        nodes[i] = graph_add_node(a->graph, ops[i]);
        for (int j = 0; j < 2; j++) {
            assert(graph_set_pin_type(a->graph, nodes[i], 0, j, type, name) == 0);
        }
        assert(graph_set_pin_type(a->graph, nodes[i], 1, 0, type, name) == 0);
    }
    /* Shared values survive both left/right reuse and later operations. */
    assert(
        graph_set_pin_binding(
            a->graph,
            nodes[0],
            0,
            0,
            &(pin_binding){ .kind = PIN_BINDING_VARIABLE, .variable_id = id }
        ) == 0
    );
    assert(graph_connect_data(a->graph, get, 0, nodes[0], 1) == 0);
    assert(graph_connect_data(a->graph, nodes[0], 0, nodes[1], 0) == 0);
    assert(graph_connect_data(a->graph, get, 0, nodes[1], 1) == 0);
    assert(graph_connect_data(a->graph, nodes[0], 0, nodes[2], 0) == 0);
    assert(graph_connect_data(a->graph, nodes[1], 0, nodes[2], 1) == 0);
    assert(graph_connect_data(a->graph, nodes[2], 0, nodes[3], 0) == 0);
    assert(graph_connect_data(a->graph, nodes[0], 0, nodes[3], 1) == 0);
    node *sink = behavior_asset_add_set_node(a, id);
    assert(graph_connect_data(a->graph, nodes[3], 0, sink, 1) == 0);
    behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    const behavior_graph_program *program = behavior_program_graph(a->program, a->graph);
    if (behavior_default_backend() == BEHAVIOR_SLJIT) {
        assert(behavior_program_input_is_direct(program, sink, 1));
    }
    eval_context ctx = { .instance = instance,
                         .runtime = runtime_store_create(a->graph) };
    arena storage = { 0 };
    double cases[] = { 0., -0., 1., -1., .125, 1e10, INFINITY, -INFINITY, NAN };
    for (int i = 0; i < 9; i++) {
        pin_value x = { .type = type, .type_name = name };
        if (type == PIN_F32) {
            x.f = (float)cases[i];
        } else {
            x.d = cases[i];
        }
        assert(behavior_instance_set_variable(instance, id, &x) == 0);
        pin_value expected;
        runtime_store_reset_memo(ctx.runtime);
        assert(graph_eval_output(a->graph, nodes[3], 0, &storage, &expected, &ctx) == 0);
        for (int mode = 0; mode < 3; mode++) {
            behavior_instance_set_trace_enabled(instance, mode == 2);
            runtime_store_reset_memo(ctx.runtime);
            pin_value actual;
            behavior_backend backend = mode == 1 ? BEHAVIOR_BYTECODE
                                                 : behavior_default_backend();
            assert(behavior_program_input(program, sink, 1, &storage, &actual, &ctx, backend) == 0);
            double want = type == PIN_F32 ? expected.f : expected.d;
            double got = type == PIN_F32 ? actual.f : actual.d;
            assert(
                (isnan(want) && isnan(got)) ||
                (want == got && signbit(want) == signbit(got))
            );
            pin_value stored = runtime_node_inputs(
                runtime_store_node(ctx.runtime, sink)
            )[1];
            assert(stored.type == type);
            assert(
                memcmp(&stored.d, &actual.d, type == PIN_F32 ? sizeof(float) : sizeof(double)) == 0
            );
        }
        behavior_instance_set_trace_enabled(instance, 0);
    }
    arena_free(&storage);
    runtime_store_free(ctx.runtime);
    behavior_instance_destroy(instance);
    behavior_asset_free(a);
}

void test_compiled_behaviors()
{
    numeric_regions(PIN_F32);
    numeric_regions(PIN_F64);
    generation_guards();
    behavior_backend native = behavior_default_backend();
    compiled_counter(native);
    compiled_counter(BEHAVIOR_BYTECODE);
    compiled_counter(BEHAVIOR_ORACLE);
    behavior_backend previous = behavior_debug_backend(BEHAVIOR_BYTECODE);
    run_runtime_execution_checks();
    behavior_debug_backend(BEHAVIOR_ORACLE);
    run_runtime_execution_checks();
    behavior_debug_backend(previous);
    puts("PASS: compiled native, bytecode and oracle counters, waits, cancellation, source maps and linked runtime contracts");
}
