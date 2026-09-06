#include "../core/array.h"
#include "../core/behavior_asset.h"
#include "../core/compile.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../game/components.h"

#include <assert.h>
#include <stdio.h>

static int received;
static int continuations;
static flow_host_request *request;

static int receive(node *, struct flow_context *ctx)
{
    pin_value value;
    assert(flow_context_eval_data(ctx, 1, &value) == FLOW_OK);
    assert(value.type == PIN_I32);
    received = value.i;
    continuations++;
    return FLOW_OK;
}

static int host_wait(node *, struct flow_context *ctx)
{
    assert(!request);
    assert(flow_context_continue(ctx, 0) == FLOW_OK);
    request = flow_context_host_wait(ctx);
    assert(request);
    return FLOW_OK;
}

static int fail_child(node *, struct flow_context *ctx)
{
    return flow_context_fail(ctx, FLOW_ERR_NODE, "child failure");
}

static pin_spec exec_pin[] = { { "Exec", PIN_FLOW, NULL, PIN_EXEC } };
static pin_spec receive_pins[] = { { "Exec", PIN_FLOW, NULL, PIN_EXEC },
                                   { "Value", PIN_I32, "int", PIN_DATA } };
static node_desc receiver = { .name = "Function wait receiver",
                              .inputs = receive_pins,
                              .input_count = 2,
                              .flow_kind = NODE_FLOW_CUSTOM,
                              .flow = receive };
static node_desc waiter = { .name = "Function host wait",
                            .inputs = exec_pin,
                            .input_count = 1,
                            .outputs = exec_pin,
                            .output_count = 1,
                            .flow_kind = NODE_FLOW_CUSTOM,
                            .flow = host_wait,
                            .latent = 1 };
static node_desc failure = { .name = "Function child failure",
                             .inputs = exec_pin,
                             .input_count = 1,
                             .outputs = exec_pin,
                             .output_count = 1,
                             .flow_kind = NODE_FLOW_CUSTOM,
                             .flow = fail_child };

typedef struct
{
    behavior_asset *asset;
    graph *function;
    node *body;
    node *call;
    node *event;
} fixture;

static void signature(graph *g)
{
    pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
    assert(graph_add_pin(g, graph_node_by_id(g, g->entry_id), 1, &value) >= 0);
    assert(graph_add_pin(g, graph_node_by_id(g, g->return_id), 0, &value) >= 0);
}

static fixture make_fixture(const node_desc *body_desc, bool nested)
{
    fixture f = { .asset = behavior_asset_new("Waiting function") };
    f.function = behavior_asset_add_function(f.asset, "Identity later", 0);
    signature(f.function);
    node *entry = graph_node_by_id(f.function, f.function->entry_id);
    node *ret = graph_node_by_id(f.function, f.function->return_id);
    f.body = graph_add_node(f.function, body_desc);
    graph_connect_exec(f.function, entry, 0, f.body, 0, 0);
    graph_connect_exec(f.function, f.body, 0, ret, 0, 0);
    graph_connect_data(f.function, entry, 1, ret, 1);
    if (body_desc == &NODE_Delay) {
        pin_value duration = { .type = PIN_F64, .type_name = "double", .d = 1 };
        assert(graph_set_pin_default(f.function, f.body, 0, 1, &duration) == 0);
    }
    graph *target = f.function;
    if (nested) {
        target = behavior_asset_add_macro(f.asset, "Wrapped identity");
        signature(target);
        node *nested_call = behavior_asset_add_function_call_node_to_graph(f.asset, target, f.function);
        node *macro_entry = graph_node_by_id(target, target->entry_id);
        node *macro_return = graph_node_by_id(target, target->return_id);
        graph_connect_exec(target, macro_entry, 0, nested_call, 0, 0);
        graph_connect_exec(target, nested_call, 0, macro_return, 0, 0);
        graph_connect_data(target, macro_entry, 1, nested_call, 1);
        graph_connect_data(target, nested_call, 1, macro_return, 1);
    }
    graph *g = f.asset->graph;
    f.event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, f.event, "Go") == 0);
    pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
    assert(graph_add_pin(g, f.event, 1, &value) >= 0);
    f.call = nested ? behavior_asset_add_macro_call_node(f.asset, target)
                    : behavior_asset_add_function_call_node(f.asset, target);
    node *sink = graph_add_node(g, &receiver);
    graph_connect_exec(g, f.event, 0, f.call, 0, 0);
    graph_connect_data(g, f.event, 1, f.call, 1);
    graph_connect_exec(g, f.call, 0, sink, 0, 0);
    graph_connect_data(g, f.call, 1, sink, 1);
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(f.asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        fprintf(stderr, "function wait: %s\n", report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
    return f;
}

static double now(void *user)
{
    return *(double *)user;
}

static flow_run *dispatch(behavior_instance *instance, int number)
{
    pin_value input = { .type = PIN_I32, .type_name = "int", .i = number };
    flow_error error;
    flow_run *run = behavior_instance_dispatch(instance, "Go", (flow_inputs){ &input, 1 }, &error);
    assert(run);
    return run;
}

static void delay_calls(bool nested)
{
    fixture f = make_fixture(&NODE_Delay, nested);
    double time = 0;
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, now, &time);
    continuations = 0;
    for (int i = 0; i < 2; i++) {
        assert(behavior_instance_tick(instance, time) >= 0);
        flow_run *run = dispatch(instance, 42 + i);
        assert(flow_run_status(run) == FLOW_SUSPENDED);
        assert(continuations == i);
        assert(!flow_run_ready(run, time + 0.5));
        assert(flow_run_ready(run, time + 1));
        assert(flow_run_tick_at(run, time + 0.5) >= 0);
        assert(flow_run_status(run) == FLOW_SUSPENDED);
        time += 1;
        assert(flow_run_tick_at(run, time) == FLOW_OK);
        assert(flow_run_status(run) == FLOW_COMPLETED);
        assert(continuations == i + 1 && received == 42 + i);
        pin_value result;
        assert(flow_run_watch_output(run, f.call, 1, &result) == 0 && result.i == 42 + i);
    }
    behavior_instance_destroy(instance);
    behavior_asset_free(f.asset);
}

