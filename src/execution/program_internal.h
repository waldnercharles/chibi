#ifndef CHIBI_EXECUTION_PROGRAM_INTERNAL_H
#define CHIBI_EXECUTION_PROGRAM_INTERNAL_H
#include "ecs/ecs.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module_internal.h"
#include "execution/request.h"
typedef struct
{
    pin_value value;
    arena storage;
    arena *retired;
} state_value;
typedef struct execution_activation execution_activation;
typedef struct execution_inspector execution_inspector;
typedef struct
{
    execution_activation *activation;
    behavior_function_context *context;
    int command;
    const behavior_function *function;
    bool pending, delivered;
    flow_host_request *request;
    bool begun, timer, yield;
    double deadline;
    uint64_t update; /* A yield resumes on any later update. */
} command_wait;
typedef struct
{
    behavior_function_context *context;
    uint64_t action;
    pin_value *values;
    int count;
    bool break_requested;
} action_values;
struct execution_activation
{
    execution_instance *instance;
    const behavior_function *function;
    behavior_function_context context;
    behavior_function_activation activation;
    arena storage, payload;
    command_wait *wait;
    command_wait **requests;
    action_values *actions;
    execution_activation *continuation_owner;
    behavior_function_context *continuation_context;
    uint64_t origin_action;
    uint64_t task; /* Handle when this activation runs a task, else 0. */
    execution_box **boxes; /* Boxes over this activation's locals. */
    execution_box **held;  /* Boxes this task's references keep. */
    uint64_t debug_run;
    uint64_t debug_previous, debug_module, debug_function;
    bool debug_paused;
    int status;
    /* Cancelled while running: stops when control returns to it. */
    bool finished, running, has_run, stopping;
    bool cacheable; /* Cleared by inspection, suspension, cancellation or resumption. */
};
typedef struct
{
    pin_value *inputs;
    pin_value output;
    arena storage;
    bool valid;
} binding_cache;
typedef struct
{
    int function;
    uint64_t entity;
    pin_value value;
    arena storage;
    bool seen;
} observer_cache;
/* Tasks retain completion snapshots while a live handle can observe them.
 * Links name slots as index + 1, 0 for none. Running tasks form their parent's
 * child list; unused slots form the instance's spare list. */
typedef struct
{
    uint32_t generation;
    execution_activation *activation; /* NULL once the task has finished. */
    int parent, child, next, previous;
    pin_value *results;
    int result_count;
    arena storage;
    struct task_waiter *waiters;
    double started, deadline;
    bool timed, cancelled, marked;
} execution_task;
typedef struct
{
    uint64_t entity, version;
} query_change;
struct execution_instance
{
    execution_program *program;
    uint64_t owner;
    bool query_managed;
    int index;
    bool query_seen, query_ready, query_started, query_new;
    uint64_t query_variables[ECS_QUERY_VARS];
    uint64_t query_order;
    query_change *query_changes, *query_snapshot;
    pin_value *values, *node_states;
    state_value *states;
    behavior_variable_storage *layouts;
    binding_cache *bindings;
    observer_cache *observations;
    execution_activation **activations;
    execution_task *tasks;
    int spare; /* Unobserved finished slots, linked by next. */
    arena selections;
    execution_activation *running;
    execution_activation **system_activations;
    bool system_prepared, system_ran;
    int system_status;
    double time, last_time;
};
typedef struct
{
    uint64_t event, owner;
    pin_value *arguments;
    int count;
    arena storage;
} queued_event;
typedef struct queued_event_batch
{
    struct queued_event_batch *previous;
    queued_event *events;
    int first;
} queued_event_batch;
typedef struct
{
    uint64_t entity, component;
    size_t offset;
    pin_value value;
    arena storage;
} observer_edge;
typedef struct
{
    uint64_t entity, component, layout;
    size_t offset;
    pin_value value;
    execution_reduction_op op;
} component_reduction;
typedef struct
{
    uint64_t component, layout;
    const type_desc *type;
    size_t offset;
    pin_type scalar;
    bool unbounded;
} reduction_field;
struct execution_program
{
    execution_module *module;
    uint64_t updates;
    uint64_t task_lookup_steps;
    execution_program_failure failure;
    execution_profile_observer observer;
    void *observer_context;
    char *name;
    ecs_world *world;
    service_binding *services;
    /* Borrowed native entries its synchronous calls run; NULL runs them all
     * in the VM. */
    const struct execution_natives *natives;
    service_binding *pending_services;
    arena service_names;
    behavior_managed_heap *managed;
    eval_component_reader reader;
    state_value *shared;
    bool registration_state; /* Inherited by libraries before state initialization. */
    execution_instance **instances;
    execution_instance **row_table;
    int *change_queries;
    uint64_t query_rows, query_lookup_steps;
    bool query_streaming;
    int stream_system;
    uint64_t *query_admissions;
    struct query_evaluator *query_evaluators;
    size_t query_cache_bytes;
    /* Detached scalar contributions and immutable prepared query row IDs. */
    component_reduction *reductions;
    int *reduction_table;
    /* Successful immutable field metadata only; never column or row pointers. */
    reduction_field reduction_fields[8];
    int next_reduction_field;
    int unordered_reductions, ordered_reductions;
    uint64_t reduction_contributions, reduction_records;
    size_t reduction_peak_bytes;
    uint64_t *system_rows;
    struct execution_row_worker *row_workers;
    int row_parallel_jobs, row_parallel_workers;
    bool reducing;
    struct execution_program **imports;
    struct execution_program *owner_program;
    queued_event *events;
    queued_event_batch *dispatch_events; /* Stack-owned batches remain GC roots. */
    observer_edge *field_edges;
    ecs_query_plan *query_plan;
    int *binding_order;
    struct binding_schedule *binding_schedule;
    struct binding_identity *binding_identity;
    double now, delta;
    unsigned budget;
    bool dispatching, enabled, queried;
    execution_activation *running;
    execution_activation *spare_activation; /* Idle, with no borrowed instance/context. */
    execution_activation **system_activations;
    bool system_prepared, system_ran, system_update;
    int system_status;
    const execution_program *replacement_of;
    pin_value *replacement_roots; /* Stable slots pinned in the original heap. */
    int replacement_root_count;   /* Successfully registered prefix only. */
    execution_inspector *inspector;
    const execution_module *replacement_module;
};

