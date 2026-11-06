#include "runtime/terrain_storage_definition.h"

#include "runtime/terrain_storage.h"

static const native_parameter_definition REQUEST_IN[] = {
    TERRAIN_STORAGE_REQUEST_INPUTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition REQUEST_OUT[] = {
    TERRAIN_STORAGE_REQUEST_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition RESULT_IN[] = {
    TERRAIN_STORAGE_RESULT_INPUTS(NATIVE_PORT)
};
static const native_parameter_definition RESULT_OUT[] = {
    TERRAIN_STORAGE_RESULT_OUTPUTS(NATIVE_PORT)
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = TERRAIN_SAVE_NAME,
      .source_name = "save_terrain",
      TERRAIN_SAVE_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = REQUEST_IN,
      .input_count = NATIVE_COUNT(REQUEST_IN),
      .outputs = REQUEST_OUT,
      .output_count = NATIVE_COUNT(REQUEST_OUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = TERRAIN_LOAD_NAME,
      .source_name = "load_terrain",
      TERRAIN_LOAD_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = REQUEST_IN,
      .input_count = NATIVE_COUNT(REQUEST_IN),
      .outputs = REQUEST_OUT,
      .output_count = NATIVE_COUNT(REQUEST_OUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = TERRAIN_STORAGE_RESULT_NAME,
      .source_name = "terrain_storage_result",
      TERRAIN_STORAGE_RESULT_PRESENTATION,
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = RESULT_IN,
      .input_count = NATIVE_COUNT(RESULT_IN),
      .outputs = RESULT_OUT,
      .output_count = NATIVE_COUNT(RESULT_OUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = TERRAIN_SAVE_NAME,
      .implementation = { .call = &TERRAIN_SAVE_CALL,
                          .access = &TERRAIN_STORAGE_WRITE_ACCESS } },
    { .canonical_name = TERRAIN_LOAD_NAME,
      .implementation = { .call = &TERRAIN_LOAD_CALL,
                          .access = &TERRAIN_STORAGE_WRITE_ACCESS } },
    { .canonical_name = TERRAIN_STORAGE_RESULT_NAME,
      .implementation = { .call = &TERRAIN_STORAGE_RESULT_CALL,
                          .access = &TERRAIN_STORAGE_READ_ACCESS } },
};
const native_definition_pack TERRAIN_STORAGE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
