#ifndef CHIBI_VISUAL_CATALOG_H
#define CHIBI_VISUAL_CATALOG_H
#include "authoring/preview.h"
#include "authoring/workspace_documents.h"
#include "authoring/workspace_resources.h"
#include "render/shader_publish.h"
#include "runtime/images/catalog.h"
enum
{
    VISUAL_CATALOG_SOURCES_MAX = 256
};
/* Independent complete candidate; failure retires all candidate state without
 * touching the current catalog. Exact open-buffer overlays supersede disk.
 * Caller owns both outputs and must destroy the world inside its resource scope. */
bool visual_catalog_build(
    workspace_documents *,
    const char *root,
    workspace_resources *,
    render_image_loader,
    preview_world **,
    render_resource_context **,
    char *error,
    size_t
);
#endif
