#ifndef CBLUEPRINT_INSTANCE_H
#define CBLUEPRINT_INSTANCE_H

#include "behavior_asset.h"
#include "compile.h"
#include "flow.h"
#include "service.h"

#include <stdint.h>

/* A behavior_instance is runtime behavior state referring to one
 * shared definition. It owns variable storage initialized from authored
 * defaults, per-node scratch keyed by node ID, an optional owner handle, and
 * the ready/suspended event activations that currently belong to it. */
typedef struct behavior_instance behavior_instance;
typedef enum
{
    INSTANCE_UPDATE_READY,
    INSTANCE_UPDATE_BUSY,
    INSTANCE_UPDATE_RESTART,
} instance_update_status;
typedef struct instance_scratch_slot
{
    uint64_t node_id;
    pin_value value;
} instance_scratch_slot;

#define BEHAVIOR_ASSET_INSTANCE_TRACE_MAX 512

typedef struct
{
    flow_traversal traversal;
    char *event_name;
    uint64_t current_node_id;
    flow_state state;
    int error_code;
} behavior_instance_trace_entry;

typedef struct behavior_delegate_binding
{
    uint64_t id;
    behavior_instance *target;
    uint64_t event_id;
    uint64_t *pin_ids;
    pin_type *types;
    const char **type_names;
} behavior_delegate_binding;

typedef struct behavior_delegate_ref
{
    behavior_instance *source;
    uint64_t binding_id;
} behavior_delegate_ref;

struct behavior_instance
{
    behavior_asset *bp;   /* borrowed; must outlive the instance */
    pin_value *variables; /* runtime storage, parallel to bp->variables */
    arena *variable_arenas; /* owned values; replaced atomically with each assignment */
    int variable_count;
    instance_scratch_slot *scratch;
    pin_value owner; /* PIN_HANDLE value; type PIN_FLOW when no owner */
    char **strings;  /* instance-owned string variable values */
    flow_clock_fn now;
    void *now_user;
    flow_parallel_for_fn parallel_for;
    void *parallel_user;
    double current_time;
    double delta_seconds;
    flow_sink_fn sink;
    void *sink_user;
    flow_run **activations;     /* scheduler queue, FIFO by dispatch sequence */
    flow_run **activation_pool; /* idle reusable activations */
    behavior_instance_trace_entry *trace; /* oldest-to-newest completed records */
    int pool_enabled;
    int trace_enabled;
    int breakpoints_override;
    int breakpoints_enabled;
    behavior_delegate_binding *delegates; /* outgoing weak bindings */
    behavior_delegate_ref *delegate_incoming;
    service_binding *services;
    uint64_t next_delegate_id;
    int alive;

    /* RUN-12: cached compile report, rebuilt whenever the topology revision
     * moves. Any error refuses every dispatch/enqueue start. The report's
     * message strings stay valid for the instance's lifetime. */
    compile_report compile;
    unsigned long long compile_revision;
};

/* Create an instance and acquire the definition's mutation guard (EXEC-19,
 * RUN-20). `owner` may be NULL or a PIN_HANDLE matching the definition's
 * declared owner type. Variable storage is initialized from authored defaults
 * exactly once (VAR-11). */
behavior_instance *behavior_instance_create(
    behavior_asset *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
);

/* Destroy cancels every ready, running, paused, or suspended activation and
 * releases instance variables, scratch, and activation storage without
 * executing another node (RUN-15). Releases the mutation guard. */
void behavior_instance_destroy(behavior_instance *bi);
/* At a scheduler boundary, update a compiled definition without Begin or state
 * reset. Check every affected instance before updating any of them. */
instance_update_status behavior_instance_can_update(
    const behavior_instance *bi,
    const behavior_asset *next
);
void behavior_instance_update(behavior_instance *bi, behavior_asset *next);

/* Release/reuse instance storage for a per-definition pool. The Behavior Asset must
 * remain the same definition between release and reuse. */
