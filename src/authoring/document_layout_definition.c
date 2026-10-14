#include "authoring/document_layout_definition.h"
#define DATA(N, T, S)                                                          \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
static const native_parameter_definition INPUTS[] = {
    DATA("Document", PIN_I32, "int"),
    DATA("Revision", PIN_I32, "int"),
    DATA("Graph", PIN_STR, "const char *"),
    DATA("Node", PIN_STR, "const char *"),
    DATA("X", PIN_F32, "float"),
    DATA("Y", PIN_F32, "float"),
    DATA("Placed", PIN_BOOL, "bool"),
    DATA("Arrange", PIN_BOOL, "bool")
};
static const native_parameter_definition OUTPUTS[] = {
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Error", PIN_STR, "const char *"),
    DATA("Path", PIN_STR, "const char *"),
    DATA("Metadata", PIN_STR, "const char *")
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Authoring.prepare_source_layout",
      .source_name = "prepare_source_layout",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Authoring.prepare_source_layout",
      .implementation = { .call = &DOCUMENT_LAYOUT_CALL, .access = &DOCUMENT_LAYOUT_ACCESS } }
};
const native_definition_pack DOCUMENT_LAYOUT_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
