#ifndef CBLUEPRINT_INSTANCE_H
#define CBLUEPRINT_INSTANCE_H

#include "blueprint.h"
#include "flow.h"

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

struct blueprint_instance
{
    blueprint *bp;        /* borrowed; must outlive the instance */
    pin_value *variables; /* runtime storage, parallel to bp->variables */
    instance_scratch_slot *scratch;
    pin_value owner; /* PIN_HANDLE value; type PIN_FLOW when no owner */
    char **strings;  /* instance-owned string variable values */
    flow_clock_fn now;
    void *now_user;
    double current_time;
    flow_sink_fn sink;
    void *sink_user;
    flow_run **activations; /* scheduler queue, FIFO by dispatch sequence */
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

#endif