int behavior_instance_pool_release(behavior_instance *bi);
behavior_instance *behavior_instance_pool_reuse(
    behavior_instance *bi,
    behavior_asset *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
);

/* Remove one activation from the instance queue before its storage is freed. */
int behavior_instance_remove_activation(behavior_instance *bi, flow_run *act);

/* Enable pooled activation storage. The setting may change only while the
 * instance has no active activations. */
int behavior_instance_set_activation_pool(behavior_instance *bi, int enabled);
int behavior_instance_release_activation(behavior_instance *bi, flow_run *act);

/* Rebind a reusable synchronous instance to one Entity. Runtime variables and
 * scratch are reset; no activation may be queued. */
int behavior_instance_rebind(behavior_instance *bi, const pin_value *owner);
void behavior_instance_set_trace_enabled(behavior_instance *bi, int enabled);

/* Borrow the bounded, oldest-to-newest execution trace. Entries remain valid
 * until the instance is released, reused, or destroyed. */
const behavior_instance_trace_entry *behavior_instance_trace(const behavior_instance *bi, int *count);

/* Dispatch a named event (EVT-9). Creates a distinct activation carrying
 * copied parameter values and runs the scheduler. NULL with a structured
 * error on unknown or duplicate name (EVT-7). */
flow_run *behavior_instance_dispatch(
    behavior_instance *bi,
    const char *name,
    flow_inputs inputs,
    flow_error *error
);

/* Advance the scheduler to `now` (host monotonic time). Runs the oldest ready
 * activation to completion or to its next suspension (EXEC-17). */
int behavior_instance_tick(behavior_instance *bi, double now);

/* Stop one activation, preserving instance variables and scratch (RUN-17).
 * The instance and its guard are untouched. */
int behavior_instance_stop_activation(behavior_instance *bi, flow_run *act);

/* Variable storage accessors. Values are shallow-copied; STR and NAME values
 * are copied into instance-owned storage. */
int behavior_instance_get_variable(behavior_instance *bi, uint64_t id, pin_value *out);
int behavior_instance_copy_variable(behavior_instance *bi, uint64_t id, arena *storage, pin_value *out);
int behavior_instance_set_variable(behavior_instance *bi, uint64_t id, const pin_value *value);
pin_value behavior_instance_owner(const behavior_instance *bi);
void behavior_instance_set_parallel_for(
    behavior_instance *bi,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
);
void behavior_instance_set_breakpoints_enabled(behavior_instance *bi, int enabled);
void behavior_instance_set_delta_seconds(behavior_instance *bi, double delta_seconds);
int behavior_instance_breakpoints_enabled(const behavior_instance *bi);

/* Per-node scratch keyed by node ID (EXEC-15). Returns a zero-initialized
 * slot, created on first access; the pointer stays valid until the instance
 * is destroyed. */
pin_value *behavior_instance_scratch(behavior_instance *bi, uint64_t node_id);

/* Create a separate activation for `entry` on this instance. Used by host
 * dispatch; never C-recurses. Returns the new activation, or NULL with a
 * structured error. The scheduler runs it on the next pump. */
flow_run *behavior_instance_enqueue(behavior_instance *bi, node *entry, flow_inputs inputs, flow_error *error);

/* EVT-13..18: weak, ID-addressed, deterministic delegate bindings. Runtime
 * bindings belong to instances and never enter Behavior Asset JSON. */
uint64_t behavior_instance_bind_event(behavior_instance *source, behavior_instance *target, uint64_t event_id);
int behavior_instance_unbind_event(behavior_instance *source, uint64_t binding_id);
flow_run *behavior_instance_dispatch_delegate(
    behavior_instance *source,
    uint64_t binding_id,
    flow_inputs inputs,
    flow_error *error
);
int behavior_instance_broadcast_event(
    behavior_instance *source,
    uint64_t event_id,
    flow_inputs inputs,
    flow_error *error
);
int behavior_instance_bind_service(behavior_instance *bi, const char *type_name, void *value);
void *behavior_instance_service(const behavior_instance *bi, const char *type_name);

#endif
