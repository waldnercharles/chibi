#include "compiler_prepared_fixture.h"

#include "execution/function.h"
#include "support/arena.h"
#include "toolchain/compiler_prepared_scalar.h"
#include "types/array.h"

#include <stdlib.h>
#include <string.h>

enum
{
    PLAN,
    MODULE,
    FUNCTION,
    ENTRY,
    RESULT_STORAGE,
    INPUT_COUNT,
    BYTES = 0,
    DIAGNOSTIC,
    OUTPUT_COUNT
};
static const native_parameter_definition INPUTS[] = {
    { .name = "plan",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_FLOW,
      .type_flags = SCRIPT_TYPE_UNTYPED,
      .generic_group = 1 },
    { .name = "module", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_U64, .type_name = "uint64_t" },
    { .name = "function", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_U64, .type_name = "uint64_t" },
    { .name = "entry", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_STR, .type_name = "const char *" },
    { .name = "result_storage",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int" },
};
static const native_parameter_definition OUTPUTS[] = {
    { .name = "bytes", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_U8, .container = PIN_CONTAINER_ARRAY },
    { .name = "diagnostic",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *" },
};
_Static_assert(NATIVE_COUNT(INPUTS) == INPUT_COUNT, "prepared fixture inputs");
_Static_assert(NATIVE_COUNT(OUTPUTS) == OUTPUT_COUNT, "prepared fixture outputs");

static const char *own(arena *storage, const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return copy;
}
static const char *write_prepared(
    const behavior_function_context *context,
    void *service,
    const pin_value *arguments,
    pin_value *results
)
{
    const compiler_prepared_fixture_service *configuration = service;
    if (!context || !context->value_storage || !configuration ||
        !configuration->ready_tag || !arguments || !results) {
        return "Invalid prepared fixture invocation";
    }
    for (int i = MODULE; i < INPUT_COUNT; i++) {
        if (arguments[i].type != INPUTS[i].storage ||
            arguments[i].container != PIN_CONTAINER_SINGLE) {
            return "Invalid prepared fixture argument type";
        }
    }
    compiler_prepared_scalar_declaration declaration = {
        .module = arguments[MODULE].u64,
        .function = arguments[FUNCTION].u64,
        .entry = arguments[ENTRY].str,
        .result = arguments[RESULT_STORAGE].i
    };
    unsigned char *bytes = NULL;
    size_t length = 0;
    char error[192];
    bool valid = compiler_prepared_scalar_write(
        &arguments[PLAN],
        configuration->ready_tag,
        &declaration,
        &bytes,
        &length,
        error,
        sizeof error
    );
    if (!valid) {
        results[BYTES].ptr = NULL;
        results[DIAGNOSTIC].str = own(context->value_storage, error);
        return NULL;
    }
    /* The bridge bounds length below INT_MAX before conversion. Array elements
     * are owned values, not a borrowed view into the temporary malloc buffer. */
    pin_array *out = pin_array_snapshot_allocate(
        context->value_storage,
        PIN_U8,
        OUTPUTS[BYTES].type_name,
        (int)length
    );
    for (int i = 0; i < (int)length; i++) {
        out->items[i] = (pin_value){ .type = PIN_U8, .u8 = bytes[i] };
    }
    free(bytes);
    results[BYTES].ptr = out;
    results[DIAGNOSTIC].str = own(context->value_storage, "");
    return NULL;
}
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Compiler Prepared Scalar Fixture",
      .source_name = "compiler_prepared_scalar",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1,
      .pure = true },
};
static const node_host_call CALL = { .service = COMPILER_PREPARED_FIXTURE_SERVICE,
                                     .context = write_prepared };
static const node_access ACCESS = { .flags = NODE_EFFECT_PURE };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Compiler Prepared Scalar Fixture",
      .implementation = { .call = &CALL, .access = &ACCESS } },
};
const native_definition_pack COMPILER_PREPARED_FIXTURE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
