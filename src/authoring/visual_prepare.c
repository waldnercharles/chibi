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
typedef struct
{
    player_asset *assets;
    size_t total;
    authoring_visual_budget *budget;
    const image_import_proposal *proposals;
    int proposal_count;
    bool seen[64];
} collection;

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
static uint64_t hash_number(uint64_t hash, uint64_t value)
{
    unsigned char bytes[8];
    for (int i = 0; i < 8; i++) {
        bytes[i] = (unsigned char)(value >> (8 * i));
    }
    return hash_bytes(hash, bytes, sizeof bytes);
}
static bool charge(
    authoring_visual_budget *b,
    bool proposed,
    const char *name,
    player_asset_kind kind,
    size_t length,
    uint64_t payload_hash,
    size_t pixels
)
{
    authoring_visual_cost *cost = proposed ? b->proposed : b->current;
    size_t *bytes = proposed ? &b->proposed_bytes : &b->current_bytes;
    int *count = proposed ? &b->proposed_count : &b->current_count;
    uint64_t *hash = proposed ? &b->proposed_hash : &b->current_hash;
    if (length > SIZE_MAX - *bytes) {
        return false;
    }
    cost[kind].count++;
    cost[kind].bytes += length;
    *bytes += length;
    (*count)++;
    *hash = hash_number(*hash, (uint64_t)kind);
    *hash = hash_number(*hash, strlen(name));
    *hash = hash_bytes(*hash, name, strlen(name));
    *hash = hash_number(*hash, length);
    *hash = hash_number(*hash, payload_hash);
    if (proposed) {
        b->proposed_pixel_bytes += pixels;
    } else {
        b->current_pixel_bytes += pixels;
    }
    return true;
}
static bool append(
    collection *c,
    const char *name,
    player_asset_kind kind,
    unsigned char *data,
    size_t length,
    char *error,
    size_t size
)
{
    if (!data || !length || length > SIZE_MAX - c->total ||
        acount(c->assets) == PLAYER_MAX_ASSETS ||
        (!c->budget && length > PACKAGE_MAX_BYTES - c->total)) {
        free(data);
        if (size) {
            snprintf(
                error,
                size,
                "Prepared visual collection exceeds count or byte limits at %s "
                "(used %zu, payload %zu, limit %u)",
                name,
                c->total,
                length,
                (unsigned)PACKAGE_MAX_BYTES
            );
        }
        return false;
    }
    uint64_t hash = player_asset_fingerprint(data, length);
    c->total += length;
    if (c->budget) {
        size_t pixels = 0;
        if (kind == PLAYER_ASSET_IMAGE) {
            int width, height;
            if (render_image_info(name, &width, &height)) {
                pixels = (size_t)width * height * 4;
            }
        }
        bool charged = charge(c->budget, false, name, kind, length, hash, pixels);
        assert(charged);
        const image_import_plan *replacement = NULL;
        for (int i = 0; kind == PLAYER_ASSET_IMAGE && i < c->proposal_count; i++) {
            if (!strcmp(name, c->proposals[i].key)) {
                replacement = c->proposals[i].plan;
                c->seen[i] = true;
                break;
            }
        }
        charged = charge(
            c->budget,
            true,
            name,
            kind,
            replacement ? replacement->prepared_bytes : length,
            replacement ? replacement->prepared_hash : hash,
            replacement ? replacement->pixel_bytes : pixels
        );
        free(data);
        data = NULL;
        if (!charged) {
            if (size) {
                snprintf(error, size, "Proposed visual byte total overflows size_t");
            }
            return false;
        }
    }
    apush(
        c->assets,
        ((player_asset){ .name = xstrdup(name),
                         .kind = kind,
                         .data = data,
                         .length = length,
                         .content_hash = hash })
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
static bool append_sky(collection *c, const char *key, char *error, size_t size)
{
    if (!key || !key[0] || contains(c->assets, key, PLAYER_ASSET_SKY)) {
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
           append(c, key, PLAYER_ASSET_SKY, bytes, length, error, size);
}
static bool collect(const scene *const *scenes, int scene_count, collection *c, char *error, size_t size)
{
    assert(c && scene_count >= 0 && (!scene_count || scenes) && (!size || error));
    if (scene_count > PLAYER_MAX_ASSETS) {
        if (size) {
            snprintf(error, size, "Prepared visual scene count exceeds limit");
        }
        return false;
    }
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
        if (!append(c, asset->name, PLAYER_ASSET_SHADER, bytes, length, error, size)) {
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
            if (!append(c, asset->name, PLAYER_ASSET_MODEL, bytes, length, error, size)) {
                goto fail;
            }
        }
    }
    size_t length = 0;
    unsigned char *values = shader_values_save(&length);
    if (!append(c, "parameters", PLAYER_ASSET_SHADER_VALUES, values, length, error, size)) {
        goto fail;
    }
    unsigned char *parts = NULL;
    if (!render_scene3d_parts_save(&parts, &length, error, size) ||
        !append(c, "parts", PLAYER_ASSET_PARTS, parts, length, error, size)) {
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
        if (!append(c, key, PLAYER_ASSET_IMAGE, bytes, length, error, size)) {
            goto fail;
        }
    }
    for (int i = 0; i < scene_count; i++) {
        for (int j = 0; j < scene_object_count(scenes[i]); j++) {
            for (int k = 0; k < scene_object_component_count(scenes[i], j); k++) {
                scene_component component = scene_object_component_at(scenes[i], j, k);
                if (component.desc->id == COMPONENT_Environment.id &&
                    !append_sky(c, ((const Environment *)component.data)->sky, error, size)) {
                    goto fail;
                }
                const char
                    *source = authoring_component_mesh(component.desc, component.data);
                if (!source || contains(c->assets, source, PLAYER_ASSET_MESH)) {
                    continue;
                }
                const render_mesh_asset *mesh = render_mesh_asset_load(source, error, size);
                unsigned char *bytes = NULL;
                size_t length = 0;
                if (!mesh ||
                    !render_mesh_asset_prepared_write(mesh, &bytes, &length, error, size) ||
                    !append(c, source, PLAYER_ASSET_MESH, bytes, length, error, size)) {
                    goto fail;
                }
            }
        }
    }
    return true;
fail:
    authoring_visual_prepared_free(c->assets);
    c->assets = NULL;
    return false;
}
bool authoring_visual_prepare(
    const scene *const *scenes,
    int scene_count,
    player_asset **result,
    char *error,
    size_t size
)
{
    assert(result);
    *result = NULL;
    collection c = { 0 };
    if (!collect(scenes, scene_count, &c, error, size)) {
        return false;
    }
    *result = c.assets;
    return true;
}
static bool proposal_valid(const image_import_proposal *p, char *error, size_t size)
{
    const char *key = p->key;
    size_t length = key ? strnlen(key, 1025) : 0;
    if (length < 13 || length > 1024 || strncmp(key, "content/", 8) ||
        strstr(key, "..") || !player_relative_path(key + 8) ||
        strcmp(key + length - 4, ".png")) {
        if (size) {
            snprintf(error, size, "Image proposal requires a content/... .png key without traversal");
        }
        return false;
    }
    const image_import_plan *plan = p->plan;
    size_t expected = 0;
    shader_error diagnostic = { 0 };
    if (!plan || !plan->png || !plan->png_bytes ||
        !render_image_prepared_size(plan->width, plan->height, &expected, &diagnostic) ||
        plan->prepared_bytes != expected ||
        plan->pixel_bytes != (size_t)plan->width * plan->height * 4) {
        if (size) {
            snprintf(error, size, "Image proposal %s requires a valid prepared image plan", key);
        }
        return false;
    }
    return true;
}
bool authoring_visual_measure(
    const scene *const *scenes,
    int scene_count,
    const image_import_proposal *proposals,
    int proposal_count,
    authoring_visual_budget *result,
    char *error,
    size_t size
)
{
    if (!result || scene_count < 0 || (scene_count && !scenes) || proposal_count < 0 ||
        proposal_count > 64 || (proposal_count && !proposals)) {
        if (size) {
            snprintf(error, size, "Visual measurement requires bounded scenes and 0..64 image proposals");
        }
        return false;
    }
    for (int i = 0; i < proposal_count; i++) {
        if (!proposal_valid(&proposals[i], error, size)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(proposals[i].key, proposals[j].key)) {
                if (size) {
                    snprintf(
                        error,
                        size,
                        "Image proposal key is duplicated: %s",
                        proposals[i].key
                    );
                }
                return false;
            }
        }
    }
    authoring_visual_budget budget = { .limit_bytes = PACKAGE_MAX_BYTES,
                                       .limit_count = PLAYER_MAX_ASSETS,
                                       .current_hash = UINT64_C(14695981039346656037),
                                       .proposed_hash = UINT64_C(14695981039346656037) };
    collection c = { .budget = &budget,
                     .proposals = proposals,
                     .proposal_count = proposal_count };
    if (!collect(scenes, scene_count, &c, error, size)) {
        return false;
    }
    for (int i = 0; i < proposal_count; i++) {
        if (!c.seen[i]) {
            const image_import_plan *p = proposals[i].plan;
            if (!charge(
                    &budget,
                    true,
                    proposals[i].key,
                    PLAYER_ASSET_IMAGE,
                    p->prepared_bytes,
                    p->prepared_hash,
                    p->pixel_bytes
                )) {
                authoring_visual_prepared_free(c.assets);
                if (size) {
                    snprintf(error, size, "Proposed visual byte total overflows size_t");
                }
                return false;
            }
        }
    }
    authoring_visual_prepared_free(c.assets);
    budget.fits = budget.proposed_bytes <= budget.limit_bytes &&
                  budget.proposed_count <= budget.limit_count;
    *result = budget;
    return true;
}
