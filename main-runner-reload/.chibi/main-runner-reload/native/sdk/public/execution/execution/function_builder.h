#ifndef CHIBI_EXECUTION_FUNCTION_BUILDER_H
#define CHIBI_EXECUTION_FUNCTION_BUILDER_H
#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/value_kind.h"

typedef struct behavior_function_builder behavior_function_builder;
/* A builder owns mutable lowering data until finish transfers a verified,
 * immutable definition. Input values, origins and capability descriptors are
 * borrowed until finish snapshots them; catalog types and callback code stay
 * alive for the published module generation. Native artifacts compile separately. */
behavior_function_builder *behavior_function_builder_new();
void behavior_function_builder_free(behavior_function_builder *builder);
void behavior_function_builder_signature(
    behavior_function_builder *builder,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count,
    const pin_value *variables,
    int variable_count
);
int behavior_function_builder_slot(behavior_function_builder *builder, pin_type type);
int behavior_function_builder_local(behavior_function_builder *builder, pin_value initial);
/* Logical aggregate shape of an address saved in a scalar slot or local. */
void behavior_function_builder_root(
    behavior_function_builder *,
    behavior_value_bank,
    int index,
    pin_value shape
);
/* Declare provenance explicitly; pointer-width numeric types never imply it. */
bool behavior_function_builder_kind(behavior_function_builder *, behavior_value_bank, int index, behavior_value_kind);
int behavior_function_builder_origin(behavior_function_builder *builder, behavior_function_source source);
int behavior_function_builder_source_point(behavior_function_builder *, behavior_source_point);
int behavior_function_builder_source_watch(behavior_function_builder *, behavior_source_watch);
int behavior_function_builder_emit(behavior_function_builder *builder, behavior_function_instruction instruction);
bool behavior_function_builder_patch_target(behavior_function_builder *builder, int instruction, int target);
/* Reserve contiguous typed value headers; finish snapshots their defaults. */
int behavior_function_builder_values(
    behavior_function_builder *builder,
    const pin_value *defaults,
    int count
);
int behavior_function_builder_host(
    behavior_function_builder *builder,
    const node_host_call *call,
    const node_access *access,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count
);
/* Persistent capabilities require a nonzero stable identity and ABI revision.
 * Zero is reserved for in-memory-only descriptors. IDs never encode addresses. */
bool behavior_function_builder_host_identity(
    behavior_function_builder *,
    int index,
    uint64_t id,
    uint64_t abi
);
/* Outcome IDs are borrowed until finish, then copied into the definition. */
bool behavior_function_builder_host_outcomes(
    behavior_function_builder *,
    int index,
    const uint64_t *ids,
    int count
);
bool behavior_function_builder_query_identity(
    behavior_function_builder *,
    int index,
    uint64_t id,
    uint64_t abi
);
bool behavior_function_builder_command_identity(
    behavior_function_builder *,
    int index,
    uint64_t id,
    uint64_t abi
);
/* Typed native event senders retain the selected event declaration separately
 * from their generic native capability declaration. */
bool behavior_function_builder_command_event_identity(
    behavior_function_builder *,
    int index,
    uint64_t id,
    uint64_t abi
);
/* Linked callees are borrowed from the same module generation. The module
 * retires all definitions together after its last activation is released. */
const behavior_function *behavior_function_builder_definition(const behavior_function_builder *builder);
int behavior_function_builder_call(
    behavior_function_builder *builder,
    const behavior_function *callee,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count
);
/* Starts the callee as a task; the invocation's single result is its handle. */
int behavior_function_builder_task(
    behavior_function_builder *builder,
    const behavior_function *callee,
    const pin_value *arguments,
    int argument_count
);
int behavior_function_builder_call_first(const behavior_function_builder *builder, int call);
int behavior_function_builder_call_results(const behavior_function_builder *builder, int call);
int behavior_function_builder_query(
    behavior_function_builder *builder,
    const char *service,
    const char *key,
    node_query_fn read
);
int behavior_function_builder_command(
    behavior_function_builder *builder,
    const node_command *command,
    const char *reference,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count,
    uint64_t node_id
);
/* Completion pin IDs are borrowed until finish snapshots them. Without an
 * explicit map the host uses canonical payload ordinals. */
bool behavior_function_builder_command_results_map(
    behavior_function_builder *builder,
    int command,
    const int *pins,
    int count
);
int behavior_function_builder_command_first(const behavior_function_builder *builder, int command);
int behavior_function_builder_command_results(const behavior_function_builder *builder, int command);
int behavior_function_builder_component(
    behavior_function_builder *builder,
    uint64_t id,
    bool binding,
    bool written
);
void behavior_function_builder_limits(behavior_function_builder *builder, unsigned steps, unsigned depth);
/* HOST_SLOT indexes this contiguous value bank; result values follow arguments. */
int behavior_function_builder_host_first(const behavior_function_builder *builder, int host);
int behavior_function_builder_host_results(const behavior_function_builder *builder, int host);
void behavior_function_builder_host_owned_arguments(behavior_function_builder *, int host);
void behavior_function_builder_call_owned_arguments(behavior_function_builder *, int call);
behavior_function *behavior_function_builder_finish(
    behavior_function_builder *builder,
    char *error,
    size_t size
);
/* Returns borrowed, immutable declared defaults for the signature banks. */
const pin_value *behavior_function_arguments(const behavior_function *function, int *count);
const pin_value *behavior_function_results(const behavior_function *function, int *count);
#endif
