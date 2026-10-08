#include "graph/nodes/compare_nodes.h"

#include "graph/model/native_definition.h"
#include "runtime/comparisons.h"
static pin_spec real_inputs[] = { NATIVE_COMPARE_REAL_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec integer_inputs[] = { NATIVE_COMPARE_INTEGER_INPUTS(GRAPH_NATIVE_PORT) };
static pin_spec outputs[] = { NATIVE_COMPARE_OUTPUTS(GRAPH_NATIVE_PORT) };
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    static pin_spec KEY##_inputs[] = { INPUTS(GRAPH_NATIVE_PORT) };
NATIVE_COMPARE_VALUES(VALUE)
#undef VALUE
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define NODE(KEY, NAME, SOURCE, OP, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)   \
    node_desc NODE_##KEY = { .name = NAME,                                     \
                             .source_name = SOURCE,                            \
                             .operator_name = OP,                              \
                             .input_count = COUNT(INPUTS),                     \
                             .output_count = COUNT(outputs),                   \
                             .inputs = INPUTS,                                 \
                             .outputs = outputs,                               \
                             .pure_kind = NODE_PURE_CALL,                      \
                             .fn = native_compare_##KEY,                       \
                             .execution_call = &NATIVE_COMPARE_##KEY##_CALL,   \
                             .display_class = NODE_DISPLAY_PURE,               \
                             .category = CATEGORY,                             \
                             .keywords = KEYWORDS,                             \
                             .description = DESCRIPTION,                       \
                             .access = &NATIVE_COMPARE_ACCESS };
#define REAL(KEY, NAME, SOURCE, OP, EQUALITY, NEGATE, KEYWORDS, DESCRIPTION)   \
    NODE(KEY, NAME, SOURCE, #OP, real_inputs, "Math/Comparison", KEYWORDS, DESCRIPTION)
#define INTEGER(KEY, NAME, SOURCE, OP, KEYWORDS, DESCRIPTION)                  \
    NODE(KEY, NAME, SOURCE, #OP, integer_inputs, "Math/Comparison", KEYWORDS, DESCRIPTION)
#define VALUE(KEY, NAME, SOURCE, INPUTS, CATEGORY, KEYWORDS, DESCRIPTION)      \
    NODE(KEY, NAME, SOURCE, NULL, KEY##_inputs, CATEGORY, KEYWORDS, DESCRIPTION)
NATIVE_COMPARE_REALS(REAL)
NATIVE_COMPARE_INTEGERS(INTEGER)
NATIVE_COMPARE_VALUES(VALUE)
