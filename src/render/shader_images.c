#include "render/shader_images.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct
{
    char *key;
    CF_Image image;
    char error[256];
} image_source;

typedef struct
{
    int source;
    shader_image_color_space color_space;
    shader_image_filter filter;
    shader_image_wrap wrap;
    CF_Texture texture;
} image_variant;

static image_source *sources;
static image_variant *variants;
static char **mounts;
typedef struct
{
    CF_Texture texture;
    uint64_t frame;
} retired_texture;
static retired_texture *retired;

void render_images_collect()
{
    for (int i = acount(retired) - 1; i >= 0; i--) {
        if (retired[i].frame != CF_TICKS) {
            cf_destroy_texture(retired[i].texture);
            adel(retired, i);
        }
    }
}

bool render_image_mount_content(const char *root, char *message, size_t size)
{
    for (int i = 0; i < acount(mounts); i++) {
        if (!strcmp(mounts[i], root)) {
            return true;
        }
    }
    /* Shader graphs use the stable content/... key space. A project selects a
     * physical content directory and mounts it into that space. Appending the
     * mount lets the selected project override an older mount without making
     * shader graphs know where the project lives. */
    CF_Result result = cf_fs_mount(root, "/content", true);
    if (cf_is_error(result)) {
        if (size) {
            snprintf(
                message,
                size,
                "Cannot mount image content %s: %s",
                root,
                result.details
            );
        }
        return false;
    }
    /* The default content mount lives with the Cute app, across catalog resets. */
    if (strcmp(root, "content")) {
        apush(mounts, xstrdup(root));
    }
    if (size) {
        message[0] = 0;
    }
    return true;
}

static int find_source(const char *key)
{
    for (int i = 0; i < acount(sources); i++) {
        if (!strcmp(sources[i].key, key)) {
            return i;
        }
    }
    return -1;
}

static CF_Texture upload(const image_variant *variant, const CF_Image *image)
{
    CF_TextureParams params = cf_texture_defaults(image->w, image->h);
    params.pixel_format = variant->color_space == SHADER_IMAGE_SRGB
                            ? CF_PIXEL_FORMAT_R8G8B8A8_UNORM_SRGB
                            : CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.filter = variant->filter == SHADER_IMAGE_BILINEAR ? CF_FILTER_LINEAR
                                                             : CF_FILTER_NEAREST;
    params.mip_filter = variant->filter == SHADER_IMAGE_BILINEAR
                          ? CF_MIP_FILTER_LINEAR
                          : CF_MIP_FILTER_NEAREST;
    params.wrap_u = params.wrap_v = variant->wrap == SHADER_IMAGE_REPEAT
                                      ? CF_WRAP_MODE_REPEAT
                                      : CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(params);
    if (texture.id) {
        cf_texture_update(
            texture,
            image->pix,
            image->w * image->h * (int)sizeof *image->pix
        );
    }
    return texture;
}

static CF_Result decode(const char *key, CF_Image *image)
{
    const char *extension = strrchr(key, '.');
    bool png = extension && !strcasecmp(extension, ".png");
    bool jpg = extension &&
               (!strcasecmp(extension, ".jpg") || !strcasecmp(extension, ".jpeg"));
    if (!png && !jpg) {
        return cf_result_error("Expected a PNG, JPG or JPEG image.");
    }
    size_t size = 0;
    void *bytes = cf_fs_read_entire_file_to_memory(key, &size);
    if (!bytes) {
        return cf_result_error("Cannot read the image file.");
    }
    if (size > INT_MAX) {
        cf_free(bytes);
        return cf_result_error("Image file exceeds the decoder size limit.");
    }
    int width = 0, height = 0;
    CF_Result result = png ? cf_image_load_png_wh(bytes, (int)size, &width, &height)
                           : cf_image_load_jpg_wh(bytes, (int)size, &width, &height);
    if (!cf_is_error(result) && (width < 1 || height < 1 || width > RENDER_IMAGE_SIDE_MAX ||
                                 height > RENDER_IMAGE_SIDE_MAX)) {
        result = cf_result_error("Image dimensions must be between 1 and 2048 pixels per side.");
    }
    if (!cf_is_error(result)) {
        result = png ? cf_image_load_png_from_memory(bytes, (int)size, image)
                     : cf_image_load_jpg_from_memory(bytes, (int)size, image);
    }
    cf_free(bytes);
    return result;
}

