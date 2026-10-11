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

/* A field definition relinked against a staged source collection. */
typedef struct
{
    game_preview_definition *definition;
    behavior_asset *snapshot;
} game_preview_field_relink;
/* Relinks the field definitions that call one of `libraries` against `code`,
 * checking that each still compiles. On failure *out stays NULL. */
bool game_preview_fields_relink(
    game_preview_world *,
    const authoring_sources *code,
    const char *const *libraries,
    int count,
    game_preview_field_relink **out,
    char *message,
    size_t size
);
/* Applies the relinked fields and frees the vector. */
void game_preview_fields_relink_commit(game_preview_world *, game_preview_field_relink *);
void game_preview_fields_relink_free(game_preview_field_relink *);

const authoring_source *game_preview_source_at_path(const authoring_sources *, const char *);
bool game_preview_sources_staged(const game_preview_world *);

#endif
