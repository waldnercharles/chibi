#ifndef CHIBI_AUTHORING_SOURCE_LINK_H
#define CHIBI_AUTHORING_SOURCE_LINK_H
#include "language/lower.h"

/* This preparation context and its borrowed arrays/catalog must outlive each
 * script_lower call. The resulting module retains every imported generation. */
typedef struct
{
    script_lower_bindings native;
    const script_catalog *catalog;
    execution_module *const *libraries;
    int library_count;
} authoring_source_link;

script_lower_bindings authoring_source_bindings(const authoring_source_link *);
#endif
