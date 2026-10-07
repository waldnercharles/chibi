#ifndef CHIBI_INSTANCE_H
#define CHIBI_INSTANCE_H

#include "graph/compiler/behavior_program.h"
#include "graph/compiler/compile.h"
#include "graph/model/behavior_asset.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/service.h"

#include <stdint.h>

/* A behavior_instance is runtime behavior state referring to one
 * shared definition. It owns variable storage initialized from authored
 * defaults, per-node scratch keyed by node ID, an optional owner handle, and
 * the ready/suspended event activations that currently belong to it. */
typedef struct behavior_instance behavior_instance;
typedef struct behavior_instance_bank behavior_instance_bank;
typedef struct
{
    double current_time;
    double delta_seconds;
} behavior_clock;
/* Owned snapshot; remains valid after activation release and hot apply. */
typedef struct
{
    int code, pin;
    unsigned long steps;
    uint64_t graph_id, node_id, program_revision;
    behavior_backend backend;
    const char *event_name, *graph_name, *message;
} behavior_instance_failure;
/* Disjoint setup/execution intervals. Execution includes nested functions,
 * excluding other synchronous root events and their setup. No waiting wall time. */
typedef enum
{
    BEHAVIOR_PROFILE_SETUP,
    BEHAVIOR_PROFILE_START,
    BEHAVIOR_PROFILE_RESUME
} behavior_profile_phase;
typedef void (*behavior_profile_fn)(
    const char *event,
    uint64_t nanoseconds,
    uint64_t inclusive_nanoseconds,
    behavior_profile_phase phase,
    void *user
);
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
    const char *event_name; /* interned by the instance */
    uint64_t current_node_id;
    flow_state state;
    int error_code;
} behavior_instance_trace_entry;

/* A traced instance's record of one wire: when it last carried execution or
 * a value, on the instance clock, and how often since tracing began. */
typedef struct
{
    flow_traversal last;
    uint64_t count; /* zero marks an empty slot */
} behavior_instance_activity;

struct behavior_instance
{
    /* Borrowed; must outlive the instance. */
    behavior_asset *asset;
    /* Runtime storage, parallel to asset->variables. */
    pin_value *variables;
    /* Fields used together by prepared Tick guards and context binding. */
    behavior_variable_storage *variable_storage;
    pin_value *node_states;
    flow_run **activations; /* scheduler queue, FIFO by dispatch sequence */
    behavior_profile_fn profile;
    uint64_t services_revision; /* Invalidates prepared update bindings. */
    pin_value owner; /* PIN_HANDLE value; type PIN_FLOW when no owner */
    const behavior_clock *clock; /* Borrowed, or points to local_clock. */
    behavior_backend backend;
    int alive;
    int trace_enabled;

    /* Current arenas; first managed write adds one spare per variable.
     * Assignments swap them; pooling release frees both. Plain structs use
     * variable_storage backing. */
    arena *variable_arenas;
    int variable_count;
    instance_scratch_slot *scratch;
    const uint64_t *state_ids; /* Borrowed from the guarded program generation. */
    char **strings;            /* instance-owned string variable values */
    behavior_clock local_clock;
    flow_clock_fn now;
    void *now_user;
    flow_sink_fn sink;
    void *sink_user;
    void *profile_user;
    uint64_t schedule_revision; /* Queue edits and asynchronous readiness changes. */
    /* Idle reusable activations. */
    flow_run **activation_pool;
    behavior_instance_failure *failure; /* allocated only after a runtime failure */
    behavior_instance_trace_entry *trace; /* bounded ring; read through behavior_instance_trace */
    behavior_instance_trace_entry *trace_view; /* lazily ordered inspection cache */
    char **trace_names;
    int trace_head;
    bool trace_view_dirty;
    uint64_t trace_revision;
    /* Traced only: activity per wire, open-addressed, and the newest
     * completed run of each event, newest last, whose watches keep the
     * values pins last carried. Both clear with the trace. */
    behavior_instance_activity *activity;
    int activity_capacity, activity_used;
    uint64_t activity_generation; /* changes whenever the table clears */
    bool debug_target;
    flow_run **retained;
    bool variable_spares;
    int pool_enabled;
    int breakpoints_override;
    int breakpoints_enabled;
    service_binding *services;
    /* Optional owner notification. May invalidate caches, never execute graphs. */
    void (*services_changed)(behavior_instance *instance);
    /* Queue empty/nonempty transition. May update owner scheduling metadata,
     * never execute graphs. Different instances may notify in parallel. */
    void (*activations_changed)(behavior_instance *instance);

