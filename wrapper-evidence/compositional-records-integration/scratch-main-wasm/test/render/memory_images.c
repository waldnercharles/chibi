#include "memory_images.h"

#include "runtime/images/catalog.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const test_image *mounted;
static int mounted_count;

/* Hands the catalog a copy of mounted image `key`, which it then owns. */
static bool read_image(void *, const char *key, shader_image_view *view, char *error, size_t size)
{
    for (int i = 0; i < mounted_count; i++) {
        const test_image *image = &mounted[i];
        if (strcmp(image->key, key)) {
            continue;
        }
        size_t bytes = (size_t)image->width * (size_t)image->height * 4;
        unsigned char *pixels = xmalloc(bytes);
        memcpy(pixels, image->pixels, bytes);
        *view = (shader_image_view){ pixels, image->width, image->height };
        return true;
    }
    snprintf(error, size, "No test image %s", key);
    return false;
}

void test_mount_images(const test_image *images, int count)
{
    render_image_catalog_clear();
    mounted = images;
    mounted_count = count;
    render_images_set_loader((render_image_loader){ .read = read_image });
}

int test_sprite_variant(const char *key, bool linear)
{
    shader_image_source source = { .key = (char *)key,
                                   .color_space = linear ? SHADER_IMAGE_LINEAR
                                                         : SHADER_IMAGE_SRGB,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    shader_error error = { 0 };
    int variant = render_image_variant(&source, &error);
    assert(variant > 0);
    return variant;
}
