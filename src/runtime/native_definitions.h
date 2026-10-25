#ifndef CHIBI_NATIVE_DEFINITIONS_H
#define CHIBI_NATIVE_DEFINITIONS_H

#include "capabilities/definition.h"
#include "runtime/native_capabilities.h"

typedef struct
{
    const char *canonical_name;
    /* Identity, ABI and result map are derived from the shared definition;
     * these implementation fields must be zero/NULL in the producer. */
    native_capability_binding implementation;
} native_definition_binding;
typedef struct
{
    const char *canonical_name;
    behavior_function_instruction instruction;
} native_definition_global_binding;
typedef struct
{
    script_catalog base; /* Types/builtin IDs; no capabilities/globals. */
    const native_capability_definition *definitions;
    int definition_count;
    const native_definition_binding *bindings;
    int binding_count;
    const native_global_definition *global_definitions;
    int global_count;
    const native_definition_global_binding *global_bindings;
    int global_binding_count;
    const type_desc *const *layouts;
    int layout_count;
} native_definition_pack;

/* Known engine operations generate their binding from the definition; supplying
 * a second implementation for one is an error. Other operations need an
 * explicit generic binding and acquire no checked-operation guarantees.
 * Owns the same immutable table as native_capabilities_new. Shared definition
 * defaults and names are snapshotted; kernel/access descriptors remain borrowed
 * for the lifetime of every module that binds them. */
native_capabilities *native_capabilities_from_definition_packs(
    const native_capability_pack *existing,
    int existing_count,
    const native_definition_pack *,
    int count,
    char *error,
    size_t size
);
#endif
