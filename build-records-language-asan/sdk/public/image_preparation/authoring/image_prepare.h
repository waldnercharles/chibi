#ifndef CHIBI_IMAGE_PREPARE_H
#define CHIBI_IMAGE_PREPARE_H
#include "runtime/images/catalog.h"
#include "runtime/images/sky.h"
/* Owns copied content roots and supplies the CPU PNG/JPEG decoder. */
bool render_image_mount_content(const char *root, char *error, size_t size);
/* The bytes of the file at `path` when they decode as a PNG, owned by the
 * caller, with their count in `length`; NULL with `error` saying why not. */
void *image_prepare_png_read(const char *path, int *length, char *error, size_t size);
/* "png" or "jpg" when `bytes` decode as that image within the resident side
 * limit; NULL with `error` saying why not. */
const char *image_prepare_check(const void *bytes, size_t length, char *error, size_t size);
/* Decodes a Radiance .hdr file into an owned sky; false with `error` saying
 * why when it isn't an equirectangular RGBE image within the sky limits. */
bool image_prepare_sky_decode(const void *, size_t, render_sky *, char *error, size_t size);
/* Makes the sky at a content/... .hdr key resident, reading it from the
 * mounted content roots unless it already is. */
bool image_prepare_sky(const char *key, char *error, size_t size);
/* Copies the .hdr file at `path` into the content folder `root` as `key`, or
 * content/skies/ and its file name when `key` is NULL, creating its folders.
 * It checks the file decodes and never replaces one. Returns the owned key,
 * or NULL with `error` saying why. */
char *image_prepare_sky_import(const char *root, const char *path, const char *key, char *error, size_t size);
#endif
