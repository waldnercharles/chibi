#include "toolchain/compiler_json.h"

#include "execution/function.h"

/* Retired. The compiler decodes string literals with tools/compiler/json_string
 * and no source calls these natives any more. Their declarations stay because
 * the native list is part of every prepared artifact's ABI, so dropping them
 * means a new runtime, build host and seed; do that with the next change to
 * the list. */
static const char *retired(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "compiler_json_string and compiler_json_value_string are retired; "
           "use the json_string package";
}

static const native_parameter_definition INPUTS[] = {
    { .name = "literal", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_STR, .type_name = "const char *" },
};
static const native_parameter_definition OUTPUTS[] = {
    { .name = "valid", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_BOOL, .type_name = "bool" },
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_STR, .type_name = "const char *" },
    { .name = "byte_length", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Compiler JSON String",
      .source_name = "compiler_json_string",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1,
      .pure = true },
    { .canonical_name = "Compiler JSON Value String",
      .source_name = "compiler_json_value_string",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1,
      .pure = true },
};
static const node_host_call CALL = { .context = retired };
static const node_access ACCESS = { .flags = NODE_EFFECT_PURE };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Compiler JSON String",
      .implementation = { .call = &CALL, .access = &ACCESS } },
    { .canonical_name = "Compiler JSON Value String",
      .implementation = { .call = &CALL, .access = &ACCESS } },
};
const native_definition_pack COMPILER_JSON_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