static void host_calls(bool cancel)
{
    fixture f = make_fixture(&waiter, true);
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, NULL, NULL);
    continuations = 0;
    flow_run *run = dispatch(instance, 73);
    assert(flow_run_status(run) == FLOW_SUSPENDED && flow_run_waiting_for_host(run));
    assert(request && flow_host_run(request) == run);
    if (cancel) {
        assert(flow_run_stop(run) == FLOW_OK);
        assert(flow_run_status(run) == FLOW_IDLE);
        assert(flow_host_status(request) == FLOW_HOST_CANCELLED);
        assert(!flow_host_complete(request, -1, (pin_value){ 0 }));
        assert(continuations == 0);
        assert(flow_run_stop(run) == FLOW_OK);
    } else {
        assert(flow_host_complete(request, -1, (pin_value){ 0 }));
        assert(!flow_host_complete(request, -1, (pin_value){ 0 }));
        assert(flow_host_resume(request) == FLOW_OK);
        assert(flow_run_status(run) == FLOW_COMPLETED && continuations == 1 && received == 73);
    }
    behavior_instance_destroy(instance);
    assert(!flow_host_run(request));
    flow_host_release(request);
    request = NULL;
    behavior_asset_free(f.asset);
}

static void breakpoint_call()
{
    fixture f = make_fixture(&NODE_Delay, true);
    assert(graph_set_breakpoint(f.function, f.body, true) == 0);
    double time = 0;
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, now, &time);
    behavior_instance_set_breakpoints_enabled(instance, true);
    continuations = 0;
    flow_run *run = dispatch(instance, 91);
    assert(flow_run_status(run) == FLOW_PAUSED);
    const flow_run *leaf = flow_run_active_leaf(run);
    assert(leaf && flow_run_current_node(leaf) == f.body);
    assert(flow_run_step(run) >= 0);
    assert(flow_run_status(run) == FLOW_SUSPENDED && continuations == 0);
    time = 1;
    assert(flow_run_tick_at(run, time) >= 0);
    assert(flow_run_status(run) == FLOW_PAUSED && continuations == 0);
    assert(flow_run_resume(run) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_COMPLETED && continuations == 1 && received == 91);
    assert(behavior_instance_tick(instance, time) >= 0);
    run = dispatch(instance, 92);
    assert(flow_run_status(run) == FLOW_PAUSED);
    assert(flow_run_current_node(flow_run_active_leaf(run)) == f.body);
    assert(flow_run_resume(run) >= 0);
    assert(flow_run_status(run) == FLOW_SUSPENDED);
    time = 2;
    assert(flow_run_tick_at(run, time) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_COMPLETED && continuations == 2 && received == 92);
    behavior_instance_destroy(instance);
    behavior_asset_free(f.asset);
}

