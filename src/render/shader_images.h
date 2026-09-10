#ifndef CHIBI_SHADER_IMAGES_H
#define CHIBI_SHADER_IMAGES_H

#include "graph/shader/shader_graph.h"
#include "platform/cute.h"

#include <stddef.h>
enum
{
    RENDER_IMAGE_SIDE_MAX = 2048
};

/* Authoring operations. Failed reloads retain the last valid pixels and textures. */
bool render_image_mount_content(const char *root, char *message, size_t size);
bool render_image_load(const char *key, char *message, size_t size);
bool render_image_reload(const char *key, char *message, size_t size);
bool render_image_info(const char *key, int *width, int *height);
const char *render_image_error(const char *key);

/* Variant IDs are catalog-owned. Resolving and binding resident data does no file IO. */
int render_image_variant(const shader_image_source *source, shader_error *error);
CF_Texture render_image_texture(int variant, shader_error *error);
void render_images_collect(); /* Release replaced GPU handles after their submission frame. */
bool render_image_resolve(void *, const shader_image_source *source, shader_image_view *view);
void render_images_clear();

#endif
