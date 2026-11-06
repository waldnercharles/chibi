#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_PREPARE_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_PREPARE_H

#include "authoring/source_package_snapshot.h"
#include "language/lower.h"

typedef struct
{
    const char *identity;
    script_analysis *analysis;
    execution_module *module;
    const script_package_exports *exports;
    const script_diagnostic *diagnostics;
    int diagnostic_count;
    bool valid;
    const void *generation; /* Immutable identity, shared only for exact reuse. */
} authoring_package;
typedef struct authoring_packages authoring_packages;

/* Takes ownership of snapshot, including invalid snapshots. Native declarations
 * are copied; binding callback descriptors must outlive their modules. Previous
 * is borrowed during construction and is never changed or retained. Immutable
 * package generations retain their direct dependencies independently of owners. */
authoring_packages *authoring_packages_prepare(
    source_package_snapshot *,
    const script_catalog *native,
    const script_lower_bindings *,
    behavior_backend,
    const authoring_packages *previous
);
bool authoring_packages_valid(const authoring_packages *);
int authoring_packages_count(const authoring_packages *);
int authoring_packages_reused(const authoring_packages *);
/* Graph package indices, not dependency-order ordinals. Views are borrowed. */
const authoring_package *authoring_packages_at(const authoring_packages *, int package);
const source_package_snapshot *authoring_packages_snapshot(const authoring_packages *);
/* Owner-level preparation/snapshot error, or NULL. Semantic/lowering diagnostics
 * remain on each package view, with document indices local to that package. */
const char *authoring_packages_error(const authoring_packages *);
void authoring_packages_free(authoring_packages *);

#endif
