#ifndef CHIBI_NATIVE_CAPABILITIES_H
#define CHIBI_NATIVE_CAPABILITIES_H

#include "capabilities/catalog.h"
#include "execution/host.h"
#include "execution/instruction.h"
#include "types/type.h"

#include <stddef.h>

/* Kernels and access descriptors have native declaration lifetime. The table
 * owns its copied catalog, service names and result maps. */
typedef struct
{
    uint64_t id, abi;
    const node_host_call *call;
    const node_access *access;
    const node_command *command;
    node_query_fn query;
    const char *query_service;
    behavior_function_op intrinsic;
    const int *result_pins;
    int result_count;
    bool instance_state;
} native_capability_binding;
typedef struct
{
    uint64_t id;
    behavior_function_instruction instruction;
} native_global_binding;
typedef struct
{
    const script_catalog *catalog;
    const native_capability_binding *bindings;
    int binding_count;
    const native_global_binding *globals;
    int global_count;
    /* Explicit target layouts, including nested structs. Borrowed only during
     * construction; every declared struct must have a supplied descriptor. */
    const type_desc *const *layouts;
    int layout_count;
} native_capability_pack;
typedef struct native_capabilities native_capabilities;

/* ABI is computed from the merged declarations and target layouts. Producer
 * binding.abi must be zero. Conflicting identities or contracts reject the pack. */
native_capabilities *native_capabilities_new(
    const native_capability_pack *,
    int count,
    char *error,
    size_t size
);
/* Retain an immutable table; free releases one reference. Kernel descriptors
 * remain borrowed and immutable for every table/module lifetime. Replacing a
 * native implementation creates a new table. References are thread-confined. */
native_capabilities *native_capabilities_retain(native_capabilities *);
void native_capabilities_free(native_capabilities *);
/* Owned table, arena and vector capacities; borrowed kernel descriptors are
 * excluded. Canonical allocation bytes, not allocator overhead or process RSS.
 * Saturates at SIZE_MAX on overflow. */
size_t native_capabilities_storage_bytes(const native_capabilities *);
const script_catalog *native_capabilities_catalog(const native_capabilities *);
/* Process-unique immutable table generation, independent of allocator reuse. */
uint64_t native_capabilities_generation(const native_capabilities *);
const native_capability_binding *native_capabilities_find(
    const native_capabilities *,
    uint64_t id,
    uint64_t abi
);
const native_global_binding *native_capabilities_global(const native_capabilities *, uint64_t id);
/* Owned reflected target descriptor snapshot, borrowed until table release. */
const type_desc *native_capabilities_layout(const native_capabilities *, uint64_t type);
#endif
