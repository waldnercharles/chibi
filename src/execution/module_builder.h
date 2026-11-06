#ifndef CHIBI_EXECUTION_MODULE_BUILDER_H
#define CHIBI_EXECUTION_MODULE_BUILDER_H
#include "execution/module.h"
#include "support/arena.h"
#include "types/state_machine.h"

typedef struct
{
    int start, end, line, column;
    int document; /* Source snapshot index; zero for single-document callers. */
} execution_source_range;
typedef struct
{
    execution_source_range range;
    char message[192];
} execution_module_diagnostic;
typedef struct
{
    uint64_t id;
    pin_value initial; /* Payload may be borrowed until finish snapshots it. */
    bool shared;
    bool contains_tasks; /* Schema fact, including empty task collections. */
} execution_module_state;
typedef struct
{
    uint64_t id;
    int value;
    pin_value type;
} execution_module_capture;
typedef struct
{
    uint64_t component, layout, field;
    size_t offset;
    pin_value type;
    int argument;
} execution_field_observer;
typedef struct
{
    uint64_t id, event, event_abi, action, parent, outcome;
    int capture_count;
    execution_module_capture *captures; /* Owned malloc buffer. */
    int local_capture_count;
    bool continuation_wait;
    char *entry, *event_name; /* Owned malloc strings. */
    execution_function_kind kind;
    behavior_function *body;
    /* Optional independent evaluator returns an entity or collection of entities. */
    behavior_function *subscription;
    execution_field_observer observer;
    unsigned effects;
} execution_module_function;
typedef enum
{
    EXECUTION_QUERY_HAS,
    EXECUTION_QUERY_ALL,
    EXECUTION_QUERY_ANY,
    EXECUTION_QUERY_NOT,
    EXECUTION_QUERY_RELATION,
    EXECUTION_QUERY_GENERATOR,
    EXECUTION_QUERY_VALUE,
    EXECUTION_QUERY_CHANGED,
} execution_query_kind;
typedef struct
{
    execution_query_kind kind;
    bool required; /* Component binding membership, independent of ordered guards. */
    uint64_t component;
    uint64_t source_relationship;
    int source_traversal;
    int source_variable, target_variable;
    behavior_function *predicate; /* Owned evaluator for EXECUTION_QUERY_VALUE. */
    int *children;                /* Owned malloc buffer. */
    int child_count;
} execution_module_query;
typedef struct
{
    int query, query_variable_count;
    pin_value row_type;
    /* Arguments are captured values. Results are acceptance followed by the
     * named row fields, in declaration order. */
    behavior_function *evaluate;
} execution_module_query_description;
typedef struct
{
    uint64_t component;
    size_t offset, size;
    bool any_entity;
} execution_field_access;
typedef struct
{
    uint64_t target, component, layout;
    size_t offset;
    pin_value target_type;
    uint64_t *reads, *writes; /* Owned malloc buffers. */
    int read_count, write_count;
    execution_field_access *fields; /* support/array.h buffer. */
    int field_count;
    behavior_function *dependencies;
    int dependency_count;
    behavior_function *body;
} execution_module_binding;
typedef struct
{
    uint64_t id, capability, completion;
    native_state_kind state_kind;
    bool loop;
    pin_value *defaults; /* Buffer belongs to plan.storage; payloads snapshot at finish. */
    int result_count;
    uint64_t *outcomes; /* Owned malloc buffer. */
    int outcome_count;
} execution_module_action;
typedef struct
{
    uint64_t id, layout;
    pin_type storage;
    const pin_record *record; /* Authored layout owned by plan.storage. */
    const char *name;         /* String belongs to plan.storage. */
    size_t size;
} execution_type_requirement;
typedef struct
{
    const char *name;
    pin_value initial;
    uint64_t nominal;
    bool has_default, default_owner;
} execution_module_event_field;
typedef struct
{
    uint64_t id, abi;
    const char *name;
    execution_module_event_field *fields; /* Owned malloc buffer. */
    int field_count;
} execution_module_event_declaration;
/* Mutable producer storage. Top-level tables use support/array.h. Other strings
 * and retained capture type names belong to storage unless specified otherwise.
 * Arrays and nested owned buffers belong to this plan.
 * Imports are retained finalized modules; borrowed native callback descriptors
 * must outlive the resulting module. No plan pointer may be used after successful finish. */
typedef struct execution_module_plan
{
    execution_module_state *states;
    execution_module_function *functions;
    execution_module_query *queries;
    execution_module_query_description *descriptions;
    execution_module_binding *bindings;
    execution_module_action *actions;
    execution_module_diagnostic *diagnostics;
    const char **services;
    int service_count;
    execution_module **imports;
    int import_count;
    int state_count, function_count, query_count, binding_count, action_count,
        diagnostic_count;
    int description_count, system_description; /* One-based scheduled description. */
    int query;
    int query_filter; /* One-based authored header filter; zero if absent. */
    int query_variable_count;
    uint64_t id, owner_type;
    arena storage;
    bool valid;
    execution_type_requirement *types;
    int type_count;
    execution_module_event_declaration *events;
    int event_count;
} execution_module_plan;
execution_module_plan *execution_module_plan_new();
void execution_module_plan_free(execution_module_plan *);
/* Success transfers the same owned payload and sets *plan to NULL. Failure
 * leaves *plan owned by the caller for diagnostics or destruction; it cannot
 * affect a live module. Compiled function artifacts are reused on a retry. */
execution_module *execution_module_finish(
    execution_module_plan **plan,
    behavior_backend backend,
    char *error,
    size_t size
);
#endif
