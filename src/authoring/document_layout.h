#ifndef CHIBI_DOCUMENT_LAYOUT_H
#define CHIBI_DOCUMENT_LAYOUT_H
#include "authoring/document_semantics.h"
/* Existing authored source sidecar, beside the source. NULL for memory or an
 * oversized path. The caller owns the returned string. */
char *document_layout_path(const workspace_documents *, int document, int revision, char *, size_t);
/* Produces source-bound metadata without editing source or adopting history.
 * Arrange ignores node/coordinates; placement checks finite bounded coordinates.
 * The authored caller owns sidecar adoption, undo grouping and save policy. */
bool document_layout_prepare(
    const document_semantics *,
    uint64_t graph,
    uint64_t node,
    float x,
    float y,
    bool placed,
    bool arrange,
    char **metadata,
    char *,
    size_t
);
#endif
