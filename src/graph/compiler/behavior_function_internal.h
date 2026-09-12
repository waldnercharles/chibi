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
    BF_PRESENT,
    BF_TYPE_NAME,
    BF_OWNER,
    BF_WORLD,
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
    BF_SELECT,
#define MATH_OP(op, name, fn) BF_##op,
    BEHAVIOR_MATH_UNARY(MATH_OP)
    BEHAVIOR_MATH_BINARY(MATH_OP)
#undef MATH_OP
    BF_ATAN2_F32,
    BF_MOD_F32,
    BF_MOD_F64,
    BF_HYPOT,
    BF_ENTITY,
    BF_COMPONENT,
    BF_LOAD,
    BF_STORE,
    BF_RESULT,
    BF_SAVE,
    BF_RESULT_PRESENT,
    BF_RESULT_TYPE_NAME,
    BF_REQUIRE,
    BF_ASSERT,
    BF_WRITE,
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
    return op >= BF_SIN && op <= BF_HYPOT;
}
#define BF_POINTER_TYPE                                                        \
    (sizeof(uintptr_t) == sizeof(uint64_t) ? PIN_U64 : PIN_U32)

typedef struct
{
    behavior_function_op op;
    pin_type type;
    int output, left, right, target, frame, source;
    int pin;
    size_t offset;
    bool indirect;
    uint64_t node_id;
    pin_value value;
    uint64_t layout;
    const char *message;
} behavior_function_instruction;
typedef struct
{
    pin_value initial;
    int frame;
    const node *node;
    int pin;
    size_t offset;
} behavior_function_local;
typedef struct
{
    int index;
    const type_desc *type;
} behavior_function_result;
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
    behavior_function_result *results;
    void *code;
    void *dispatch;
    struct behavior_function *fast;
    unsigned root_steps, child_budget, depth_limit;
    unsigned pure_depth_limit;
    bool component_bindings, component_access;
};
static inline bool behavior_function_left_slot(behavior_function_op op)
{
    return (op >= BF_ADD && op <= BF_SAVE) || op == BF_BRANCH ||
           op == BF_REQUIRE || op == BF_ASSERT || op == BF_WRITE ||
           op == BF_RESULT_PRESENT || op == BF_RESULT_TYPE_NAME;
}
static inline bool behavior_function_right_slot(behavior_function_op op)
{
    return (op >= BF_ADD && op < BF_STORE && op != BF_LOAD) || op == BF_WRITE;
}
static inline bool behavior_function_integer_clobber(behavior_function_op op)
{
    return behavior_function_math(op) || op == BF_ENTITY || op == BF_COMPONENT;
}
void *behavior_function_jit(const behavior_function *function);
void *behavior_function_guard(const behavior_function *function);
void behavior_function_prune_values(behavior_function *function);
void behavior_function_reuse_accesses(behavior_function *function);
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
