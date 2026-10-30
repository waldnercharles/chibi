#ifndef CHIBI_AUTHORING_SOURCE_PREPARE_H
#define CHIBI_AUTHORING_SOURCE_PREPARE_H

#include "language/lower.h"

typedef struct
{
    uint64_t function; /* Entry point whose transitive body carries this cost. */
    unsigned cost;     /* One EXECUTION_COST_* bit. */
    bool possible;
    behavior_function_source origin; /* May point into a linked library. */
    char message[192];
} authoring_performance_diagnostic;
typedef struct
{
    uint64_t function;
    const char *entry;
    /* First finalized origin of this body, with its source document path. */
    behavior_function_source origin;
    execution_cost_summary summary;
} authoring_function_costs;

/* One owned source generation. Preparation never replaces a live registration.
 * Invalid edits retain syntax, resolved facts and diagnostics for authoring. */
typedef struct
{
    char *path;
    script_syntax *syntax;
    script_analysis *analysis;
    execution_module *module;
    script_diagnostic *diagnostics;
    int diagnostic_count;
    authoring_performance_diagnostic *performance;
    int performance_count;
    /* One row per finalized executable body. All pointers borrow this source
     * generation, including evidence in imported modules. A complete traversal
     * may still report possible costs at native boundaries. These static facts
     * are neither measured traffic nor an allocation-free guarantee. */
    authoring_function_costs *costs;
    int cost_count;
    bool valid;
    int references; /* Immutable generations may be shared by source collections. */
} authoring_source;

/* Catalog declarations are copied by analysis/lowering. Native callback
 * descriptors referenced by bindings must outlive the executable module. */
authoring_source *authoring_source_prepare(
    const char *path,
    const char *text,
    const script_catalog *,
    const script_lower_bindings *,
    behavior_backend
);
/* Explicit stable asset key, independent of display path and declared name.
 * Project collections use content-relative keys in declaration discovery too. */
authoring_source *authoring_source_prepare_document(
    const char *path,
    const char *key,
    const char *text,
    const script_catalog *,
    const script_lower_bindings *,
    behavior_backend
);
void authoring_source_free(authoring_source *);

#endif
