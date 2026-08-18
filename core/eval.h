#ifndef CBLUEPRINT_EVAL_H
#define CBLUEPRINT_EVAL_H

#include "graph.h"
#include "pin.h"

/* Clear per-run memo/cycle state on every node. Call once per run (with
 * arena_reset) before the first graph_eval_output. */
void graph_eval_reset(graph *g);

/* Pull-eval one output pin of n; writes the value to *out. Returns 0 on
 * success, -1 on a bad pin index, -2 on a cycle. */
int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out);

#endif
