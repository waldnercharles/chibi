#ifndef CHIBI_UI_AUTHORING_DEFINITION_H
#define CHIBI_UI_AUTHORING_DEFINITION_H
#include "authoring/ui_authoring.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_definition_pack UI_AUTHORING_DEFINITION_PACK;
extern const native_type_definition UI_AUTHORING_TYPE_DEFINITIONS[2];
extern const type_catalog_pack UI_AUTHORING_TYPE_PACK;
extern const type_desc UI_AUTHORING_ROW_LAYOUT, UI_AUTHORING_STATUS_LAYOUT;
extern const node_host_call UI_AUTHORING_QUERY_CALL, UI_AUTHORING_EDIT_CALL;
extern const node_access UI_AUTHORING_ACCESS;
#endif
