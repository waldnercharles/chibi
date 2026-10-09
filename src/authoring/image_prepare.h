#ifndef CHIBI_IMAGE_PREPARE_H
#define CHIBI_IMAGE_PREPARE_H
#include "runtime/images/catalog.h"
/* Owns copied content roots and supplies the CPU PNG/JPEG decoder. */
bool render_image_mount_content(const char *root, char *error, size_t size);
/* Whether the file at `path` decodes as a PNG; `error` says why not. */
bool image_prepare_png_valid(const char *path, char *error, size_t size);
#endif
