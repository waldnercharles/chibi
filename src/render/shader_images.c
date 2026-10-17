#include "render/shader_images.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int variant;
    uint64_t revision;
    shader_image_source sampler;
    CF_Texture texture;
} image_texture;
typedef struct
{
    CF_Texture texture;
    uint64_t frame;
} retired_texture;
static image_texture *textures;
static retired_texture *retired;
static CF_Texture upload(const shader_image_source *variant, const shader_image_view *image)
{
    CF_TextureParams params = cf_texture_defaults(image->width, image->height);
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
    /* Filtered images get a full mip chain and anisotropic sampling, so
     * receding and grazing surfaces do not shimmer or alias into noise. GPU
     * blits fill the chain, which needs color-target usage. Nearest images
     * stay single-level, keeping their texels crisp. */
    bool filtered = variant->filter == SHADER_IMAGE_BILINEAR;
    if (filtered) {
        params.allocate_mipmaps = true;
        params.max_anisotropy = 8;
        params.usage |= CF_TEXTURE_USAGE_COLOR_TARGET_BIT;
    }
    CF_Texture texture = cf_make_texture(params);
    if (texture.id) {
        cf_texture_update(texture, (void *)image->pixels, image->width * image->height * 4);
        if (filtered) {
            cf_generate_mipmaps(texture);
        }
    }
    return texture;
}
static void retire(void *, uint64_t revision)
{
    for (int i = acount(textures) - 1; i >= 0; i--) {
        if (textures[i].revision != revision) {
            continue;
        }
        if (textures[i].texture.id) {
            apush(retired, ((retired_texture){ textures[i].texture, CF_TICKS }));
        }
        free((void *)textures[i].sampler.key);
        adel(textures, i);
    }
}
static bool replace(
    void *,
    const char *key,
    const shader_image_view *view,
    uint64_t previous,
    uint64_t revision,
    char *error,
    size_t size
)
{
    CF_Texture *fresh = NULL;
    bool valid = true;
    for (int i = 0; i < acount(textures); i++) {
        CF_Texture texture = { 0 };
        if (textures[i].revision == previous && !strcmp(textures[i].sampler.key, key)) {
            texture = upload(&textures[i].sampler, view);
            valid &= texture.id != 0;
        }
        apush(fresh, texture);
    }
    if (!valid) {
        for (int i = 0; i < acount(fresh); i++) {
            if (fresh[i].id) {
                cf_destroy_texture(fresh[i]);
            }
        }
        afree(fresh);
        if (size) {
            snprintf(error, size, "Could not upload replacement image");
        }
        return false;
    }
    for (int i = 0; i < acount(textures); i++) {
        if (!fresh[i].id) {
            continue;
        }
        apush(retired, ((retired_texture){ textures[i].texture, CF_TICKS }));
        textures[i].texture = fresh[i];
        textures[i].revision = revision;
    }
    afree(fresh);
    return true;
}
CF_Texture render_image_texture(int id, shader_error *error)
{
    shader_image_source sampler;
    shader_image_view view;
    uint64_t revision;
    if (!render_image_sampler(id, &sampler, &view, &revision)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Image sampler is not resident");
        }
        return (CF_Texture){ 0 };
    }
    for (int i = 0; i < acount(textures); i++) {
        if (textures[i].variant == id && textures[i].revision == revision) {
            return textures[i].texture;
        }
    }
    render_images_set_backend((render_image_backend){ .replace = replace,
                                                      .retire = retire });
    CF_Texture texture = upload(&sampler, &view);
    if (!texture.id) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "%.100s: could not create image texture",
                sampler.key
            );
        }
        return texture;
    }
    sampler.key = xstrdup(sampler.key);
    apush(textures, ((image_texture){ id, revision, sampler, texture }));
    return texture;
}
void render_images_collect()
{
    for (int i = acount(retired) - 1; i >= 0; i--) {
        if (retired[i].frame == CF_TICKS) {
            continue;
        }
        cf_destroy_texture(retired[i].texture);
        adel(retired, i);
    }
}
void render_images_clear()
{
    render_image_catalog_clear();
    assert(!acount(textures));
    afree(textures);
    for (int i = 0; i < acount(retired); i++) {
        cf_destroy_texture(retired[i].texture);
    }
    afree(retired);
}
