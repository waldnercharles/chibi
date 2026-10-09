#ifndef CHIBI_AUTHORING_SOURCE_COLLECTION_H
#define CHIBI_AUTHORING_SOURCE_COLLECTION_H
#include "authoring/source_catalog.h"
#include "authoring/source_prepare.h"

typedef struct authoring_sources authoring_sources;
/* Every source's diagnostic while the declaration catalog has diagnostics. */
#define AUTHORING_SOURCES_CATALOG_INCOMPLETE                                   \
    "Project declaration catalog is incomplete"
/* Prepares the project's bounded declaration snapshot and every source without
 * executing or registering it. Native declarations are borrowed during this
 * call. The returned collection owns declarations, analyzed sources and imported modules. */
authoring_sources *authoring_sources_prepare(
    const char *content_root,
    const script_catalog *native,
    const script_lower_bindings *,
    behavior_backend
);
/* Consumes an already checked declaration snapshot, avoiding a second parse
 * when a caller enforces aggregate project limits before preparation. */
authoring_sources *authoring_sources_prepare_catalog(
    source_catalog *owned,
    const script_lower_bindings *,
    behavior_backend
);
const script_catalog *authoring_sources_catalog(const authoring_sources *);
int authoring_sources_definition_count(const authoring_sources *);
const source_catalog_definition *authoring_sources_definitions(const authoring_sources *);
int authoring_sources_reference_count(const authoring_sources *);
const source_catalog_reference *authoring_sources_references(const authoring_sources *);
int authoring_sources_count(const authoring_sources *);
const authoring_source *authoring_sources_at(const authoring_sources *, int index);
int authoring_sources_catalog_diagnostic_count(const authoring_sources *);
const source_catalog_diagnostic *authoring_sources_catalog_diagnostic_at(
    const authoring_sources *,
    int index
);
bool authoring_sources_valid(const authoring_sources *);
authoring_sources *authoring_sources_retain(const authoring_sources *);
void authoring_sources_free(authoring_sources *);
#endif
