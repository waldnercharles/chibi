#ifndef CBLUEPRINT_NODE_H
#define CBLUEPRINT_NODE_H

#include "pin.h"
#include "type.h"

typedef struct node node;
typedef struct arena arena;

/* Uniform signature for call-node functions. reflect emits a wrapper that
 * adapts a native C function to this shape. */
typedef pin_value (*node_fn)(const pin_value *inputs, int input_count, arena *a);

typedef struct
{
    const char *name;
    int input_count;
    int output_count;
    pin_spec *inputs; /* name + type per pin */
    pin_spec *outputs;
    void (*eval)(node *n, arena *a); /* reads inputs, writes outputs */
    const type_desc *type;           /* Make/Break nodes: the struct type */
    node_fn fn;                      /* call nodes: the adapted function */
} node_desc;

struct node
{
    const node_desc *desc;
    pin_value *inputs;  /* input_count values; unconnected pins hold literals */
    pin_value *outputs; /* output_count values */
    float x, y;         /* editor layout hint; core eval ignores it */
    unsigned computed : 1; /* eval memo: computed this run */
    unsigned visiting : 1; /* eval cycle guard: currently on the stack */
};

#endif
