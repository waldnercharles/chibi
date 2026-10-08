#include "runtime/save_definition.h"

#include "runtime/save_native.h"

#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                            \
      .kind = KIND,                                                                            \
      .storage = STORAGE,                                                                      \
      .type_name = TYPE_NAME,                                                                  \
      .container = CONTAINER,                                                                  \
      .type_flags = FLAGS,                                                                     \
      .generic_group = GROUP,                                                                  \
      .reference_kind = REFERENCE,                                                             \
      .has_default = true,                                                                     \
      .default_owner = OWNER,                                                                  \
      .default_value = VALUE },
static const native_parameter_definition READ_IN[] = { GAME_SAVE_READ_INPUTS(PORT) };
static const native_parameter_definition READ_OUT[] = { GAME_SAVE_READ_OUTPUTS(PORT) };
static const native_parameter_definition SET_IN[] = { GAME_SAVE_SET_INPUTS(PORT) };
static const native_parameter_definition REMOVE_IN[] = { GAME_SAVE_REMOVE_INPUTS(PORT) };
static const native_parameter_definition EXEC[] = { GAME_SAVE_EXEC(PORT) };
static const native_parameter_definition THEN[] = { GAME_SAVE_THEN(PORT) };
static const native_parameter_definition COMMIT_OUT[] = { GAME_SAVE_COMMIT_OUTPUTS(PORT) };
#undef PORT
#define COUNT(ARRAY) ((int)(sizeof ARRAY / sizeof *ARRAY))
#define ACTION(KEY, NAME, SOURCE, INPUTS, OUTPUTS)                             \
    { .canonical_name = NAME,                                                  \
      KEY##_PRESENTATION,                                                      \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUTS,                                                        \
      .input_count = COUNT(INPUTS),                                            \
      .outputs = OUTPUTS,                                                      \
      .output_count = COUNT(OUTPUTS),                                          \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = GAME_SAVE_READ_NAME,
      GAME_SAVE_READ_PRESENTATION,
      .source_name = "read_saved_text",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = READ_IN,
      .input_count = COUNT(READ_IN),
      .outputs = READ_OUT,
      .output_count = COUNT(READ_OUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ },
    ACTION(GAME_SAVE_SET, GAME_SAVE_SET_NAME, "set_saved_text", SET_IN, THEN),
    ACTION(GAME_SAVE_REMOVE, GAME_SAVE_REMOVE_NAME, "remove_saved_text", REMOVE_IN, THEN),
    ACTION(GAME_SAVE_COMMIT, GAME_SAVE_COMMIT_NAME, "commit_save", EXEC, COMMIT_OUT),
    ACTION(GAME_SAVE_DISCARD, GAME_SAVE_DISCARD_NAME, "discard_save_changes", EXEC, THEN),
};
#define BIND(NAME, CALL, ACCESS)                                               \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND(GAME_SAVE_READ_NAME, GAME_SAVE_READ_CALL, GAME_SAVE_READ_ACCESS),
    BIND(GAME_SAVE_SET_NAME, GAME_SAVE_SET_CALL, GAME_SAVE_WRITE_ACCESS),
    BIND(GAME_SAVE_REMOVE_NAME, GAME_SAVE_REMOVE_CALL, GAME_SAVE_WRITE_ACCESS),
    BIND(GAME_SAVE_COMMIT_NAME, GAME_SAVE_COMMIT_CALL, GAME_SAVE_WRITE_ACCESS),
    BIND(GAME_SAVE_DISCARD_NAME, GAME_SAVE_DISCARD_CALL, GAME_SAVE_WRITE_ACCESS),
};
const native_definition_pack GAME_SAVE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
