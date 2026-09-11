#ifndef CHIBI_BEHAVIOR_DATA_H
#define CHIBI_BEHAVIOR_DATA_H
#include "graph/compiler/behavior_program.h"

/* Host value operations consume prepared slots; they never traverse a graph. */
int behavior_data_instruction(
    behavior_data_context *context,
    const graph_ir_behavior_instruction *instruction
);
int behavior_control_branch(flow_context *ctx);
void behavior_data_scalar(behavior_data_context *context, const node *n);
int behavior_data_call(node *n, arena *a, eval_context *ctx);
int eval_prepared_node(node *n, arena *a, eval_context *ctx);

#endif
