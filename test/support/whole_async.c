#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "support/xalloc.h"
#include "test/support/report.h"
#include "test/whole_async_fixture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int begin(
    void *service,
    const char *,
    const pin_value *values,
    int count,
    flow_host_request *request,
    const char **error
)
{
    whole_async_capture *c = service;
    assert(count == 3 && values[0].type == PIN_NAME && values[1].type == PIN_I32);
    assert(values[2].type == PIN_STR && !strcmp(values[2].str, "constant \"text\"\nwith \\ escape"));
    if (++c->calls == c->fail_at) {
        *error = "asynchronous test rejected";
        return FLOW_ERR_NODE;
    }
    apush(c->requests, request);
    apush(c->names, xstrdup(values[0].str));
    apush(c->arguments, values[1].i);
    if (c->immediate) {
        assert(flow_host_complete(
            request,
            1,
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = 100 + values[1].i }
        ));
    }
    return FLOW_OK;
}
static int invoke(node *, flow_context *ctx)
{
    void *service = flow_context_service(ctx, "test.async");
    if (!service) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "missing asynchronous service");
    }
    pin_value values[3];
    for (int i = 0; i < 3; i++) {
        int status = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (status < 0) {
            return status;
        }
    }
    flow_host_request *request = flow_context_host_wait(ctx);
    const char *error = NULL;
    int status = begin(service, NULL, values, 3, request, &error);
    if (status < 0) {
        flow_host_finish(ctx->run, FLOW_HOST_CANCELLED);
        flow_host_release(request);
        return flow_context_fail(ctx, status, error);
    }
    return flow_context_continue(ctx, 0);
}
static pin_spec inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Name", .type = PIN_NAME },
    { .name = "Index", .type = PIN_I32, .type_name = "int" },
    { .name = "Literal",
      .type = PIN_STR,
      .default_value = { .type = PIN_STR, .str = "constant \"text\"\nwith \\ escape" } }
};
static pin_spec outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" }
};
static const node_command command = {
    .service = "test.async",
    .missing_service = "missing asynchronous service",
    .begin = begin
};
static const node_desc asynchronous = { .name = "Asynchronous test request",
                                        .inputs = inputs,
                                        .input_count = 4,
                                        .outputs = outputs,
                                        .output_count = 2,
                                        .flow_kind = NODE_FLOW_CUSTOM,
                                        .flow = invoke,
                                        .command = &command,
                                        .latent = true };
node *whole_async_node(graph *g)
{
    return graph_add_node(g, &asynchronous);
}
static void next(graph *g, node *from, int pin, node *to)
{
    assert(!graph_connect_exec(g, from, pin, to, 0, 0));
}
behavior_asset *whole_async_fixture()
{
    behavior_asset *a = behavior_asset_new("Asynchronous body");
    uint64_t last = behavior_asset_add_variable(a, "Last", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE, NULL, "")
                        ->id;
    uint64_t done = behavior_asset_add_variable(a, "Done", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                        ->id;
    graph *g = behavior_asset_add_function(a, "Request", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    assert(graph_add_pin(g, entry, 1, &inputs[1]) == 1);
    assert(graph_add_pin(g, entry, 1, &inputs[2]) == 2);
    assert(graph_add_pin(g, ret, 0, &outputs[1]) == 1);
    node *before = graph_add_node(g, &NODE_Delay),
         *after = graph_add_node(g, &NODE_Delay);
    assert(!graph_set_pin_default(g, before, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 }));
    assert(!graph_set_pin_default(g, after, 0, 1, &(pin_value){ .type = PIN_F64, .d = .25 }));
    node *request = graph_add_node(g, &asynchronous);
    next(g, entry, 0, before);
    next(g, before, 0, request);
    next(g, request, 0, after);
    next(g, after, 0, ret);
    assert(!graph_connect_data(g, entry, 1, request, 1));
    assert(!graph_connect_data(g, entry, 2, request, 2));
    assert(!graph_connect_data(g, request, 1, ret, 1));
    graph *root = a->graph;
    node *event = graph_add_node(root, &NODE_Event),
         *loop = graph_add_node(root, &NODE_ForLoop);
    assert(!graph_set_event_name(root, event, "Run"));
    assert(graph_add_pin(root, event, 1, &inputs[1]) == 1);
    node *call = behavior_asset_add_function_call_node(a, g);
    node *remember = behavior_asset_add_set_node(a, last),
         *finish = behavior_asset_add_set_node(a, done);
    assert(!graph_set_pin_default(root, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 }));
    assert(!graph_set_pin_default(root, finish, 0, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    next(root, event, 0, loop);
    next(root, loop, 0, call);
    next(root, call, 0, remember);
    next(root, loop, 2, finish);
    assert(!graph_connect_data(root, event, 1, call, 1));
    assert(!graph_connect_data(root, loop, 1, call, 2));
    assert(!graph_connect_data(root, call, 1, remember, 1));
    return a;
}
void whole_async_clear(whole_async_capture *capture)
{
    for (int i = 0; i < acount(capture->requests); i++) {
        if (capture->requests[i]) {
            flow_host_release(capture->requests[i]);
        }
    }
    for (int i = 0; i < acount(capture->names); i++) {
        free(capture->names[i]);
    }
    afree(capture->requests);
    afree(capture->names);
    afree(capture->arguments);
    *capture = (whole_async_capture){ 0 };
}
void whole_async_smoke()
{
    behavior_asset *asset = whole_async_fixture();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, false);
    whole_async_capture capture = { 0 };
    assert(!behavior_instance_bind_service(instance, "test.async", &capture));
    char *name = xstrdup("captured \"prototype\"\nname\\");
    pin_value input = { .type = PIN_NAME, .str = name };
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(
        instance,
        "Run",
        (flow_inputs){ .values = &input, .count = 1 },
        NULL
    );
    free(name);
    assert(run && run->compiled && flow_run_status(run) == FLOW_SUSPENDED);
    double now = 0;
    for (int i = 0; i < 3; i++) {
        now += .125;
        assert(behavior_instance_tick(instance, now) >= 0);
        assert(capture.calls == i + 1 && capture.arguments[i] == i);
        assert(!strcmp(capture.names[i], "captured \"prototype\"\nname\\"));
        assert(flow_run_waiting_for_host(flow_run_active_leaf(run)));
        assert(flow_host_complete(
            capture.requests[i],
            1,
            (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 100 + i }
        ));
        assert(flow_host_resume(capture.requests[i]) == FLOW_SUSPENDED_RESULT);
        now += .25;
        assert(behavior_instance_tick(instance, now) >= 0);
        assert(instance->variables[0].u64 == (uint64_t)(100 + i));
    }
    assert(flow_run_status(run) == FLOW_COMPLETED && instance->variables[1].i == 1);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(instance);
    whole_async_clear(&capture);
    behavior_asset_free(asset);
    test_note(
        "PASS: %s asynchronous command, nested waits and captured string "
        "arguments; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
