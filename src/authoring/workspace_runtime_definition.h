#ifndef CHIBI_WORKSPACE_RUNTIME_DEFINITION_H
#define CHIBI_WORKSPACE_RUNTIME_DEFINITION_H
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
enum
{
    WORKSPACE_RUNTIME_OPERATION_COUNT = 33
};
extern const native_type_definition RUNTIME_PROPERTY_TYPE_DEFINITIONS[2];
extern const native_capability_definition WORKSPACE_RUNTIME_DEFINITIONS[WORKSPACE_RUNTIME_OPERATION_COUNT];
extern const native_definition_pack WORKSPACE_RUNTIME_DECLARATION_PACK;
extern const native_definition_pack WORKSPACE_RUNTIME_DEFINITION_PACK;
#endif
