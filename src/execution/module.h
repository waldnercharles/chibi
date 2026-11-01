#ifndef CHIBI_EXECUTION_MODULE_H
#define CHIBI_EXECUTION_MODULE_H
#include "execution/backend.h"
#include "execution/function.h"
#include "execution/service.h"

typedef struct ecs_world ecs_world;
typedef struct execution_program execution_program;
typedef struct execution_module execution_module;
/* Owned canonical storage including arena/vector capacities and nested tables.
 * Imports, backend code/cache allocations and live instance banks are excluded.
 * SIZE_MAX reports overflow. */
size_t execution_module_storage_bytes(const execution_module *);
typedef struct
{
    size_t managed_bytes, managed_capacity; /* The program family's managed heap. */
    size_t state_bank_bytes; /* Retained states and their views. */
} execution_memory_usage;
/* After each call, bytes describe live managed values; capacity includes
 * reusable allocation slots. State bank bytes are logical byte counts and
 * exclude allocator overhead and nested buffers. */
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
/* A program owns one registration's retained state. Services must be NULL or
 * a support/array.h vector (apush/acount), never a fixed or sentinel array.
 * Binding rows are copied; the input vector may retire after return. Its
 * type_name strings and host values are borrowed and must outlive the program,
 * including imported programs and replacements. */
/* Register host-retained pointer values with this heap before the next run.
 * Destroying the program clears registered root slots; pointers never outlive
 * their owning program. The returned heap remains stable across replacement. */
behavior_managed_heap *execution_program_managed_heap(execution_program *);
execution_program *execution_program_new(execution_module *module, const service_binding *services);
void execution_program_set_name(execution_program *program, const char *name);
/* Rebinding during a call is applied when the call ends; the running call
 * keeps its current service. The name argument is needed only during this
 * call. Host values remain borrowed; an old value must remain alive until the
 * running call and the deferred rebind have finished. */
int execution_program_bind_service(execution_program *program, const char *name, void *value);
/* Read-only preflight for required services and the deferred rebind queue. */
bool execution_program_service_bind_valid(const execution_program *, const char *name, void *value);
void execution_program_free(execution_program *program);
/* Calls one package-level authored function, keeping its retained state and
 * imported modules. The function takes no arguments and returns nothing or
 * one int; result receives zero or that value on success. A zero budget runs
 * the default number of steps. The program must be idle. A wait or task fails
 * the call without leaving work queued. */
int execution_program_call(execution_program *, uint64_t function, int *result, unsigned budget);
const pin_value *execution_program_state(const execution_program *, uint64_t id);
int execution_program_set_state(execution_program *, uint64_t id, pin_value value);
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
