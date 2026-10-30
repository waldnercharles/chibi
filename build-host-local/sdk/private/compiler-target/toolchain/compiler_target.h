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
/* CompileBehaviorsWithCatalog's eighteen arguments for the behaviors
 * identities[i] holding sources[i], as one JSON array: their identities and
 * sources, the same six, then what a behavior links against:
 * - each event's name, ID, signature ABI and range of payload fields, then
 *   each payload field's name, storage, nominal type ID and defaults;
 * - each type that is not internal or a union, in catalog order, with whether
 *   a module requires it, as stage zero requires every one but the
 *   namespaces, the index of its underlying type, its ID, layout hash,
 *   storage, size, component ID (0 for none) and range of fields, then each
 *   field's name, storage, byte offset, whether it is writable, the index of
 *   its nominal type among these types, whether it is a constant whose value
 *   is its name, the index among the natives of the one that reads it (-1 for
 *   no type or native) and the key it reads with ("" for none);
 * - the entity type's ID;
 * - each native that combines numbers, and records of them field by field,
 *   for a binary operator: its index among the natives and the operator;
 * - each global's name, storage and the index of its nominal type;
 * - each native that makes a record of its arguments, with the record's type
 *   and where its fields start, then for each argument the position of the
 *   record field it gives;
 * - each native's effect mask, in native order.
 * Unlike the bootstrap target, CatalogNative rows add `direct`,
 * `qualified_name` (falling back to name), the script_capability_kind `kind`
 * and the actual linked `intrinsic`, and CatalogPort rows add nominal IDs and
 * parameter defaults. Callable functions retain their indices; unsupported
 * functions then actions append, each in catalog order. Unsupported functions
 * are not direct. Only synchronous invoke or context actions with one
 * entry/completion and no special execution contract are direct. Their
 * identity and ABI must match bindings; NULL bindings keeps actions
 * unsupported while preserving function admission. Fixed nominal ports are
 * supported only for single native records and handles whose exported type
 * and alias chain have matching storage and target layout. Authored records,
 * nominal arrays, sets, maps, nested collections and other flagged types
 * retain unsupported storage (-1). Defaults retain has_default and
 * default_owner; default_supported is false for aggregate/inferred/container
 * defaults even when the port accepts an explicitly supplied value. Scalar
 * bits and length-carrying text are copied into the JSON, with no native
 * addresses. Result ports have no default policy. Catalog and bindings are
 * borrowed until return. Malloc-owned; release with free(). */
char *compiler_behavior_catalog_input_json(
    const script_catalog *,
    const native_capabilities *bindings,
    const char *const *identities,
    const char *const *sources,
    int count,
    size_t *length
);

#endif
