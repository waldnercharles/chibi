#ifndef CHIBI_AUTOMATION_ASSET_WRITE_H
#define CHIBI_AUTOMATION_ASSET_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint64_t automation_content_hash(const char *text, size_t bytes);
/* Publish inside content/<folder>. expected_hash is required for replacements.
 * Cooperating writers serialize on a directory lock; other editors are checked
 * again immediately before publication. No asset is changed on failure. */
int automation_asset_publish(
    const char *root,
    const char *folder,
    const char *filename,
    const char *text,
    const char *expected_hash,
    bool create,
    char *error,
    size_t error_size
);

/* One file of a batch: its new text at `rename_to` (or in place), or, with
 * no text, its removal. Existing files require their current expected_hash;
 * create requires an absent filename, text, and no rename_to. */
typedef struct
{
    const char *folder;
    const char *filename;
    const char *rename_to; /* NULL keeps the file name */
    const char *text;      /* NULL deletes the file */
    const char *expected_hash;
    bool create; /* expected_hash is unnecessary for a new file. */
} automation_asset_change;

typedef struct automation_asset_batch automation_asset_batch;

/* Publishes every change or none. Every existing file is checked against its
 * hash, and each created or renamed destination must be free before anything
 * moves; the originals then move aside and the new files take their places. On
 * success the batch holds each folder's lock and the originals until
 * automation_assets_commit drops them or automation_assets_rollback puts
 * them back, so a caller can validate the published files first. */
int automation_assets_publish(
    const char *root,
    const automation_asset_change *changes,
    int count,
    automation_asset_batch **batch,
    char *error,
    size_t error_size
);
void automation_assets_commit(automation_asset_batch *batch);
/* Restores every original and removes every new file. */
int automation_assets_rollback(automation_asset_batch *batch, char *error, size_t error_size);

#endif
