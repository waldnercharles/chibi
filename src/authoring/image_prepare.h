#ifndef CHIBI_IMAGE_PREPARE_H
#define CHIBI_IMAGE_PREPARE_H
#include "runtime/images/catalog.h"
/* Owns copied content roots and supplies the CPU PNG/JPEG decoder. */
bool render_image_mount_content(const char *root, char *error, size_t size);
/* The bytes of the file at `path` when they decode as a PNG, owned by the
 * caller, with their count in `length`; NULL with `error` saying why not. */
void *image_prepare_png_read(const char *path, int *length, char *error, size_t size);
#endif