    /* Optional host-owned invocation storage synchronization. Called at the
     * outer execution boundary, including external host-request resumes. */
    bool (*activation_valid)(behavior_instance *instance);
    bool (*activation_enter)(behavior_instance *instance);
    void (*activation_leave)(behavior_instance *instance);
    void *activation_user;
    int activation_depth;

    /* RUN-12: the compile check, redone whenever the topology revision moves.
     * Any error refuses every dispatch/enqueue start. A lowered asset uses its
     * published program's report; only an asset with unlowered component
     * nodes compiles its own here. */
    compile_report compile;
    unsigned long long compile_revision;
    bool component_nodes;
    struct behavior_instance_page *page; /* Optional stable bank allocation. */
};

/* Create an instance and acquire the definition's mutation guard (EXEC-19,
 * RUN-20). `owner` may be NULL or a PIN_HANDLE matching the definition's
 * declared owner type. Variable storage is initialized from authored defaults
 * exactly once (VAR-11). */
behavior_instance *behavior_instance_create(
    behavior_asset *asset,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
);

/* Group instances and variable headers in stable pages, normally one bank per
 * asset in a world. Creation/destruction are serialized structural operations.
 * Destroy every instance before freeing the bank. Managed payloads keep their
 * existing instance ownership. The bank is created lazily. */
behavior_instance *behavior_instance_create_in_bank(
    behavior_instance_bank **bank,
    behavior_asset *asset,
    const pin_value *owner
);
void behavior_instance_bank_free(behavior_instance_bank *bank);

/* Explicit thread-local debug override for newly created instances. Passing
 * UNAVAILABLE restores platform selection. Existing activations are unaffected. */
behavior_backend behavior_debug_backend(behavior_backend backend);
behavior_backend behavior_default_backend();

/* Explicit parity/debug selection. Only valid before dispatch. */
int behavior_instance_set_backend(behavior_instance *bi, behavior_backend backend);
behavior_backend behavior_instance_backend(const behavior_instance *bi);
/* Inspection also snapshots held failed activations. Borrowed until the next
 * failure, explicit clear, pool release or destruction. Success retains it. */
const behavior_instance_failure *behavior_instance_last_failure(const behavior_instance *bi);
void behavior_instance_clear_failure(behavior_instance *bi);

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
    behavior_asset *asset,
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

/* Borrow the bounded, oldest-to-newest execution trace. The view is valid until
 * another activation is recorded, tracing is cleared, or the instance changes
 * definition, is released, reused, or destroyed. */
const behavior_instance_trace_entry *behavior_instance_trace(const behavior_instance *bi, int *count);
/* Changes on append, bounded overwrite and clear, not on inspection reads. */
uint64_t behavior_instance_trace_revision(const behavior_instance *bi);
/* Records a traversal in a traced instance's activity table. */
void behavior_instance_record_activity(behavior_instance *bi, const flow_traversal *traversal);
/* The activity table: `capacity` slots, empty ones with a zero count, and a
 * generation that changes whenever it clears. NULL until something ran. */
const behavior_instance_activity *behavior_instance_activity_table(
    const behavior_instance *bi,
    int *capacity,
    uint64_t *generation
);
/* The debugger's target: traced, it records every wire and value (running
 * interpreted where compiled code has no observers) and keeps the newest
 * completed run of each event, rather than pooling it, for the values pins
 * show. Off releases those runs. */
