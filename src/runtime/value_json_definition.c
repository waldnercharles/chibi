#include "runtime/value_json_definition.h"

#include "runtime/value_json_native.h"

static const native_parameter_definition INPUTS[] = {
    { .name = "Value",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_FLOW,
      .container = PIN_CONTAINER_SINGLE,
      .type_flags = SCRIPT_TYPE_UNTYPED }
};
static const native_parameter_definition OUTPUTS[] = {
    { .name = "Text",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_SINGLE },
    { .name = "Ok",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_BOOL,
      .type_name = "bool",
      .container = PIN_CONTAINER_SINGLE },
    { .name = "Error",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_SINGLE }
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Value to JSON",
      .source_name = "value_to_json",
      .category = "Values/JSON",
      .description = "Encode a typed value as bounded UTF-8 JSON. Refusals "
                     "return Ok=false and Error; no tasks, maps or nonfinite "
                     "numbers.",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Value to JSON",
      .implementation = { .call = &VALUE_JSON_CALL, .access = &VALUE_JSON_ACCESS } }
};
const native_definition_pack VALUE_JSON_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
