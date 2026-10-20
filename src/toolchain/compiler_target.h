#ifndef CHIBI_TOOLCHAIN_COMPILER_TARGET_H
#define CHIBI_TOOLCHAIN_COMPILER_TARGET_H

#include "capabilities/catalog.h"

#include <stddef.h>

/* Compile's last six arguments for a catalog, as a JSON array: the value
 * layout, prepared ABI, instruction names, the catalog's callable natives
 * (data functions with no flow, outcomes or suspension), their ports, and each
 * native's identity and signature ABI. Malloc-owned; release with free(). */
char *compiler_target_json(const script_catalog *, size_t *length);
/* The same six, then what a behavior links against, as one object:
 * - events: each event's name, ID, signature ABI and payload fields;
 * - types: each type a module requires, as stage zero lists them: every type
 *   that is not internal, a namespace or a union, with its ID, layout hash,
 *   storage, size, component ID and fields (name, ID, storage, container,
 *   nominal type ID and byte offset);
 * - entity: the entity type's ID, and tick: the Tick event's ID.
 * Malloc-owned; release with free(). */
char *compiler_game_target_json(const script_catalog *, size_t *length);

#endif
