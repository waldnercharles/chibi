#ifndef CBLUEPRINT_NODES_H
#define CBLUEPRINT_NODES_H

#include "node.h"
#include "runtime.h"

/* Make: struct fields (inputs) -> one struct instance (output), in the arena. */
void node_eval_make(node *n, runtime_node *state, arena *a);

/* Break: one struct instance (input) -> its fields (outputs). */
void node_eval_break(node *n, runtime_node *state, arena *a);

/* Call: params (inputs) -> return value (output), via desc->fn. */
void node_eval_call(node *n, runtime_node *state, arena *a);

/* Constant/literal: no-op; the caller pre-sets output[0]. */
void node_eval_const(node *n, runtime_node *state, arena *a);

#endif
