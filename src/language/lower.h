#ifndef CHIBI_LANGUAGE_LOWER_H
#define CHIBI_LANGUAGE_LOWER_H
#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "language/semantic.h"

/* Linking resolves stable catalog IDs against the embedding application's ABI.
 * The resolver never receives source spellings or graph objects. Descriptors
 * and their callbacks must outlive the returned executable module. */
typedef struct
{
    const node_host_call *call;
    const behavior_function *function;
    execution_module *module;
    const node_access *access;
    const node_command *command;
    node_query_fn query;
    const char *query_service;
    const char *reference;
    const int *result_pins;
    int result_count;
    behavior_function_op intrinsic; /* BF_DELAY, BF_OWNER, BF_DELTA, or zero. */
} script_lower_host;
typedef struct
{
    void *user;
    bool (*host)(void *user, uint64_t capability, script_lower_host *out);
    bool (*action)(void *user, uint64_t capability, uint64_t instance, int entry, script_lower_host *out);
    bool (*global)(void *user, uint64_t capability, behavior_function_instruction *out);
} script_lower_bindings;
/* Invalid modules retain diagnostics but contain no executable bodies. Neither
 * analysis nor catalog needs to outlive the module. Host descriptors do. */
execution_module_plan *script_lower(
    const script_analysis *,
    const script_catalog *,
    const script_lower_bindings *
);
#endif
