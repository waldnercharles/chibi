#ifndef CHIBI_IMAGE_SKY_H
#define CHIBI_IMAGE_SKY_H

#include <stddef.h>
#include <stdint.h>

#define RENDER_SKY_PREPARED_VERSION 1

enum
{
    /* A 4096 x 2048 sky holds 32 MiB of texels. */
    RENDER_SKY_WIDTH_MAX = 4096
};

/* An equirectangular HDR sky in Radiance's RGBE format: four bytes per texel,
 * a shared exponent after three mantissas. It is twice as wide as it is tall.
 * Row 0 looks straight up. The middle column looks along -Z, the column three
 * quarters across along +X, and the left and right edges along +Z. */
typedef struct
{
    int width, height;
    unsigned char *texels;
    /* Set by the catalog from the size and texels, so equal skies share it. */
    uint64_t identity;
} render_sky;

void render_sky_free(render_sky *);
bool render_sky_sides_valid(int width, int height);
/* One RGBE texel for linear radiance rgb, nonnegative. */
void render_sky_encode(const float rgb[3], unsigned char texel[4]);
/* The edge of each cube face built from the sky: the largest power of two up
 * to a quarter of its width. */
int render_sky_face_size(const render_sky *);
/* Writes face 0-5 (+X, -X, +Y, -Y, +Z, -Z, in the GPU cube-map convention) as
 * size * size RGBA16F texels, bilinearly sampled and clamped to 65504. */
void render_sky_face(const render_sky *, int face, int size, uint16_t *texels);

/* The irradiance of the sky's radiance, clamped to 65504 as in its faces, as
 * nine spherical-harmonic coefficients per channel, convolved with the cosine
 * lobe and folded with their basis constants, so the irradiance arriving at a
 * surface facing unit direction (x, y, z) is
 * c0 + c1 x + c2 y + c3 z + c4 xy + c5 yz + c6 (3y^2 - 1) + c7 xz +
 * c8 (x^2 - z^2). A white sky of radiance 1 gives pi everywhere.
 * Reference: https://graphics.stanford.edu/papers/envmap/envmap.pdf */
void render_sky_irradiance(const render_sky *, float coefficients[9][3]);
/* Writes the split-sum table for lighting from a prefiltered sky as size *
 * size RG16F texels: the scale and bias that turn a surface's reflectance at
 * normal incidence into its reflectance under even light, for n.v across
 * and GGX roughness down, at texel centers. The visibility matches the
 * direct lights' height-correlated Smith term.
 * Reference: https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf */
void render_sky_split_sum(int size, uint16_t *texels);

/* Resident skies keyed by content path. Lookups do no file IO; publishing
 * takes ownership of the texels and replaces an earlier sky with that key. */
const render_sky *render_sky_find(const char *key);
void render_sky_publish(const char *key, render_sky *);
void render_skies_clear();

/* Portable prepared bytes for one sky; the reader charges what it keeps. */
bool render_sky_prepared_write(const render_sky *, unsigned char **, size_t *, char *, size_t);

/* A detached generation of skies, swapped in whole. Decoding takes a key the
 * table does not hold yet. */
typedef struct render_sky_table render_sky_table;
render_sky_table *render_sky_table_new();
size_t render_sky_table_header_bytes();
bool render_sky_table_decode(
    render_sky_table *,
    const char *key,
    const void *,
    size_t,
    size_t *remaining,
    char *error,
    size_t size
);
bool render_sky_table_contains(const render_sky_table *, const char *key);
void render_sky_table_exchange(render_sky_table *);
void render_sky_table_free(render_sky_table *);

#endif
