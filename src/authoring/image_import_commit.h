#ifndef CHIBI_IMAGE_IMPORT_COMMIT_H
#define CHIBI_IMAGE_IMPORT_COMMIT_H

#include "authoring/visual_prepare.h"

typedef struct
{
    int max_side; /* 0 preserves source dimensions; otherwise 1024 or 2048. */
    image_import_semantic semantic;
} image_import_file_options;

typedef struct
{
    void *context;
    /* CLI measures an offline saved project; Studio measures its current live
     * scenes/catalogs. The callback is synchronous and must not write assets. */
    bool (*measure)(void *, const image_import_proposal *, int, authoring_visual_budget *, char *, size_t);
} image_import_budget_host;

typedef struct
{
    char *source, *destination;
    image_import_file_options options;
    image_import_plan image;
    authoring_visual_budget budget;
    uint64_t hash;
    bool reimport;
    uint64_t texture_hash, previous_image_hash; /* Optimistic replacement guards. */
} image_import_file_plan;

#define IMAGE_IMPORT_TEXTURE_SUFFIX ".texture.kdl"

typedef struct
{
    char *source; /* Owned. Project-root relative when possible, otherwise absolute. */
    image_import_file_options options;
    uint64_t hash; /* Exact serialized metadata fingerprint; 0 for a new record. */
} image_import_texture;

/* Readable KDL import configuration, separate from runtime PNG pixels.
 * Parsing/reading requires a zeroed output. Strings returned by serialize are
 * malloc-owned. Material color space/filter/wrap remain material-owned. */
bool image_import_texture_parse(const char *text, image_import_texture *, char *error, size_t size);
char *image_import_texture_serialize(const image_import_texture *, char *error, size_t size);
void image_import_texture_free(image_import_texture *);
bool image_import_texture_read(
    const char *content,
    const char *key,
    image_import_texture *,
    char *error,
    size_t size
);
/* Atomic metadata-only save. Requires the currently stored exact hash;
 * expected_hash=0 creates metadata for an existing unmanaged PNG exclusively.
 * Does not reimport pixels. Reload to obtain the new hash after success. */
bool image_import_texture_save(
    const char *content,
    const char *key,
    const image_import_texture *,
    uint64_t expected_hash,
    char *error,
    size_t size
);
/* Uses saved source/settings, preserves the exact content/... PNG key. */
bool image_import_file_reimport_prepare(
    const char *content,
    const char *key,
    image_import_budget_host,
    image_import_file_plan *,
    char *error,
    size_t size
);

/* Read-only host plan. Source is an absolute regular PNG/JPEG file. Destination
 * is a virtual content/... .png key, with no traversal or symlink directories.
 * Missing destination directories are allowed but are not created. Plans own
 * their strings/image. A measured over-budget plan succeeds with fits=false. */
bool image_import_file_prepare(
    const char *content,
    const char *source,
    const char *destination,
    image_import_file_options,
    image_import_budget_host,
    image_import_file_plan *,
    char *error,
    size_t size
);
void image_import_file_plan_free(image_import_file_plan *);
/* Re-read source, remeasure the host, and require the same plan hash and budget
 * admission before writing. New imports publish PNG plus import configuration
 * exclusively, rolling back the new metadata if PNG publication fails.
 * Reimport atomically replaces only the PNG after verifying the old PNG and
 * metadata hashes. Original source bytes are never modified. Neither API
 * promises a multi-file power-loss transaction or concurrent-writer locking.
 * Failure removes staging bytes and directories created by this attempt. */
bool image_import_file_commit(
    const char *content,
    const image_import_file_plan *,
    image_import_budget_host,
    char *error,
    size_t size
);

#endif
