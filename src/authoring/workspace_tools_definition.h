#ifndef CHIBI_WORKSPACE_TOOLS_DEFINITION_H
#define CHIBI_WORKSPACE_TOOLS_DEFINITION_H
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_definition_pack WORKSPACE_TOOLS_DEFINITION_PACK,
    WORKSPACE_TOOLS_DECLARATION_PACK;
extern const native_type_definition WORKSPACE_TOOLS_TYPES[2];
extern const type_catalog_pack WORKSPACE_TOOLS_TYPE_PACK;
extern const node_host_call WORKSPACE_TOOLS_CALLS[4];
extern const node_access WORKSPACE_TOOLS_READ, WORKSPACE_TOOLS_WRITE;
#endif
