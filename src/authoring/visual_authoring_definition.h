#ifndef CHIBI_VISUAL_AUTHORING_DEFINITION_H
#define CHIBI_VISUAL_AUTHORING_DEFINITION_H
#include "authoring/visual_authoring.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"

typedef struct
{
    const char *name;
    int slot, width, presentation;
    double x, y, z, w;
    const char *kind;
    bool editable, bounded;
    double minimum, maximum;
} visual_parameter;
typedef struct
{
    int nodes, uniforms, images, ramp_vec4s, texture_samples, detail_expressions;
} visual_costs;
typedef struct
{
    int slot;
    float x, y, z, w;
} visual_sample;
typedef struct
{
    workspace_documents *documents;
    /* Canonical host-owned roots; required only by explicit material creation. */
    const char *content_root, *workspace_root;
    /* Optional CPU linker returns an owned snapshot or NULL. Called only
     * synchronously; source remains borrowed for this call. */
    visual_source_linker link_source;
    void *link_userdata;
    shader_image_resolver resolve_image;
    void *image_userdata;
} visual_authoring_service;
extern const native_definition_pack VISUAL_AUTHORING_DEFINITION_PACK;
extern const native_type_definition VISUAL_AUTHORING_TYPES[4];
extern const type_desc VISUAL_MATERIAL_SETTINGS_LAYOUT;
extern const type_desc VISUAL_PARAMETER_LAYOUT, VISUAL_COSTS_LAYOUT,
    VISUAL_SAMPLE_LAYOUT;
#endif
