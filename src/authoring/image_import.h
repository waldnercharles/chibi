#ifndef CHIBI_IMAGE_IMPORT_H
#define CHIBI_IMAGE_IMPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    IMAGE_IMPORT_COLOR,
    IMAGE_IMPORT_SCALAR,
    IMAGE_IMPORT_NORMAL
} image_import_semantic;

typedef struct
{
    int width, height; /* Explicit output dimensions, 1..2048. */
    image_import_semantic semantic;
} image_import_options;

typedef struct
{
    unsigned char *png; /* Owned canonical RGBA8 PNG; free with image_import_free. */
    size_t png_bytes, source_bytes, pixel_bytes, prepared_bytes;
    uint64_t source_hash, output_hash; /* FNV-1a64 of encoded source/output bytes. */
    uint64_t prepared_hash; /* FNV-1a64 of the exact prepared image payload. */
    int source_width, source_height, width, height;
    image_import_semantic semantic;
    const char *source_format; /* Static "png" or "jpg". */
} image_import_plan;

/* Detached CPU operation: no file writes, catalog changes or GPU. Input is
 * PNG/JPEG <=64 MiB and <=4096 per side; output stays within the resident 2048
 * side limit. Unsupported 16-bit PNG is rejected, never silently truncated.
 * Color averages premultiplied linear sRGB; scalar averages channels directly;
 * normal averages signed vectors then renormalizes. Alpha is linear coverage.
 * Output starts zeroed. Failure leaves it unchanged. Originals are borrowed.
 * Exact prepared_bytes uses the resident image codec, not PNG compression. */
bool image_import_prepare(
    const void *,
    size_t,
    image_import_options,
    image_import_plan *,
    char *error,
    size_t size
);
/* UI resolution choices: 0 keeps source size (must fit 2048); 1024 or 2048
 * fits the longest side, preserving aspect and never upscaling. One decode. */
bool image_import_prepare_fit(
    const void *,
    size_t,
    int max_side,
    image_import_semantic,
    image_import_plan *,
    char *error,
    size_t size
);
void image_import_free(image_import_plan *);

typedef struct
{
    const char *key; /* Unique content/... .png destination, no traversal. */
    const image_import_plan *plan;
} image_import_proposal;

#endif
