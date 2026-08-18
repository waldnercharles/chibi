#ifndef CBLUEPRINT_NODE_H
#define CBLUEPRINT_NODE_H

#include "pin.h"
#include "type.h"

#include <stdint.h>

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

typedef struct
{
    const char *name;
    int input_count;
    int output_count;
    pin_spec *inputs;
    pin_spec *outputs;
    void (*eval)(node *n, arena *a);
    const type_desc *type;
    node_fn fn;
    node_flow_kind flow_kind;
    node_flow_fn flow;
    unsigned pure;
    unsigned latent;
    node_display_class display_class;
} node_desc;

struct node
{
    const node_desc *desc;
    uint64_t id;
    int input_count;
    int output_count;
    pin_spec *input_pins;
    pin_spec *output_pins;
    /* Runtime values. Authored defaults live in input_pins/output_pins and are
     * copied here when the instance is created or an activation begins. */
    pin_value *inputs;
    pin_value *outputs;
    char *title_override;
    char *comment;
    uint64_t reference_id;
    char *reference_name;
    float x, y;
    unsigned computed;
    unsigned visiting;
    unsigned disabled;
    int eval_error;
    const char *eval_error_message;
};

static inline const pin_spec *node_input_pin(const node *n, int index)
{
    return n && index >= 0 && index < n->input_count ? &n->input_pins[index] : NULL;
}

static inline const pin_spec *node_output_pin(const node *n, int index)
{
    return n && index >= 0 && index < n->output_count ? &n->output_pins[index] : NULL;
}

#endif
