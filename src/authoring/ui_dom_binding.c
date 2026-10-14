#include "authoring/ui_dom_definition.h"
extern const native_capability_definition UI_DOM_DEFINITIONS[8];
#define BIND(N, C)                                                             \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &C,                                                        \
            .access = &UI_DOM_ACCESS                                           \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND("Query UI DOM", UI_DOM_QUERY_CALL),
    BIND("Inspect UI DOM", UI_DOM_INSPECT_CALL),
    BIND("Pick UI DOM", UI_DOM_PICK_CALL),
    BIND("Force UI DOM Pseudo", UI_DOM_PSEUDO_CALL),
    BIND("Query UI DOM Contexts", UI_DOM_CONTEXTS_CALL),
    BIND("Select UI DOM Context", UI_DOM_SELECT_CALL),
    BIND("Resolve UI DOM Unit", UI_DOM_UNIT_CALL),
    { .canonical_name = "Measure UI Text",
      .implementation = { .call = &UI_DOM_MEASURE_CALL, .access = &UI_DOM_MEASURE_ACCESS } }
};
static const type_desc *const TYPES[] = { &UI_DOM_ROW_LAYOUT,
                                          &UI_DOM_PROPERTY_LAYOUT,
                                          &UI_DOM_STATUS_LAYOUT,
                                          &UI_DOM_CONTEXT_LAYOUT };
const native_definition_pack UI_DOM_DEFINITION_PACK = { .definitions = UI_DOM_DEFINITIONS,
                                                        .definition_count = 8,
                                                        .bindings = BINDINGS,
                                                        .binding_count = 8,
                                                        .layouts = TYPES,
                                                        .layout_count = 4 };
