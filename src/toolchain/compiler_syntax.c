#include "toolchain/compiler_syntax.h"

#include "execution/function.h"

enum
{
    ROOT,
    KINDS,
    STARTS,
    ENDS,
    ROWS,
    COLUMNS,
    PARENTS,
    FIRST_CHILDREN,
    NEXT_SIBLINGS,
    ERRORS,
    MISSING,
    HAS_ERRORS,
    FIELDS,
    IS_NAMED,
    IS_EXTRA,
    RESULT_COUNT,
};
static const native_parameter_definition INPUTS[] = {
    { .name = "text", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_STR, .type_name = "const char *" },
};
#define ARRAY_OUTPUT(NAME, TYPE, C_NAME)                                       \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = TYPE,                                                         \
      .type_name = C_NAME,                                                     \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition OUTPUTS[] = {
    { .name = "root", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    ARRAY_OUTPUT("kinds", PIN_STR, "const char *"),
    ARRAY_OUTPUT("starts", PIN_I32, "int"),
    ARRAY_OUTPUT("ends", PIN_I32, "int"),
    ARRAY_OUTPUT("rows", PIN_I32, "int"),
    ARRAY_OUTPUT("columns", PIN_I32, "int"),
    ARRAY_OUTPUT("parents", PIN_I32, "int"),
    ARRAY_OUTPUT("first_children", PIN_I32, "int"),
    ARRAY_OUTPUT("next_siblings", PIN_I32, "int"),
    ARRAY_OUTPUT("errors", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("missing", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("has_errors", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("fields", PIN_STR, "const char *"),
    ARRAY_OUTPUT("is_named", PIN_BOOL, "bool"),
    ARRAY_OUTPUT("is_extra", PIN_BOOL, "bool"),
};
#undef ARRAY_OUTPUT
_Static_assert(NATIVE_COUNT(OUTPUTS) == RESULT_COUNT, "syntax result schema");

/* Retired. The compiler reads syntax with tools/compiler/source_parser and no
 * source calls this native any more. Its declaration stays because the native
 * list is part of every prepared artifact's ABI, so dropping it means a new
 * runtime, build host and seed; do that with the next change to the list. */
static const char *parse(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "compiler_syntax_parse is retired; use the source_parser package";
}

static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Compiler Syntax Parse",
      .source_name = "compiler_syntax_parse",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INPUTS,
      .input_count = NATIVE_COUNT(INPUTS),
      .outputs = OUTPUTS,
      .output_count = NATIVE_COUNT(OUTPUTS),
      .completion_output = -1,
      .pure = true },
};
static const node_host_call CALL = { .context = parse };
static const node_access ACCESS = { .flags = NODE_EFFECT_PURE };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Compiler Syntax Parse",
      .implementation = { .call = &CALL, .access = &ACCESS } },
};
const native_definition_pack COMPILER_SYNTAX_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
