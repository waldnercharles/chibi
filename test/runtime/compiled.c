#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/library.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"
#include "test/whole_function_fixture.h"

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
    if (backend != BEHAVIOR_ORACLE) {
        const behavior_graph_program
            *program = behavior_program_graph(asset->program, asset->graph);
        node *event = NULL, *delay = NULL;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            node *n = asset->graph->nodes[i];
            if (n->desc == &NODE_Event) {
                event = n;
            }
            if (n->desc == &NODE_Delay) {
                delay = n;
            }
        }
        assert(event && delay && !behavior_program_diagnostic(asset->program));
        const behavior_function_diagnostic *d = behavior_program_function_diagnostic(program, event);
        assert(d && d->graph == asset->graph && d->node == delay && d->pin == -1);
    }
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

static void whole_function_control(behavior_backend backend)
{
    behavior_asset *a = whole_function_fixture();
    graph *g = a->graph;
    uint64_t x = a->variables[0].id, y = a->variables[1].id;
    node *event = NULL, *loop = NULL, *branch = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == &NODE_Event) {
            event = g->nodes[i];
        }
        if (g->nodes[i]->desc == registry_node_by_name("WhileLoop")) {
            loop = g->nodes[i];
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == registry_node_by_name("Branch")) {
            branch = g->nodes[i];
        }
    }
    assert(event && loop && branch);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value actual;
    assert(behavior_instance_get_variable(bi, x, &actual) == 0 && actual.f == 4);
    assert(behavior_instance_get_variable(bi, y, &actual) == 0 && actual.f == 8);
    assert(flow_run_steps(run) == 14);
    if (backend != BEHAVIOR_ORACLE) {
        assert(!flow_run_input_value(run, loop, 1));
        behavior_instance_set_trace_enabled(bi, 1);
        flow_run *inspected = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
        assert(inspected && flow_run_status(inspected) == FLOW_COMPLETED);
        assert(
            flow_run_watch_input(inspected, branch, 1, &actual) == 0 && !actual.b
        );
        assert(flow_run_input_value(inspected, loop, 1));
        int count = 0;
        assert(flow_run_traversals(inspected, &count) && count > 0);
    }
    if (backend != BEHAVIOR_ORACLE) {
        const behavior_function
            *f = behavior_program_function(behavior_program_graph(a->program, g), event);
        assert(
            f &&
            !behavior_program_function_diagnostic(behavior_program_graph(a->program, g), event)
        );
        pin_value variables[2] = { { .type = PIN_F32 }, { .type = PIN_F32 } };
        behavior_function_context ctx = { .variables = variables, .budget = 5 };
        assert(behavior_function_run(f, &ctx, backend == BEHAVIOR_SLJIT) == FLOW_ERR_STEP_BUDGET);
        assert(ctx.steps == 5 && ctx.node_id == loop->id && variables[0].f == 1);
        behavior_instance_set_trace_enabled(bi, 0);
        flow_options options = { .instance = bi, .step_budget = 5 };
        flow_run *limited = flow_run_start(g, event, (flow_inputs){ 0 }, &options, &error);
        assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
        assert(flow_run_current_node(limited) == loop && flow_run_error(limited)->node == loop);
        int count = 0;
        const flow_call_frame *frames = flow_run_call_stack(limited, &count);
        assert(count == 1 && frames[0].node_id == loop->id);
        flow_run_free(limited);
    }
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void whole_function_library(behavior_backend backend, bool effectful)
{
    behavior_asset *lib = behavior_asset_new("WholeMath");
    lib->domain = BEHAVIOR_DOMAIN_LIBRARY;
    pin_spec spec = { .name = "Value", .type = PIN_F64, .type_name = "double", .kind = PIN_DATA };
    graph *identity = behavior_asset_add_function(lib, "Identity", 1);
    node *entry = graph_node_by_id(identity, identity->entry_id);
    node *ret = graph_node_by_id(identity, identity->return_id);
    assert(graph_add_pin(identity, entry, 1, &spec) == 0);
    assert(graph_add_pin(identity, ret, 0, &spec) == 0);
    assert(graph_connect_data(identity, entry, 0, ret, 0) == 0);
    graph *wrapper = behavior_asset_add_function(lib, "Wrapper", 1);
    entry = graph_node_by_id(wrapper, wrapper->entry_id);
    ret = graph_node_by_id(wrapper, wrapper->return_id);
    assert(graph_add_pin(wrapper, entry, 1, &spec) == 0);
    assert(graph_add_pin(wrapper, ret, 0, &spec) == 0);
    node *inner = behavior_asset_add_function_call_node_to_graph(lib, wrapper, identity);
    assert(graph_connect_data(wrapper, entry, 0, inner, 0) == 0);
    assert(graph_connect_data(wrapper, inner, 0, ret, 0) == 0);
    graph *target = wrapper;
    if (effectful) {
        target = behavior_asset_add_function(lib, "Forward", 0);
        entry = graph_node_by_id(target, target->entry_id);
        ret = graph_node_by_id(target, target->return_id);
        assert(graph_add_pin(target, entry, 1, &spec) >= 0);
        assert(graph_add_pin(target, ret, 0, &spec) >= 0);
        node *call = behavior_asset_add_function_call_node_to_graph(lib, target, wrapper);
        assert(graph_connect_data(target, entry, 1, call, 0) == 0);
        assert(graph_connect_data(target, call, 0, ret, 1) == 0);
        assert(graph_connect_exec(target, entry, 0, ret, 0, 0) == 0);
    }
    behavior_asset *a = behavior_asset_new("WholeLibrary");
    uint64_t id = behavior_asset_add_variable(a, "Result", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    node *call = behavior_asset_add_function_call_node_to_graph(a, a->graph, target);
    node *set = behavior_asset_add_set_node(a, id);
    assert(
        graph_set_pin_default(a->graph, call, 0, effectful, &(pin_value){ .type = PIN_F64, .d = 42 }) == 0
    );
    assert(graph_connect_exec(a->graph, event, 0, effectful ? call : set, 0, 0) == 0);
    if (effectful) {
        assert(graph_connect_exec(a->graph, call, 0, set, 0, 0) == 0);
    }
    assert(graph_connect_data(a->graph, call, effectful, set, 1) == 0);
    const behavior_asset *catalog[] = { lib };
    char message[256];
    behavior_asset *linked = behavior_asset_link_libraries(a, catalog, 1, message, sizeof message);
    assert(linked);
    behavior_instance *bi = behavior_instance_create(linked, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(
        run && flow_run_status(run) == FLOW_COMPLETED &&
        flow_run_steps(run) == (effectful ? 3 : 2)
    );
    pin_value result;
    assert(behavior_instance_get_variable(bi, id, &result) == 0 && result.d == 42);
    if (backend != BEHAVIOR_ORACLE) {
        if (effectful) {
            assert(run->whole_function_executed && !acount(run->children));
        } else {
            node *linked_event = graph_node_by_id(linked->graph, event->id);
            assert(behavior_program_function(
                behavior_program_graph(linked->program, linked->graph),
                linked_event
            ));
        }
    }
    if (!effectful) {
        node *linked_call = graph_node_by_id(linked->graph, call->id);
        arena storage = { 0 };
        eval_context ctx = { .instance = bi,
                             .runtime = runtime_store_create(linked->graph) };
        uint64_t allocations = runtime_store_creations();
        int status = backend == BEHAVIOR_ORACLE
                       ? graph_eval_output(linked->graph, linked_call, 0, &storage, &result, &ctx)
                       : behavior_program_output(
                             behavior_program_graph(linked->program, linked->graph),
                             linked_call,
                             0,
                             &storage,
                             &result,
                             &ctx,
                             backend
                         );
        assert(!status && result.d == 42);
        assert(runtime_store_creations() - allocations == (backend == BEHAVIOR_ORACLE ? 2 : 0));
        runtime_store_free(ctx.runtime);
        arena_free(&storage);
    }
    behavior_instance_destroy(bi);
    behavior_asset_free(linked);
    behavior_asset_free(a);
    behavior_asset_free(lib);
}

static void assert_real(double actual, double expected)
{
    assert(isnan(expected) ? isnan(actual) : actual == expected);
    if (expected == 0) {
        assert(signbit(actual) == signbit(expected));
    }
}

static void whole_function_arguments(behavior_backend backend)
{
    behavior_asset *a = whole_arguments_fixture();
    graph *empty = behavior_asset_add_function(a, "Missing return", 0);
    node *missing = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, missing, "Missing") == 0);
    node *call = behavior_asset_add_function_call_node(a, empty);
    assert(graph_connect_exec(a->graph, missing, 0, call, 0, 0) == 0);
    graph *g = a->graphs[1];
    node *entry = graph_node_by_id(g, g->entry_id),
         *ret = graph_node_by_id(g, g->return_id);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(behavior_instance_set_activation_pool(bi, 1) == 0);
    double cases[] = { NAN, INFINITY, -INFINITY,    -0.,
                       0.,  -1.e-40,  1.0000000001, 16777217. };
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            pin_value args[] = { { .type = PIN_F64, .d = cases[x] },
                                 { .type = PIN_F32, .f = (float)cases[y] },
                                 { .type = PIN_BOOL, .b = x % 2 } };
            flow_error error = { 0 };
            flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 3 }, &error);
            assert(run && flow_run_status(run) == FLOW_COMPLETED);
            float small = (float)args[0].d < args[1].f ? (float)args[0].d
                                                       : args[1].f;
            double wide = (double)args[1].f > args[0].d ? (double)args[1].f
                                                        : args[0].d;
            assert_real(bi->variables[0].d, wide);
            assert_real(bi->variables[1].f, small);
            assert(bi->variables[2].b == args[2].b);
            if (backend == BEHAVIOR_ORACLE) {
                flow_run *leaf = run->children[0]->children[0];
                assert(flow_run_steps(leaf) == 3 && leaf->returned == ret);
            } else {
                assert(run->whole_function_executed && !acount(run->children));
                assert(flow_run_steps(run) == 5);
            }
            assert(behavior_instance_release_activation(bi, run) == 0);
        }
    }
    /* Calling the leaf directly exercises entry defaults and both argument
     * coercions; the normal call boundary has already normalized these. */
    pin_value args[] = { { .type = PIN_F32, .f = 5.5f },
                         { .type = PIN_F64, .d = -7.125 },
                         { .type = PIN_I32, .i = 0 } };
    flow_options options = { .instance = bi, .step_budget = 3 };
    for (int count = 0; count <= 3; count++) {
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, entry, (flow_inputs){ args, count }, &options, &error);
        assert(run && flow_run_resume(run) == FLOW_OK);
        assert_real(flow_run_input_value(run, ret, 1)->d, count ? 5.5 : 3.25);
        assert_real(flow_run_input_value(run, ret, 2)->f, count >= 2 ? -7.125 : -2.5);
        assert(flow_run_input_value(run, ret, 3)->b);
        flow_run_free(run);
    }
    options.step_budget = 2;
    flow_error error = { 0 };
    flow_run *limited = flow_run_start(g, entry, (flow_inputs){ 0 }, &options, &error);
    assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
    assert(flow_run_error(limited)->node == ret);
    int count = 0;
    const flow_call_frame *stack = flow_run_call_stack(limited, &count);
    assert(count > 0 && stack[count - 1].node_id == ret->id);
    flow_run_free(limited);
    flow_run *failed = behavior_instance_dispatch(bi, "Missing", (flow_inputs){ 0 }, &error);
    assert(failed && flow_run_status(failed) == FLOW_FAILED);
    assert(flow_run_error(failed)->code == FLOW_ERR_INVALID_GRAPH);
    assert(strstr(flow_run_error(failed)->message, "without Return"));
    assert(behavior_instance_release_activation(bi, failed) == 0);
    behavior_instance_set_trace_enabled(bi, 1);
    flow_run *inspected = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(inspected && flow_run_status(inspected) == FLOW_COMPLETED);
    flow_run *leaf = inspected->children[0]->children[0];
    assert(!leaf->whole_function_executed);
    pin_value watched;
    assert(flow_run_watch_input(leaf, ret, 1, &watched) == 0 && watched.d == 3.25);
    assert(flow_run_traversals(leaf, &count) && count > 0);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void whole_function_comparisons()
{
    const char *names[] = { "Less",    "Less Or Equal",
                            "Greater", "Greater Or Equal",
                            "Equal",   "Not Equal" };
    float cases[] = { NAN, INFINITY, -INFINITY, -1, -0.f, 0, 1 };
    for (int op = 0; op < 6; op++) {
        behavior_asset *a = behavior_asset_new("WholeComparison");
        uint64_t x = behavior_asset_add_variable(a, "X", PIN_F32, "float", PIN_CONTAINER_SINGLE, NULL, "")
                         ->id;
        uint64_t y = behavior_asset_add_variable(a, "Result", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, NULL, "")
                         ->id;
        node *event = graph_add_node(a->graph, &NODE_Event);
        assert(graph_set_event_name(a->graph, event, "Run") == 0);
        node *get = behavior_asset_add_get_node(a, x);
        node *compare = graph_add_node(a->graph, registry_node_by_name(names[op]));
        node *set = behavior_asset_add_set_node(a, y);
        assert(graph_connect_data(a->graph, get, 0, compare, 0) == 0);
        assert(graph_connect_data(a->graph, compare, 0, set, 1) == 0);
        assert(graph_connect_exec(a->graph, event, 0, set, 0, 0) == 0);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, 0);
        assert(behavior_program_function(behavior_program_graph(a->program, a->graph), event));
        for (int mode = 0; mode < 3; mode++) {
            assert(
                behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ behavior_default_backend(),
                                          BEHAVIOR_BYTECODE,
                                          BEHAVIOR_ORACLE }[mode]
                ) == 0
            );
            for (int i = 0; i < 7; i++) {
                float v = cases[i];
                bool want = (bool[]){ v < 0,  v <= 0, v > 0,
                                      v >= 0, v == 0, v != 0 }[op];
                assert(
                    behavior_instance_set_variable(bi, x, &(pin_value){ .type = PIN_F32, .f = v }) == 0
                );
                flow_error error = { 0 };
                flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
                assert(run && flow_run_status(run) == FLOW_COMPLETED);
                pin_value actual;
                assert(behavior_instance_get_variable(bi, y, &actual) == 0 && actual.b == want);
                assert(behavior_instance_release_activation(bi, run) == 0);
            }
        }
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
}

