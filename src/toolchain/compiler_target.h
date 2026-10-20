#ifndef CHIBI_TOOLCHAIN_COMPILER_TARGET_H
#define CHIBI_TOOLCHAIN_COMPILER_TARGET_H

#include "capabilities/catalog.h"

#include <stddef.h>

/* Compile's last six arguments for a catalog, as a JSON array: the value
 * layout, prepared ABI, instruction names, the catalog's callable natives
 * (data functions with no flow, outcomes or suspension), their ports, and each
 * native's identity and signature ABI. Malloc-owned; release with free(). */
char *compiler_target_json(const script_catalog *, size_t *length);
/* CompileBehaviors' arguments for the behaviors identities[i] holding
 * sources[i], as one JSON array: their identities and sources, the same six,
 * then what a behavior links against:
 * - each event's name, ID, signature ABI and range of payload fields, then
 *   each payload field's name, storage, nominal type ID and defaults;
 * - each type a module requires, as stage zero lists them: every type that is
 *   not internal, a namespace or a union, with its ID, layout hash, storage,
 *   size, component ID (0 for none) and range of fields, then each field's
 *   name, storage, byte offset and whether it is writable;
 * - the entity type's ID.
 * Malloc-owned; release with free(). */
char *compiler_behavior_input_json(
    const script_catalog *,
    const char *const *identities,
    const char *const *sources,
    int count,
    size_t *length
);

#endif
