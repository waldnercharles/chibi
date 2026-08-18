#ifndef CBLUEPRINT_NODES_H
#define CBLUEPRINT_NODES_H

#include "node.h"

/* Make: struct fields (inputs) -> one struct instance (output), in the arena. */
void node_eval_make(node *n, arena *a);

/* Break: one struct instance (input) -> its fields (outputs). */
void node_eval_break(node *n, arena *a);

/* Call: params (inputs) -> return value (output), via desc->fn. */
void node_eval_call(node *n, arena *a);

/* Constant/literal: no-op; the caller pre-sets output[0]. */
void node_eval_const(node *n, arena *a);

#endif