bool program_same_type(pin_value a, pin_value b);
bool program_world_views_allowed(const behavior_function_context *);
bool program_view_disjoint(
    const behavior_function *,
    const behavior_function_context *,
    const void *,
    size_t
);
execution_instance *program_instance_row(execution_program *, uint64_t owner, const uint64_t *variables);
void program_query_consume(execution_instance *);
bool program_preflight(const execution_module *, const ecs_world *);
bool program_binding_order(execution_program *p);
void program_bindings_free(execution_program *);
size_t program_binding_storage_bytes(const execution_program *);
int program_binding_run(execution_instance *, int, double, double, unsigned);
void program_collect_states(execution_program *p);
void program_activation_free(execution_activation *a);
void program_activation_cache_free(execution_program *);
size_t program_activation_cache_bytes(const execution_program *);
size_t program_row_worker_bytes(const execution_program *);
void program_activation_cancel(execution_activation *a);
void program_bind_tasks(behavior_function_context *);
int program_task_command(
    behavior_function_context *,
    const behavior_function_command *,
    node_command_intrinsic,
    flow_host_request *
);
void program_task_finished(execution_activation *, bool cancelled);
/* Detaches the boxes over the locals of `context`, or of every frame of the
 * activation when `context` is NULL. */
void program_boxes_detach(execution_activation *, behavior_function_context *context);
void program_boxes_free(execution_activation *);
void program_tasks_free(execution_instance *);
void program_tasks_collect(execution_program *);
void program_tasks_tick(execution_instance *, double now);
bool program_task_due(const execution_activation *, double now);
bool program_request_deliver(flow_host_request *, const pin_value *, int);
int program_activation_run(execution_activation *, double, double, unsigned);
execution_activation *program_activation_new(
    execution_instance *,
    const behavior_function *,
    const pin_value *,
    int,
    double,
    double,
    unsigned
);
int program_dispatch_handler(
    execution_instance *,
    const execution_module_function *,
    const pin_value *,
    int,
    double,
    double,
    unsigned
);
bool program_instance_busy(const execution_instance *);
int program_observers_maintain(execution_program *, double, double, unsigned);
int program_observe(execution_instance *, double, double, unsigned, uint64_t, uint64_t, size_t, const pin_value *);
void program_observers_free(execution_instance *);
void program_bind_instance(const behavior_function *, behavior_function_context *);
void program_commit_instance(behavior_function_context *);
void program_apply_services(execution_program *);
void program_bind_world_callbacks(behavior_function_context *);
void program_bind_commands(behavior_function_context *);
int program_complete_action(behavior_function_context *, uint64_t, const pin_value *, int);
void program_continuation_finish(execution_activation *);
/* The task an activation, or the activation it continues, belongs to. */
uint64_t program_owning_task(const execution_activation *);
bool program_capture_reference(
    arena *,
    behavior_function_context *,
    const execution_module_capture *,
    pin_value *
);
void program_collect_payloads(execution_program *, execution_activation *extra);
static inline bool program_wait_due(const command_wait *wait, double now)
{
    return wait->timer &&
           (wait->yield ? wait->update != wait->activation->instance->program->updates
                        : wait->deadline <= now);
}

#endif
