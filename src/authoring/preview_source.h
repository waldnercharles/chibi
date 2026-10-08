#ifndef CHIBI_AUTHORING_PREVIEW_SOURCE_H
#define CHIBI_AUTHORING_PREVIEW_SOURCE_H

#include "authoring/preview.h"
#include "authoring/source_collection.h"
#include "runtime/native_capabilities.h"

/* Preparing/editing owns a working generation and diagnostics, but does not
 * change live registrations. Native declarations outlive both working and
 * applied collections. Failed drafts leave the applied generation available. */
/* A new manager uses the ordinary rendering/game catalog. Applications needing
 * optional capabilities initialize through prepare_with_catalog instead. */
int game_preview_sources_prepare(
    game_preview_world *,
    const char *content_root,
    behavior_backend,
    char *error,
    size_t error_size
);
/* Initializes a source manager with the application's explicit native table.
 * Retains the table; the caller may release its reference after return.
 * An existing manager retains its original catalog and rejects reinitialization.
 * Subsequent preparation uses game_preview_sources_prepare. */
int game_preview_sources_prepare_with_catalog(
    game_preview_world *,
    const char *content_root,
    behavior_backend,
    native_capabilities *,
    char *error,
    size_t error_size
);
/* Mirrors an exact valid generation using destination-owned runtime services.
 * The resulting manager accepts further mirrors, not disk-based edits. Failed
 * staging preserves the destination's working and applied generations. */
int game_preview_sources_mirror(
    game_preview_world *destination,
    const game_preview_world *source,
    bool applied,
    char *error,
    size_t error_size
);
int game_preview_source_edit(
    game_preview_world *,
    const char *path,
    const char *text,
    char *error,
    size_t error_size
);
/* NULL text removes a path; new paths insert sources. The entire edit batch
 * is prepared once, retaining invalid drafts without changing applied state. */
int game_preview_sources_edit(
    game_preview_world *,
    const source_catalog_edit *,
    int count,
    char *error,
    size_t error_size
);
/* Complete in-memory source membership, for checkpoint restoration. */
int game_preview_sources_replace(
    game_preview_world *,
    const source_catalog_edit *,
    int count,
    char *error,
    size_t error_size
);
/* Owned full candidate, preserving working/applied state. replace=true uses
 * exact supplied membership; false merges the current unsaved overlays. */
authoring_sources *game_preview_sources_candidate(
    const game_preview_world *,
    const source_catalog_edit *,
    int count,
    bool replace,
    char *error,
    size_t error_size
);
int game_preview_sources_validate(
    const game_preview_world *,
    const source_catalog_edit *,
    int count,
    char *error,
    size_t error_size
);
/* Publishes one complete valid candidate at the runtime transaction boundary.
 * Only restart_path resets its owner state; dependent module replacements
 * preserve compatible state. NULL preserves every compatible owner. */
/* A stage retains the exact candidate and prepares every runtime and authoring
 * generation. Keep the world quiescent until commit/discard consumes it. Prepare
 * all participating worlds before committing any. Discard preserves applied
 * generations; commit performs no rejecting operation or dispatch. */
typedef struct game_preview_source_staging game_preview_source_staging;
game_preview_source_staging *game_preview_sources_stage(
    game_preview_world *,
    const char *restart_path,
    char *error,
    size_t error_size
);
void game_preview_sources_commit(game_preview_source_staging *);
void game_preview_sources_discard(game_preview_source_staging *);
int game_preview_sources_publish(game_preview_world *, const char *restart_path, char *error, size_t error_size);
int game_preview_source_apply(
    game_preview_world *,
    const char *path,
    const char *text,
    bool restart,
    char *error,
    size_t error_size
);
const authoring_sources *game_preview_sources_working(const game_preview_world *);
const authoring_sources *game_preview_sources_applied(const game_preview_world *);
/* Complete owned overlay records, borrowed until the next edit/prepare/free.
 * Includes declarative drafts as well as executable membership edits. */
int game_preview_sources_overlay_count(const game_preview_world *);
const source_catalog_edit *game_preview_sources_overlay_at(const game_preview_world *, int index);
const authoring_source *game_preview_source_find(
    const game_preview_world *,
    const char *path,
    bool applied
);
/* Releases authoring snapshots; live registrations retain their own modules. */
void game_preview_sources_free(game_preview_world *);

#endif
