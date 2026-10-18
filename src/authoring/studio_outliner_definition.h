#ifndef CHIBI_STUDIO_OUTLINER_DEFINITION_H
#define CHIBI_STUDIO_OUTLINER_DEFINITION_H

#include "authoring/studio_outliner_schema.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"

typedef struct execution_module execution_module;

#define STUDIO_OUTLINER_CAPTURE_NAME "Capture Outliner"
#define STUDIO_OUTLINER_PUBLISH_NAME "Publish Outliner"

extern const native_definition_pack STUDIO_OUTLINER_DEFINITION_PACK;
extern const native_type_definition STUDIO_OUTLINER_TYPE_DEFINITIONS[3];
extern const node_host_call STUDIO_OUTLINER_CAPTURE_CALL, STUDIO_OUTLINER_PUBLISH_CALL;
extern const node_access STUDIO_OUTLINER_CAPTURE_ACCESS, STUDIO_OUTLINER_PUBLISH_ACCESS;

/* Validates the complete finalized closure. Never issues runtime authority. */
bool studio_outliner_module_valid(const execution_module *, char *error, size_t size);

#endif
