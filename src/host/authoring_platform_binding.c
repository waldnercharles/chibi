#include "host/authoring_platform_definition.h"
extern const native_capability_definition AUTHORING_PLATFORM_DEFINITIONS[6];
#define COUNT(A) ((int)(sizeof A / sizeof *A))
#define BIND(NAME, CALL)                                                       \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &CALL,                                                     \
            .access = &AUTHORING_PLATFORM_ACCESS                               \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND("Request Authoring Platform", AUTHORING_PLATFORM_REQUEST_CALL),
    BIND("Poll Authoring Platform", AUTHORING_PLATFORM_POLL_CALL),
    BIND("Cancel Authoring Platform", AUTHORING_PLATFORM_CANCEL_CALL),
    BIND("Release Authoring Platform", AUTHORING_PLATFORM_RELEASE_CALL),
    BIND("Authoring Platform Environment", AUTHORING_PLATFORM_ENVIRONMENT_CALL),
    BIND("Query Physical Controllers", AUTHORING_PHYSICAL_CONTROLLERS_CALL),
};
static const type_desc *const TYPES[] = { &AUTHORING_PLATFORM_RESULT_LAYOUT,
                                          &AUTHORING_PLATFORM_ENVIRONMENT_LAYOUT,
                                          &AUTHORING_PHYSICAL_CONTROLLER_LAYOUT };
const native_definition_pack AUTHORING_PLATFORM_DEFINITION_PACK = {
    .definitions = AUTHORING_PLATFORM_DEFINITIONS,
    .definition_count = 6,
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
    .layouts = TYPES,
    .layout_count = COUNT(TYPES)
};
