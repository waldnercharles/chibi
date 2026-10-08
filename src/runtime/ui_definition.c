#include "runtime/ui_definition.h"

#include "runtime/ui_native.h"

#define PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                            \
      .kind = KIND,                                                                            \
      .storage = STORAGE,                                                                      \
      .type_name = TYPE_NAME,                                                                  \
      .container = CONTAINER,                                                                  \
      .type_flags = FLAGS,                                                                     \
      .generic_group = GROUP,                                                                  \
      .reference_kind = REFERENCE,                                                             \
      .default_owner = OWNER,                                                                  \
      .has_default = true,                                                                     \
      .default_value = VALUE },
#define ARRAYS(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                      \
    static const native_parameter_definition KEY##_inputs[] = { INPUTS(PORT) }; \
    static const native_parameter_definition KEY##_outputs[] = { OUTPUTS(PORT) };
GAME_UI_OPERATIONS(ARRAYS)
#undef ARRAYS
#undef PORT
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define DEFINITION(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                 \
    { .canonical_name = NAME,                                                  \
      GAME_UI_##KEY##_PRESENTATION,                                            \
      .source_name = SOURCE,                                                   \
      .kind = ACTION ? SCRIPT_CAPABILITY_ACTION : SCRIPT_CAPABILITY_FUNCTION,  \
      .inputs = KEY##_inputs,                                                  \
      .input_count = COUNT(KEY##_inputs),                                      \
      .outputs = KEY##_outputs,                                                \
      .output_count = COUNT(KEY##_outputs),                                    \
      .completion_output = ACTION ? 0 : -1,                                    \
      .pure = !ACTION },
static const native_capability_definition DEFINITIONS[] = { GAME_UI_OPERATIONS(DEFINITION) };
#undef DEFINITION
#define BINDING(KEY, NAME, SOURCE, INPUTS, OUTPUTS, ACTION)                    \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &GAME_UI_##KEY##_CALL,                       \
                          .access = ACTION ? &GAME_UI_WRITE_ACCESS             \
                                           : &GAME_UI_READ_ACCESS } },
static const native_definition_binding BINDINGS[] = { GAME_UI_OPERATIONS(BINDING) };
#undef BINDING
const native_definition_pack GAME_UI_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = COUNT(BINDINGS),
};
#undef COUNT
