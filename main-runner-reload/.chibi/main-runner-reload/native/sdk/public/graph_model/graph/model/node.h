#ifndef CHIBI_NODE_H
#define CHIBI_NODE_H

#include "execution/host.h"
#include "execution/instruction.h"
#include "graph/model/pin.h"
#include "support/enum.h"
#include "types/array.h"
#include "types/type.h"

#include <stdint.h>

typedef struct node node;
typedef struct arena arena;
typedef struct flow_context flow_context;
typedef struct eval_context eval_context;
struct behavior_lower_builder;
/* Publication-only intrinsic expansion into the shared behavior IR. A data
 * output returns its typed slot. A synchronous flow node receives pin=-1 and
 * returns zero after emitting effects; it has one Then output. Negative means
 * unsupported and must carry a source diagnostic. Never called by execution. */
typedef int (*node_lower_fn)(struct behavior_lower_builder *, node *, int pin);

typedef enum
{
    NODE_FLOW_NONE = 0,
    NODE_FLOW_EVENT,
    NODE_FLOW_BRANCH,
    NODE_FLOW_SEQUENCE,
    NODE_FLOW_FOR_LOOP,
    NODE_FLOW_DELAY,
    NODE_FLOW_CUSTOM,
} node_flow_kind;

typedef enum
{
    NODE_DISPLAY_PURE = 0,
    NODE_DISPLAY_EVENT,
    NODE_DISPLAY_FUNCTION,
    NODE_DISPLAY_FLOW,
    NODE_DISPLAY_VARIABLE,
    NODE_DISPLAY_LITERAL,
} node_display_class;

typedef int (*node_flow_fn)(node *n, flow_context *ctx);

enum
{
    NODE_EVAL_OK = 0,
    NODE_EVAL_ALLOCATION = -3,
    NODE_EVAL_INVALID = -4,
};

typedef pin_value (*node_fn)(const pin_value *inputs, int input_count, arena *a);
/* Contextual read: returns an error message, or NULL after producing a value. */
typedef const char *(*node_read_fn)(const eval_context *ctx, arena *a, pin_value *out);
/* Contextual reads with evaluated data inputs; exactly one read callback is
 * set. out has output_count slots; the callback fills every declared output. */
typedef const char *(*node_read_inputs_fn)(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *a,
    pin_value *out
);
typedef enum
{
    NODE_JIT_NONE = 0,
    NODE_JIT_MUL,
    NODE_JIT_ADD,
    NODE_JIT_SUB,
    NODE_JIT_DIV,
    NODE_JIT_MIN,
    NODE_JIT_MAX,
    NODE_JIT_CLAMP,
    NODE_JIT_NEG,
    NODE_JIT_ABS,
    NODE_JIT_V2_MOVE,
    NODE_JIT_V2_NORMALIZE,
    NODE_JIT_V2_DIRECTION,
    NODE_JIT_V2_FROM_ANGLE,
    NODE_JIT_V2_LENGTH,
    NODE_JIT_V2_DOT,
    NODE_JIT_V2_DISTANCE,
    NODE_JIT_RANDOM_VECTOR,
    NODE_JIT_UNWRAP,
    /* Explicit promise: copy a fixed-layout struct and retag its nominal type. */
    NODE_JIT_IDENTITY,
} node_jit_kind;

typedef enum
{
    NODE_PURE_CONST = 0, /* outputs pre-set (literal, or written by flow) */
    NODE_PURE_COPY,      /* one data input copied to one data output */
    NODE_PURE_MAKE,      /* desc->type: field inputs -> one struct output */
    NODE_PURE_BREAK,     /* desc->type: one struct input -> field outputs */
    NODE_PURE_CALL,      /* desc->fn: inputs -> one output */
    NODE_PURE_GET,       /* variable read from the behavior_asset instance */
    NODE_PURE_SELF,     /* owner handle read from the behavior_asset instance */
    NODE_PURE_FUNCTION, /* pure function graph call */
    NODE_PURE_VOID_CALL, /* reflected pure call with no data result */
    NODE_PURE_READ,      /* read host state without an execution edge */
} node_pure_kind;

typedef enum
{
    NODE_COMPONENT_NONE,
    NODE_COMPONENT_GET_SELF,
    NODE_COMPONENT_HAS_SELF,
    NODE_COMPONENT_SET_SELF,
    NODE_COMPONENT_GET_ENTITY,
    NODE_COMPONENT_HAS_ENTITY,
    NODE_COMPONENT_SET_ENTITY,
    NODE_COMPONENT_SET_FIELD,
} node_component_kind;

