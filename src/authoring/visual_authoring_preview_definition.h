#ifndef CHIBI_VISUAL_PREVIEW_DEFINITION_H
#define CHIBI_VISUAL_PREVIEW_DEFINITION_H
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
struct visual_previews;
struct workspace_resources;
typedef struct
{
    struct visual_previews *previews;
    struct workspace_resources *resources;
    const char *engine_version, *build_id, *manifest_hash;
} visual_preview_service;
enum
{
    VISUAL_PREVIEW_OPERATION_COUNT = 12
};
typedef struct
{
    int document, revision;
    bool valid;
    const char *error;
} visual_publication_row;
extern const type_desc VISUAL_PUBLICATION_LAYOUT;
extern const native_type_definition VISUAL_PREVIEW_TYPES[1];
extern const native_capability_definition VISUAL_PREVIEW_DEFINITIONS[VISUAL_PREVIEW_OPERATION_COUNT];
extern const native_definition_pack VISUAL_PREVIEW_DECLARATION_PACK;
extern const native_definition_pack VISUAL_PREVIEW_DEFINITION_PACK;
#endif
