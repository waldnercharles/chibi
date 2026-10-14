#ifndef CHIBI_AUTHORING_OBSERVATIONS_DEFINITION_H
#define CHIBI_AUTHORING_OBSERVATIONS_DEFINITION_H
#include "authoring/observations.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
extern const native_definition_pack AUTHORING_OBSERVATIONS_DEFINITION_PACK;
extern const native_type_definition AUTHORING_OBSERVATIONS_TYPES[2];
extern const type_desc AUTHORING_LOG_ROW_LAYOUT, AUTHORING_TIMING_LAYOUT;
extern const type_catalog_pack AUTHORING_OBSERVATIONS_TYPE_PACK;
#endif
