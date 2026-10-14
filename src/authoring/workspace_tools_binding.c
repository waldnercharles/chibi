#include "authoring/workspace_tools_definition.h"
extern const native_capability_definition WORKSPACE_TOOLS_DEFINITIONS[4];
extern const type_desc *const WORKSPACE_TOOLS_LAYOUTS[2];
#define C(N, I, ACCESS)                                                        \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &WORKSPACE_TOOLS_CALLS[I],                                 \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    C("Query Runtime Saved Data", 0, WORKSPACE_TOOLS_READ),
    C("Edit Runtime Saved Data", 1, WORKSPACE_TOOLS_WRITE),
    C("Query Workspace CSS", 2, WORKSPACE_TOOLS_READ),
    C("Edit Workspace CSS", 3, WORKSPACE_TOOLS_WRITE)
};
const native_definition_pack WORKSPACE_TOOLS_DEFINITION_PACK = {
    .definitions = WORKSPACE_TOOLS_DEFINITIONS,
    .definition_count = 4,
    .bindings = BINDINGS,
    .binding_count = 4,
    .layouts = WORKSPACE_TOOLS_LAYOUTS,
    .layout_count = 2
};
