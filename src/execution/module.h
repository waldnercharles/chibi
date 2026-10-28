#ifndef CHIBI_EXECUTION_MODULE_H
#define CHIBI_EXECUTION_MODULE_H
#include "execution/backend.h"
#include "execution/function.h"
#include "execution/service.h"

typedef struct ecs_world ecs_world;
typedef struct execution_program execution_program;
typedef struct execution_instance execution_instance;
typedef struct execution_module execution_module;
typedef struct task_system task_system;
typedef struct
{
    const uint64_t *reads, *writes, *reduces;
    int read_count, write_count, reduce_count;
    bool declared, structural, host_sink, world_read, deterministic, parallel_safe;
    /* First instruction whose effects could not be proved; -1 when absent. */
    int unknown_operation, unknown_instruction;
    uint64_t unknown_capability, unknown_function;
} execution_effect_summary;
/* Immutable verified effects, rederived after prepared linking. parallel_safe
 * proves a synchronous SYSTEM closure with disjoint owning-entity writes or
 * private reduction contributions. Reduced components are published in order
 * at the system boundary and conflict with ordinary readers and writers.
 * Unknown effects retain conservative flags and cannot enter worker execution. */
execution_effect_summary execution_module_effects(const execution_module *);
/* Includes module entry points, binding bodies, and imported modules. For one
 * entry point use behavior_function_costs(info.body), which follows only calls. */
execution_cost_summary execution_module_costs(const execution_module *);

/* Owned canonical storage including arena/vector capacities and nested tables.
 * Imports, backend code/cache allocations and live instance banks are excluded.
 * SIZE_MAX reports overflow. */
size_t execution_module_storage_bytes(const execution_module *);
/* Read-only component/query/binding layout validation against installed schemas;
 * no program, entity, service or dispatch is created. */
bool execution_module_components_valid(const execution_module *, const ecs_world *);
typedef struct
{
    size_t payload_bytes, payload_capacity; /* Activation payloads. */
    size_t managed_bytes, managed_capacity; /* The program family's managed heap. */
    int task_slots, retained_tasks;
    size_t task_result_bytes;
    uint64_t task_lookup_steps; /* Handle-index probes in the last collection. */
    int instance_count;
    size_t instance_bytes, state_bank_bytes, activation_bytes;
    size_t activation_cache_bytes;
    size_t query_history_bytes, query_index_bytes, ecs_change_bytes;
    size_t query_cache_bytes;
    size_t binding_schedule_bytes;
    size_t reduction_bytes, reduction_capacity;
    size_t system_row_bytes, system_row_capacity;
    size_t row_worker_capacity;
    int row_parallel_jobs, row_parallel_workers;
    uint64_t reduction_contributions, reduction_records;
    size_t reduction_peak_bytes;
    uint64_t query_rows, query_lookup_steps;
} execution_memory_usage;
/* After each safe event/update boundary, bytes describe live managed values;
 * capacity includes reusable allocation slots. Retained external component cell
 * backing is included once per owning arena; a cell shared by distinct arenas
 * is conservatively counted once for each owner. Task result bytes include their
 * owned snapshots; task slots include reusable slots. Instance/activation headers,
 * fixed state banks, query history and index slots are logical byte counts and
 * exclude allocator overhead and nested buffers. activation_cache_bytes counts
 * owned idle activation storage, including retired payload backing, separately
 * from live payload_capacity, bounded to 64 KiB per program.
 * query_cache_bytes counts idle predicate evaluators, independently bounded to
 * 64 KiB per program, including fixed host, helper and arena storage.
 * row_worker_capacity counts private row execution scratch, bounded to 1 MiB
 * when idle and at most the executor worker budget (maximum 32 chunks).
 * ecs_change_bytes is shared
 * world-owned storage (do not sum across programs); query counters describe the
 * latest refresh. system_row_bytes/capacity include staged query row headers;
 * their payloads belong to the managed heap. Stateless streaming retains no
 * instance headers. Binding
 * schedule bytes include retained buffers and an upper bound for shared
 * generation keys, which may also belong to another program's cache. */
execution_memory_usage execution_program_memory_usage(const execution_program *);
/* Owned snapshot. Copying the record preserves all text and source coordinates.
 * The first failure is retained until explicitly cleared. The borrowed getter
 * remains valid until the program is changed or released. */
