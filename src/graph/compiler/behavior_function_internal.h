#ifndef CHIBI_BEHAVIOR_FUNCTION_INTERNAL_H
#define CHIBI_BEHAVIOR_FUNCTION_INTERNAL_H
#include "graph/compiler/behavior_expression_wasm.h"
#include "graph/compiler/behavior_function.h"
#include "graph/compiler/behavior_scalar.h"
#include "support/cos_sin.h"

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
    BF_END
} behavior_function_op;

enum
{
    BF_STORE_VALUE_SET_ADD = 1
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
    return behavior_function_math(op) || op == BF_OWN_NAME || op == BF_COPY_STRING ||
           op == BF_COPY_ARRAY || op == BF_INTERN_STRING || op == BF_STRING_EQUAL ||
           op == BF_STORE_VALUE || op == BF_QUERY || op == BF_COMMAND ||
           op == BF_COMMAND_BEGIN || op == BF_HOST_CALL || op == BF_RANDOM ||
           op == BF_RANDOM_RANGE || op == BF_STATE_SELECT || op == BF_RETRIGGER;
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
typedef struct
{
    pin_value initial;
    int frame;
    const node *node;
    int pin;
    size_t offset;
    bool parameter;
    /* Nonempty array defaults are supplied by the immutable value bank in C exports. */
    bool array_default;
    int data_index;
} behavior_function_local;
typedef struct
{
    int index;
    const type_desc *type;
} behavior_function_result;
typedef struct
{
    behavior_function_source source;
    int depth, resume, source_index, stack;
} behavior_function_frame;
typedef struct
{
    const char *service, *key;
    node_query_fn read;
} behavior_function_query;
typedef struct
{
    const node_host_call *call;
    const node_access *access;
    int first, result_first, result_count;
} behavior_function_host_call;
typedef struct
{
    pin_value initial;
    size_t size;
} behavior_function_data_value;
typedef struct
{
    const node_command *command;
    const char *reference;
    int first, count;
    node *node;
    int frame, result_first;
} behavior_function_command;
/* Both functions belong to the same immutable program generation. Result banks
 * use canonical Return input indices, including the unused EXEC slot. */
typedef struct
{
    const behavior_function *function;
    int first, count, result_first, result_count;
} behavior_function_invocation;
enum
{
    BEHAVIOR_FUNCTION_CALL_RESULT = 3
};
typedef struct behavior_function_vm_storage
{
    pin_value *values;
    uintptr_t *parents;
} behavior_function_vm_storage;
typedef struct
{
    int pc;
    int *values; /* Slots followed by locals, in the activation's scalar storage. */
} behavior_function_suspension;
/* A read-only entity reduction has a checked prelude, one cursor loop and a
 * private accumulator. Publication uses these regions to fuse compatible scans. */
typedef struct
{
    int begin, loop, body, next, end;
    int pointer, index, entity, index_local, count_local;
    uint64_t graph_id, node_id, pin_id;
} behavior_reduction;
typedef struct
{
    uint64_t *components;
    bool result;
} behavior_component_proof;
struct behavior_function
{
    behavior_reduction *reductions;
    compile_diagnostic *query_diagnostics;
    behavior_function_suspension *suspensions;
    behavior_function_point *points;
    bool waits, breakpoints, batchable, managed, custom_lowering, uses_storage,
        checkpoints, pin_watches;
    behavior_function_watch *watches;
    behavior_function_edge *edges;
    size_t watch_size;
    behavior_function_command *commands;
    pin_value *command_arguments;
    pin_value *command_results;
    behavior_function_query *queries;
    behavior_function_host_call *host_calls;
    behavior_function_invocation *calls;
    /* The root owns separately lowered definitions. Calls borrow references. */
    behavior_function **definitions;
    behavior_function_data_value *data_values;
    behavior_function_instruction *instructions;
    pin_type *slots;
    behavior_function_local *locals;
    behavior_function_frame *frames;
    behavior_function_source *sources, *stacks;
    behavior_function_result *results;
    void *code, *system_code;
    void *dispatch;
    uintptr_t *resume_addresses;
    void *inspection_code;
    uintptr_t *inspection_addresses;
    struct behavior_function *fast;
    behavior_function_instruction *runtime_instructions;
    behavior_function_instruction *runtime_body; /* Optimized values at canonical PCs. */
    int *runtime_map; /* Canonical PC to compact, uninspected instruction index. */
    behavior_expression_wasm *wasm;
    behavior_expression_wasm_region *wasm_regions;
    unsigned root_steps, child_budget, depth_limit;
    unsigned pure_depth_limit;
    bool component_bindings, component_access;
    uint64_t *component_writes;
    behavior_component_proof *component_proofs;
    uint64_t *parallel_reads;
    bool parallel;
    struct behavior_function_component *components;
    behavior_state_selection *state_selections;
};
typedef struct behavior_function_component
{
    uint64_t id;
    bool binding;
} behavior_function_component;
static inline bool behavior_function_inspectable(const behavior_function *f)
{
    return f->waits || f->host_calls != NULL || f->calls != NULL ||
           f->managed || f->custom_lowering || f->checkpoints || f->pin_watches;
}
int behavior_function_run_once(
    const behavior_function *function,
    behavior_function_context *context,
    bool native
);
int behavior_function_run_calls(
    const behavior_function *function,
    behavior_function_context *context,
    bool native
);
void behavior_function_calls_free(struct behavior_function_calls *calls);
static inline bool behavior_function_runtime_keeps(const behavior_function *f, int pc)
{
    return !f->runtime_map || f->runtime_map[pc] != f->runtime_map[pc + 1];
}
static inline bool behavior_function_left_slot(behavior_function_op op)
{
    return (op >= BF_ADD && op <= BF_SAVE && op != BF_STORE_VALUE) ||
           op == BF_BRANCH || op == BF_REQUIRE || op == BF_ASSERT ||
           op == BF_WRITE || op == BF_RESULT_PRESENT || op == BF_RESULT_TYPE_NAME ||
           op == BF_STORE_HEADER || op == BF_STORE_TYPE_NAME ||
           op == BF_OWN_NAME || op == BF_COPY_STRING || op == BF_COPY_ARRAY ||
           op == BF_INTERN_STRING || op == BF_STRING_EQUAL ||
           op == BF_STATE_SELECT || op == BF_COMMAND_ARGUMENT ||
           op == BF_COMMAND_OUTPUT || op == BF_DELAY || op == BF_WATCH_VALUE ||
           op == BF_WATCH_PRESENT || op == BF_WATCH_END || op == BF_RESULT_ARRAY;
}
static inline bool behavior_function_right_slot(behavior_function_op op)
{
    return (op >= BF_ADD && op < BF_STORE && op != BF_LOAD && op != BF_READ &&
            op != BF_COLUMN && op != BF_COLUMN_TRY && op != BF_RETRIGGER) ||
           op == BF_WRITE || op == BF_STORE_HEADER || op == BF_INTERN_STRING ||
           op == BF_STRING_EQUAL || op == BF_STATE_SELECT || op == BF_RESULT_ARRAY;
}
/* Finalize operand metadata, continuation maps and runtime code after lowering. */
void behavior_function_finalize(behavior_function *function);
void behavior_function_parallel_proof(behavior_function *function);
void *behavior_function_jit_system(const behavior_function *function);
void *behavior_function_jit(const behavior_function *function, bool inspected);
void behavior_function_points(behavior_function *function);
void *behavior_function_guard(const behavior_function *function);
/* Canonicalize registry/builtin names whose lifetime exceeds program generations. */
bool behavior_function_static_name(const char **name);
void behavior_function_constant_locals(behavior_function *function);
void behavior_function_prune_values(behavior_function *function);
void behavior_function_runtime_code(behavior_function *function);
void behavior_function_borrow_arrays(behavior_function *function);
void behavior_function_compile_wasm_regions(behavior_function *function);
void behavior_function_reuse_accesses(behavior_function *function);
void behavior_function_reuse_reads(behavior_function *function, bool *keep);
void behavior_function_optimize(behavior_function *function);
typedef struct
{
    int first, last; /* Canonical instruction PCs of immutable I32 inputs. */
    unsigned entry_steps, iteration_steps;
} behavior_loop_budget;
bool behavior_function_loop_budget(const behavior_function *function, behavior_loop_budget *proof);
void behavior_function_fuse_queries(behavior_function *function);
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
bool behavior_function_owned_set_add_candidate(
    const behavior_asset *asset,
    const graph *g,
    const node *add
);
#endif
