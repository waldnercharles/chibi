#include "authoring/image_prepare.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define CUTE_PNG_IMPLEMENTATION
#include "vendor/image_png.h"
#define CUTE_JPG_IMPLEMENTATION
#include "vendor/image_jpg.h"
#define IMAGE_BYTES_MAX (64 * 1024 * 1024)
static char **roots;
static void clear(void *)
{
    for (int i = 0; i < acount(roots); i++) {
        free(roots[i]);
    }
    afree(roots);
}
static bool failure(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
/* Closes `file` and returns its contents when it holds 1 to IMAGE_BYTES_MAX
 * bytes. */
static void *read_file(FILE *file, int *length)
{
    bool valid = !fseek(file, 0, SEEK_END);
    long bytes = valid ? ftell(file) : -1;
    valid = bytes > 0 && bytes <= IMAGE_BYTES_MAX && !fseek(file, 0, SEEK_SET);
    unsigned char *data = valid ? xmalloc((size_t)bytes) : NULL;
    if (valid) {
        valid = fread(data, 1, (size_t)bytes, file) == (size_t)bytes &&
                fgetc(file) == EOF;
    }
    valid = fclose(file) == 0 && valid;
    if (!valid) {
        free(data);
        return NULL;
    }
    *length = (int)bytes;
    return data;
}
static void *read_bytes(const char *key, int *length)
{
    if (!key || strncmp(key, "content/", 8) || strstr(key, "..") || strchr(key, '\\')) {
        return NULL;
    }
    for (int i = acount(roots) - 1; i >= 0; i--) {
        size_t size = strlen(roots[i]) + strlen(key + 8) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", roots[i], key + 8);
        FILE *file = fopen(path, "rb");
        free(path);
        if (file) {
            return read_file(file, length);
        }
    }
    return NULL;
}
static bool decode(void *, const char *key, shader_image_view *out, char *error, size_t size)
{
    const char *extension = strrchr(key, '.');
    bool png = extension && !strcasecmp(extension, ".png");
    bool jpg = extension &&
               (!strcasecmp(extension, ".jpg") || !strcasecmp(extension, ".jpeg"));
    if (!png && !jpg) {
        return failure(error, size, "Expected a PNG, JPG or JPEG image");
    }
    int length = 0;
    void *bytes = read_bytes(key, &length);
    if (!bytes) {
        return failure(error, size, "Cannot read bounded image file");
    }
    int width = 0, height = 0;
    if (png) {
        chibi_image_cp_load_png_wh(bytes, length, &width, &height);
    } else {
        chibi_image_cj_load_jpg_wh(bytes, length, &width, &height);
    }
    if (width < 1 || height < 1 || width > RENDER_IMAGE_SIDE_MAX ||
        height > RENDER_IMAGE_SIDE_MAX) {
        free(bytes);
        return failure(error, size, "Image dimensions must be between 1 and 2048 pixels per side");
    }
    unsigned char *pixels = NULL;
    if (png) {
        chibi_image_cp_image_t image = chibi_image_cp_load_png_mem(bytes, length);
        pixels = (unsigned char *)image.pix;
        width = image.w;
        height = image.h;
    } else {
        chibi_image_cj_image_t image = chibi_image_cj_load_jpg_mem(bytes, length);
        pixels = (unsigned char *)image.pix;
        width = image.w;
        height = image.h;
    }
    free(bytes);
    if (!pixels) {
        return failure(error, size, png ? chibi_image_cp_error_reason : chibi_image_cj_error_reason);
    }
    *out = (shader_image_view){ pixels, width, height };
    return true;
}
bool image_prepare_png_valid(const char *path, char *error, size_t size)
{
    int length = 0;
    FILE *file = fopen(path, "rb");
    void *bytes = file ? read_file(file, &length) : NULL;
    if (!bytes) {
        return failure(error, size, "Cannot read bounded image file");
    }
    chibi_image_cp_image_t image = chibi_image_cp_load_png_mem(bytes, length);
    free(bytes);
    if (!image.pix) {
        return failure(error, size, chibi_image_cp_error_reason);
    }
    chibi_image_cp_free_png(&image);
    return true;
}
bool render_image_mount_content(const char *root, char *error, size_t size)
{
    struct stat info;
    if (!root || stat(root, &info) || !S_ISDIR(info.st_mode)) {
        return failure(error, size, "Image content root is not a directory");
    }
    render_images_set_loader((render_image_loader){ .read = decode, .clear = clear });
    for (int i = 0; i < acount(roots); i++) {
        if (!strcmp(roots[i], root)) {
            return true;
        }
    }
    apush(roots, xstrdup(root));
    if (size) {
        error[0] = 0;
    }
    return true;
}
