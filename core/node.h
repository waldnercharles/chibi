#ifndef CBLUEPRINT_NODE_H
#define CBLUEPRINT_NODE_H

#include "pin.h"
#include "type.h"

typedef struct node node;
typedef struct arena arena;
typedef struct flow_context flow_context;

typedef enum
{
    NODE_FLOW_NONE = 0,
    NODE_FLOW_EVENT,
    NODE_FLOW_BRANCH,
    NODE_FLOW_SEQUENCE,
    NODE_FLOW_FOR_LOOP,
    NODE_FLOW_DELAY,
    NODE_FLOW_CUSTOM,
    NODE_FLOW_KIND_NONE = NODE_FLOW_NONE,
    NODE_FLOW_KIND_EVENT = NODE_FLOW_EVENT,
    NODE_FLOW_KIND_BRANCH = NODE_FLOW_BRANCH,
    NODE_FLOW_KIND_SEQUENCE = NODE_FLOW_SEQUENCE,
    NODE_FLOW_KIND_FOR_LOOP = NODE_FLOW_FOR_LOOP,
    NODE_FLOW_KIND_DELAY = NODE_FLOW_DELAY,
} node_flow_kind;

/* A hand-authored imperative node can use this callback instead of a built-in
 * flow kind. The flow_context helpers live in flow.h. */
typedef int (*node_flow_fn)(node *n, flow_context *ctx);

enum
{
    NODE_EVAL_OK = 0,
    NODE_EVAL_ALLOCATION = -3,
    NODE_EVAL_INVALID = -4,
};

/* Uniform signature for call-node functions. reflect emits a wrapper that
 * adapts a native C function to this shape. */
typedef pin_value (*node_fn)(const pin_value *inputs, int input_count, arena *a);

typedef struct
{
    const char *name;
    int input_count;
    int output_count;
    pin_spec *inputs; /* name + type + DATA/EXEC kind per pin */
    pin_spec *outputs;
    void (*eval)(node *n, arena *a); /* reads inputs, writes outputs */
    const type_desc *type;           /* Make/Break nodes: the struct type */
    node_fn fn;                      /* call nodes: the adapted function */
    node_flow_kind flow_kind;        /* imperative evaluator behavior */
    node_flow_fn flow;               /* custom imperative evaluator */
} node_desc;

struct node
{
    const node_desc *desc;
    pin_value *inputs;  /* input_count values; unconnected pins hold literals */
    pin_value *outputs; /* output_count values */
    float x, y;         /* editor layout hint; core eval ignores it */
    unsigned computed : 1; /* eval memo: computed this run */
    unsigned visiting : 1; /* eval cycle guard: currently on the stack */
    int eval_error;
    const char *eval_error_message;
    const char *event_name; /* graph-owned name for NODE_FLOW_EVENT nodes */
};

#endif
