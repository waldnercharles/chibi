#ifndef CHIBI_AUTHORING_SOURCE_PREPARE_H
#define CHIBI_AUTHORING_SOURCE_PREPARE_H

#include "language/lower.h"

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
    bool valid;
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
