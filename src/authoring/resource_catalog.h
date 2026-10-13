#ifndef CHIBI_RESOURCE_CATALOG_H
#define CHIBI_RESOURCE_CATALOG_H
#include "support/asset_value.h"

enum
{
    AUTHORING_RESOURCE_FILES_MAX = 4096,
    AUTHORING_RESOURCE_FILE_BYTES_MAX = 16 * 1024 * 1024,
    AUTHORING_RESOURCE_TOTAL_BYTES_MAX = 64 * 1024 * 1024,
    AUTHORING_RESOURCE_DEPTH_MAX = 16,
    AUTHORING_RESOURCE_OCCURRENCES_MAX = 8192
};
/* Detached disk inventory. Does not publish resources or touch renderer state.
 * content is the actual content directory; runtime keys remain content/....
 * The caller owns the returned document. NULL means content cannot be opened.
 * content_hash includes raw resources and every inspected KDL/.chibi/RML/RCSS
 * source, even with no resource uses; resource_hash covers raw resources only.
 * Both definitive identities are omitted when complete=false. authored_sources
 * lists sorted source paths, bytes, status and fingerprints.
 * Partial results explicitly set complete=false; per-resource status distinguishes
 * metadata inspection from validation. Consumers are textual quoted path
 * occurrences, including comments, not semantic references. Dynamic names,
 * relative URLs, escaped literals and bare sound names are outside coverage. */
av_doc *authoring_resource_catalog(const char *content, char *error, size_t size);
#endif
