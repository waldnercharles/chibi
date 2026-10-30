#ifndef CHIBI_IMAGE_CATALOG_H
#define CHIBI_IMAGE_CATALOG_H

#define RENDER_IMAGE_PREPARED_VERSION 2

#include "runtime/shaders/program.h"

#include <stddef.h>
enum
{
    RENDER_IMAGE_SIDE_MAX = 2048
};

/* Optional authoring loader transfers malloc-owned RGBA8 pixels on success.
 * Desktop replacement hooks preflight GPU uploads before resident pixel commit;
 * retirement hooks run while the old immutable pixel owner still exists.
 * These copied hooks and contexts are thread-confined and outlive catalog use. */
typedef struct
{
    void *context;
    bool (*read)(void *, const char *, shader_image_view *, char *, size_t);
    void (*clear)(void *);
} render_image_loader;
typedef struct
{
    void *context;
    bool (*replace)(
        void *,
        const char *,
        const shader_image_view *,
        uint64_t previous,
        uint64_t revision,
        char *,
        size_t
    );
    void (*retire)(void *, uint64_t);
} render_image_backend;
void render_images_set_loader(render_image_loader);
render_image_loader render_images_loader();
void render_images_set_backend(render_image_backend);
bool render_image_sampler(int id, shader_image_source *, shader_image_view *, uint64_t *revision);

/* Authoring operations. Failed reloads retain the last valid pixels and textures. */
bool render_image_reload(const char *key, char *message, size_t size);
bool render_image_info(const char *key, int *width, int *height);

bool render_image_validate_volumes(const shader_graph *, shader_error *);

/* Variant IDs are catalog-owned. Resolving and binding resident data does no file IO. */
int render_image_variant(const shader_image_source *source, shader_error *error);
/* Prepared runtime handoff: requires existing pixels and never loads a path. */
int render_image_variant_resident(const shader_image_source *, shader_error *);
bool render_image_resolve(void *, const shader_image_source *source, shader_image_view *view);
void render_image_catalog_clear();
/* Portable RGBA8 resident pixel snapshots; no file reads or GPU upload. */
int render_image_source_count();
const char *render_image_source_key(int);
bool render_image_prepared_write(const char *, unsigned char **, size_t *, shader_error *);
/* Copies caller RGBA8 pixels into the same portable image artifact. */
bool render_image_pixels_prepared_write(const shader_image_view *, unsigned char **, size_t *, shader_error *);

#endif
