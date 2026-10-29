#include "graph/model/native_definition.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/core_definition.h"
/* graph/nodes/const_nodes.c -- corrected core literal nodes, registered as EXTRA_NODES.
 *
 * CAT-30: the core literals are Bool, Byte, Integer, Integer64, Real, Name,
 * and String. Each is pure and has exactly one typed "Value" data input
 * carrying the authored default and one typed return output. The output is
 * the input, so an unconnected literal evaluates to its authored default.
 */
#include "graph/model/node.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/fmt_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "runtime/arithmetic.h"

#define GENERIC_UNARY_DESC(VAR, NAME, FN, KEYWORDS, DESCRIPTION, JIT)                          \
    static pin_spec NODE_##VAR##_inputs[] = {                                                  \
        NATIVE_ARITHMETIC_UNARY_INPUTS(GRAPH_NATIVE_PORT)                                      \
    };                                                                                         \
    static pin_spec NODE_##VAR##_outputs[] = { NATIVE_ARITHMETIC_OUTPUTS(GRAPH_NATIVE_PORT) }; \
    node_desc NODE_##VAR = { .execution_call = &NATIVE_ARITHMETIC_##VAR##_CALL,                \
                             .name = NAME,                                                     \
                             .input_count = 1,                                                 \
                             .output_count = 1,                                                \
                             .inputs = NODE_##VAR##_inputs,                                    \
                             .outputs = NODE_##VAR##_outputs,                                  \
                             .access = &NATIVE_ARITHMETIC_ACCESS,                              \
                             .pure_kind = NODE_PURE_CALL,                                      \
                             .jit_kind = JIT,                                                  \
                             .fn = FN,                                                         \
                             .display_class = NODE_DISPLAY_PURE,                               \
                             .category = "Math",                                               \
                             .keywords = KEYWORDS,                                             \
                             .description = DESCRIPTION };

static pin_spec NODE_Clamp_inputs[] = { NATIVE_ARITHMETIC_CLAMP_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec NODE_Clamp_outputs[] = { NATIVE_ARITHMETIC_OUTPUTS(GRAPH_NATIVE_PORT) };
node_desc NODE_Clamp = { .name = NATIVE_ARITHMETIC_Clamp_NAME,
                         .input_count = 3,
                         .output_count = 1,
                         .inputs = NODE_Clamp_inputs,
                         .outputs = NODE_Clamp_outputs,
                         .access = &NATIVE_ARITHMETIC_ACCESS,
                         .pure_kind = NODE_PURE_CALL,
                         .jit_kind = NODE_JIT_CLAMP,
                         .fn = native_arithmetic_clamp,
                         .execution_call = &NATIVE_ARITHMETIC_Clamp_CALL,
                         .numeric_aggregate = true,
                         .display_class = NODE_DISPLAY_PURE,
                         .category = "Math",
                         .keywords = "clamp bound constrain limit",
                         .description = "Limits Value to the range from Min to "
                                        "Max, per component for vectors." };

#define GENERIC_BINARY_DESC(VAR, NAME, FN, KEYWORDS, DESCRIPTION, JIT)                         \
    static pin_spec NODE_##VAR##_inputs[] = {                                                  \
        NATIVE_ARITHMETIC_BINARY_INPUTS(GRAPH_NATIVE_PORT)                                     \
    };                                                                                         \
    static pin_spec NODE_##VAR##_outputs[] = { NATIVE_ARITHMETIC_OUTPUTS(GRAPH_NATIVE_PORT) }; \
    node_desc NODE_##VAR = {                                                                   \
        .name = NAME,                                                                          \
        .operator_name = NATIVE_ARITHMETIC_##VAR##_OPERATOR,                                   \
        .numeric_aggregate = true,                                                             \
        .input_count = 2,                                                                      \
        .output_count = 1,                                                                     \
        .inputs = NODE_##VAR##_inputs,                                                         \
        .outputs = NODE_##VAR##_outputs,                                                       \
        .access = &NATIVE_ARITHMETIC_ACCESS,                                                   \
        .pure_kind = NODE_PURE_CALL,                                                           \
        .jit_kind = JIT,                                                                       \
        .fn = FN,                                                                              \
        .execution_call = &NATIVE_ARITHMETIC_##VAR##_CALL,                                     \
        .display_class = NODE_DISPLAY_PURE,                                                    \
        .category = "Math",                                                                    \
        .keywords = KEYWORDS,                                                                  \
        .description = DESCRIPTION,                                                            \
    };

