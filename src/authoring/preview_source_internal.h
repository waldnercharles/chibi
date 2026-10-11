#ifndef CHIBI_AUTHORING_PREVIEW_SOURCE_INTERNAL_H
#define CHIBI_AUTHORING_PREVIEW_SOURCE_INTERNAL_H

#include "authoring/language_catalog.h"
#include "authoring/preview_source.h"

struct preview_source_state
{
    char *root;
    authoring_language_catalog *native;
    authoring_sources *working, *applied;
    source_catalog_edit *edits;
    behavior_backend backend;
    bool replace_sources;
    bool mirrored; /* Immutable thumbnail/execution mirror; no disk-based edits. */
    preview_source_staging *staging;
};

const authoring_source *preview_source_at_path(const authoring_sources *, const char *);
bool preview_sources_staged(const preview_world *);

#endif
