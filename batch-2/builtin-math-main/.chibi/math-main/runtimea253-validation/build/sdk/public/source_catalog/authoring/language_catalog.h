#ifndef CHIBI_AUTHORING_LANGUAGE_CATALOG_H
#define CHIBI_AUTHORING_LANGUAGE_CATALOG_H

#include "language/lower.h"
#include "runtime/native_capabilities.h"

typedef struct authoring_language_catalog authoring_language_catalog;
typedef struct
{
    native_capabilities *native;
    /* Project declarations are copied during construction. */
    const script_catalog *project;
} authoring_language_catalog_input;

/* Retains the explicit native table and owns a merged declaration snapshot
 * that also holds the loaded project's declared types. Actual native kernels/layouts
 * keep their declaration lifetime. */
authoring_language_catalog *authoring_language_catalog_new(
    const authoring_language_catalog_input *,
    char *error,
    size_t error_size
);
const script_catalog *authoring_language_catalog_view(const authoring_language_catalog *);
authoring_language_catalog *authoring_language_catalog_retain(authoring_language_catalog *);
void authoring_language_catalog_free(authoring_language_catalog *);
script_lower_bindings authoring_language_bindings(const authoring_language_catalog *);
#endif