GENERIC_BINARY_DESC(
    Add,
    NATIVE_ARITHMETIC_Add_NAME,
    native_arithmetic_add,
    "add plus vector",
    "Sums A and B as reals, integers or vectors; a scalar applies to every "
    "vector component.",
    NODE_JIT_ADD
)
GENERIC_BINARY_DESC(
    Multiply,
    NATIVE_ARITHMETIC_Multiply_NAME,
    native_arithmetic_multiply,
    "multiply mul times vector",
    "Multiplies A by B as reals, integers or vectors; a scalar scales every "
    "vector component.",
    NODE_JIT_MUL
)
GENERIC_BINARY_DESC(
    Subtract,
    NATIVE_ARITHMETIC_Subtract_NAME,
    native_arithmetic_subtract,
    "subtract minus vector",
    "Takes B from A as reals, integers or vectors; a scalar applies to every "
    "vector component.",
    NODE_JIT_SUB
)
GENERIC_BINARY_DESC(
    Divide,
    NATIVE_ARITHMETIC_Divide_NAME,
    native_arithmetic_divide,
    "divide over vector",
    "Divides A by B as reals, integers or vectors; integers truncate toward "
    "zero and fail when B is 0.",
    NODE_JIT_DIV
)
GENERIC_BINARY_DESC(
    Min,
    NATIVE_ARITHMETIC_Min_NAME,
    native_arithmetic_min,
    "min minimum vector",
    "Returns the smaller of A and B, per component for vectors.",
    NODE_JIT_MIN
)
GENERIC_BINARY_DESC(
    Max,
    NATIVE_ARITHMETIC_Max_NAME,
    native_arithmetic_max,
    "max maximum vector",
    "Returns the larger of A and B, per component for vectors.",
    NODE_JIT_MAX
)
GENERIC_BINARY_DESC(
    Modulo,
    NATIVE_ARITHMETIC_Modulo_NAME,
    native_arithmetic_modulo,
    "mod modulo remainder",
    "Returns the remainder of A divided by B, taking the sign of A; fails when "
    "an integer B is 0.",
    NODE_JIT_NONE
)
GENERIC_UNARY_DESC(
    Negate,
    NATIVE_ARITHMETIC_Negate_NAME,
    native_arithmetic_negate,
    "negate negative minus",
    "Flips the sign of a real or integer Value.",
    NODE_JIT_NEG
)
GENERIC_UNARY_DESC(
    Abs,
    NATIVE_ARITHMETIC_Abs_NAME,
    native_arithmetic_abs,
    "abs absolute",
    "Returns the absolute value of a real or integer Value.",
    NODE_JIT_ABS
)

#define LITERAL_DESC(VAR, NAME, DESCRIPTION)                                   \
    static pin_spec NODE_##VAR##_inputs[] = {                                  \
        NATIVE_ARITHMETIC_##VAR##_PORTS(GRAPH_NATIVE_PORT)                     \
    };                                                                         \
    static pin_spec NODE_##VAR##_outputs[] = {                                 \
        NATIVE_ARITHMETIC_##VAR##_PORTS(GRAPH_NATIVE_PORT)                     \
    };                                                                         \
    node_desc NODE_##VAR = {                                                   \
        .execution_call = &NATIVE_ARITHMETIC_##VAR##_CALL,                     \
        .name = NAME,                                                          \
        .input_count = 1,                                                      \
        .output_count = 1,                                                     \
        .inputs = NODE_##VAR##_inputs,                                         \
        .outputs = NODE_##VAR##_outputs,                                       \
        .access = &NATIVE_ARITHMETIC_ACCESS,                                   \
        .pure_kind = NODE_PURE_CALL,                                           \
        .fn = native_arithmetic_literal_identity,                              \
        .display_class = NODE_DISPLAY_LITERAL,                                 \
        .description = DESCRIPTION,                                            \
    };

LITERAL_DESC(Bool, NATIVE_ARITHMETIC_Bool_NAME, "Returns Value as a constant true or false.")
LITERAL_DESC(Byte, NATIVE_ARITHMETIC_Byte_NAME, "Returns Value as a constant whole number from 0 to 255.")
LITERAL_DESC(Integer, NATIVE_ARITHMETIC_Integer_NAME, "Returns Value as a constant 32-bit signed whole number.")
LITERAL_DESC(Integer64, NATIVE_ARITHMETIC_Integer64_NAME, "Returns Value as a constant 64-bit signed whole number.")
LITERAL_DESC(Real, NATIVE_ARITHMETIC_Real_NAME, "Returns Value as a constant f64.")
LITERAL_DESC(Name, NATIVE_ARITHMETIC_Name_NAME, "Returns Value as a constant name.")
LITERAL_DESC(String, NATIVE_ARITHMETIC_String_NAME, "Returns Value as a constant text string.")

static node_desc *const EXTRA_NODES[] = {
#define MATH_NODE_ENTRY(name) &NODE_##name,
    MATH_EXPRESSION_NODES(MATH_NODE_ENTRY)
#undef MATH_NODE_ENTRY
        & NODE_Greater_Or_Equal,
    &NODE_Integer_Greater_Or_Equal,
    &NODE_Less,
    &NODE_Integer_Less,
    &NODE_Less_Or_Equal,
    &NODE_Integer_Less_Or_Equal,
    &NODE_Greater,
    &NODE_Integer_Greater,
    &NODE_Equal,
    &NODE_Integer_Equal,
    &NODE_Not_Equal,
    &NODE_Integer_Not_Equal,
    &NODE_Has_Any_Flags,
    &NODE_And,
    &NODE_Or,
    &NODE_Not,
    &NODE_Bool,
    &NODE_Byte,
    &NODE_Integer,
    &NODE_Integer64,
    &NODE_Real,
    &NODE_Name,
    &NODE_String,
    &NODE_To_String,
    &NODE_To_Name,
    &NODE_Splat_vec2,
    &NODE_Splat_vec3,
    &NODE_Splat_ShaderVec2,
    &NODE_Splat_ShaderVec3,
    &NODE_Splat_ShaderVec4,
    &NODE_Add,
    &NODE_Multiply,
    &NODE_Subtract,
    &NODE_Divide,
    &NODE_Min,
    &NODE_Max,
    &NODE_Clamp,
    &NODE_Modulo,
    &NODE_Negate,
    &NODE_Abs,
    &NODE_To_Real,
    &NODE_To_Integer64,
    &NODE_To_Byte,
    &NODE_To_Integer,
    &NODE_Format_Text,
    &NODE_Text_Equal,
};

const registry_pack CORE_EXTRA_PACK = {
    .name = "core.extra",
    .nodes = EXTRA_NODES,
    .node_count = sizeof EXTRA_NODES / sizeof EXTRA_NODES[0],
};
