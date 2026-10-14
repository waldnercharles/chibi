#include "authoring/ui_source_preview_definition.h"
extern const native_capability_definition UI_SOURCE_PREVIEW_DEFINITIONS[3];
#define B(N, I)                                                                \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &UI_SOURCE_PREVIEW_CALLS[I],                               \
            .access = &UI_SOURCE_PREVIEW_ACCESS                                \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    B("Open UI Source Preview", 0),
    B("Draw UI Source Preview", 1),
    B("Close UI Source Preview", 2)
};
static const type_desc *const TYPES[] = { &UI_SOURCE_PREVIEW_RESULT_LAYOUT };
const native_definition_pack UI_SOURCE_PREVIEW_DEFINITION_PACK = {
    .definitions = UI_SOURCE_PREVIEW_DEFINITIONS,
    .definition_count = 3,
    .bindings = BINDINGS,
    .binding_count = 3,
    .layouts = TYPES,
    .layout_count = 1
};
