#ifndef CHIBI_EXECUTION_HOST_H
#define CHIBI_EXECUTION_HOST_H

#include "types/value.h"

typedef struct arena arena;

struct pin_array;
struct behavior_function_context;
/* Snapshot producer: fills caller-owned reusable ecs.entity handle storage.
 * Every item is a single PIN_HANDLE with the canonical "ecs.entity" type name.
 * It cannot mutate membership, layouts or behavior storage. An error message
 * must remain valid after the snapshot is released. */
typedef const char *(*node_query_fn)(void *service, const char *key, struct pin_array *out);
/* Typed host capability. Arguments borrow scalar values, strings or plain
 * structs and arrays for this call. Arrays and their payloads are read-only
 * borrowed inputs; they cannot be retained by the helper. The caller supplies result
 * headers and non-null struct backing; the callee writes every result payload
 * on success, preserving those headers and pointers. Data nodes are read-only;
 * execution nodes may perform synchronous effects. Neither may execute graphs,
 * invalidate component/behavior storage or retain borrowed values. Error text
 * must outlive the activation. Access metadata describes the host effects. */
typedef const char *(*node_host_call_fn)(void *service, const pin_value *arguments, pin_value *results);
/* Synchronous context-aware effects receive borrowed runtime metadata. They
 * may not retain or mutate the context, arguments, component storage or graphs.
 * An explicitly bound application program service may synchronously invoke a
 * separately owned, quiescent program. Its runtime and managed heap differ
 * from the caller's; no borrowed values cross that boundary. The service
 * rejects reentry into an active program. This allowance does not apply to
 * ordinary game callbacks. */
typedef const char *(*node_context_call_fn)(
    const struct behavior_function_context *,
    void *service,
    const pin_value *arguments,
    pin_value *results
);
/* Synchronous action selects exactly one authoritative outcome ordinal. All
 * payloads and outcome IDs are borrowed for the call; error text is stable. */
typedef const char *(*node_action_call_fn)(
    const struct behavior_function_context *,
    void *service,
    const pin_value *arguments,
    int argument_count,
    pin_value *results,
    int result_count,
    const uint64_t *outcomes,
    int outcome_count,
    int *selected_outcome
);
/* NODE_PURE_CALL operation with one owned result. Inputs are borrowed for this
 * call; dynamic result payloads and names belong to storage. It writes the
 * complete result header, never evaluates graphs or retains inputs, and
 * returns stable error text on failure. A successful struct result has non-null
 * backing, matching NODE_PURE_CALL's oracle contract. */
typedef const char *(*node_value_call_fn)(
    arena *storage,
    const pin_value *arguments,
    int count,
    pin_value *result
);
typedef struct
{
    /* NULL for a native value operation that requires no host service. */
    const char *service;
    node_host_call_fn invoke;
    /* Optional service check before evaluating arguments, preserving nodes whose
     * original contract rejects a missing host before input effects or errors. */
    const char *missing_service;
    /* Exactly one of invoke/value/context/action is set. Value calls have no host service. */
    node_value_call_fn value;
    node_context_call_fn context;
    node_action_call_fn action;
    /* A missing optional service reaches the callback as NULL. */
    bool optional_service;
    /* The final declared input repeats after any fixed prefix. */
    bool variadic;
    /* Contextual data read with no observable effects. The same node and inputs
     * produce the same outputs until behavior state or host state changes.
     * Allows reuse during uninterrupted, unobserved execution. */
    bool reusable;
    /* Trusted language sequence kernel: opaque managed values stay in the
     * caller heap. The callback handles views and opaque managed pointers
     * safely; retained values require owned capture or managed roots. Only
     * service-free value callbacks may opt in; this policy is part of the ABI. */
    bool language_values;
    /* Value callback publishes fresh logical collection owners, including
     * nested fields. Text bytes and explicit pointer targets may be shared.
     * The runtime adopts these owners; borrowed selections must be copied. */
    bool owned_result;
    /* Reads its arguments only during the call and returns fresh snapshots
     * that keep managed pointer identity, so arguments may hold managed
     * pointers and are not walked at the native boundary. */
    bool borrows;
} node_host_call;
/* Synchronous commands may append deferred host effects, but cannot dispatch
 * graphs, flush structural effects or invalidate behavior/column storage. The
 * callee owns any retained arguments before returning zero or a negative error. */
typedef int (*node_command_fn)(void *service, const char *reference, const pin_value *arguments, int count);
struct flow_host_request;
/* A successful begin transfers the host reference to the service. On failure
 * it must neither retain the request nor perform deferred work. */
typedef int (*node_command_begin_fn)(
    void *service,
    const char *reference,
    const pin_value *arguments,
    int count,
    struct flow_host_request *request,
    const char **error
);
typedef enum
{
    NODE_COMMAND_NATIVE,
    NODE_COMMAND_DELAY,
    NODE_COMMAND_YIELD,
    NODE_COMMAND_TASK_JOIN,
    NODE_COMMAND_TASK_CANCEL,
    NODE_COMMAND_TASK_RUNNING,
    NODE_COMMAND_TASK_CANCEL_AFTER
} node_command_intrinsic;
typedef struct
{
    const char *service, *missing_service, *missing_reference, *failure_message;
    node_command_intrinsic intrinsic;
    node_command_fn invoke;
    node_command_begin_fn begin;
} node_command;

typedef struct
{
    const uint64_t *reads;
    int read_count;
    const uint64_t *writes;
    int write_count;
    unsigned flags;
    /* One-based host argument proven to be Self for entity-scoped writes. */
    int target_input;
    /* Additional reads by reference to reflected IDs, NULL terminated. */
    const uint64_t *const *read_refs;
} node_access;

enum
{
    NODE_EFFECT_STRUCTURAL = 1u << 0,
    NODE_EFFECT_WORLD_READ = 1u << 1,
    NODE_EFFECT_HOST_SINK = 1u << 2,
    NODE_EFFECT_PURE = 1u << 3,
    NODE_EFFECT_ENTITY_SCOPED = 1u << 4,
    /* Host supports concurrent calls while declared read dependencies are
     * stable. Writes require disjoint Self targets. No hidden world effects. */
    NODE_EFFECT_PARALLEL = 1u << 5,
    NODE_EFFECT_RANDOM = 1u << 6,
};

#endif