static void whole_function_scopes(behavior_backend backend)
{
    behavior_asset *a = whole_scopes_fixture();
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(behavior_instance_set_activation_pool(bi, 1) == 0);
    for (int i = 0; i < 32; i++) {
        bi->variables[0].f = i;
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        assert(bi->variables[0].f == 99 && bi->variables[1].f == i + 101);
        assert(flow_run_steps(run) == 4);
        if (backend != BEHAVIOR_ORACLE) {
            assert(run->whole_function_executed && !acount(run->children));
        }
        assert(behavior_instance_release_activation(bi, run) == 0);
    }
    node *event = NULL;
    for (int i = 0; i < acount(a->graph->nodes); i++) {
        if (a->graph->nodes[i]->desc == &NODE_Event) {
            event = a->graph->nodes[i];
        }
    }
    assert(event);
    graph *f = a->graphs[1];
    node *ret = graph_node_by_id(f, f->return_id);
    bi->variables[0].f = 0;
    bi->variables[1].f = 0;
    flow_options options = { .instance = bi, .step_budget = 3 };
    flow_error error = { 0 };
    flow_run *limited = flow_run_start(a->graph, event, (flow_inputs){ 0 }, &options, &error);
    assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
    /* The first callee reaches its Return after using its own three steps. */
    assert(flow_run_steps(limited) == 2 && flow_run_error(limited)->steps == 3);
    assert(flow_run_error(limited)->node == ret && flow_run_current_node(limited) == ret);
    assert(bi->variables[0].f == 99 && bi->variables[1].f == 0);
    int count = 0;
    const flow_call_frame *frames = flow_run_call_stack(limited, &count);
    assert(count >= 2 && frames[count - 1].graph == f && frames[count - 1].node_id == ret->id);
    assert(frames[count - 2].graph == a->graph);
    flow_run_free(limited);
    options.call_depth = 64;
    bi->variables[0].f = 0;
    limited = flow_run_start(a->graph, event, (flow_inputs){ 0 }, &options, &error);
    assert(limited && flow_run_resume(limited) == FLOW_ERR_STEP_BUDGET);
    assert(strstr(flow_run_error(limited)->message, "call depth"));
    assert(bi->variables[0].f == 0 && flow_run_steps(limited) == 2);
    flow_run_free(limited);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void count_data_edge(const node *, int, const node *, int, void *user)
{
    (*(int *)user)++;
}

static void pure_unused_return(behavior_backend backend)
{
    behavior_asset *a = behavior_asset_new("Unused pure return");
    uint64_t result = behavior_asset_add_variable(a, "Result", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    graph *f = behavior_asset_add_function(a, "Sample", 1);
    node *ret = graph_node_by_id(f, f->return_id);
    pin_spec output = { .name = "Value",
                        .kind = PIN_DATA,
                        .type = PIN_F64,
                        .type_name = "double",
                        .default_value = { .type = PIN_F64, .d = 42 } };
    assert(graph_add_pin(f, ret, 0, &output) >= 0);
    output.name = "Sample";
    assert(graph_add_pin(f, ret, 0, &output) >= 0);
    node *random = graph_add_node(f, registry_node_by_name("Random Real"));
    assert(graph_connect_data(f, random, 0, ret, 1) == 0);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(graph_set_event_name(a->graph, event, "Run") == 0);
    node *call = behavior_asset_add_function_call_node(a, f);
    node *set = behavior_asset_add_set_node(a, result);
    assert(graph_connect_data(a->graph, call, 0, set, 1) == 0);
    assert(graph_connect_exec(a->graph, event, 0, set, 0, 0) == 0);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    if (backend != BEHAVIOR_ORACLE) {
        const behavior_function_diagnostic *d = behavior_program_function_diagnostic(
            behavior_program_graph(a->program, a->graph),
            event
        );
        assert(d && d->graph == f && d->node == random);
    }
    srand(2718);
    (void)rand();
    int expected_next = rand();
    srand(2718);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(bi->variables[0].d == 42 && rand() == expected_next);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void whole_pure_calls(behavior_backend backend)
{
    behavior_asset *a = whole_pure_fixture();
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_trace_enabled(bi, 0);
    node *call = a->graph->nodes[0];
    const behavior_graph_program *program = behavior_program_graph(a->program, a->graph);
    graph *wrapper = a->graphs[2];
    node *inner = wrapper->nodes[2];
    assert(inner->desc->pure_kind == NODE_PURE_FUNCTION);
    if (backend != BEHAVIOR_ORACLE) {
        for (int i = 1; i <= 2; i++) {
            graph *f = a->graphs[i];
            assert(behavior_program_function(
                behavior_program_graph(a->program, f),
                graph_node_by_id(f, f->entry_id)
            ));
        }
    }
    arena storage = { 0 };
    eval_context ctx = { .instance = bi,
                         .runtime = runtime_store_create(a->graph),
                         .backend = backend };
    double cases[] = { -0.,       0.,  .25,      -3.,      1.e-40,
                       16777217., NAN, INFINITY, -INFINITY };
    for (int inspect = 0; inspect < 2; inspect++) {
        int edges = 0;
        ctx.on_data_edge = inspect ? count_data_edge : NULL;
        ctx.on_data_edge_user = &edges;
        for (int depth_case = 0; depth_case < 4; depth_case++) {
            ctx.call_depth = (unsigned[]){ 0, 62, 63, 64 }[depth_case];
            for (int k = 0; k < 9; k++) {
                runtime_store_reset_memo(ctx.runtime);
                bi->variables[0].d = cases[k];
                uint64_t allocated = runtime_store_creations(),
                         oracle = eval_oracle_calls();
                pin_value value;
                int result = backend == BEHAVIOR_ORACLE
                               ? graph_eval_output(a->graph, call, 0, &storage, &value, &ctx)
                               : behavior_program_output(program, call, 0, &storage, &value, &ctx, backend);
                if (depth_case >= 2) {
                    assert(result == NODE_EVAL_INVALID);
                    assert(ctx.status.node == (depth_case == 2 ? inner : call));
                    assert(ctx.status.pin == 0);
                    assert(strstr(ctx.status.message, "pure function call depth limit"));
                    continue;
                }
                assert(!result);
                assert_real(value.d, (cases[k] + .25) + cases[k] * 1.25);
                const pin_value *outputs = runtime_node_outputs_const(
                    runtime_store_node(ctx.runtime, call)
                );
                assert_real(outputs[1].d, (cases[k] + .25) * (cases[k] * 1.25));
                if (backend != BEHAVIOR_ORACLE) {
                    assert(oracle == eval_oracle_calls());
                    assert(runtime_store_creations() - allocated == (inspect ? 2 : 0));
                } else {
                    assert(runtime_store_creations() - allocated == 2);
                }
            }
        }
        assert(!inspect || edges > 0);
    }
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

void test_compiled_behaviors()
{
    test_compiled_components();
    test_compiled_loops();
    test_compiled_conversions();
    test_compiled_aliases();
    test_compiled_stores();
    test_compiled_arrays();
    test_compiled_aggregates();
    test_compiled_math();
    test_compiled_integers();
    pure_unused_return(behavior_default_backend());
    pure_unused_return(BEHAVIOR_BYTECODE);
    pure_unused_return(BEHAVIOR_ORACLE);
    whole_pure_calls(behavior_default_backend());
    whole_pure_calls(BEHAVIOR_BYTECODE);
    whole_pure_calls(BEHAVIOR_ORACLE);
    whole_function_scopes(behavior_default_backend());
    whole_function_scopes(BEHAVIOR_BYTECODE);
    whole_function_scopes(BEHAVIOR_ORACLE);
    whole_function_arguments(behavior_default_backend());
    whole_function_arguments(BEHAVIOR_BYTECODE);
    whole_function_arguments(BEHAVIOR_ORACLE);
    whole_function_comparisons();
    for (int effectful = 0; effectful <= 1; effectful++) {
        whole_function_library(behavior_default_backend(), effectful);
        whole_function_library(BEHAVIOR_BYTECODE, effectful);
        whole_function_library(BEHAVIOR_ORACLE, effectful);
    }
    whole_function_control(behavior_default_backend());
    whole_function_control(BEHAVIOR_BYTECODE);
    whole_function_control(BEHAVIOR_ORACLE);
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