typedef struct
{
    int status;
    behavior_function_failure kind;
    uint64_t module_id, function_id, owner, node_id;
    int start, end, line, column;
    char asset_name[128], message[192];
} execution_program_failure;
const execution_program_failure *execution_program_error(const execution_program *);
void execution_program_clear_error(execution_program *);

typedef enum
{
    EXECUTION_FUNCTION_AUTHORED,
    EXECUTION_FUNCTION_HANDLER,
    EXECUTION_FUNCTION_CONTINUATION,
    EXECUTION_FUNCTION_SYSTEM,
    EXECUTION_FUNCTION_MACRO_ENTRY,
} execution_function_kind;
/* Borrowed metadata, valid until the retained module is released. */
typedef struct
{
    uint64_t id, event, event_abi, action, parent, outcome;
    int capture_count, local_capture_count;
    bool continuation_wait;
    const char *entry, *event_name;
    execution_function_kind kind;
    const behavior_function *body, *subscription;
    unsigned effects;
} execution_module_function_info;
uint64_t execution_module_id(const execution_module *);
behavior_backend execution_module_backend(const execution_module *);
int execution_module_function_count(const execution_module *);
execution_module_function_info execution_module_function_at(const execution_module *, int index);
int execution_module_import_count(const execution_module *);
/* Borrowed immutable generation, valid while the owning module is retained. */
const execution_module *execution_module_import_at(const execution_module *, int index);
uint64_t execution_module_event(const execution_module *module, const char *name);
typedef struct
{
    uint64_t id, abi;
    const char *name;
    int field_count;
} execution_module_event_info;
typedef struct
{
    const char *name;
    pin_value initial;
    uint64_t nominal;
    bool has_default, default_owner;
} execution_module_event_field_info;
/* Declaration names and default payloads are borrowed from the retained module.
 * Field names preserve the declaration independently of handler aliases. */
int execution_module_event_count(const execution_module *);
execution_module_event_info execution_module_event_at(const execution_module *, int index);
execution_module_event_field_info execution_module_event_field_at(
    const execution_module *,
    int event,
    int field
);
const char *const *execution_module_services(const execution_module *module, int *count);
void execution_module_retain(execution_module *module);
void execution_module_free(execution_module *module);
/* A program owns one registration's shared state and entity attachments.
 * Services must be NULL or a support/array.h vector (apush/acount), never a
 * fixed or sentinel array. Binding rows are copied; the input vector may retire
 * after return. Its type_name strings, host values and ECS world are borrowed
 * and must outlive the program, including imported programs and replacements. */
/* Register host-retained pointer values with this heap before the next run.
 * Destroying the program clears registered root slots; pointers never outlive
 * their owning program. The returned heap remains stable across replacement. */
behavior_managed_heap *execution_program_managed_heap(execution_program *);
execution_program *execution_program_new(
    execution_module *module,
    ecs_world *world,
    const service_binding *services
);
/* Optional inclusive process-CPU observation at actual function entry/resume.
 * Nested activation samples overlap; synchronous outlined calls are included
 * in their parent sample. No clock is read while the observer is NULL. All
 * pointers are borrowed only during the callback; callbacks cannot mutate the
 * program. Install/remove only at a quiescent root-program boundary. */
typedef struct
{
    const execution_program *program;
    const execution_module *module;
    const behavior_function *function;
    uint64_t owner;
    bool resume, cpu_available;
    double cpu_seconds;
    int status;
} execution_profile_sample;
typedef void (*execution_profile_observer)(void *, const execution_profile_sample *);
void execution_program_observe(execution_program *, execution_profile_observer, void *);
bool execution_program_observed(const execution_program *);
void execution_program_set_name(execution_program *program, const char *name);
void execution_program_invalidate_bindings(execution_program *program);
/* Rebinding during execution is applied at the next owning program boundary;
 * active callbacks retain their current service. The name argument is needed
 * only during this call. Host values remain borrowed; an old value must remain
 * alive until active callbacks and the deferred boundary have finished. */
int execution_program_bind_service(execution_program *program, const char *name, void *value);
/* Read-only preflight for required services and the deferred rebind queue. */
bool execution_program_service_bind_valid(const execution_program *, const char *name, void *value);
void execution_program_free(execution_program *program);
/* Disabling retires automatic query attachments. Explicit attachments remain active. */
void execution_program_set_enabled(execution_program *program, bool enabled);
/* Multi-target systems attach their tuples automatically; owner-only attach
 * returns NULL for those modules. */
