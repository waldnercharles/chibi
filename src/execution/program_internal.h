#ifndef CHIBI_EXECUTION_PROGRAM_INTERNAL_H
#define CHIBI_EXECUTION_PROGRAM_INTERNAL_H
#include "ecs/ecs.h"
#include "execution/component.h"
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
    bool begun, timer;
    double deadline;
} command_wait;
typedef struct
{
    behavior_function_context *context;
    uint64_t action;
    pin_value *values;
    int count;
    bool break_requested;
} action_values;
typedef struct
{
    behavior_function_context *context;
    int value;
    bool ready, initializing;
} local_value;
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
    local_value *locals;
    execution_activation *continuation_owner;
    behavior_function_context *continuation_context;
    uint64_t origin_action;
    uint64_t debug_run;
    uint64_t debug_previous, debug_module, debug_function;
    bool debug_paused;
    int status;
    bool finished, running, initializer, has_run;
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
struct execution_instance
{
    execution_program *program;
    uint64_t owner;
    bool query_managed;
    pin_value *values, *node_states;
    state_value *states;
    behavior_variable_storage *layouts;
    binding_cache *bindings;
    observer_cache *observations;
    execution_activation **activations;
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
typedef struct
{
    uint64_t entity, component;
    size_t offset;
    pin_value value;
    arena storage;
} observer_edge;
struct execution_program
{
    execution_module *module;
    execution_program_failure failure;
    execution_profile_observer observer;
    void *observer_context;
    char *name;
    ecs_world *world;
    service_binding *services;
    service_binding *pending_services;
    arena service_names;
    eval_component_reader reader;
    state_value *shared;
    execution_instance **instances;
    struct execution_program **imports;
    struct execution_program *owner_program;
    queued_event *events;
    observer_edge *field_edges;
    ecs_query_expr *query;
    int *binding_order;
    double now, delta;
    unsigned budget;
    bool dispatching, enabled;
    execution_activation *running;
    execution_activation **system_activations;
    bool system_prepared, system_ran;
    int system_status;
    const execution_program *replacement_of;
    execution_inspector *inspector;
    const execution_module *replacement_module;
};

bool program_same_type(pin_value a, pin_value b);
bool program_preflight(const execution_module *, const ecs_world *);
bool program_binding_order(execution_program *p);
int program_binding_run(execution_instance *, int, double, double, unsigned);
void program_collect_states(execution_program *p);
void program_activation_free(execution_activation *a);
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
pin_value *program_capture_value(behavior_function_context *, const execution_module_capture *);
int program_initialize_local(behavior_function_context *, pin_value *, bool);
int program_store_local_group(behavior_function_context *, pin_value *, int, bool);
void program_collect_payloads(execution_program *, execution_activation *extra);
const pin_value *program_event_argument(behavior_function_context *, uint64_t, int);
#endif
