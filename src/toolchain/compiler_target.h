#ifndef CHIBI_TOOLCHAIN_COMPILER_TARGET_H
#define CHIBI_TOOLCHAIN_COMPILER_TARGET_H

#include "capabilities/catalog.h"

#include <stddef.h>

typedef struct native_capabilities native_capabilities;

/* Compile's last six arguments for a catalog, as a JSON array: the value
 * layout, prepared ABI, instruction names, the catalog's callable natives
 * (data functions with no flow, outcomes or suspension), their ports, and each
 * native's identity and signature ABI. Malloc-owned; release with free(). */
char *compiler_target_json(const script_catalog *, size_t *length);
/* Per-native script effect masks, in the same callable order as target_json.
 * The explicit effect-aware compile entries take this as their last argument;
 * it does not change the stable six-argument bootstrap target description.
 * Malloc-owned; release with free(). */
char *compiler_target_effects_json(const script_catalog *, size_t *length);
/* The arguments of the effect-aware package compile entries
 * (CompileWithEffects, CompileClosureWithEffects): the project directory, the
 * names and roots of its collections, the files of every package, the first
 * naming the root package, with their texts (sizes[i] bytes of texts[i]), no
 * debugging, then the catalog's target and effect masks. A JSON array;
 * malloc-owned, NULL when the catalog cannot be described. */
char *compiler_package_request_json(
    const char *project,
    const char *const *names,
    const char *const *roots,
    int collections,
    const char *const *paths,
    const char *const *texts,
    const size_t *sizes,
    int files,
    const script_catalog *,
    size_t *length
);
/* The arguments of CompileWithCatalog and CompileClosureWithCatalog: what
 * compiler_package_request_json lists, then the rich target (native
 * capabilities with their nominal ports), the catalog's native types and their
 * fields, the entity type's ID, the platform (os, then arch, as core:target
 * numbers them) and each native's effect mask. `bindings` may
 * be NULL when no action has to be linked. Malloc-owned; NULL on failure. */
char *compiler_package_catalog_request_json(
    const char *project,
    const char *const *names,
    const char *const *roots,
    int collections,
    const char *const *paths,
    const char *const *texts,
    const size_t *sizes,
    int files,
    const script_catalog *,
    const native_capabilities *bindings,
    size_t *length
);

#endif
