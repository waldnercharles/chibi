#ifndef CHIBI_NODE_H
#define CHIBI_NODE_H

#include "graph/model/pin.h"
#include "graph/model/type.h"

#include <stdint.h>

typedef struct node node;
typedef struct arena arena;
typedef struct flow_context flow_context;
typedef struct eval_context eval_context;

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

typedef struct
{
    const uint64_t *reads;
    int read_count;
    const uint64_t *writes;
    int write_count;
    unsigned flags;
} node_access;

enum
{
    NODE_EFFECT_STRUCTURAL = 1u << 0,
    NODE_EFFECT_WORLD_READ = 1u << 1,
    NODE_EFFECT_HOST_SINK = 1u << 2,
    NODE_EFFECT_PURE = 1u << 3,
    NODE_EFFECT_ENTITY_SCOPED = 1u << 4,
};

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
/* Typed adapters: output and inputs point at storage for the declared pin types. */
typedef int (*node_fast_fn)(void *output, const void *const *inputs);
typedef int (*node_fast0_fn)(void *output);
typedef int (*node_fast1_fn)(void *output, const void *input0);
typedef int (*node_fast2_fn)(void *output, const void *input0, const void *input1);
typedef int (*node_fast3_fn)(void *output, const void *input0, const void *input1, const void *input2);
typedef int (*node_fast4_fn)(
    void *output,
    const void *input0,
    const void *input1,
    const void *input2,
    const void *input3
);

typedef union
{
    node_fast0_fn zero;
    node_fast1_fn one;
    node_fast2_fn two;
    node_fast3_fn three;
    node_fast4_fn four;
} node_fast_direct_fn;

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
    NODE_JIT_ATAN2_F32,
    NODE_JIT_UNWRAP,
    /* Explicit promise: copy a fixed-layout struct and retag its nominal type. */
    NODE_JIT_IDENTITY,
} node_jit_kind;

enum
{
    NODE_JIT_FLAG_FAST_MATH = 1u << 0,
};

typedef enum
{
    NODE_PURE_CONST = 0, /* outputs pre-set (literal, or written by flow) */
    NODE_PURE_DELTA, /* current simulation step from the evaluation context */
    NODE_PURE_COPY,  /* one data input copied to one data output */
    NODE_PURE_MAKE,  /* desc->type: field inputs -> one struct output */
    NODE_PURE_BREAK, /* desc->type: one struct input -> field outputs */
    NODE_PURE_CALL,  /* desc->fn: inputs -> one output */
    NODE_PURE_GET,   /* variable read from the behavior_asset instance */
    NODE_PURE_SELF,  /* owner handle read from the behavior_asset instance */
    NODE_PURE_FUNCTION,  /* pure function graph call */
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

typedef struct
{
    const char *name;
    int input_count;
    int output_count;
    pin_spec *inputs;
    pin_spec *outputs;
    node_pure_kind pure_kind; /* how to compute DATA outputs; CONST is zero default */
    node_jit_kind jit_kind;
    unsigned jit_flags;
    node_component_kind component_kind;
    const struct ecs_component_desc *component;
    int component_field;
    const type_desc *type;
    node_fn fn;
    node_read_fn read;
    node_read_inputs_fn read_inputs;
    node_fast_fn fast;
    node_fast_direct_fn direct;
    int direct_count; /* arity plus one; zero means no direct adapter */
    node_flow_kind flow_kind;
    node_flow_fn flow;
    int latent;
    node_display_class display_class;
    /* CAT-27: spawn-menu category path and space-separated search keywords.
     * Both are static display/search data; NULL means uncategorized. */
    const char *category;
    const char *keywords;
    const char *conversion_from;
    const char *conversion_to;
    const node_access *access;
} node_desc;

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
    unsigned disabled;
    bool inline_source;
    unsigned breakpoint;
    unsigned breakpoint_enabled;
    bool show_advanced;
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
