#include "../core/flow.h"

#include <assert.h>
#if defined(__clang__)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define HOST_WAIT_ASAN 1
#endif
#endif

static flow_host_request *pending;
static int received;

static int request_host(node *, flow_context *ctx)
{
    int result = flow_context_continue(ctx, 0);
    assert(result == FLOW_OK);
    pending = flow_context_host_wait(ctx);
    return FLOW_OK;
}

static int receive(node *, flow_context *ctx)
{
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    assert(result == FLOW_OK);
    received = value.i;
    return FLOW_OK;
}

static pin_spec request_outputs[] = {
    { .name = "Then", .kind = PIN_EXEC },
    { .name = "Value", .type = PIN_I32 },
};
static pin_spec receive_inputs[] = {
    { .name = "In", .kind = PIN_EXEC },
    { .name = "Value", .type = PIN_I32 },
};
static const node_desc request_desc = {
    .name = "HostRequestTest",
    .outputs = request_outputs,
    .output_count = 2,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = request_host,
    .latent = 1,
};
static const node_desc receive_desc = {
    .name = "HostReceiveTest",
    .inputs = receive_inputs,
    .input_count = 2,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = receive,
};

static void test_host_wait_loop()
{
    pin_spec loop_input = { .name = "In", .kind = PIN_EXEC };
    node_desc loop_desc = request_desc;
    loop_desc.inputs = &loop_input;
    loop_desc.input_count = 1;
    graph g = { 0 };
    node *loop = graph_add_node(&g, &loop_desc);
    graph_connect_exec(&g, loop, 0, loop, 0, 0);
    flow_run *run = flow_run_start(&g, loop, (flow_inputs){ 0 }, NULL, NULL);
    assert(flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
    for (int i = 0; i < 1024; i++) {
        flow_host_request *finished = pending;
        assert(flow_host_complete(finished, -1, (pin_value){ 0 }));
        assert(flow_host_resume(finished) == FLOW_SUSPENDED_RESULT);
        assert(pending != finished);
        assert(flow_host_status(finished) == FLOW_HOST_FINISHED);
        assert(flow_host_run(finished) == run);
        flow_host_release(finished);
#ifdef HOST_WAIT_ASAN
        /* The still-running activation must not retain released observers. */
        assert(__asan_address_is_poisoned(finished));
#endif
    }
    /* Giving up the host reference must not turn pending work into a timer. */
    flow_host_release(pending);
    assert(flow_run_tick_at(run, 1e30) == FLOW_SUSPENDED_RESULT);
    flow_run_free(run);
    graph_free(&g);
}

void test_host_wait()
{
    graph g = { 0 };
    node *request = graph_add_node(&g, &request_desc);
    node *sink = graph_add_node(&g, &receive_desc);
    graph_connect_exec(&g, request, 0, sink, 0, 0);
    graph_connect(&g, request, 1, sink, 1);
    flow_run *run = flow_run_start(&g, request, (flow_inputs){ 0 }, NULL, NULL);
    assert(run);
    assert(flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
    assert(flow_host_status(pending) == FLOW_HOST_PENDING);
    assert(flow_run_waiting_for_host(run));
    assert(!flow_run_ready(run, 1e30));
    assert(flow_run_tick_at(run, 1e30) == FLOW_SUSPENDED_RESULT);
    assert(received == 0);
    assert(!flow_host_complete(pending, 0, (pin_value){ .type = PIN_I32, .i = 42 }));
    assert(flow_host_complete(pending, 1, (pin_value){ .type = PIN_I32, .i = 42 }));
    assert(!flow_host_complete(pending, 1, (pin_value){ .type = PIN_I32, .i = 99 }));
    assert(flow_run_ready(run, 0));
    assert(flow_host_resume(pending) == FLOW_OK);
    assert(received == 42);
    assert(flow_host_status(pending) == FLOW_HOST_FINISHED);
    assert(flow_host_run(pending) == run);
    flow_run_stop(run);
    assert(flow_host_status(pending) == FLOW_HOST_CANCELLED);
    assert(!flow_host_run(pending));
    flow_host_request *old = pending;
    assert(flow_run_start_reuse(run, &g, request, (flow_inputs){ 0 }, NULL, NULL) == run);
    assert(flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
    assert(!flow_host_complete(old, 1, (pin_value){ .type = PIN_I32, .i = 99 }));
    assert(flow_host_status(pending) == FLOW_HOST_PENDING);
    flow_host_release(old);
    flow_run_free(run);
    assert(!flow_host_run(pending));
    assert(!flow_host_complete(pending, 1, (pin_value){ .type = PIN_I32, .i = 99 }));
    flow_host_release(pending);

    run = flow_run_start(&g, request, (flow_inputs){ 0 }, NULL, NULL);
    assert(flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
    flow_host_cancel(pending);
    assert(flow_run_status(run) == FLOW_IDLE);
    assert(flow_host_status(pending) == FLOW_HOST_CANCELLED);
    flow_host_release(pending);
    flow_run_free(run);
    sink->breakpoint = 1;
    flow_options options = { .breakpoints_override = 1, .breakpoints_enabled = 1 };
    run = flow_run_start(&g, request, (flow_inputs){ 0 }, &options, NULL);
    assert(flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
    assert(flow_host_complete(pending, 1, (pin_value){ .type = PIN_I32, .i = 73 }));
    assert(flow_host_resume(pending) == FLOW_PAUSED_RESULT);
    assert(flow_host_status(pending) == FLOW_HOST_FINISHED);
    assert(flow_host_run(pending) == run);
    assert(flow_run_status(flow_host_run(pending)) == FLOW_PAUSED);
    assert(flow_host_resume(pending) == FLOW_OK);
    assert(received == 73);
    flow_run_free(run);
    assert(!flow_host_run(pending));
    flow_host_release(pending);
    graph_free(&g);
    test_host_wait_loop();
}
