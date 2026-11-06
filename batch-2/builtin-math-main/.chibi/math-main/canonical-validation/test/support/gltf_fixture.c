#include "test/support/gltf_fixture.h"

#include "platform/cute.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    GL_UNSIGNED_SHORT = 5123,
    GL_FLOAT = 5126,
    GLB_MAGIC = 0x46546c67, /* "glTF" */
    GLB_VERSION = 2,
    GLB_JSON = 0x4e4f534a, /* "JSON" */
    GLB_BIN = 0x004e4942,  /* "BIN" */
};

static const char *const TYPES[] = { [1] = "SCALAR", [2] = "VEC2", [3] = "VEC3", [4] = "VEC4" };

static void pad(unsigned char **bytes, unsigned char with)
{
    while (acount(*bytes) % 4) {
        apush(*bytes, with);
    }
}

static void append(unsigned char **bytes, const void *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        apush(*bytes, ((const unsigned char *)data)[i]);
    }
}

static int add(gltf_fixture *f, const void *values, size_t length, const char *type, int component, int count)
{
    gltf_fixture_accessor accessor = { type, component, count, (size_t)acount(f->buffer), length };
    append(&f->buffer, values, length);
    pad(&f->buffer, 0);
    apush(f->accessors, accessor);
    return acount(f->accessors) - 1;
}

int gltf_fixture_floats(gltf_fixture *f, const float *values, int count, int components)
{
    assert(components >= 1 && components <= 4);
    size_t length = sizeof *values * (size_t)count * (size_t)components;
    return add(f, values, length, TYPES[components], GL_FLOAT, count);
}

int gltf_fixture_indices(gltf_fixture *f, const uint16_t *values, int count)
{
    return add(f, values, sizeof *values * (size_t)count, "SCALAR", GL_UNSIGNED_SHORT, count);
}

void gltf_fixture_square(gltf_fixture *f, const float uvs[8])
{
    static const float positions[] = { -1, 0, -1, -1, 0, 1, 1, 0, 1, 1, 0, -1 };
    static const float normals[] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    static const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
    gltf_fixture_floats(f, positions, 4, 3);
    gltf_fixture_floats(f, normals, 4, 3);
    gltf_fixture_floats(f, uvs, 4, 2);
    gltf_fixture_indices(f, indices, 6);
}

int gltf_fixture_image(gltf_fixture *f, const char *file, const void *bytes, size_t length)
{
    gltf_fixture_file image = { .bytes = xmalloc(length ? length : 1), .length = length };
    assert(strlen(file) < sizeof image.file);
    strcpy(image.file, file);
    memcpy(image.bytes, bytes, length);
    apush(f->images, image);
    return acount(f->images) - 1;
}

int gltf_fixture_png(gltf_fixture *f, const char *file, int width, int height, const uint8_t rgba[4])
{
    CF_Pixel *pixels = xmalloc(sizeof *pixels * (size_t)width * (size_t)height);
    for (int i = 0; i < width * height; i++) {
        pixels[i] = (CF_Pixel){ .colors = { rgba[0], rgba[1], rgba[2], rgba[3] } };
    }
    CF_Image image = { width, height, pixels };
    void *png = NULL;
    int length = 0;
    bool saved = !cf_is_error(cf_image_save_png_to_memory(&image, &png, &length));
    assert(saved);
    (void)saved;
    free(pixels);
    int index = gltf_fixture_image(f, file, png, (size_t)length);
    cf_free(png);
    return index;
}

static bool write_bytes(const char *path, const void *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool written = fwrite(bytes, 1, length, file) == length;
    return !fclose(file) && written;
}

/* `path` with its file name replaced by `file`. */
static char *beside(const char *path, const char *file)
{
    const char *slash = strrchr(path, '/');
    int folder = slash ? (int)(slash - path) + 1 : 0;
    size_t size = (size_t)folder + strlen(file) + 1;
    char *out = xmalloc(size);
    snprintf(out, size, "%.*s%s", folder, path, file);
    return out;
}

/* The document's JSON. In a .glb the images follow the accessors' bytes in
 * `buffer` as views of their own; in a .gltf they are files beside it. */
