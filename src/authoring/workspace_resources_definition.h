#ifndef CHIBI_WORKSPACE_RESOURCES_DEFINITION_H
#define CHIBI_WORKSPACE_RESOURCES_DEFINITION_H
#include "authoring/workspace_resources.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_definition_pack WORKSPACE_RESOURCE_DEFINITION_PACK;
extern const native_type_definition WORKSPACE_RESOURCE_TYPE_DEFINITIONS[2];
extern const type_catalog_pack WORKSPACE_RESOURCE_TYPE_PACK;
extern const type_desc WORKSPACE_RESOURCE_ROW_LAYOUT, WORKSPACE_RESOURCE_RESULT_LAYOUT;
extern const node_host_call WORKSPACE_RESOURCE_QUERY_CALL,
    WORKSPACE_RESOURCE_REFRESH_CALL, WORKSPACE_RESOURCE_LOOKUP_CALL;
extern const node_access WORKSPACE_RESOURCE_READ_ACCESS,
    WORKSPACE_RESOURCE_REFRESH_ACCESS;
#endif
