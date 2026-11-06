#ifndef CHIBI_EXECUTION_REQUEST_H
#define CHIBI_EXECUTION_REQUEST_H

#include "types/value.h"

#include <stdbool.h>

typedef struct flow_host_request flow_host_request;
typedef enum
{
    FLOW_HOST_PENDING,
    FLOW_HOST_READY,
    FLOW_HOST_FINISHED,
    FLOW_HOST_CANCELLED,
} flow_host_state;
/* Producer activation states. Detached requests have no owner and report idle.
 * identity is borrowed only for equality; services must not inspect its memory. */
typedef enum
{
    EXECUTION_OWNER_IDLE,
    EXECUTION_OWNER_RUNNING,
    EXECUTION_OWNER_PAUSED,
    EXECUTION_OWNER_SUSPENDED,
    EXECUTION_OWNER_COMPLETED,
    EXECUTION_OWNER_FAILED,
} execution_owner_state;
/* Callbacks borrow the producer-owned activation context. complete validates
 * and snapshots a result before ready wakes the activation. detach unlinks and
 * frees that context; no callback may retain the request after detachment. */
typedef struct
{
    bool (*complete)(void *owner, int output_pin, pin_value value);
    void (*ready)(void *owner);
    void (*cancel)(void *owner);
    int (*resume)(void *owner);
    void (*detach)(void *owner);
    const void *(*identity)(void *owner);
    execution_owner_state (*state)(void *owner);
    bool (*valid)(void *owner);
    bool (*runnable)(void *owner, double now);
    int (*tick)(void *owner, double now);
} execution_request_sink;
/* Serial scheduler operations. The constructor creates one producer reference
 * and one host reference. The host releases its reference exactly once, even
 * after cancellation. Detachment drops the producer reference and makes later
 * completion fail without touching the retired activation. */
flow_host_request *execution_request_new(const execution_request_sink *, void *owner, uint64_t entity);
uint64_t execution_request_entity(const flow_host_request *);
const void *execution_request_owner(const flow_host_request *);
execution_owner_state execution_request_state(const flow_host_request *);
bool execution_request_owner_valid(const flow_host_request *);
bool execution_request_ready(const flow_host_request *, double now);
int execution_request_tick(flow_host_request *, double now);
void execution_request_retain(flow_host_request *);
void execution_request_finish(flow_host_request *, flow_host_state);
void execution_request_detach(flow_host_request *);
void *execution_request_context(const flow_host_request *, const execution_request_sink *);
flow_host_state flow_host_status(const flow_host_request *);
/* output_pin == -1 completes without a result. Other outputs are validated
 * and retained by the producer callback before the request becomes ready. */
bool flow_host_complete(flow_host_request *, int output_pin, pin_value value);
void flow_host_cancel(flow_host_request *);
void flow_host_release(flow_host_request *);
int flow_host_resume(flow_host_request *);

#endif
