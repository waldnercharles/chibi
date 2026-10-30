#include "runtime/save_definition.h"

#include "runtime/save_native.h"

static const native_parameter_definition READ_IN[] = { SAVE_KEY(
    NATIVE_CSTRING_PORT
) SAVE_PORT(NATIVE_PORT, "Default", NATIVE_PARAMETER_DATA, PIN_STR, NULL) };
static const native_parameter_definition READ_OUT[] = { SAVE_READ_OUTPUTS(NATIVE_PORT) };
static const native_parameter_definition SET_IN[] = { SAVE_SET_INPUTS(NATIVE_CSTRING_PORT) };
static const native_parameter_definition READ_NUMBER_IN[] = {
    SAVE_READ_NUMBER_INPUTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition READ_NUMBER_OUT[] = {
    SAVE_READ_NUMBER_OUTPUTS(NATIVE_PORT)
};
static const native_parameter_definition SET_NUMBER_IN[] = {
    SAVE_SET_NUMBER_INPUTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition REMOVE_IN[] = {
    SAVE_REMOVE_INPUTS(NATIVE_CSTRING_PORT)
};
static const native_parameter_definition EXEC[] = { SAVE_EXEC(NATIVE_PORT) };
static const native_parameter_definition THEN[] = { SAVE_THEN(NATIVE_PORT) };
static const native_parameter_definition COMMIT_OUT[] = { SAVE_COMMIT_OUTPUTS(NATIVE_PORT) };
#define ACTION(KEY, NAME, SOURCE, INPUTS, OUTPUTS)                             \
    { .canonical_name = NAME,                                                  \
      KEY##_PRESENTATION,                                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUTS,                                                        \
      .input_count = NATIVE_COUNT(INPUTS),                                     \
      .outputs = OUTPUTS,                                                      \
      .output_count = NATIVE_COUNT(OUTPUTS),                                   \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = SAVE_READ_NAME,
      SAVE_READ_PRESENTATION,
      .source_name = "read_saved_text",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = READ_IN,
      .input_count = NATIVE_COUNT(READ_IN),
      .outputs = READ_OUT,
      .output_count = NATIVE_COUNT(READ_OUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ },
    ACTION(SAVE_SET, SAVE_SET_NAME, "set_saved_text", SET_IN, THEN),
    { .canonical_name = SAVE_READ_NUMBER_NAME,
      SAVE_READ_NUMBER_PRESENTATION,
      .source_name = "read_saved_number",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = READ_NUMBER_IN,
      .input_count = NATIVE_COUNT(READ_NUMBER_IN),
      .outputs = READ_NUMBER_OUT,
      .output_count = NATIVE_COUNT(READ_NUMBER_OUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ },
    ACTION(SAVE_SET_NUMBER, SAVE_SET_NUMBER_NAME, "set_saved_number", SET_NUMBER_IN, THEN),
    ACTION(SAVE_REMOVE, SAVE_REMOVE_NAME, "remove_saved_text", REMOVE_IN, THEN),
    ACTION(SAVE_COMMIT, SAVE_COMMIT_NAME, "commit_save", EXEC, COMMIT_OUT),
    ACTION(SAVE_DISCARD, SAVE_DISCARD_NAME, "discard_save_changes", EXEC, THEN),
};
#define BIND(NAME, CALL, ACCESS)                                               \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND(SAVE_READ_NAME, SAVE_READ_CALL, SAVE_READ_ACCESS),
    BIND(SAVE_SET_NAME, SAVE_SET_CALL, SAVE_WRITE_ACCESS),
    BIND(SAVE_READ_NUMBER_NAME, SAVE_READ_NUMBER_CALL, SAVE_READ_ACCESS),
    BIND(SAVE_SET_NUMBER_NAME, SAVE_SET_NUMBER_CALL, SAVE_WRITE_ACCESS),
    BIND(SAVE_REMOVE_NAME, SAVE_REMOVE_CALL, SAVE_WRITE_ACCESS),
    BIND(SAVE_COMMIT_NAME, SAVE_COMMIT_CALL, SAVE_WRITE_ACCESS),
    BIND(SAVE_DISCARD_NAME, SAVE_DISCARD_CALL, SAVE_WRITE_ACCESS),
};
const native_definition_pack SAVE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
