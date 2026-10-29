#include "execution/request.h"

#include "execution/status.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

struct flow_host_request
{
    int references;
    flow_host_state state;
    bool active;
    const execution_request_sink *sink;
    void *owner;
    uint64_t entity;
};

static void unref(flow_host_request *request)
{
    assert(request->references > 0);
    if (!--request->references) {
        free(request);
    }
}

flow_host_request *execution_request_new(const execution_request_sink *sink, void *owner, uint64_t entity)
{
    assert(sink && owner && sink->complete && sink->detach);
    flow_host_request *request = xmalloc(sizeof *request);
    *request = (flow_host_request){ .references = 2,
                                    .state = FLOW_HOST_PENDING,
                                    .active = true,
                                    .sink = sink,
                                    .owner = owner,
                                    .entity = entity };
    return request;
}

void execution_request_retain(flow_host_request *request)
{
    assert(request && request->references > 0);
    request->references++;
}

const void *execution_request_owner(const flow_host_request *request)
{
    assert(request);
    return request->owner && request->sink->identity
             ? request->sink->identity(request->owner)
             : NULL;
}

execution_owner_state execution_request_state(const flow_host_request *request)
{
    assert(request);
    return request->owner && request->sink->state
             ? request->sink->state(request->owner)
             : EXECUTION_OWNER_IDLE;
}

bool execution_request_owner_valid(const flow_host_request *request)
{
    assert(request);
    return request->owner && request->sink->valid &&
           request->sink->valid(request->owner);
}

bool execution_request_ready(const flow_host_request *request, double now)
{
    assert(request);
    return request->owner && request->sink->runnable &&
           request->sink->runnable(request->owner, now);
}

int execution_request_tick(flow_host_request *request, double now)
{
    assert(request);
    return request->owner && request->sink->tick
             ? request->sink->tick(request->owner, now)
             : FLOW_ERR_STATE;
}

static void detach(flow_host_request *request)
{
    void *owner = request->owner;
    const execution_request_sink *sink = request->sink;
    request->owner = NULL;
    request->sink = NULL;
    sink->detach(owner);
    unref(request);
}

void execution_request_detach(flow_host_request *request)
{
    assert(request && request->owner);
    request->state = FLOW_HOST_CANCELLED;
    detach(request);
}

void execution_request_finish(flow_host_request *request, flow_host_state state)
{
    assert(request && request->owner && request->active);
    assert(state == FLOW_HOST_FINISHED || state == FLOW_HOST_CANCELLED);
    request->state = state;
    request->active = false;
    if (request->references == 1) {
        detach(request);
    }
}

void *execution_request_context(const flow_host_request *request, const execution_request_sink *sink)
{
    assert(request);
    return request->sink == sink ? request->owner : NULL;
}

void flow_host_release(flow_host_request *request)
{
    assert(request && request->references > 0);
    if (request->owner && !request->active && request->references == 2) {
        detach(request);
    }
    unref(request);
}

flow_host_state flow_host_status(const flow_host_request *request)
{
    assert(request);
    return request->state;
}

bool flow_host_complete(flow_host_request *request, int output_pin, pin_value value)
{
    assert(request);
    if (request->state != FLOW_HOST_PENDING || !request->owner ||
        !request->sink->complete(request->owner, output_pin, value)) {
        return false;
    }
    request->state = FLOW_HOST_READY;
    if (request->sink->ready) {
        request->sink->ready(request->owner);
    }
    return true;
}

void flow_host_cancel(flow_host_request *request)
{
    assert(request);
    if (request->owner && request->sink->cancel) {
        request->sink->cancel(request->owner);
    }
}

int flow_host_resume(flow_host_request *request)
{
    assert(request);
    if (request->state == FLOW_HOST_PENDING) {
        return FLOW_SUSPENDED_RESULT;
    }
    return request->owner && request->sink->resume
             ? request->sink->resume(request->owner)
             : FLOW_ERR_STATE;
}