typedef enum
{
    NODE_WORK_NONE,
    NODE_WORK_WORLD_QUERY,
    NODE_WORK_ENTITY_REDUCTION,
    NODE_WORK_LINEAR_SEARCH,
    NODE_WORK_SET,
    NODE_WORK_SET_COPY,
} node_work_kind;
typedef enum
{
    NODE_SOURCE_STATE_NONE,
    NODE_SOURCE_STATE_READ,
    NODE_SOURCE_STATE_WRITE,
} node_source_state_access;
typedef enum
{
    NODE_SOURCE_COMPONENT_NONE,
    NODE_SOURCE_COMPONENT_READ,
    NODE_SOURCE_COMPONENT_WRITE,
    NODE_SOURCE_COMPONENT_HAS,
    NODE_SOURCE_COMPONENT_ADD,
    NODE_SOURCE_COMPONENT_REMOVE,
} node_source_component_access;
typedef enum
{
    NODE_SOURCE_FLOW_NONE,
    NODE_SOURCE_FLOW_BRANCH,
    NODE_SOURCE_FLOW_RANGE,
    NODE_SOURCE_FLOW_SEQUENCE,
    NODE_SOURCE_FLOW_WHILE,
    NODE_SOURCE_FLOW_EACH,
    NODE_SOURCE_FLOW_BREAK,
} node_source_flow_kind;
typedef struct
{
    node_source_flow_kind kind;
    /* Data, result, outcome and entry ordinals exclude the opposite pin kind. */
    int inputs[2], results[2], outcomes[2], break_entry;
} node_source_flow;

typedef struct
{
    const char *name;
    int input_count;
    int output_count;
    pin_spec *inputs;
    pin_spec *outputs;
    node_pure_kind pure_kind; /* how to compute DATA outputs; CONST is zero default */
    node_jit_kind jit_kind;
    node_component_kind component_kind;
    const struct ecs_component_desc *component;
    int component_field;
    const type_desc *type;
    node_fn fn;
    node_read_fn read;
    node_read_inputs_fn read_inputs;
    node_flow_kind flow_kind;
    node_flow_fn flow;
    int latent;
    bool instance_state; /* One zero-initialized pin_value per node and instance. */
    node_display_class display_class;
    /* CAT-27: spawn-menu category path and space-separated search keywords.
     * Both are static display/search data; NULL means uncategorized. */
    const char *category;
    const char *keywords;
    /* One sentence on what the node does, for hover text and search. */
    const char *description;
    const char *conversion_from;
    const char *conversion_to;
    const node_access *access;
    /* Generated component getter contract; NULL for arbitrary contextual reads. */
    const char *binding_component;
    /* Source declaration presentation only; never an executable graph node. */
    bool metadata_only;
    bool foldable; /* Explicit permission to evaluate constant inputs at authoring time. */
    const char *query_service;
    node_query_fn query;
    const node_command *command;
    const node_host_call *host_call;
    node_lower_fn lower;
    /* Cost semantics supplied by the capability, independent of its label. */
    node_work_kind work;
    /* Source capability declarations. Presentation and parsers consume these
     * names; the native definition owns aliases and operator behavior. */
    const char *source_name, *method_name, *operator_name;
    bool numeric_aggregate;
    node_source_state_access source_state_access;
    node_source_component_access source_component_access;
    int source_component_entity, source_component_value; /* Data ordinals. */
    bool source_event_sender;
    bool source_variadic_optional;
    node_source_flow source_flow;
    const node_host_call *execution_call;
    behavior_function_op execution_intrinsic;
} node_desc;

/* Publish the same native value operation through the execution ABI. This
 * forwarding shim carries no registry lookup or mutable callback context. */
#define NODE_VALUE_ADAPTER(NAME, FUNCTION)                                                            \
    static const char *NAME(arena *storage, const pin_value *arguments, int count, pin_value *result) \
    {                                                                                                 \
        pin_value value = FUNCTION(arguments, count, storage);                                        \
        if (value.type == PIN_FLOW ||                                                                 \
            (value.type == PIN_STRUCT &&                                                              \
             value.container == PIN_CONTAINER_SINGLE && !value.ptr)) {                                \
            return "Native value operation failed";                                                   \
        }                                                                                             \
        return pin_value_snapshot_to_arena(storage, result, &value)                                   \
                 ? "Cannot retain native value result"                                                \
                 : NULL;                                                                              \
    }                                                                                                 \
    static const node_host_call NAME##_host = { .value = NAME }

