#ifndef CHIBI_UI_SOURCE_PREVIEW_DEFINITION_H
#define CHIBI_UI_SOURCE_PREVIEW_DEFINITION_H
#include "authoring/ui_source_preview.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_definition_pack UI_SOURCE_PREVIEW_DECLARATION_PACK,
    UI_SOURCE_PREVIEW_DEFINITION_PACK;
extern const native_type_definition UI_SOURCE_PREVIEW_TYPE_DEFINITIONS[1];
extern const type_catalog_pack UI_SOURCE_PREVIEW_TYPE_PACK;
extern const type_desc UI_SOURCE_PREVIEW_RESULT_LAYOUT;
extern const node_host_call UI_SOURCE_PREVIEW_CALLS[3];
extern const node_access UI_SOURCE_PREVIEW_ACCESS;
#endif
