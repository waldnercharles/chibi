#ifndef CHIBI_BEHAVIOR_FUNCTION_INTERNAL_H
#define CHIBI_BEHAVIOR_FUNCTION_INTERNAL_H
#include "graph/compiler/behavior_function.h"
#include "graph/compiler/behavior_scalar.h"

/* Platform libm routines with scalar C ABIs, called directly by native code.
 * Keep inexpensive hardware operations separate from this call boundary. */
#define BEHAVIOR_MATH_UNARY(X)                                                 \
    X(SIN, Sine, sin)                                                          \
    X(COS, Cosine, cos)                                                        \
    X(TAN, Tangent, tan)                                                       \
    X(ASIN, ArcSine, asin)                                                     \
    X(ACOS, ArcCosine, acos)                                                   \
    X(ATAN, ArcTangent, atan)                                                  \
    X(EXP, Exponential, exp)                                                   \
    X(LOG, Logarithm, log)                                                     \
    X(LOG10, Logarithm10, log10)
#define BEHAVIOR_MATH_BINARY(X)                                                \
    X(POW, Power, pow)                                                         \
    X(ATAN2, ArcTangent2, atan2)

typedef enum
{
    BF_CONSTANT,
    BF_VARIABLE,
    BF_DELTA,
    BF_ARGUMENT,
    BF_LOCAL,
    BF_ADD,
    BF_SUB,
    BF_MUL,
    BF_DIV,
    BF_MOD,
    BF_NEG,
    BF_ABS,
    BF_MIN,
    BF_MAX,
    BF_CONVERT,
    BF_CAST,
    BF_FLAGS,
    BF_LT,
    BF_LE,
    BF_GT,
    BF_GE,
    BF_EQ,
    BF_NE,
#define MATH_OP(op, name, fn) BF_##op,
    BEHAVIOR_MATH_UNARY(MATH_OP)
    BEHAVIOR_MATH_BINARY(MATH_OP)
#undef MATH_OP
    BF_STORE,
    BF_RESULT,
    BF_SAVE,
    BF_CHECK,
    BF_CALL_CHECK,
    BF_ENTER,
    BF_LEAVE,
    BF_FAIL,
    BF_JUMP,
    BF_BRANCH,
    BF_RETURN,
    BF_END
} behavior_function_op;
static inline bool behavior_function_math(behavior_function_op op)
{
    return op >= BF_SIN && op <= BF_ATAN2;
}
typedef struct
{
    behavior_function_op op;
    pin_type type;
    int output, left, right, target, frame, source;
    uint64_t node_id;
    pin_value value;
} behavior_function_instruction;
typedef struct
{
    pin_value initial;
    int frame;
    const node *node;
    int pin;
} behavior_function_local;
typedef struct
{
    behavior_function_source source;
    int depth, resume, source_index;
} behavior_function_frame;
struct behavior_function
{
    behavior_function_instruction *instructions;
    pin_type *slots;
    behavior_function_local *locals;
    behavior_function_frame *frames;
    behavior_function_source *sources;
    void *code;
    void *dispatch;
    struct behavior_function *fast;
    unsigned root_steps, child_budget, depth_limit;
    unsigned pure_depth_limit;
};
void *behavior_function_jit(const behavior_function *function);
void *behavior_function_guard(const behavior_function *function);
void behavior_function_optimize(behavior_function *function);
static inline bool behavior_function_fast_allowed(
    const behavior_function *f,
    const behavior_function_context *ctx
)
{
    return f->fast && ctx->steps <= ctx->budget &&
           ctx->budget - ctx->steps >= f->fast->root_steps &&
           ctx->budget >= f->fast->child_budget &&
           (!f->fast->depth_limit || ctx->call_depth < f->fast->depth_limit);
}
#endif
