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

#endif
