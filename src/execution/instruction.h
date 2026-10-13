#ifndef CHIBI_EXECUTION_INSTRUCTION_H
#define CHIBI_EXECUTION_INSTRUCTION_H

#include "support/cos_sin.h"
#include "types/value.h"

/* Scalar math operations share semantics across backends. Sine and cosine
 * use the deterministic field approximation; native targets select hardware
 * instructions for the supported operations and call the scalar C ABI for
 * the rest. */
#define BEHAVIOR_MATH_UNARY(X)                                                 \
    X(SIN, Sine, cos_sin_sine)                                                 \
    X(COS, Cosine, cos_sin_cosine)                                             \
    X(TAN, Tangent, tan)                                                       \
    X(ASIN, ArcSine, asin)                                                     \
    X(ACOS, ArcCosine, acos)                                                   \
    X(ATAN, ArcTangent, atan)                                                  \
    X(EXP, Exponential, exp)                                                   \
    X(LOG, Logarithm, log)                                                     \
    X(LOG10, Logarithm10, log10)                                               \
    X(FRACT, Fract, behavior_fract)                                            \
    X(SIGN, Sign, behavior_sign)                                               \
    X(SQRT, SquareRoot, sqrt)                                                  \
    X(FLOOR, Floor, floor)                                                     \
    X(CEIL, Ceiling, ceil)                                                     \
    X(ROUND, Round, round)
#define BEHAVIOR_MATH_BINARY(X)                                                \
    X(POW, Power, pow)                                                         \
    X(ATAN2, Atan2, atan2)

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
    BF_CONTEXT,
    BF_STORAGE_NAME,
    BF_ARRAY,
    BF_QUERY,
    BF_HOST_SLOT,
    BF_HOST_SERVICE,
    BF_COMMAND_SERVICE,
    BF_COMMAND_RESULT,
    BF_OWN_NAME,
    BF_COPY_STRING,
    BF_COPY_ARRAY,
    BF_INTERN_STRING,
    BF_STRING_EQUAL,
    BF_STATE_SELECT,
    BF_RANDOM,
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
    BF_BIT_AND,
    BF_BIT_OR,
    BF_BIT_XOR,
    BF_BIT_NOT,
    BF_SHL,
    BF_SHR,
#define MATH_OP(op, name, fn) BF_##op,
    BEHAVIOR_MATH_UNARY(MATH_OP) BEHAVIOR_MATH_BINARY(MATH_OP)
#undef MATH_OP
        BF_MOD_F32,
    BF_MOD_F64,
    BF_HYPOT,
    BF_FMA,
    BF_RANDOM_RANGE,
    BF_ENTITY,
    BF_COMPONENT,
    BF_COLUMN,
    BF_COLUMN_TRY, /* Non-trapping layout guard for speculative read-only work. */
    BF_ROW, /* checked column + live entity slot; no storage-changing effects */
    BF_INDEX,
    BF_LOAD,
    BF_READ,
    BF_RETRIGGER,
    BF_STORE,
    BF_STORE_VALUE,
    BF_RESULT,
    BF_SAVE,
    BF_RESULT_PRESENT,
    BF_RESULT_TYPE_NAME,
    BF_RESULT_ARRAY,
    BF_STORE_HEADER,
    BF_STORE_TYPE_NAME,
    BF_REQUIRE,
    BF_ASSERT,
    BF_WRITE,
    BF_COMMAND_ARGUMENT,
    BF_COMMAND_OUTPUT,
    BF_COMMAND,
    BF_COMMAND_BEGIN,
    BF_HOST_CALL,
    BF_INVOKE,
    BF_SUSPEND,
    BF_DELAY,
    BF_POINT,
    BF_WATCH_BEGIN,
    BF_WATCH_RESET,
    BF_WATCH_VALUE,
    BF_WATCH_PRESENT,
    BF_WATCH_END,
    BF_TRACE,
    BF_CHECK,
    BF_CALL_CHECK,
    BF_ENTER,
    BF_LEAVE,
    BF_FAIL,
    BF_JUMP,
    BF_BRANCH,
    BF_RETURN,
    BF_SOURCE_POINT,
    BF_SOURCE_WATCH,
    BF_END
} behavior_function_op;

static inline bool behavior_function_bitwise(behavior_function_op op)
{
    return op >= BF_BIT_AND && op <= BF_SHR;
}

enum
{
    BF_STORE_VALUE_SET_ADD = 1
};
/* Derived runtime bodies only. Canonical and prepared instructions reject this
 * flag; verification and the borrowing proof run before it can be introduced. */
enum
{
    BF_HOST_BORROW_ARRAY = 1
};
static inline bool behavior_function_math(behavior_function_op op)
{
    return op >= BF_SIN && op <= BF_FMA;
}
static inline bool behavior_function_third_slot(behavior_function_op op)
{
    return op == BF_SELECT || op == BF_FMA;
}
static inline bool behavior_function_call(behavior_function_op op)
{
    return behavior_function_math(op) || op == BF_OWN_NAME ||
           op == BF_COPY_STRING || op == BF_COPY_ARRAY || op == BF_INTERN_STRING ||
           op == BF_STRING_EQUAL || op == BF_STORE_VALUE || op == BF_QUERY ||
           op == BF_COMMAND || op == BF_COMMAND_BEGIN || op == BF_HOST_CALL ||
           op == BF_RANDOM || op == BF_RANDOM_RANGE || op == BF_STATE_SELECT ||
           op == BF_RETRIGGER || op == BF_SOURCE_POINT || op == BF_SOURCE_WATCH;
}
static inline bool behavior_function_observer(behavior_function_op op)
{
    return op >= BF_WATCH_BEGIN && op <= BF_TRACE;
}
static inline bool behavior_function_inspection_call(behavior_function_op op)
{
    return op == BF_POINT || op == BF_WATCH_END || op == BF_WATCH_RESET || op == BF_TRACE;
}
#define BF_POINTER_TYPE                                                        \
    (sizeof(uintptr_t) == sizeof(uint64_t) ? PIN_U64 : PIN_U32)

typedef struct
{
    behavior_function_op op;
    pin_type type;
    int output, left, right, target, frame, source, stack;
    int pin;
    size_t offset;
    bool indirect;
    /* Computed after lowering; fits existing padding before component. */
    bool left_slot, right_slot;
    int component;
    uint64_t node_id;
    pin_value value;
    uint64_t layout;
    const char *message;
} behavior_function_instruction;
#endif
