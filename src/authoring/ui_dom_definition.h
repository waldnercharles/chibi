#ifndef CHIBI_UI_DOM_DEFINITION_H
#define CHIBI_UI_DOM_DEFINITION_H
#include "authoring/ui_dom.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_definition_pack UI_DOM_DECLARATION_PACK, UI_DOM_DEFINITION_PACK;
extern const native_type_definition UI_DOM_TYPE_DEFINITIONS[4];
extern const type_catalog_pack UI_DOM_TYPE_PACK;
extern const type_desc UI_DOM_ROW_LAYOUT, UI_DOM_PROPERTY_LAYOUT, UI_DOM_STATUS_LAYOUT;
extern const type_desc UI_DOM_CONTEXT_LAYOUT;
extern const node_host_call UI_DOM_QUERY_CALL, UI_DOM_INSPECT_CALL,
    UI_DOM_PICK_CALL, UI_DOM_PSEUDO_CALL, UI_DOM_CONTEXTS_CALL,
    UI_DOM_SELECT_CALL, UI_DOM_UNIT_CALL, UI_DOM_MEASURE_CALL;
extern const node_access UI_DOM_ACCESS, UI_DOM_MEASURE_ACCESS;
#endif
