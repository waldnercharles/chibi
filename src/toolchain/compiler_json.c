#include "toolchain/compiler_json.h"

#include "execution/function.h"
#include "support/arena.h"

#include <limits.h>
#include <string.h>
#include <yyjson.h>

static const char *decode_string(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *results
)
{
    if (!context || !context->value_storage || !arguments || !results ||
        arguments[0].type != PIN_STR || arguments[0].container != PIN_CONTAINER_SINGLE) {
        return "Invalid JSON string arguments";
    }
    arena *storage = context->value_storage;
    results[0].b = false;
    results[1].str = "";
    results[2].i = 0;
    const char *literal = arguments[0].str;
    if (!literal) {
        return NULL;
    }
    yyjson_doc *document = yyjson_read(literal, strlen(literal), 0);
    yyjson_val *value = document ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_str(value)) {
        yyjson_doc_free(document);
        return NULL;
    }
    size_t length = yyjson_get_len(value);
    if (length > INT_MAX) {
        yyjson_doc_free(document);
        return "JSON string length exceeds integer range";
    }
    char *copy = arena_alloc(storage, length + 1);
    memcpy(copy, yyjson_get_str(value), length);
    copy[length] = 0;
    yyjson_doc_free(document);
    results[0].b = true;
    results[1].str = copy;
    results[2].i = (int)length;
    return NULL;
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
};
static const node_host_call CALL = { .context = decode_string };
static const node_access ACCESS = { .flags = NODE_EFFECT_PURE };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Compiler JSON String",
      .implementation = { .call = &CALL, .access = &ACCESS } },
};
const native_definition_pack COMPILER_JSON_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
