#ifndef CHIBI_EXECUTION_PREPARED_PRODUCER_H
#define CHIBI_EXECUTION_PREPARED_PRODUCER_H

#include "execution/host.h"
#include "execution/module.h"
#include "types/type.h"

/* Trusted host policy, borrowed for one validation call. Expected callback
 * contracts and ABIs must come from engine declarations, never the candidate. */
typedef struct
{
    pin_value shape;
    int group; /* Independent generic element group; zero is unconstrained. */
    bool generic;
} execution_producer_port;
typedef struct
{
    uint64_t id, abi;
    const node_host_call *call;
    const node_access *access;
    const execution_producer_port *inputs, *outputs;
    int input_count, output_count;
    bool variadic, variadic_optional;
    /* Successful scalar-struct results have backing. Record-array results
     * contain only backed elements. The host must enforce this guarantee. */
    bool backed_results;
} execution_producer_call;
typedef struct
{
    uint64_t id;
    pin_value initial;
} execution_producer_state;
typedef struct
{
    const char *name;
    pin_value shape;
} execution_producer_field;
typedef struct
{
    uint64_t id, abi;
    const char *name;
    const execution_producer_field *fields;
    int field_count;
} execution_producer_event;
typedef struct
{
    uint64_t module;
    const execution_producer_state *states;
    int state_count;
    const execution_producer_event *events;
    int event_count;
    const execution_producer_call *calls;
    int call_count;
    const type_desc *const *native_types;
    int native_type_count;
    const pin_record *const *required_records;
    int required_record_count;
    const char *service;
} execution_producer_contract;

/* Fail-closed synchronous producer subset. Inspects all roots, owned bodies,
 * callees and imports, including unused executable definitions. Raw context,
 * reference, component, task and dynamic array address operations are denied.
 * Use typed native array accessors; only immutable flat record payload reads
 * and typed private value-bank writes are accepted. Work is bounded internally.
 * This proves an executable surface, never source provenance or authority. */
bool execution_prepared_producer_valid(
    const execution_module *,
    const execution_producer_contract *,
    char *error,
    size_t size
);

#endif
