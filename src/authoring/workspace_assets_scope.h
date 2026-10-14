#ifndef CHIBI_WORKSPACE_ASSETS_SCOPE_H
#define CHIBI_WORKSPACE_ASSETS_SCOPE_H
#include "authoring/workspace_assets.h"
#include "capabilities/catalog.h"
/* Owns an independent project registry and prototype source snapshot. The native
 * catalog and documents remain borrowed. No global catalog slots are changed. */
workspace_assets_service *workspace_assets_service_new(
    workspace_documents *,
    const char *root,
    const script_catalog *native,
    char *error,
    size_t size
);
/* Borrowed declaration metadata, valid until next successful sync/refresh/free. */
const script_catalog *workspace_assets_service_component_catalog(const workspace_assets_service *);
/* Returned source/path are borrowed until the next scope refresh. */
bool workspace_assets_service_prototype_source(
    workspace_assets_service *,
    const char *name,
    const char **path,
    const char **text,
    char *error,
    size_t size
);
/* Private candidate registry over exact revisioned replacements and all named
 * open buffers. No source handles/history or disk files change. Returned
 * service owns its private document snapshots; caller frees it normally. */
workspace_assets_service *workspace_assets_service_candidate(
    workspace_assets_service *,
    const workspace_document_replacement *,
    int count,
    char *error,
    size_t size
);
void workspace_assets_service_free(workspace_assets_service *);
/* Explicit disk refresh; document overlays are incorporated before publication.
 * Refusal preserves the last complete scope. */
bool workspace_assets_service_refresh(workspace_assets_service *, char *error, size_t size);
/* Cheap document-revision check, refreshes cached source only when changed. */
bool workspace_assets_service_sync(workspace_assets_service *, char *error, size_t size);
#endif
