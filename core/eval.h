#ifndef CBLUEPRINT_EVAL_H
#define CBLUEPRINT_EVAL_H

#include "graph.h"
#include "pin.h"

/* Clear memo/cycle state for a pull-eval activation. Call with arena_reset
 * before a standalone run; flow_run performs this between node activations. */
void graph_eval_reset(graph *g);

typedef struct
{
    int code;
    const node *node;
    int pin;
    const char *message;
} eval_status;

typedef struct eval_context
{
    eval_status status;
} eval_context;

void eval_context_init(eval_context *ctx);
void eval_context_reset(eval_context *ctx);
const eval_status *eval_context_status(const eval_context *ctx);

/* Pull-eval one output pin of n; writes the value to *out. Returns 0 on
 * success, -1 on a bad pin index, -2 on a cycle, -3 on allocation failure,
 * and -4 on invalid graph data. Status is stored on the supplied context. */
int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out, eval_context *ctx);

#endif
