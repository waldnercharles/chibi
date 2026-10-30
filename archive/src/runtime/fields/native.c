#include "runtime/fields/native.h"

#include "runtime/fields/definition.h"
#include "runtime/fields/program.h"

#include <math.h>

const node_access FIELD_NATIVE_ACCESS = { .flags = NODE_EFFECT_PURE };

/* A field program fails on a nonfinite scalar, so a call does too. */
static const char *apply(field_op op, const char *failure, const pin_value *in, pin_value *out)
{
    float operands[FIELD_INPUT_MAX];
    for (int i = 0; i < field_op_arity(op); i++) {
        if (!isfinite(in[i].f)) {
            return failure;
        }
        operands[i] = in[i].f;
    }
    out->f = field_op_value(op, operands, 0);
    return isfinite(out->f) ? NULL : failure;
}
#define OPERATION(OP)                                                           \
    static const char *invoke_##OP(void *, const pin_value *in, pin_value *out) \
    {                                                                           \
        static const char FAILURE[] = FIELD_NODE_##OP##_NAME                    \
            " requires finite values in its domain";                            \
        return apply(FIELD_##OP, FAILURE, in, out);                             \
    }                                                                           \
    const node_host_call FIELD_##OP##_CALL = { .invoke = invoke_##OP };
FIELD_NATIVE_OPERATIONS(OPERATION)
#undef OPERATION

#define SAMPLE(OP)                                                             \
    static const char *invoke_##OP(void *, const pin_value *, pin_value *)     \
    {                                                                          \
        return FIELD_NODE_##OP##_NAME " has a value only in a field program";  \
    }                                                                          \
    const node_host_call FIELD_##OP##_CALL = { .invoke = invoke_##OP };
FIELD_NATIVE_SAMPLES(SAMPLE)
#undef SAMPLE

static const char *constant(void *, const pin_value *in, pin_value *out)
{
    out->f = in[0].f;
    return isfinite(out->f) ? NULL : FIELD_NODE_CONSTANT_NAME " requires a finite Value";
}
const node_host_call FIELD_CONSTANT_CALL = { .invoke = constant };