execution_instance *execution_program_attach(execution_program *program, uint64_t owner);
void execution_program_detach(execution_program *program, uint64_t owner);
execution_instance *execution_program_instance(execution_program *program, uint64_t owner);
/* Exact tuple lookup. Variables are numbered from one; count excludes root.
 * The owner-only lookup returns the first attached row; detach removes all
 * rows of the owner. This accessor preserves generation-bearing handles. */
execution_instance *execution_program_row(
    execution_program *,
    uint64_t owner,
    const uint64_t *variables,
    int count
);
/* Queued payloads are snapshotted. Dispatch and structural effects occur at the
 * caller's event boundary, preserving stable iteration membership. */
/* Nested dispatch appends to the next event boundary; outer dispatch pumps the
 * detached initial queue without starting systems or reevaluating bindings. */
int execution_program_dispatch(
    execution_program *program,
    uint64_t event,
    uint64_t source,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
);
int execution_program_emit(
    execution_program *program,
    uint64_t event,
    uint64_t owner,
    const pin_value *arguments,
    int count
);
int execution_program_update(execution_program *program, double now, double delta, unsigned budget);
/* Serial preparation refreshes query membership, Begin, queued events and
 * ready continuations, then owns the system activations for one update.
 * run_system performs no query refresh. The caller keeps world membership and
 * services fixed until serial finish_system consumes the prepared update.
 * Create an inspector before preparation; an existing inspector remains usable
 * while its prepared update is paused. */
int execution_program_prepare_system(execution_program *, double now, double delta, unsigned budget);
int execution_program_run_system(execution_program *);
/* Submitting-thread entry. Inferred independent reducer rows may use the
 * borrowed executor; other bodies and small dispatches run serially. */
int execution_program_run_system_parallel(execution_program *, const task_system *);
int execution_program_finish_system(execution_program *);
/* Cancel publication before finish, for a successful worker after an earlier
 * scheduler peer failed. Collection storage retains at most 64 KiB when idle. */
void execution_program_discard_reductions(execution_program *);
/* Cancel a quiescent prepared dispatch, including a retained debugger pause.
 * Discards contributions and releases pending activations without publication. */
void execution_program_cancel_system(execution_program *);
/* Orders all attached entity bindings by overlapping normalized ECS fields.
 * A cycle is rejected before any binding output is committed. */
int execution_program_bindings_all(
    execution_program *const *programs,
    int count,
    double now,
    double delta,
    unsigned budget,
    char *error,
    size_t size
);
int execution_program_bindings(execution_program *program, double now, double delta, unsigned budget);
/* Delivers an authoritative queued field edge to qualified observers. Generic
 * event dispatch does not deliver these handlers a second time. */
int execution_program_field_changed(
    execution_program *,
    uint64_t entity,
    uint64_t component,
    size_t offset,
    pin_value,
    double now,
    double delta,
    unsigned budget
);

int execution_program_pump(execution_program *program, double now, double delta, unsigned budget);
int execution_program_step(execution_program *program, double now, double delta, unsigned budget);
int execution_instance_dispatch(
    execution_instance *instance,
    uint64_t event,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
);
const pin_value *execution_instance_state(const execution_instance *instance, uint64_t id);
int execution_instance_set_state(execution_instance *instance, uint64_t id, pin_value value);
int execution_instance_pending(const execution_instance *instance);
/* Matching stable IDs keep state, converted when its type changed. A reload
 * always applies: state that cannot convert, or that reaches a record layout
 * the replacement changed, restarts from its initializer. Replacement cancels
 * old activations before releasing the prior module generation. */
int execution_program_replace(execution_program *program, execution_module *module, bool restart);
/* Preparation leaves live state/work untouched. Discard with program_free.
 * The original program must outlive its staged candidate. Commit consumes the
 * matching staged replacement at a quiescent boundary. */
execution_program *execution_program_prepare_replace(
    const execution_program *,
    execution_module *,
    bool restart
);
/* Existing bindings win over defaults; explicit overrides win over both.
 * The candidate owns copied names and borrows the supplied service values. */
execution_program *execution_program_prepare_replace_services(
    const execution_program *,
    execution_module *,
    bool restart,
    const service_binding *defaults,
    const service_binding *overrides
);
/* Resets kept values that reach a record layout the replacement changed: the
 * program may change them between preparation and commit. */
void execution_program_commit_replace(execution_program *, execution_program *prepared);
/* Borrowed current finalized generation; ends at replacement or program free. */
const execution_module *execution_program_module(const execution_program *);
#endif
