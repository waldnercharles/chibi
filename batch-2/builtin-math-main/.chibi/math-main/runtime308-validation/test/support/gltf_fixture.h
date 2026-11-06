#ifndef CHIBI_TEST_GLTF_FIXTURE_H
#define CHIBI_TEST_GLTF_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A glTF 2.0 model a case builds: its accessors gather in one buffer, its
 * images are files, and the case writes the document's other members. */
typedef struct
{
    const char *type;
    int component, count;
    size_t offset, length;
} gltf_fixture_accessor;

typedef struct
{
    char file[64];
    unsigned char *bytes;
    size_t length;
} gltf_fixture_file;

typedef struct
{
    unsigned char *buffer; /* array.h, each accessor 4-byte aligned */
    gltf_fixture_accessor *accessors; /* array.h */
    gltf_fixture_file *images;        /* array.h */
} gltf_fixture;

/* Adds `count` elements of `components` floats each and returns the
 * accessor's index. */
int gltf_fixture_floats(gltf_fixture *, const float *values, int count, int components);
int gltf_fixture_indices(gltf_fixture *, const uint16_t *values, int count);
/* Adds the two-unit square on y = 0 facing +y as accessors 0 to 3, in
 * GLTF_FIXTURE_SQUARE's order. `uvs` are the texture coordinates of its
 * corners at (x, z) = (-1, -1), (-1, 1), (1, 1) and (1, -1). */
void gltf_fixture_square(gltf_fixture *, const float uvs[8]);
#define GLTF_FIXTURE_SQUARE                                                    \
    "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},"           \
    "\"indices\":3"
/* Adds a copy of `bytes` as the image file `file` and returns its index. */
int gltf_fixture_image(gltf_fixture *, const char *file, const void *bytes, size_t length);
/* Adds a width x height PNG of one color as the image file `file`. */
int gltf_fixture_png(gltf_fixture *, const char *file, int width, int height, const uint8_t rgba[4]);
/* Writes the model to `path` with the JSON object members `members` (such as
 * "meshes":[...],"nodes":[...]) besides its buffer, views, accessors and
 * images. A .glb holds everything, naming each image for its file's stem; a
 * .gltf names <stem>.bin and each image file beside it. */
bool gltf_fixture_write(const gltf_fixture *, const char *path, const char *members);
void gltf_fixture_free(gltf_fixture *);

#endif
