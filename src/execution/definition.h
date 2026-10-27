#ifndef CHIBI_EXECUTION_DEFINITION_H
#define CHIBI_EXECUTION_DEFINITION_H
#include "execution/artifact.h"
#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/scalar.h"
#include "execution/value_kind.h"
#include "support/arena.h"

typedef struct
{
    pin_value initial;
    int frame;
    uint64_t node_id;
    int pin;
    size_t offset;
    bool parameter;
    /* Nonempty array defaults are supplied by the immutable value bank in C exports. */
    bool array_default;
    int data_index;
    behavior_value_kind kind;
} behavior_function_local;
typedef struct
{
    int index;
    const type_desc *type;
} behavior_function_result;
typedef struct
{
    pin_value initial;
    int frame, pin;
} behavior_function_parameter;
typedef struct
{
    behavior_function_source source;
    int depth, resume, source_index, stack;
} behavior_function_frame;
typedef struct
{
    const char *service, *key;
    node_query_fn read;
    uint64_t capability_id, capability_abi;
} behavior_function_query;
typedef struct
{
    const node_host_call *call;
    const node_access *access;
    int first, result_first, result_count;
    const uint64_t *outcomes;
    int outcome_count;
    uint64_t capability_id, capability_abi;
} behavior_function_host_call;
typedef struct
{
    pin_value initial;
    size_t size;
    behavior_value_kind kind;
} behavior_function_data_value;
typedef struct
{
    const node_command *command;
    const char *reference;
    int first, count;
    uint64_t node_id;
    int frame, result_first;
    const int *result_pins;
    int result_count;
    uint64_t capability_id, capability_abi;
    uint64_t event_id, event_abi;
} behavior_function_command;
/* Both functions belong to the same immutable program generation. Result banks
 * use canonical Return input indices, including the unused EXEC slot. A task
 * invocation starts the callee beside the caller; its only result is the
 * task handle. */
