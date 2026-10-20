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

/* One declaration owns both numeric opcodes and prepared schema names. Keep
 * the order stable; payload and semantic changes still need a prepared version. */
// clang-format off
#define BEHAVIOR_FUNCTION_OPS(OP, MATH) \
    OP(CONSTANT) \
    OP(VARIABLE) \
    OP(DELTA) \
    OP(ARGUMENT) \
    OP(LOCAL) \
    OP(PRESENT) \
    OP(TYPE_NAME) \
    OP(OWNER) \
    OP(WORLD) \
    OP(CONTEXT) \
    OP(STORAGE_NAME) \
    OP(ARRAY) \
    OP(QUERY_VARIABLE) /* Checked current tuple handle; target is 1-based. */ \
    OP(QUERY) \
    OP(HOST_SLOT) \
    OP(HOST_SERVICE) \
    OP(COMMAND_SERVICE) \
    OP(COMMAND_RESULT) \
    OP(OWN_NAME) \
    OP(COPY_STRING) \
    OP(COPY_ARRAY) \
    OP(INTERN_STRING) \
    OP(STRING_EQUAL) \
    OP(STATE_SELECT) \
    OP(RANDOM) \
    OP(ADD) \
    OP(SUB) \
    OP(MUL) \
    OP(DIV) \
    OP(MOD) \
    OP(NEG) \
    OP(ABS) \
    OP(MIN) \
    OP(MAX) \
    OP(CONVERT) \
    OP(CAST) \
    OP(FLAGS) \
    OP(LT) \
    OP(LE) \
    OP(GT) \
    OP(GE) \
    OP(EQ) \
    OP(NE) \
    OP(SELECT) \
    OP(BIT_AND) \
    OP(BIT_OR) \
    OP(BIT_XOR) \
    OP(BIT_NOT) \
    OP(SHL) \
    OP(SHR) \
    BEHAVIOR_MATH_UNARY(MATH) \
    BEHAVIOR_MATH_BINARY(MATH) \
    OP(MOD_F32) \
    OP(MOD_F64) \
    OP(HYPOT) \
    OP(FMA) \
    OP(RANDOM_RANGE) \
    OP(ENTITY) \
    OP(COMPONENT) \
    /* Checked scalar copy: entity, component ID/layout/stride, field byte offset. */ \
    OP(COMPONENT_READ) \
    OP(COLUMN) \
    OP(COLUMN_TRY) /* Non-trapping layout guard for speculative read-only work. */ \
    OP(ROW) /* checked column + live entity slot; no storage-changing effects */ \
    OP(INDEX) \
    OP(LOAD) \
    OP(READ) \
    OP(RETRIGGER) \
    OP(STORE) \
    OP(STORE_VALUE) \
    OP(RESULT) \
    OP(SAVE) \
    OP(RESULT_PRESENT) \
    OP(RESULT_TYPE_NAME) \
    OP(RESULT_ARRAY) \
    OP(STORE_HEADER) \
    OP(STORE_TYPE_NAME) \
    OP(REQUIRE) \
    OP(ASSERT) \
    OP(WRITE) \
    /* Outputless checked publication through the authoritative component store. */ \
    OP(COMPONENT_WRITE) \
    OP(COMMAND_ARGUMENT) \
    OP(COMMAND_OUTPUT) \
    OP(COMMAND) \
    OP(COMMAND_BEGIN) \
    OP(HOST_CALL) \
    OP(INVOKE) \
    OP(SUSPEND) \
    OP(DELAY) \
    OP(POINT) \
    OP(WATCH_BEGIN) \
    OP(WATCH_RESET) \
    OP(WATCH_VALUE) \
    OP(WATCH_PRESENT) \
    OP(WATCH_END) \
    OP(TRACE) \
    OP(CHECK) \
    OP(CALL_CHECK) \
    OP(ENTER) \
    OP(LEAVE) \
    OP(FAIL) \
    OP(JUMP) \
    OP(BRANCH) \
    OP(RETURN) \
    OP(SOURCE_POINT) \
    OP(SOURCE_WATCH) \
    OP(END)
// clang-format on

typedef enum
{
#define FUNCTION_OP(op) BF_##op,
#define MATH_OP(op, name, fn) FUNCTION_OP(op)
    BEHAVIOR_FUNCTION_OPS(FUNCTION_OP, MATH_OP)
#undef MATH_OP
#undef FUNCTION_OP
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
           op == BF_RETRIGGER || op == BF_SOURCE_POINT ||
           op == BF_SOURCE_WATCH || op == BF_COMPONENT_WRITE;
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
