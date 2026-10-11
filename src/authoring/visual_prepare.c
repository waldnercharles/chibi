#include "authoring/visual_prepare.h"

#include "authoring/mesh_prepare.h"
#include "render/scene3d_parts.h"
#include "render/shader_catalog_records.h"
#include "render/shader_values.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/images/catalog.h"
#include "runtime/images/sky.h"
#include "runtime/models/prepared.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool append(
    player_asset **assets,
    const char *name,
    player_asset_kind kind,
    unsigned char *data,
    size_t length,
    size_t *total,
    char *error,
    size_t size
)
{
    if (!data || !length || acount(*assets) == 4096 ||
        length > PACKAGE_MAX_BYTES - *total) {
        free(data);
        if (size) {
            snprintf(error, size, "Prepared visual collection exceeds count or byte limits");
        }
        return false;
    }
    *total += length;
    apush(
        *assets,
        ((player_asset){ .name = xstrdup(name),
                         .kind = kind,
                         .data = data,
                         .length = length,
                         .content_hash = player_asset_fingerprint(data, length) })
    );
    return true;
}
void authoring_visual_prepared_free(player_asset *assets)
{
    for (int i = 0; i < acount(assets); i++) {
        free((void *)assets[i].name);
        free((void *)assets[i].data);
    }
    afree(assets);
}
static bool contains(const player_asset *assets, const char *name, player_asset_kind kind)
{
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].kind == kind && !strcmp(assets[i].name, name)) {
            return true;
        }
    }
    return false;
}
/* Adds the resident sky at `key` unless it is empty or already added. */
static bool append_sky(player_asset **assets, const char *key, size_t *total, char *error, size_t size)
{
    if (!key || !key[0] || contains(*assets, key, PLAYER_ASSET_SKY)) {
        return true;
    }
    const render_sky *sky = render_sky_find(key);
    if (!sky) {
        if (size) {
            snprintf(error, size, "Sky %s has not been prepared", key);
        }
        return false;
    }
    unsigned char *bytes = NULL;
    size_t length = 0;
    return render_sky_prepared_write(sky, &bytes, &length, error, size) &&
           append(assets, key, PLAYER_ASSET_SKY, bytes, length, total, error, size);
}
bool authoring_visual_prepare(
    const scene *const *scenes,
    int scene_count,
    player_asset **result,
    char *error,
    size_t size
)
{
    assert(result && scene_count >= 0 && (!scene_count || scenes) && (!size || error));
    *result = NULL;
    if (scene_count > PLAYER_MAX_ASSETS) {
        if (size) {
            snprintf(error, size, "Prepared visual scene count exceeds limit");
        }
        return false;
    }
    size_t total = 0;
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        const shader_asset *asset = &shader_catalog_assets[i];
        if (!asset->name || !asset->name[0]) {
            continue;
        }
        /* Encoding borrows the immutable graph; the temporary envelope owns
         * nothing and is never passed to the decoded-value destructor. */
        shader_prepared_asset value = { .name = asset->name,
                                        .graph = asset->graph,
                                        .shape = asset->shape,
                                        .alpha_mode = asset->alpha_mode };
        memcpy(value.names, asset->names, sizeof value.names);
        memcpy(value.widths, asset->widths, sizeof value.widths);
        memcpy(value.defaults, asset->defaults, sizeof value.defaults);
        memcpy(value.bounds, asset->bounds, sizeof value.bounds);
        unsigned char *bytes = NULL;
        size_t length = 0;
        shader_error diagnostic = { 0 };
        if (!shader_asset_prepared_write(&value, &bytes, &length, &diagnostic)) {
            if (size) {
                snprintf(error, size, "%s: %s", asset->name, diagnostic.message);
            }
            goto fail;
        }
        if (!append(result, asset->name, PLAYER_ASSET_SHADER, bytes, length, &total, error, size)) {
            goto fail;
        }
        if (asset->is_model) {
            model_program model = asset->model;
            model.shader = asset->graph;
            if (!model_program_prepared_write(&model, &bytes, &length, &diagnostic)) {
                if (size) {
                    snprintf(error, size, "%s: %s", asset->name, diagnostic.message);
                }
                goto fail;
            }
            if (!append(result, asset->name, PLAYER_ASSET_MODEL, bytes, length, &total, error, size)) {
                goto fail;
            }
        }
    }
    size_t length = 0;
    unsigned char *values = shader_values_save(&length);
    if (!append(result, "parameters", PLAYER_ASSET_SHADER_VALUES, values, length, &total, error, size)) {
        goto fail;
    }
    unsigned char *parts = NULL;
    if (!render_scene3d_parts_save(&parts, &length, error, size) ||
        !append(result, "parts", PLAYER_ASSET_PARTS, parts, length, &total, error, size)) {
        goto fail;
    }
    for (int i = 0; i < render_image_source_count(); i++) {
        const char *key = render_image_source_key(i);
        unsigned char *bytes = NULL;
        shader_error diagnostic = { 0 };
        if (!render_image_prepared_write(key, &bytes, &length, &diagnostic)) {
            if (size) {
                snprintf(error, size, "%s: %s", key, diagnostic.message);
            }
            goto fail;
        }
        if (!append(result, key, PLAYER_ASSET_IMAGE, bytes, length, &total, error, size)) {
            goto fail;
        }
    }
    for (int i = 0; i < scene_count; i++) {
        for (int j = 0; j < scene_object_count(scenes[i]); j++) {
            for (int k = 0; k < scene_object_component_count(scenes[i], j); k++) {
                scene_component component = scene_object_component_at(scenes[i], j, k);
                if (component.desc->id == COMPONENT_Environment.id &&
                    !append_sky(result, ((const Environment *)component.data)->sky, &total, error, size)) {
                    goto fail;
                }
                const char
                    *source = authoring_component_mesh(component.desc, component.data);
                if (!source || contains(*result, source, PLAYER_ASSET_MESH)) {
                    continue;
                }
                const render_mesh_asset *mesh = render_mesh_asset_load(source, error, size);
                unsigned char *bytes = NULL;
                size_t length = 0;
                if (!mesh ||
                    !render_mesh_asset_prepared_write(mesh, &bytes, &length, error, size) ||
                    !append(result, source, PLAYER_ASSET_MESH, bytes, length, &total, error, size)) {
                    goto fail;
                }
            }
        }
    }
    return true;
fail:
    authoring_visual_prepared_free(*result);
    *result = NULL;
    return false;
}