#define NODE_VOID_ADAPTER(NAME, FUNCTION)                                                             \
    static const char *NAME(arena *storage, const pin_value *arguments, int count, pin_value *result) \
    {                                                                                                 \
        *result = FUNCTION(arguments, count, storage);                                                \
        return NULL;                                                                                  \
    }                                                                                                 \
    static const node_host_call NAME##_host = { .value = NAME }

/* How the editor presents a pure source: `auto` folds it into its consumer's
 * pin row when graph_node_is_inline allows, `inline` also folds larger trees,
 * and `expanded` always draws its box. Evaluation ignores it. */
#define NODE_PRESENTATION_LIST(X)                                              \
    X(NODE_PRESENTATION_AUTO, "auto")                                          \
    X(NODE_PRESENTATION_INLINE, "inline")                                      \
    X(NODE_PRESENTATION_EXPANDED, "expanded")

DEFINE_ENUM(node_presentation, NODE_PRESENTATION_LIST)

struct node
{
    const node_desc *desc;
    uint64_t id;
    int input_count;
    int output_count;
    pin_spec *input_pins;
    pin_spec *output_pins;
    uint64_t reference_id;
    char *reference_name;
    char *reference_asset; /* NULL for local calls; graph-owned library identity */
    float x, y;
    /* Placed positions are saved. Arranging and loading a node without a
     * position make it automatic, and the editor arranges automatic nodes. */
    bool placed;
    unsigned disabled;
    node_presentation presentation;
    unsigned breakpoint;
    unsigned breakpoint_enabled;
    bool show_advanced;
    /* Unreal's comment bubble: graph-owned, NULL without a comment. A hidden
     * comment shows only while the node is hovered. */
    char *comment;
    bool comment_hidden;
};

static inline const pin_spec *node_input_pin(const node *n, int index)
{
    return n && index >= 0 && index < n->input_count ? &n->input_pins[index] : NULL;
}

static inline const pin_spec *node_output_pin(const node *n, int index)
{
    return n && index >= 0 && index < n->output_count ? &n->output_pins[index] : NULL;
}

/* NODE-6: a descriptor is pure when it declares no PIN_EXEC pins. */
static inline int node_desc_is_pure(const node_desc *d)
{
    if (!d) {
        return 0;
    }
    for (int i = 0; i < d->input_count; i++) {
        if (d->inputs[i].kind == PIN_EXEC) {
            return 0;
        }
    }
    for (int i = 0; i < d->output_count; i++) {
        if (d->outputs[i].kind == PIN_EXEC) {
            return 0;
        }
    }
    return 1;
}

/* NODE-13: behavior classification. A pure node has no exec pins; an
 * executable entry has no exec input and at least one exec output; every
 * other executable node has at least one exec input. Executable behavior
 * runs only from an event activation, never from preview evaluation. */
typedef enum
{
    NODE_BEHAVIOR_PURE = 0,
    NODE_BEHAVIOR_ENTRY,
    NODE_BEHAVIOR_EXEC,
} node_behavior;

static inline node_behavior node_desc_behavior(const node_desc *d)
{
    if (!d) {
        return NODE_BEHAVIOR_PURE;
    }
    int exec_input = 0, exec_output = 0;
    for (int i = 0; i < d->input_count; i++) {
        if (d->inputs[i].kind == PIN_EXEC) {
            exec_input = 1;
        }
    }
    for (int i = 0; i < d->output_count; i++) {
        if (d->outputs[i].kind == PIN_EXEC) {
            exec_output = 1;
        }
    }
    if (exec_input) {
        return NODE_BEHAVIOR_EXEC;
    }
    if (exec_output) {
        return NODE_BEHAVIOR_ENTRY;
    }
    return NODE_BEHAVIOR_PURE;
}

static inline node_behavior node_behavior_for(const node *n)
{
    if (!n) {
        return NODE_BEHAVIOR_PURE;
    }
    int exec_input = 0, exec_output = 0;
    for (int i = 0; i < n->input_count; i++) {
        exec_input |= n->input_pins[i].kind == PIN_EXEC;
    }
    for (int i = 0; i < n->output_count; i++) {
        exec_output |= n->output_pins[i].kind == PIN_EXEC;
    }
    if (exec_input) {
        return NODE_BEHAVIOR_EXEC;
    }
    if (exec_output) {
        return NODE_BEHAVIOR_ENTRY;
    }
    return NODE_BEHAVIOR_PURE;
}

#endif
