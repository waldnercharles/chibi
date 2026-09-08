#include "graph/nodes/math_nodes.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

static const node_access pure = { .flags = NODE_EFFECT_PURE };
static const node_access random_access = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec unary[] = { { .name = "Value", .type = PIN_F64, .type_name = "double" } };
static pin_spec binary[] = { { .name = "A", .type = PIN_F64, .type_name = "double" },
                             { .name = "B", .type = PIN_F64, .type_name = "double" } };
static pin_spec result[] = { { .name = "Result", .type = PIN_F64, .type_name = "double" } };

#define UNARY(NAME, LABEL, FN)                                                 \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *) \
    {                                                                          \
        assert(count == 1 && inputs[0].type == PIN_F64);                       \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d) };           \
    }                                                                          \
    node_desc NODE_##NAME = { .name = LABEL,                                   \
                              .inputs = unary,                                 \
                              .input_count = 1,                                \
                              .outputs = result,                               \
                              .output_count = 1,                               \
                              .pure_kind = NODE_PURE_CALL,                     \
                              .fn = NAME##_value,                              \
                              .access = &pure,                                 \
                              .category = "Math/Functions" };
UNARY(Sine, "Sine", sin)
UNARY(Cosine, "Cosine", cos)
UNARY(Tangent, "Tangent", tan)
UNARY(ArcSine, "Arc Sine", asin)
UNARY(ArcCosine, "Arc Cosine", acos)
UNARY(ArcTangent, "Arc Tangent", atan)
UNARY(SquareRoot, "Square Root", sqrt)
UNARY(Exponential, "Exponential", exp)
UNARY(Logarithm, "Logarithm", log)
UNARY(Logarithm10, "Logarithm 10", log10)
UNARY(Floor, "Floor", floor)
UNARY(Ceiling, "Ceiling", ceil)
UNARY(Round, "Round", round)
#undef UNARY

#define BINARY(NAME, LABEL, FN)                                                       \
    static pin_value NAME##_value(const pin_value *inputs, int count, arena *)        \
    {                                                                                 \
        assert(count == 2 && inputs[0].type == PIN_F64 && inputs[1].type == PIN_F64); \
        return (pin_value){ .type = PIN_F64, .d = FN(inputs[0].d, inputs[1].d) };     \
    }                                                                                 \
    node_desc NODE_##NAME = { .name = LABEL,                                          \
                              .inputs = binary,                                       \
                              .input_count = 2,                                       \
                              .outputs = result,                                      \
                              .output_count = 1,                                      \
                              .pure_kind = NODE_PURE_CALL,                            \
                              .fn = NAME##_value,                                     \
                              .access = &pure,                                        \
                              .category = "Math/Functions" };
BINARY(Power, "Power", pow)
BINARY(ArcTangent2, "Arc Tangent 2", atan2)
#undef BINARY

static pin_value random_sample(const pin_value *, int count, arena *)
{
    assert(count == 0);
    return (pin_value){ .type = PIN_F64, .d = (double)rand() / ((double)RAND_MAX + 1) };
}

static const char *random_value(const eval_context *, arena *, pin_value *out)
{
    *out = random_sample(NULL, 0, NULL);
    return NULL;
}

node_desc NODE_RandomReal = { .name = "Random Real",
                              .outputs = result,
                              .output_count = 1,
                              .pure_kind = NODE_PURE_READ,
                              .read = random_value,
                              .fn = random_sample,
                              .access = &random_access,
                              .category = "Math/Random",
                              .keywords = "rand random sample" };
