#include "graph/nodes/math_nodes.h"

#include "graph/model/native_definition.h"
#include "runtime/math_native.h"

static pin_spec unary[] = { NATIVE_MATH_UNARY_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec result[] = { NATIVE_MATH_RESULT_OUTPUTS(GRAPH_NATIVE_PORT) };
static pin_spec number_result[] = { NATIVE_MATH_F32_OUTPUTS(GRAPH_NATIVE_PORT) };

#define UNARY(NAME, LABEL, SOURCE, FN, DESCRIPTION)                            \
    node_desc NODE_##NAME = { .name = LABEL,                                   \
                              .inputs = unary,                                 \
                              .input_count = 1,                                \
                              .outputs = result,                               \
                              .output_count = 1,                               \
                              .pure_kind = NODE_PURE_CALL,                     \
                              .fn = native_math_##NAME,                        \
                              .execution_call = &NATIVE_MATH_##NAME##_CALL,    \
                              .access = &NATIVE_MATH_ACCESS,                   \
                              .category = "Math/Functions",                    \
                              .description = DESCRIPTION,                      \
                              .foldable = true };
NATIVE_MATH_UNARIES(UNARY)
#undef UNARY

#define BINARY(NAME, LABEL, SOURCE, FN, INPUTS, KEYWORDS, DESCRIPTION)         \
    static pin_spec NAME##_inputs[] = { INPUTS(GRAPH_NATIVE_PORT) };           \
    node_desc NODE_##NAME = { .name = LABEL,                                   \
                              .inputs = NAME##_inputs,                         \
                              .input_count = 2,                                \
                              .outputs = result,                               \
                              .output_count = 1,                               \
                              .pure_kind = NODE_PURE_CALL,                     \
                              .fn = native_math_##NAME,                        \
                              .execution_call = &NATIVE_MATH_##NAME##_CALL,    \
                              .access = &NATIVE_MATH_ACCESS,                   \
                              .category = "Math/Functions",                    \
                              .keywords = KEYWORDS,                            \
                              .description = DESCRIPTION,                      \
                              .foldable = true };
NATIVE_MATH_BINARIES(BINARY)
#undef BINARY

static const char *random_value(const eval_context *, arena *, pin_value *out)
{
    *out = native_math_RandomReal(NULL, 0, NULL);
    return NULL;
}

node_desc NODE_RandomReal = { .name = NATIVE_MATH_RandomReal_NAME,
                              .source_name = NATIVE_MATH_RandomReal_SOURCE,
                              .outputs = number_result,
                              .output_count = 1,
                              .pure_kind = NODE_PURE_READ,
                              .read = random_value,
                              .fn = native_math_RandomReal,
                              .execution_call = &NATIVE_MATH_RandomReal_CALL,
                              .access = &NATIVE_MATH_RANDOM_ACCESS,
                              .category = "Math/Random",
                              .keywords = "rand random sample",
                              .description = "Returns a new random number from "
                                             "0 up to, but not including, 1." };

static pin_spec random_range_real_inputs[] = { NATIVE_MATH_RANGE_INPUTS(GRAPH_NATIVE_PORT) };

node_desc NODE_RandomRangeReal = {
    .name = NATIVE_MATH_RandomRangeReal_NAME,
    .inputs = random_range_real_inputs,
    .input_count = 2,
    .outputs = result,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = native_math_RandomRangeReal,
    .execution_call = &NATIVE_MATH_RandomRangeReal_CALL,
    .access = &NATIVE_MATH_RANDOM_ACCESS,
    .category = "Math/Random",
    .keywords = "rand random range sample min max",
    .description = "Returns a random number between Min and Max, which default "
                   "to 0 and 1; reversed bounds swap."
};
