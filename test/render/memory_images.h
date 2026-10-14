#ifndef CHIBI_TEST_RENDER_MEMORY_IMAGES_H
#define CHIBI_TEST_RENDER_MEMORY_IMAGES_H

#include <stdbool.h>

/* Images render cases hand the image catalog from memory instead of files. */

/* RGBA8 pixels, rows top to bottom, that the catalog reads under `key`. */
typedef struct
{
    const char *key;
    int width, height;
    const unsigned char *pixels;
} test_image;

/* Clears the image catalog and makes it read `images`, which outlive its use,
 * instead of files. */
void test_mount_images(const test_image *images, int count);
/* Loads mounted image `key` as sprites sample it, texel by texel and clamped,
 * linear for a map and sRGB otherwise, and returns its variant. */
int test_sprite_variant(const char *key, bool linear);

#endif
