#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_EXPORTS_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_EXPORTS_H

#include "language/lower.h"

typedef struct source_package_exports source_package_exports;
/* Independently owns visible root functions/types/constants and the catalog
 * metadata reachable by their stable IDs. Input analysis, metadata and lowering
 * result may retire after construction. Invalid owners retain one diagnostic.
 * metadata supplies the native and dependency closure; analysis supplies this
 * package's own authored type definitions. No alias spelling is published.
 * NULL lowering creates a provisional declaration view for specialization
 * discovery; it is never published as an executable package generation. */
source_package_exports *source_package_exports_new(
    const script_analysis *,
    const script_catalog *metadata,
    const script_lower_package_result *
);
void source_package_exports_bind_imports(
    source_package_exports *,
    const script_package_import *,
    int count
);
const script_package_exports *source_package_exports_view(const source_package_exports *);
const char *source_package_exports_error(const source_package_exports *);
void source_package_exports_free(source_package_exports *);

#endif