typedef struct
{
    const behavior_function *function;
    int first, count, result_first, result_count;
    bool task;
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
    behavior_value_bank bank;
    int index;
    pin_value shape;
} behavior_value_root;
/* Live sets are exact and belong to the definition's owned arena. */
typedef struct
{
    int pc;
    int value_count, root_count;
    int *values; /* Slots followed by locals, in the activation's scalar storage. */
    behavior_value_root *roots; /* Aggregate shapes for the live saved values. */
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
/* Whether a call may pass arguments without copying them; zero is unknown. */
typedef enum
{
    BEHAVIOR_SHARING_UNKNOWN,
    BEHAVIOR_SHARING_SAFE,
    BEHAVIOR_SHARING_COPY,
    BEHAVIOR_SHARING_PENDING_SAFE,
    BEHAVIOR_SHARING_PENDING_COPY
} behavior_argument_sharing;
struct behavior_function
{
    arena owned;
    bool frozen;
    int argument_count, result_count, variable_count;
    pin_value *argument_defaults, *result_defaults, *variable_defaults;
    behavior_value_kind *argument_kinds, *result_kinds, *slot_kinds;
    behavior_value_kind *command_argument_kinds, *command_result_kinds;
    behavior_reduction *reductions;
    compile_diagnostic *query_diagnostics;
    behavior_function_suspension *suspensions;
    behavior_value_root *value_roots;
    behavior_function_point *points;
    behavior_source_point *source_points;
    behavior_source_watch *source_watches;
    bool waits, breakpoints, batchable, managed, custom_lowering, uses_storage,
        checkpoints, pin_watches;
    /* Recomputed from verified bodies, never serialized or accepted as an ABI promise. */
    bool readonly_collections;
    /* Some host call builds a returned local in the caller's storage. */
    bool builds_results;
    /* It reads components, or takes a reference that may name one, so a call
     * binds the execution world. */
    bool binds_world;
    behavior_argument_sharing argument_sharing;
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
    behavior_function_parameter *parameters;
    behavior_function_frame *frames;
    behavior_function_source *sources, *stacks;
    behavior_function_result *results;
    execution_artifact *artifact;
    struct behavior_function *fast;
    /* Development builds only: an occurrence byte per source and report kind,
     * then the matching delivered bytes. Such a body has no fast body. */
    atomic_uchar *integer_reports;
    int integer_report_sites;
    behavior_function_instruction *runtime_instructions;
    behavior_function_instruction *runtime_body; /* Optimized values at canonical PCs. */
    int *runtime_map; /* Canonical PC to compact, uninspected instruction index. */

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
static inline bool behavior_function_inspection_required(
    const behavior_function *f,
    const behavior_function_context *ctx
)
{
    if (ctx->inspect || ctx->watch || ctx->trace || ctx->source_pause ||
        ctx->source_capture) {
        return true;
    }
    if (!ctx->activation || !ctx->activation->pc) {
        return false;
    }
    /* A debug pause saves canonical live values. After detaching inspection,
     * finish this continuation canonically: optimized aliases may require
     * different slots. Real waits remain boundaries shared by both bodies. */
    behavior_function_op op = f->instructions[ctx->activation->pc - 1].op;
    return op == BF_POINT || op == BF_SOURCE_POINT;
}
/* The value a frame's Argument instruction reads for parameter `p`: the
 * passed argument of the declared type, a `number` or `f64` converted to the
 * other, or else the declared default. */
static inline pin_value behavior_function_argument(const pin_value *arguments, int count, int p, pin_value declared)
{
    if (p >= count) {
        return declared;
    }
    pin_value arg = arguments[p];
    if (arg.type == declared.type) {
        return arg;
    }
    if (arg.type == PIN_F32 && declared.type == PIN_F64) {
        declared.d = (double)arg.f;
    } else if (arg.type == PIN_F64 && declared.type == PIN_F32) {
        declared.f = (float)arg.d;
    }
    return declared;
}
void behavior_function_calls_free(struct behavior_function_calls *calls);
void behavior_function_calls_reset(struct behavior_function_calls *calls);
size_t behavior_function_calls_storage_bytes(const struct behavior_function_calls *calls);
static inline bool behavior_function_runtime_keeps(const behavior_function *f, int pc)
{
    return !f->runtime_map || f->runtime_map[pc] != f->runtime_map[pc + 1];
}
static inline bool behavior_function_left_slot(behavior_function_op op)
{
    return (op >= BF_ADD && op <= BF_SAVE && op != BF_STORE_VALUE) ||
           op == BF_BRANCH || op == BF_REQUIRE || op == BF_ASSERT ||
           op == BF_WRITE || op == BF_COMPONENT_WRITE || op == BF_RESULT_PRESENT ||
           op == BF_RESULT_TYPE_NAME || op == BF_STORE_HEADER ||
           op == BF_STORE_TYPE_NAME || op == BF_OWN_NAME ||
           op == BF_COPY_STRING || op == BF_COPY_ARRAY || op == BF_INTERN_STRING ||
           op == BF_STRING_EQUAL || op == BF_STATE_SELECT ||
           op == BF_COMMAND_ARGUMENT || op == BF_COMMAND_OUTPUT ||
           op == BF_DELAY || op == BF_WATCH_VALUE || op == BF_WATCH_PRESENT ||
           op == BF_WATCH_END || op == BF_RESULT_ARRAY || op == BF_SOURCE_WATCH;
}
static inline bool behavior_function_right_slot(behavior_function_op op)
{
    return (op >= BF_ADD && op < BF_STORE && op != BF_LOAD && op != BF_READ &&
            op != BF_COLUMN && op != BF_COLUMN_TRY && op != BF_COMPONENT_READ &&
            op != BF_RETRIGGER && op != BF_BIT_NOT && op != BF_NEG && op != BF_ABS &&
            op != BF_CAST && op != BF_CONVERT && !(op >= BF_SIN && op <= BF_ROUND)) ||
           op == BF_WRITE || op == BF_COMPONENT_WRITE || op == BF_STORE_HEADER ||
           op == BF_INTERN_STRING || op == BF_STRING_EQUAL ||
           op == BF_STATE_SELECT || op == BF_RESULT_ARRAY;
}
/* Finalize operand metadata, continuation maps and runtime code after lowering. */
/* Prepared readers reconstruct this canonical definition, then use the same
 * producer verifier and finalizer. Ownership remains with the caller on failure. */
bool behavior_function_prepare_definition(behavior_function *, size_t arena_budget, char *error, size_t size);
/* Private per-attempt descriptor checks. Entries borrow immutable records.
 * Preflight uses `seen` for reservation; verification uses it only after a
 * successful full validation. Neither table escapes its current catalog scope.
 * Budget covers both constructions, bounded comparisons, and temporary bytes. */
typedef struct
{
    const pin_record *record;
    bool seen;
} behavior_record_check;
size_t behavior_function_record_check_budget(const behavior_function *, size_t *bytes);
behavior_record_check *behavior_function_record_checks(
    const behavior_function *,
    size_t bytes,
    int *count
);
int behavior_record_check_find(const behavior_record_check *, int count, const pin_record *);
/* arena_budget caps the owned arena's total storage before each allocation;
 * SIZE_MAX leaves it unbounded. */
bool behavior_function_finalize(behavior_function *function, size_t arena_budget);
bool behavior_function_own(behavior_function *function);
void behavior_function_parallel_proof(behavior_function *function);
void *behavior_function_jit_system(const behavior_function *function);
void *behavior_function_jit(const behavior_function *function, bool inspected);
/* Gives this definition and its owned definitions report sites, dropping
 * their fast bodies, before any backend compiles them (integer_report.c). */
void behavior_function_report_integers(behavior_function *);
/* Occurrence and delivered arrays for these source sites; SIZE_MAX on overflow. */
size_t behavior_integer_report_bytes(int sites);
bool behavior_integer_reportable(const behavior_function *, const behavior_function_instruction *);
/* The report kind these operands cause, or -1 when the operation is exact. */
int behavior_integer_event(const behavior_function_instruction *, pin_value left, pin_value right);
/* The instruction's occurrence byte for kind, or NULL without report sites. */
atomic_uchar *behavior_integer_site(
    const behavior_function *,
    const behavior_function_instruction *,
    int kind
);
/* A float at or below the first bound, or at or above the second, does not
 * fit the integer type once truncated. */
const double *behavior_integer_cast_bounds(pin_type);
void behavior_integer_report(
    const behavior_function *,
    const behavior_function_instruction *,
    pin_value left,
    pin_value right
);
bool behavior_function_compile_inspection(const behavior_function *function);
bool behavior_function_compile_scoped(const behavior_function *function);
void *behavior_function_jit_scoped(const behavior_function *function);
void *behavior_function_guard(const behavior_function *function);
/* Canonicalize registry/builtin names whose lifetime exceeds program generations. */
bool behavior_function_static_name(const char **name);
void behavior_function_constant_locals(behavior_function *function);
void behavior_function_prune_values(behavior_function *function);
void behavior_function_runtime_code(behavior_function *function);
void behavior_function_rebind_host(
    const behavior_function *,
    behavior_function_context *,
    const struct service_binding *
);
void behavior_function_borrow_arrays(behavior_function *function);
void behavior_function_borrow_snapshots(behavior_function *function, const bool *keep);
void behavior_function_prepare_borrows(behavior_function *function);
/* The borrow proof walks callees while their callers' facts stay live. The
 * root's facts are part of its preparation scratch; its callees' live facts
 * together stay within this many bytes, or the proof refuses to borrow. */
#define BEHAVIOR_BORROW_NESTED_BYTES ((size_t)1 << 20)
/* The bytes of one function's borrow facts. */
size_t behavior_function_borrow_bytes(const behavior_function *function);
/* Sets what a frozen function's body implies: builds_results and binds_world. */
void behavior_function_note_derived(behavior_function *function);
/* The instructions control may reach next from `pc`; returns their count. */
int behavior_function_successors(const behavior_function *function, int pc, int targets[2]);
bool program_call_views_allowed(const behavior_function_context *context);
void behavior_function_reuse_accesses(behavior_function *function);
void behavior_function_reuse_reads(behavior_function *function, bool *keep);
void behavior_function_optimize_ranges(behavior_function *function, const bool *keep);
void behavior_function_optimize(behavior_function *function);
void behavior_function_scalarize_unions(behavior_function *function, bool *keep);
size_t behavior_function_scalarize_scratch(const behavior_function *function);
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
#endif
