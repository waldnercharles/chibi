#ifndef CHIBI_PROJECT_SEMANTICS_DEFINITION_H
#define CHIBI_PROJECT_SEMANTICS_DEFINITION_H
#include "authoring/project_semantics.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_type_definition PROJECT_SEMANTICS_TYPE_DEFINITIONS[3];
extern const type_catalog_pack PROJECT_SEMANTICS_TYPE_PACK;
extern const native_definition_pack PROJECT_SEMANTICS_DEFINITION_PACK;
extern const node_host_call PROJECT_SEMANTICS_CALLS[4];
extern const node_access PROJECT_SEMANTICS_READ_ACCESS, PROJECT_SEMANTICS_WRITE_ACCESS;
#endif
