#include "authoring/document_observation_definition.h"
#define D(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
static const native_parameter_definition INPUTS[] = {
    D("Document", PIN_I32, "int"),       D("Revision", PIN_I32, "int"),
    D("Graph", PIN_STR, "const char *"), D("Node", PIN_STR, "const char *"),
    D("Pin", PIN_STR, "const char *"),   D("Output", PIN_BOOL, "bool")
};
static const native_parameter_definition OUTPUTS[] = {
    D("Ok", PIN_BOOL, "bool"),
    D("Error", PIN_STR, "const char *"),
    D("Module", PIN_U64, "uint64"),
    D("Function", PIN_U64, "uint64"),
    D("Occurrence", PIN_U64, "uint64"),
    D("Index", PIN_I32, "int"),
    D("RecordedOutput", PIN_BOOL, "bool"),
    D("FallbackOccurrence", PIN_U64, "uint64"),
    D("FallbackIndex", PIN_I32, "int"),
    D("FallbackOutput", PIN_BOOL, "bool"),
    D("HasFallback", PIN_BOOL, "bool")
};
static const native_parameter_definition EXECUTION_INPUTS[] = {
    D("Document", PIN_I32, "int"),
    D("Revision", PIN_I32, "int"),
    D("Graph", PIN_STR, "const char *"),
    D("Node", PIN_STR, "const char *")
};
static const native_parameter_definition EXECUTION_OUTPUTS[] = {
    D("Ok", PIN_BOOL, "bool"),
    D("Error", PIN_STR, "const char *"),
    D("Module", PIN_U64, "uint64"),
    D("Function", PIN_U64, "uint64"),
    D("Occurrence", PIN_U64, "uint64")
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Authoring.source_graph_observation",
      .source_name = "source_graph_observation",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1 },
    { .canonical_name = "Authoring.source_graph_execution",
      .source_name = "source_graph_execution",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = EXECUTION_INPUTS,
      .input_count = NATIVE_COUNT(EXECUTION_INPUTS),
      .outputs = EXECUTION_OUTPUTS,
      .output_count = NATIVE_COUNT(EXECUTION_OUTPUTS),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Authoring.source_graph_observation",
      .implementation = { .call = &DOCUMENT_OBSERVATION_CALL,
                          .access = &DOCUMENT_OBSERVATION_ACCESS } },
    { .canonical_name = "Authoring.source_graph_execution",
      .implementation = { .call = &DOCUMENT_EXECUTION_CALL,
                          .access = &DOCUMENT_OBSERVATION_ACCESS } }
};
const native_definition_pack DOCUMENT_OBSERVATION_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