static char *document(const gltf_fixture *f, const char *bin, unsigned char **buffer, const char *members)
{
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    assert(out && acount(f->accessors));
    append(buffer, f->buffer, (size_t)acount(f->buffer));
    fprintf(out, "{\"asset\":{\"version\":\"2.0\"},%s,\"bufferViews\":[", members);
    for (int i = 0; i < acount(f->accessors); i++) {
        const gltf_fixture_accessor *a = &f->accessors[i];
        fprintf(
            out,
            "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}",
            i ? "," : "",
            a->offset,
            a->length
        );
    }
    for (int i = 0; bin == NULL && i < acount(f->images); i++) {
        fprintf(
            out,
            ",{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%zu}",
            acount(*buffer),
            f->images[i].length
        );
        append(buffer, f->images[i].bytes, f->images[i].length);
        pad(buffer, 0);
    }
    fprintf(out, "],\"accessors\":[");
    for (int i = 0; i < acount(f->accessors); i++) {
        const gltf_fixture_accessor *a = &f->accessors[i];
        fprintf(
            out,
            "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":"
            "\"%s\"}",
            i ? "," : "",
            i,
            a->component,
            a->count,
            a->type
        );
    }
    fprintf(out, "]");
    for (int i = 0; i < acount(f->images); i++) {
        fprintf(out, i ? "," : ",\"images\":[");
        const char *file = f->images[i].file;
        if (bin) {
            fprintf(out, "{\"uri\":\"%s\"}", file);
            continue;
        }
        int stem = (int)strcspn(file, ".");
        fprintf(
            out,
            "{\"name\":\"%.*s\",\"bufferView\":%d,\"mimeType\":\"image/png\"}",
            stem,
            file,
            acount(f->accessors) + i
        );
    }
    fprintf(out, "%s,\"buffers\":[{\"byteLength\":%d", acount(f->images) ? "]" : "", acount(*buffer));
    if (bin) {
        fprintf(out, ",\"uri\":\"%s\"", bin);
    }
    fprintf(out, "}]}");
    fclose(out);
    return text;
}

static void put_u32(unsigned char **bytes, uint32_t value)
{
    unsigned char word[4] = { value, value >> 8, value >> 16, value >> 24 };
    append(bytes, word, 4);
}

static bool write_glb(const gltf_fixture *f, const char *path, const char *members)
{
    unsigned char *buffer = NULL, *json = NULL, *file = NULL;
    char *text = document(f, NULL, &buffer, members);
    append(&json, text, strlen(text));
    free(text);
    pad(&json, ' ');
    put_u32(&file, GLB_MAGIC);
    put_u32(&file, GLB_VERSION);
    put_u32(&file, (uint32_t)(12 + 8 + acount(json) + 8 + acount(buffer)));
    put_u32(&file, (uint32_t)acount(json));
    put_u32(&file, GLB_JSON);
    append(&file, json, (size_t)acount(json));
    put_u32(&file, (uint32_t)acount(buffer));
    put_u32(&file, GLB_BIN);
    append(&file, buffer, (size_t)acount(buffer));
    bool written = write_bytes(path, file, (size_t)acount(file));
    afree(buffer);
    afree(json);
    afree(file);
    return written;
}

static bool write_gltf(const gltf_fixture *f, const char *path, const char *members)
{
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    char bin[128];
    snprintf(bin, sizeof bin, "%.*s.bin", (int)strcspn(name, "."), name);
    unsigned char *buffer = NULL;
    char *text = document(f, bin, &buffer, members);
    char *bin_path = beside(path, bin);
    bool written = write_bytes(path, text, strlen(text)) &&
                   write_bytes(bin_path, buffer, (size_t)acount(buffer));
    for (int i = 0; written && i < acount(f->images); i++) {
        char *image_path = beside(path, f->images[i].file);
        written = write_bytes(image_path, f->images[i].bytes, f->images[i].length);
        free(image_path);
    }
    free(bin_path);
    free(text);
    afree(buffer);
    return written;
}

bool gltf_fixture_write(const gltf_fixture *f, const char *path, const char *members)
{
    size_t length = strlen(path);
    return length > 4 && !strcmp(path + length - 4, ".glb")
             ? write_glb(f, path, members)
             : write_gltf(f, path, members);
}

void gltf_fixture_free(gltf_fixture *f)
{
    for (int i = 0; i < acount(f->images); i++) {
        free(f->images[i].bytes);
    }
    afree(f->images);
    afree(f->accessors);
    afree(f->buffer);
}