static void child_error()
{
    fixture f = make_fixture(&failure, true);
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, NULL, NULL);
    continuations = 0;
    flow_run *run = dispatch(instance, 1);
    assert(flow_run_status(run) == FLOW_FAILED);
    assert(flow_run_error(run)->code == FLOW_ERR_NODE && flow_run_error(run)->node == f.body);
    assert(continuations == 0);
    behavior_instance_destroy(instance);
    behavior_asset_free(f.asset);
}

static void bounded_calls()
{
    fixture f = make_fixture(&NODE_Delay, true);
    f.function->call_depth_limit = 1;
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, NULL, NULL);
    continuations = 0;
    flow_run *run = dispatch(instance, 1);
    assert(flow_run_status(run) == FLOW_FAILED);
    assert(flow_run_error(run)->code == FLOW_ERR_STEP_BUDGET);
    assert(continuations == 0);
    behavior_instance_destroy(instance);
    behavior_asset_free(f.asset);
}

static void expect_stored_vector(behavior_instance *instance, uint64_t id)
{
    pin_value value;
    assert(behavior_instance_get_variable(instance, id, &value) == 0);
    assert(value.type == PIN_STRUCT && value.ptr);
    const CF_V2 *vector = value.ptr;
    assert(vector->x == 3 && vector->y == 4);
}

static void stored_struct()
{
    fixture f = make_fixture(&NODE_Delay, false);
    CF_V2 zero = { 0 };
    pin_value initial = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &zero };
    behavior_variable *variable = behavior_asset_add_variable(
        f.asset,
        "Stored vector",
        PIN_STRUCT,
        "CF_V2",
        PIN_CONTAINER_SINGLE,
        &initial,
        ""
    );
    assert(variable);
    uint64_t id = variable->id;
    graph *g = f.function;
    graph_remove_node(g, f.body);
    node *set = graph_add_node(g, &NODE_SetVariable);
    graph_set_node_reference(g, set, id, "Stored vector");
    assert(graph_set_pin_type(g, set, 0, 1, PIN_STRUCT, "CF_V2") == 0);
    assert(graph_set_pin_type(g, set, 1, 1, PIN_STRUCT, "CF_V2") == 0);
    node *make = graph_add_node(g, registry_node_by_name("Make_CF_V2"));
    assert(make);
    pin_value x = { .type = PIN_F32, .type_name = "float", .f = 3 };
    pin_value y = { .type = PIN_F32, .type_name = "float", .f = 4 };
    assert(graph_set_pin_default(g, make, 0, 0, &x) == 0);
    assert(graph_set_pin_default(g, make, 0, 1, &y) == 0);
    graph_connect_exec(g, graph_node_by_id(g, g->entry_id), 0, set, 0, 0);
    graph_connect_exec(g, set, 0, graph_node_by_id(g, g->return_id), 0, 0);
    graph_connect_data(g, make, 0, set, 1);
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, NULL, NULL);
    flow_run *run = dispatch(instance, 1);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    expect_stored_vector(instance, id);
    assert(flow_run_stop(run) == FLOW_OK);
    expect_stored_vector(instance, id);
    flow_run_free(run);
    expect_stored_vector(instance, id);
    run = dispatch(instance, 2);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    flow_run_free(run);
    expect_stored_vector(instance, id);
    behavior_instance_destroy(instance);
    behavior_asset_free(f.asset);
}

static void missing_return()
{
    fixture f = make_fixture(&NODE_Delay, true);
    graph *g = f.function;
    assert(graph_remove_edge(g, f.body, 0, graph_node_by_id(g, g->return_id), 0) == 0);
    double time = 0;
    behavior_instance *instance = behavior_instance_create(f.asset, NULL, now, &time);
    continuations = 0;
    flow_run *run = dispatch(instance, 1);
    assert(flow_run_status(run) == FLOW_SUSPENDED);
    time = 1;
    assert(flow_run_tick_at(run, time) == FLOW_ERR_INVALID_GRAPH);
    assert(flow_run_status(run) == FLOW_FAILED);
    assert(flow_run_error(run)->code == FLOW_ERR_INVALID_GRAPH);
    assert(!flow_run_ready(run, time) && continuations == 0);
    behavior_instance_destroy(instance);
    behavior_asset_free(f.asset);
}

void test_function_wait()
{
    delay_calls(false);
    delay_calls(true);
    host_calls(false);
    host_calls(true);
    breakpoint_call();
    child_error();
    bounded_calls();
    stored_struct();
    missing_return();
    puts("function wait PASS: delayed and nested calls, snapshots, stepping, host completion and cancellation");
}
