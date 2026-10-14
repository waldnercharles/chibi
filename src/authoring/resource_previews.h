#ifndef CHIBI_RESOURCE_PREVIEWS_H
#define CHIBI_RESOURCE_PREVIEWS_H
#include "authoring/workspace_documents.h"
#include "authoring/workspace_resources.h"
#include "graph/model/behavior_asset.h"
#include "language/semantic.h"
#include "runtime/images/catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct resource_previews resource_previews;
typedef struct workspace_runtime workspace_runtime;
typedef struct
{
    const char *root, *content;
    workspace_resources *inventory; /* Borrowed complete resource snapshot. */
    workspace_documents *overlays; /* Optional borrowed read-only master buffers. */
    render_image_loader images;
    /* Returns independently owned applied sources; the service consumes/frees
     * all rows and their malloc-owned array. At most64sources per seed. */
    behavior_asset **(*catalog)(void *, int *count);
    void *catalog_user;
    /* Immutable source/type generation; acquired owner survives the thumbnail
     * semantic view, then is released exactly once. No global publication. */
    bool (*source_catalog)(void *, const script_catalog **, void **owner, char *, size_t);
    void (*source_catalog_release)(void *, void *owner);
    void *source_catalog_user;
    /* Transfers a private runtime to this service. Host chooses only standard
     * game capabilities and read_only_presentation=true; no authoring effects.
     * Factory borrows documents/root only during construction. */
    workspace_runtime *(*scene_runtime)(void *, workspace_documents *, const char *root, char *, size_t);
    void *scene_runtime_user;
} resource_preview_config;
typedef struct
{
    int preview, width, height;
    bool ready;
    const char *status, *image, *error;
    uint64_t texture, texture_revision;
} resource_preview_view;
/* GPU-thread owned, private documents/world/resources, eight retained slots.
 * Source reads are rooted/no-follow; PNG/JPEG input<=16MiB, decoded<=64MiB.
 * Output sides64..256; one queued preview step per tick. Fields bake <=8 rows
 * (<=2048 samples) per step with seed0/cellsize2/default parameters. Stylesheet
 * swatches retain <=64 literal hex colors, including gradient/fallback literals.
 * Handles never recycle. No writes, platform dialogs or master document edits. */
resource_previews *resource_previews_new(const resource_preview_config *, char *error, size_t);
void resource_previews_free(resource_previews *);
int resource_preview_open(resource_previews *, const char *path, int width, int height, char *error, size_t);
bool resource_preview_read(resource_previews *, int handle, resource_preview_view *);
bool resource_preview_close(resource_previews *, int handle);
int resource_preview_handles(resource_previews *, int *out, int capacity);
void resource_previews_tick(resource_previews *);
#endif