void behavior_instance_set_debug_target(behavior_instance *bi, bool target);
/* Those runs, newest last; borrowed until the next release, trace clear or
 * destruction. */
flow_run *const *behavior_instance_retained_runs(const behavior_instance *bi, int *count);

/* Dispatch a named event (EVT-9). Creates a distinct activation carrying
 * copied parameter values and runs the scheduler. NULL with a structured
 * error on unknown or duplicate name (EVT-7). */
flow_run *behavior_instance_dispatch(
    behavior_instance *bi,
    const char *name,
    flow_inputs inputs,
    flow_error *error
);

/* Ordered engine broadcasts may elide successful, unobserved activations.
 * False means no execution occurred; use ordinary dispatch. A failure returns
 * an inspectable activation in `failed`, without repeating any effects.
 * Profiling callbacks on this path must only record timing, never reenter.
 * Debugger holds must use ordinary dispatch. */
typedef struct behavior_batch behavior_batch;
/* The caller initializes *batch to NULL, reuses it across ordered instances,
 * and frees it at teardown. It caches portable scratch for one program entry. */
bool behavior_instance_dispatch_batch(
    behavior_instance *bi,
    const char *name,
    behavior_batch **batch,
    flow_run **failed
);
/* Synchronous ECS maintenance does not join the event queue and may run while
 * another activation sleeps. The caller proves the body only reads values and
 * maintains component fields; it cannot write behavior state or suspend. */
bool behavior_instance_maintain_batch(
    behavior_instance *bi,
    const char *name,
    behavior_batch **batch,
    flow_run **failed
);
void behavior_batch_free(behavior_batch *batch);
uint64_t behavior_batch_dispatches();

/* Advance an independently clocked instance to `now` (host monotonic time).
 * Runs the oldest ready activation to its next suspension (EXEC-17). */
int behavior_instance_tick(behavior_instance *bi, double now);

/* Borrow a clock while quiescent. It must outlive the active instance; update
 * it only between executions, after all workers have joined. */
void behavior_instance_bind_clock(behavior_instance *bi, const behavior_clock *clock);
/* Pump ready activations at the bound clock without modifying shared time. */
int behavior_instance_pump(behavior_instance *bi);

/* Stop one activation, preserving instance variables and scratch (RUN-17).
 * The instance and its guard are untouched. */
int behavior_instance_stop_activation(behavior_instance *bi, flow_run *act);

/* Get borrows storage until assignment/reset. Copy retains an independent value
 * using the supplied arena. Set retains an independent value. */
int behavior_instance_get_variable(behavior_instance *bi, uint64_t id, pin_value *out);
int behavior_instance_copy_variable(behavior_instance *bi, uint64_t id, arena *storage, pin_value *out);
int behavior_instance_set_variable(behavior_instance *bi, uint64_t id, const pin_value *value);
int behavior_instance_add_set_variable(behavior_instance *bi, uint64_t id, const pin_value *item);
void behavior_instance_bind_values(behavior_instance *bi, behavior_function_context *ctx);
pin_value behavior_instance_owner(const behavior_instance *bi);
void behavior_instance_set_breakpoints_enabled(behavior_instance *bi, int enabled);
void behavior_instance_set_delta_seconds(behavior_instance *bi, double delta_seconds);

/* Per-node scratch keyed by node ID (EXEC-15). Returns a zero-initialized
 * slot, created on first access; the pointer stays valid until the instance
 * is destroyed. */
pin_value *behavior_instance_scratch(behavior_instance *bi, uint64_t node_id);

/* Create a separate activation for `entry` on this instance. Used by host
 * dispatch; never C-recurses. Returns the new activation, or NULL with a
 * structured error. The scheduler runs it on the next pump. */
flow_run *behavior_instance_enqueue(behavior_instance *bi, node *entry, flow_inputs inputs, flow_error *error);

int behavior_instance_bind_service(behavior_instance *bi, const char *type_name, void *value);
void *behavior_instance_service(const behavior_instance *bi, const char *type_name);

#endif
