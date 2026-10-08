#ifndef CHIBI_IMAGE_PREPARE_H
#define CHIBI_IMAGE_PREPARE_H
#include "runtime/images/catalog.h"
/* Owns copied content roots and supplies the CPU PNG/JPEG decoder. */
bool render_image_mount_content(const char *root, char *error, size_t size);
#endif
