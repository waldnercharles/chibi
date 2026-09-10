#include "graph/runtime/flow_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void host_unref(flow_host_request *request)
{
    assert(request && request->references > 0);
    if (--request->references == 0) {
        free(request);
    }
}

static void host_forget(flow_host_request *request)
{
    flow_run *run = request->run;
    int index = 0;
    while (index < acount(run->host_requests) && run->host_requests[index] != request) {
        index++;
    }
    assert(index < acount(run->host_requests));
    adel(run->host_requests, index);
    request->run = NULL;
    request->node = NULL;
    host_unref(request);
}

void flow_host_release(flow_host_request *request)
{
    assert(request && request->references > 0);
    if (request->run && request->references == 2 &&
        request->run->host_request != request) {
        host_forget(request);
    }
    host_unref(request);
}

void flow_host_finish(flow_run *run, flow_host_state state)
{
    flow_host_request *request = run->host_request;
    request->state = state;
    run->host_request = NULL;
    if (request->references == 1) {
        host_forget(request);
    }
}

void flow_host_detach(flow_run *run)
{
    for (int i = 0; i < acount(run->host_requests); i++) {
        flow_host_request *request = run->host_requests[i];
        request->state = FLOW_HOST_CANCELLED;
        request->run = NULL;
        request->node = NULL;
        host_unref(request);
    }
    afree(run->host_requests);
    run->host_request = NULL;
}

flow_host_request *flow_context_host_wait(struct flow_context *ctx)
{
    assert(ctx && ctx->run && !ctx->pause_requested && !ctx->run->host_request);
    flow_host_request *request = xmalloc(sizeof(*request));
    *request = (flow_host_request){
        .references = 2,
        .state = FLOW_HOST_PENDING,
        .run = ctx->run,
        .node = ctx->node,
    };
    apush(ctx->run->host_requests, request);
    ctx->run->host_request = request;
    return request;
}

flow_host_state flow_host_status(const flow_host_request *request)
{
    assert(request);
    return request->state;
}

flow_run *flow_host_run(const flow_host_request *request)
{
    assert(request);
    flow_run *root = request->run;
    while (root && root->parent) {
        root = root->parent;
    }
    return root;
}

bool flow_host_complete(flow_host_request *request, int output_pin, pin_value value)
{
    assert(request);
    if (request->state != FLOW_HOST_PENDING) {
        return false;
    }
    flow_run *run = request->run;
    node *n = request->node;
    if (output_pin != -1) {
        if (output_pin < 0 || output_pin >= n->output_count ||
            n->output_pins[output_pin].kind == PIN_EXEC ||
            n->output_pins[output_pin].type != value.type ||
            n->output_pins[output_pin].container != value.container ||
            strcmp(
                n->output_pins[output_pin].type_name
                    ? n->output_pins[output_pin].type_name
                    : "",
                value.type_name ? value.type_name : ""
            ) != 0) {
            return false;
        }
        pin_value copy;
        if (flow_copy_input(&run->scratch, &copy, &value)) {
            return false;
        }
        runtime_node_outputs(runtime_store_node(run->runtime, n))[output_pin] = copy;
        flow_watch_capture_outputs(run, n);
    }
    request->state = FLOW_HOST_READY;
    return true;
}

void flow_host_cancel(flow_host_request *request)
{
    assert(request);
    if (request->run) {
        flow_run_stop(flow_host_run(request));
    }
}

int flow_host_resume(flow_host_request *request)
{
    assert(request);
    if (request->state == FLOW_HOST_PENDING) {
        return FLOW_SUSPENDED_RESULT;
    }
    if (!request->run) {
        return FLOW_ERR_STATE;
    }
    return flow_run_continue(flow_host_run(request), 0);
}
