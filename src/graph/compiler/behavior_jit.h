#ifndef CHIBI_BEHAVIOR_JIT_H
#define CHIBI_BEHAVIOR_JIT_H
#include "graph/compiler/graph_ir.h"

void *behavior_jit_data(const graph_ir *ir, const graph_ir_block *block);
void *behavior_jit_control(const graph_ir_control *instruction);
void behavior_jit_free(void *code);

#endif
