#include "authoring/image_prepare.h"

#include "runtime/images/sky.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <errno.h>
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
#define IMAGE_SIDES_MESSAGE                                                    \
    "Image dimensions must be between 1 and 2048 pixels per side"
#define SKY_LINE_MAX 512
#define SKY_SIDES_MESSAGE                                                      \
    "Sky must be twice as wide as tall and 2 to 4096 texels wide"
#define SKY_TEXELS_MESSAGE "Sky texels are truncated or malformed"
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
static bool sides_valid(int width, int height)
{
    return width >= 1 && height >= 1 && width <= RENDER_IMAGE_SIDE_MAX &&
           height <= RENDER_IMAGE_SIDE_MAX;
}
static bool content_key(const char *key)
{
    return key && !strncmp(key, "content/", 8) && !strstr(key, "..") &&
           !strchr(key, '\\');
}
static void *read_bytes(const char *key, int *length)
{
    if (!content_key(key)) {
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
/* Decodes `length` bytes of a PNG, or else a JPEG, within the resident side
 * limit. */
static bool decode_bytes(const void *bytes, int length, bool png, shader_image_view *out, char *error, size_t size)
{
    int width = 0, height = 0;
    if (png) {
        chibi_image_cp_load_png_wh(bytes, length, &width, &height);
    } else {
        chibi_image_cj_load_jpg_wh(bytes, length, &width, &height);
    }
    if (!sides_valid(width, height)) {
        return failure(error, size, IMAGE_SIDES_MESSAGE);
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
    if (!pixels) {
        return failure(error, size, png ? chibi_image_cp_error_reason : chibi_image_cj_error_reason);
    }
    *out = (shader_image_view){ pixels, width, height };
    return true;
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
    bool decoded = decode_bytes(bytes, length, png, out, error, size);
    free(bytes);
    return decoded;
}
const char *image_prepare_check(const void *bytes, size_t length, char *error, size_t size)
{
    static const unsigned char png_magic[] = { 0x89, 'P',  'N',  'G',
                                               '\r', '\n', 0x1a, '\n' };
    static const unsigned char jpg_magic[] = { 0xff, 0xd8, 0xff };
    bool png = length >= sizeof png_magic &&
               !memcmp(bytes, png_magic, sizeof png_magic);
    bool jpg = length >= sizeof jpg_magic &&
               !memcmp(bytes, jpg_magic, sizeof jpg_magic);
    if (!png && !jpg) {
        failure(error, size, "Expected a PNG or JPEG image");
        return NULL;
    }
    if (length > IMAGE_BYTES_MAX) {
        failure(error, size, "Image is over 64 MiB");
        return NULL;
    }
    shader_image_view view;
    if (!decode_bytes(bytes, (int)length, png, &view, error, size)) {
        return NULL;
    }
    free((void *)view.pixels);
    return png ? "png" : "jpg";
}
void *image_prepare_png_read(const char *path, int *length, char *error, size_t size)
{
    FILE *file = fopen(path, "rb");
    void *bytes = file ? read_file(file, length) : NULL;
    if (!bytes) {
        failure(error, size, "Cannot read bounded image file");
        return NULL;
    }
    shader_image_view view;
    if (!decode_bytes(bytes, *length, true, &view, error, size)) {
        free(bytes);
        return NULL;
    }
    free((void *)view.pixels);
    return bytes;
}
/* The next header line of a Radiance file, without its newline, at most
 * SKY_LINE_MAX - 1 bytes; false at the end of the bytes. */
static bool header_line(const unsigned char **at, const unsigned char *end, char line[SKY_LINE_MAX])
{
    const unsigned char *newline = memchr(*at, '\n', (size_t)(end - *at));
    if (!newline || newline - *at >= SKY_LINE_MAX) {
        return false;
    }
    memcpy(line, *at, (size_t)(newline - *at));
    line[newline - *at] = 0;
    *at = newline + 1;
    return true;
}
/* Copies one scanline of `width` flat texels. Mantissas of 1, 1, 1 mark a
 * repeat in Radiance's original run-length encoding, which no current tool
 * writes; plain texels never use them. */
static const char *flat_scanline(
    const unsigned char **at,
    const unsigned char *end,
    int width,
    unsigned char *row
)
{
    if ((size_t)(end - *at) < (size_t)width * 4) {
        return SKY_TEXELS_MESSAGE;
    }
    memcpy(row, *at, (size_t)width * 4);
    *at += (size_t)width * 4;
    for (int x = 0; x < width; x++) {
        if (row[x * 4] == 1 && row[x * 4 + 1] == 1 && row[x * 4 + 2] == 1) {
            return "Sky uses Radiance's original run-length encoding; save it "
                   "again from a current HDR tool";
        }
    }
    return NULL;
}
/* Decodes one scanline of `width` texels, run-length encoded per channel when
 * it opens with 2, 2 and the width, as Radiance writes rows 8 to 32767 wide;
 * NULL, or what is wrong with it. */
static const char *scanline(const unsigned char **at, const unsigned char *end, int width, unsigned char *row)
{
    const unsigned char *p = *at;
    if (width < 8 || end - p < 4 || p[0] != 2 || p[1] != 2 || p[2] & 0x80) {
        return flat_scanline(at, end, width, row);
    }
    if ((p[2] << 8 | p[3]) != width) {
        return SKY_TEXELS_MESSAGE;
    }
    p += 4;
    for (int channel = 0; channel < 4; channel++) {
        for (int x = 0; x < width;) {
            if (p == end) {
                return SKY_TEXELS_MESSAGE;
            }
            int count = *p++;
            bool run = count > 128;
            count -= run ? 128 : 0;
            if (!count || count > width - x || end - p < (run ? 1 : count)) {
                return SKY_TEXELS_MESSAGE;
            }
            for (int i = 0; i < count; i++) {
                row[(x + i) * 4 + channel] = p[run ? 0 : i];
            }
            p += run ? 1 : count;
            x += count;
        }
    }
    *at = p;
    return NULL;
}
bool image_prepare_sky_decode(const void *bytes, size_t length, render_sky *sky, char *error, size_t size)
{
    const unsigned char *at = bytes, *end = at + length;
    char line[SKY_LINE_MAX];
    if (!header_line(&at, end, line) ||
        (strcmp(line, "#?RADIANCE") && strcmp(line, "#?RGBE"))) {
        return failure(error, size, "Sky is not a Radiance .hdr image");
    }
    bool header = true;
    while ((header = header_line(&at, end, line)) && line[0]) {
        if (!strncmp(line, "FORMAT=", 7) && strcmp(line, "FORMAT=32-bit_rle_rgbe")) {
            return failure(error, size, "Sky must use the 32-bit_rle_rgbe format");
        }
    }
    if (!header) {
        return failure(error, size, "Sky header lines must be under 512 bytes and end with a blank line");
    }
    int width = 0, height = 0;
    char extra;
    if (!header_line(&at, end, line) ||
        sscanf(line, "-Y %d +X %d%c", &height, &width, &extra) != 2) {
        return failure(error, size, "Sky rows must run top to bottom, left to right (-Y +X)");
    }
    if (!render_sky_sides_valid(width, height)) {
        return failure(error, size, SKY_SIDES_MESSAGE);
    }
    unsigned char *texels = xmalloc((size_t)width * height * 4);
    for (int y = 0; y < height; y++) {
        const char *problem = scanline(&at, end, width, texels + (size_t)y * width * 4);
        if (problem) {
            free(texels);
            return failure(error, size, problem);
        }
    }
    *sky = (render_sky){ .width = width, .height = height, .texels = texels };
    return true;
}
static bool sky_key(const char *key, char *error, size_t size)
{
    const char *extension = strrchr(key, '.');
    if (!content_key(key) || !extension || strcasecmp(extension, ".hdr")) {
        return failure(error, size, "Sky must be a content/... .hdr path without parent traversal");
    }
    return true;
}
bool image_prepare_sky(const char *key, char *error, size_t size)
{
    if (render_sky_find(key)) {
        return true;
    }
    if (!sky_key(key, error, size)) {
        return false;
    }
    int length = 0;
    void *bytes = read_bytes(key, &length);
    if (!bytes) {
        return failure(error, size, "Cannot read the sky file, or it is over 64 MiB");
    }
    render_sky sky;
    bool valid = image_prepare_sky_decode(bytes, (size_t)length, &sky, error, size);
    free(bytes);
    if (valid) {
        render_sky_publish(key, &sky);
    }
    return valid;
}
/* Creates the folders of `path` below its first `skip` bytes. */
static bool make_folders(char *path, size_t skip)
{
    for (char *slash = strchr(path + skip, '/'); slash;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        bool made = !mkdir(path, 0777) || errno == EEXIST;
        *slash = '/';
        if (!made) {
            return false;
        }
    }
    return true;
}
/* The default key for an imported file: its name under content/skies/. */
static char *default_sky_key(const char *path)
{
    const char *name = path;
    for (const char *c = path; *c; c++) {
        name = *c == '/' || *c == '\\' ? c + 1 : name;
    }
    size_t size = strlen(name) + sizeof "content/skies/";
    char *key = xmalloc(size);
    snprintf(key, size, "content/skies/%s", name);
    return key;
}
/* Writes `length` bytes to a new file at `target`, never replacing one;
 * errors name it by `key`. */
static bool write_new(
    const char *target,
    const char *key,
    const void *bytes,
    size_t length,
    char *error,
    size_t size
)
{
    FILE *file = fopen(target, "wbx");
    if (!file) {
        int code = errno;
        if (size) {
            snprintf(
                error,
                size,
                "Cannot create %s: %s",
                key,
                code == EEXIST ? "a file is already there" : strerror(code)
            );
        }
        return false;
    }
    bool written = fwrite(bytes, 1, length, file) == length;
    written = fclose(file) == 0 && written;
    if (!written) {
        remove(target);
        return failure(error, size, "Cannot write the whole sky file");
    }
    return true;
}
/* Writes the checked sky `bytes` to a new file for `key` in the content
 * folder `root`, creating the folders it needs. */
static bool place_sky(const char *root, const char *key, const void *bytes, size_t length, char *error, size_t size)
{
    size_t target_size = strlen(root) + strlen(key + 8) + 2;
    char *target = xmalloc(target_size);
    snprintf(target, target_size, "%s/%s", root, key + 8);
    bool placed = make_folders(target, strlen(root) + 1)
                    ? write_new(target, key, bytes, length, error, size)
                    : failure(error, size, "Cannot create the sky's folders");
    free(target);
    return placed;
}
char *image_prepare_sky_import(const char *root, const char *path, const char *key, char *error, size_t size)
{
    char *owned = key ? xstrdup(key) : default_sky_key(path);
    FILE *source = fopen(path, "rb");
    int length = 0;
    void *bytes = source ? read_file(source, &length) : NULL;
    render_sky sky = { 0 };
    bool valid = sky_key(owned, error, size) &&
                 (bytes ||
                  failure(error, size, "Cannot read the source file, or it is over 64 MiB")) &&
                 image_prepare_sky_decode(bytes, (size_t)length, &sky, error, size) &&
                 place_sky(root, owned, bytes, (size_t)length, error, size);
    render_sky_free(&sky);
    free(bytes);
    if (!valid) {
        free(owned);
        return NULL;
    }
    return owned;
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
