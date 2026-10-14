#ifndef CHIBI_WORKSPACE_ASSETS_DEFINITION_H
#define CHIBI_WORKSPACE_ASSETS_DEFINITION_H
#include "authoring/workspace_assets.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
typedef struct
{
    const char *kind, *folder, *extension, *description;
    bool executable, complete;
} workspace_asset_kind_row;
typedef struct
{
    const char *key, *name;
    int fields;
    bool marker;
} workspace_component_row;
extern const type_desc WORKSPACE_ASSET_KIND_LAYOUT;
extern const native_type_definition WORKSPACE_ASSETS_TYPE_DEFINITIONS[4];
extern const type_catalog_pack WORKSPACE_ASSETS_TYPE_PACK;
extern const native_definition_pack WORKSPACE_ASSETS_DEFINITION_PACK;
extern const node_host_call WORKSPACE_PROTOTYPE_SOURCE_CALL,
    WORKSPACE_PROTOTYPE_CANDIDATE_CALL;
extern const node_host_call WORKSPACE_ASSET_TEMPLATE_CALL, WORKSPACE_ASSET_KINDS_CALL;
extern const node_host_call WORKSPACE_ASSET_INSPECT_CALL,
    WORKSPACE_ASSET_EDIT_CALL, WORKSPACE_ASSET_PREPARE_CALL;
extern const node_host_call WORKSPACE_SCENE_CALLS[5];
extern const node_host_call WORKSPACE_SCENE_CLIPBOARD_CALL,
    WORKSPACE_SCENE_PASTE_CALL, WORKSPACE_SCENE_PLACE_CALL;
extern const node_host_call WORKSPACE_ASSET_IDENTITY_CALL;
extern const node_host_call WORKSPACE_OBJECT_INSPECT_CALL,
    WORKSPACE_OBJECTS_EDIT_CALL, WORKSPACE_OBJECTS_INSPECT_CALL,
    WORKSPACE_OBJECTS_COMPONENT_CALL, WORKSPACE_OBJECTS_CANDIDATE_CALL;
extern const node_host_call WORKSPACE_COMPONENT_CATALOG_CALL,
    WORKSPACE_ASSET_COMPONENT_CALL;
extern const node_access WORKSPACE_ASSETS_READ_ACCESS, WORKSPACE_ASSETS_WRITE_ACCESS;
#endif
