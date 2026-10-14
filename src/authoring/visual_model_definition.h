#ifndef CHIBI_VISUAL_MODEL_DEFINITION_H
#define CHIBI_VISUAL_MODEL_DEFINITION_H
#include "authoring/visual_model.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
typedef struct
{
    const char *name, *asset, *scope;
    uint64_t node;
    int slot, width;
    bool local;
    float x, y, z, w, min_x, min_y, min_z, min_w, max_x, max_y, max_z, max_w;
} visual_model_parameter;
typedef struct
{
    int distance_nodes, hit_nodes, public_slots, private_slots, materials,
        images, ramp_vec4s;
    float min_x, min_y, min_z, max_x, max_y, max_z;
    bool empty;
} visual_model_info;
typedef struct
{
    int index, material, distance_nodes;
    uint64_t node;
    const char *name, *asset, *path;
} visual_model_part;
typedef struct
{
    int index;
    uint64_t node;
    const char *name, *asset, *scope;
    float x, y, z, pitch, yaw, roll;
} visual_model_anchor;
typedef struct visual_model_service visual_model_service;
/* Optional callback transfers a malloc-owned array and all of its owned source
 * snapshots. Called synchronously; the service frees them after inspection. */
typedef behavior_asset **(*visual_model_catalog_reader)(void *, int *count);
visual_model_service *visual_models_new(workspace_documents *, const char *workspace_root);
void visual_models_free(visual_model_service *);
typedef struct
{
    int bake, document, revision, triangles;
    size_t bytes;
    uint64_t samples, work;
    float cell_diagonal, max_residual;
} visual_model_bake_view;
bool visual_models_bake(
    visual_model_service *,
    int document,
    int revision,
    const model_bake_options *,
    visual_model_bake_view *,
    char *error,
    size_t
);
bool visual_model_bake_read(const visual_model_service *, int bake, visual_model_bake_view *);
bool visual_model_bake_export(visual_model_service *, int bake, const char *path, char *error, size_t);
bool visual_model_bake_export_bundle(
    visual_model_service *,
    int bake,
    const char *directory,
    char *error,
    size_t
);
bool visual_model_bake_close(visual_model_service *, int bake);
/* Optional external resource scope keeps this CPU module independent of GPU
 * libraries. Enter returns an owned host token; leave receives it once. */
void visual_models_set_scope(
    visual_model_service *,
    void *(*enter)(void *, const char **error),
    void (*leave)(void *, void *token),
    void *user
);
void visual_models_set_images(visual_model_service *, shader_image_resolver, void *);
void visual_models_set_catalog(visual_model_service *, visual_model_catalog_reader, void *);
extern const native_definition_pack VISUAL_MODEL_DEFINITION_PACK;
extern const native_type_definition VISUAL_MODEL_TYPES[5];
extern const type_desc VISUAL_MODEL_PARAMETER_LAYOUT, VISUAL_MODEL_INFO_LAYOUT,
    VISUAL_MODEL_PART_LAYOUT, VISUAL_MODEL_ANCHOR_LAYOUT,
    VISUAL_MODEL_REFERENCE_LAYOUT;
#endif
