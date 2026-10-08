#ifndef CHIBI_AUTHORING_PREVIEW_SOURCE_INTERNAL_H
#define CHIBI_AUTHORING_PREVIEW_SOURCE_INTERNAL_H

#include "authoring/language_catalog.h"
#include "authoring/preview_source.h"

struct game_preview_source_state
{
    char *root;
    authoring_language_catalog *native;
    authoring_sources *working, *applied;
    source_catalog_edit *edits;
    behavior_backend backend;
    bool replace_sources;
    bool mirrored; /* Immutable thumbnail/execution mirror; no disk-based edits. */
    game_preview_source_staging *staging;
};

const authoring_source *game_preview_source_at_path(const authoring_sources *, const char *);
bool game_preview_sources_staged(const game_preview_world *);

#endif
