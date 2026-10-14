#include "runtime/json_query_definition.h"

#include "runtime/json_query_native.h"
#define PORT(NAME, STORAGE, TYPE)                                              \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = STORAGE,                                                      \
      .type_name = TYPE,                                                       \
      .container = PIN_CONTAINER_SINGLE }
static const native_parameter_definition INPUTS[] = {
    PORT("Text", PIN_STR, "const char *"),
    PORT("Pointer", PIN_STR, "const char *")
};
static const native_parameter_definition OUTPUTS[] = {
    PORT("Ok", PIN_BOOL, "bool"),
    PORT("Found", PIN_BOOL, "bool"),
    PORT("Kind", PIN_STR, "const char *"),
    PORT("String", PIN_STR, "const char *"),
    PORT("Number", PIN_F64, "double"),
    PORT("Boolean", PIN_BOOL, "bool"),
    PORT("Json", PIN_STR, "const char *"),
    PORT("Count", PIN_I32, "int"),
    PORT("Error", PIN_STR, "const char *")
};
#undef PORT
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Query JSON",
      .source_name = "query_json",
      .category = "Values/JSON",
      .description = "Read a bounded UTF-8 JSON value by RFC 6901 pointer. "
                     "Rejects duplicate keys, embedded NUL and excessive "
                     "depth/count. Missing values return Found=false. Number "
                     "is double; inspect Kind and Json when exact integer "
                     "representation matters.",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Query JSON",
      .implementation = { .call = &JSON_QUERY_CALL, .access = &JSON_QUERY_ACCESS } }
};
const native_definition_pack JSON_QUERY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