bool render_image_reload(const char *key, char *message, size_t size)
{
    int index = find_source(key);
    if (index < 0) {
        index = acount(sources);
        apush(sources, ((image_source){ .key = xstrdup(key) }));
    }
    image_source *source = &sources[index];
    CF_Image fresh = { 0 };
    CF_Result result = decode(key, &fresh);
    if (cf_is_error(result)) {
        snprintf(source->error, sizeof source->error, "%s: %s", key, result.details);
    } else if (fresh.w <= 0 || fresh.h <= 0 || fresh.w > INT_MAX / 4 / fresh.h) {
        snprintf(source->error, sizeof source->error, "%s: image dimensions exceed the upload limit.", key);
    } else {
        CF_Texture *textures = NULL;
        bool valid = true;
        for (int i = 0; i < acount(variants); i++) {
            CF_Texture texture = { 0 };
            if (variants[i].source == index && variants[i].texture.id) {
                texture = upload(&variants[i], &fresh);
                valid &= texture.id != 0;
            }
            apush(textures, texture);
        }
        if (valid) {
            for (int i = 0; i < acount(variants); i++) {
                if (textures[i].id) {
                    apush(retired, ((retired_texture){ variants[i].texture, CF_TICKS }));
                    variants[i].texture = textures[i];
                }
            }
            afree(textures);
            cf_image_free(&source->image);
            source->image = fresh;
            source->error[0] = 0;
            if (size) {
                message[0] = 0;
            }
            return true;
        }
        for (int i = 0; i < acount(textures); i++) {
            if (textures[i].id) {
                cf_destroy_texture(textures[i]);
            }
        }
        afree(textures);
        snprintf(source->error, sizeof source->error, "%s: could not upload the replacement image.", key);
    }
    cf_image_free(&fresh);
    if (size) {
        snprintf(message, size, "%s", source->error);
    }
    return false;
}

bool render_image_load(const char *key, char *message, size_t size)
{
    int index = find_source(key);
    if (index < 0) {
        return render_image_reload(key, message, size);
    }
    if (size) {
        snprintf(message, size, "%s", sources[index].error);
    }
    return sources[index].image.pix != NULL;
}

bool render_image_info(const char *key, int *width, int *height)
{
    int index = find_source(key);
    if (index < 0 || !sources[index].image.pix) {
        return false;
    }
    *width = sources[index].image.w;
    *height = sources[index].image.h;
    return true;
}

const char *render_image_error(const char *key)
{
    int index = find_source(key);
    return index < 0 ? "" : sources[index].error;
}

int render_image_variant(const shader_image_source *source, shader_error *error)
{
    char message[256];
    if (!render_image_load(source->key, message, sizeof message)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "%s", message);
        }
        return 0;
    }
    int image = find_source(source->key);
    for (int i = 0; i < acount(variants); i++) {
        const image_variant *variant = &variants[i];
        if (variant->source == image && variant->color_space == source->color_space &&
            variant->filter == source->filter && variant->wrap == source->wrap) {
            return i + 1;
        }
    }
    apush(
        variants,
        ((image_variant){ image, source->color_space, source->filter, source->wrap, { 0 } })
    );
    return acount(variants);
}

CF_Texture render_image_texture(int id, shader_error *error)
{
    image_variant *variant = &variants[id - 1];
    image_source *source = &sources[variant->source];
    if (!variant->texture.id) {
        variant->texture = upload(variant, &source->image);
        if (!variant->texture.id) {
            snprintf(
                source->error,
                sizeof source->error,
                "%s: could not create the image texture.",
                source->key
            );
            if (error) {
                snprintf(error->message, sizeof error->message, "%s", source->error);
            }
        }
    }
    return variant->texture;
}

bool render_image_resolve(void *, const shader_image_source *source, shader_image_view *view)
{
    int index = find_source(source->key);
    if (index < 0 || !sources[index].image.pix) {
        return false;
    }
    CF_Image *image = &sources[index].image;
    *view = (shader_image_view){ (const unsigned char *)image->pix,
                                 image->w,
                                 image->h };
    return true;
}

void render_images_clear()
{
    for (int i = 0; i < acount(retired); i++) {
        cf_destroy_texture(retired[i].texture);
    }
    afree(retired);
    for (int i = 0; i < acount(variants); i++) {
        if (variants[i].texture.id) {
            cf_destroy_texture(variants[i].texture);
        }
    }
    afree(variants);
    for (int i = 0; i < acount(sources); i++) {
        free(sources[i].key);
        cf_image_free(&sources[i].image);
    }
    afree(sources);
    for (int i = 0; i < acount(mounts); i++) {
        cf_fs_dismount(mounts[i]);
        free(mounts[i]);
    }
    afree(mounts);
}
