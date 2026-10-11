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

/* A field definition relinked against a staged source collection. */
typedef struct
{
    preview_definition *definition;
    behavior_asset *snapshot;
} preview_field_relink;
/* Relinks the field definitions that call one of `libraries` against `code`,
 * checking that each still compiles. On failure *out stays NULL. */
bool preview_fields_relink(
    preview_world *,
    const authoring_sources *code,
    const char *const *libraries,
    int count,
    preview_field_relink **out,
    char *message,
    size_t size
);
/* Applies the relinked fields and frees the vector. */
void preview_fields_relink_commit(preview_world *, preview_field_relink *);
void preview_fields_relink_free(preview_field_relink *);

const authoring_source *preview_source_at_path(const authoring_sources *, const char *);
bool preview_sources_staged(const preview_world *);

#endif
