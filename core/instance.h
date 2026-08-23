#ifndef CBLUEPRINT_INSTANCE_H
#define CBLUEPRINT_INSTANCE_H

#include "blueprint.h"
#include "compile.h"
#include "flow.h"
#include "service.h"

#include <stdint.h>

/* ADR 0011: a blueprint_instance is runtime behavior state referring to one
 * shared definition. It owns variable storage initialized from authored
 * defaults, per-node scratch keyed by node ID, an optional owner handle, and
 * the ready/suspended event activations that currently belong to it. */
typedef struct blueprint_instance blueprint_instance;
typedef struct instance_scratch_slot
{
    uint64_t node_id;
    pin_value value;
} instance_scratch_slot;

typedef struct blueprint_delegate_binding
{
    uint64_t id;
    blueprint_instance *target;
    uint64_t event_id;
    uint64_t *pin_ids;
    pin_type *types;
    const char **type_names;
} blueprint_delegate_binding;

typedef struct blueprint_delegate_ref
{
    blueprint_instance *source;
    uint64_t binding_id;
} blueprint_delegate_ref;

struct blueprint_instance
{
    blueprint *bp;        /* borrowed; must outlive the instance */
    pin_value *variables; /* runtime storage, parallel to bp->variables */
    int variable_count;
    instance_scratch_slot *scratch;
    pin_value owner; /* PIN_HANDLE value; type PIN_FLOW when no owner */
    char **strings;  /* instance-owned string variable values */
    flow_clock_fn now;
    void *now_user;
    flow_parallel_for_fn parallel_for;
    void *parallel_user;
    double current_time;
    flow_sink_fn sink;
    void *sink_user;
    flow_run **activations;     /* scheduler queue, FIFO by dispatch sequence */
    flow_run **activation_pool; /* idle reusable activations */
    int pool_enabled;
    blueprint_delegate_binding *delegates; /* outgoing weak bindings */
    blueprint_delegate_ref *delegate_incoming;
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
blueprint_instance *blueprint_instance_create(
    blueprint *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
);

/* Destroy cancels every ready, running, paused, or suspended activation and
 * releases instance variables, scratch, and activation storage without
 * executing another node (RUN-15). Releases the mutation guard. */
void blueprint_instance_destroy(blueprint_instance *bi);

/* Release/reuse instance storage for a per-definition pool. The Blueprint must
 * remain the same definition between release and reuse. */
int blueprint_instance_pool_release(blueprint_instance *bi);
blueprint_instance *blueprint_instance_pool_reuse(
    blueprint_instance *bi,
    blueprint *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
);

/* Remove one activation from the instance queue before its storage is freed. */
int blueprint_instance_remove_activation(blueprint_instance *bi, flow_run *act);

/* Enable pooled activation storage. The setting may change only while the
 * instance has no active activations. */
int blueprint_instance_set_activation_pool(blueprint_instance *bi, int enabled);
int blueprint_instance_release_activation(blueprint_instance *bi, flow_run *act);

/* Dispatch a named event (EVT-9). Creates a distinct activation carrying
 * copied parameter values and runs the scheduler. NULL with a structured
 * error on unknown or duplicate name (EVT-7). */
flow_run *blueprint_instance_dispatch(
    blueprint_instance *bi,
    const char *name,
    flow_inputs inputs,
    flow_error *error
);

/* Advance the scheduler to `now` (host monotonic time). Runs the oldest ready
 * activation to completion or to its next suspension (EXEC-17). */
int blueprint_instance_tick(blueprint_instance *bi, double now);

/* Stop one activation, preserving instance variables and scratch (RUN-17).
 * The instance and its guard are untouched. */
int blueprint_instance_stop_activation(blueprint_instance *bi, flow_run *act);

/* Variable storage accessors. Values are shallow-copied; STR and NAME values
 * are copied into instance-owned storage. */
int blueprint_instance_get_variable(blueprint_instance *bi, uint64_t id, pin_value *out);
int blueprint_instance_set_variable(blueprint_instance *bi, uint64_t id, const pin_value *value);
pin_value blueprint_instance_owner(const blueprint_instance *bi);
void blueprint_instance_set_parallel_for(
    blueprint_instance *bi,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
);

/* Per-node scratch keyed by node ID (EXEC-15). Returns a zero-initialized
 * slot, created on first access; the pointer stays valid until the instance
 * is destroyed. */
pin_value *blueprint_instance_scratch(blueprint_instance *bi, uint64_t node_id);

/* Create a separate activation for `entry` on this instance (EVT-11). Used
 * by the Call Event node; never C-recurses. Returns the new activation, or
 * NULL with a structured error. The scheduler runs it on the next pump. */
flow_run *blueprint_instance_enqueue(
    blueprint_instance *bi,
    node *entry,
    flow_inputs inputs,
    flow_error *error
);

/* EVT-13..18: weak, ID-addressed, deterministic delegate bindings. Runtime
 * bindings belong to instances and never enter Blueprint JSON. */
uint64_t blueprint_instance_bind_event(
    blueprint_instance *source,
    blueprint_instance *target,
    uint64_t event_id
);
int blueprint_instance_unbind_event(blueprint_instance *source, uint64_t binding_id);
flow_run *blueprint_instance_dispatch_delegate(
    blueprint_instance *source,
    uint64_t binding_id,
    flow_inputs inputs,
    flow_error *error
);
int blueprint_instance_broadcast_event(
    blueprint_instance *source,
    uint64_t event_id,
    flow_inputs inputs,
    flow_error *error
);
int blueprint_instance_bind_service(blueprint_instance *bi, const char *type_name, void *value);
void *blueprint_instance_service(const blueprint_instance *bi, const char *type_name);

#endif
